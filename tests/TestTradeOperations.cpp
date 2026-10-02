// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestTradeOperations.h"

#include "../src/frontend/FrontendSubscriptions.h"
#include "../src/frontend/TradeOperations.h"
#include "../src/observer/gameobserver.h"
#include "../src/parser/CharLines.h"
#include "../src/parser/GameStateLines.h"
#include "../src/parser/QuietCapture.h"
#include "../src/parser/TradeLines.h"
#include "../src/proxy/GmcpMessage.h"

#include <memory>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

namespace {

NODISCARD QJsonObject payloadOf(const GmcpMessage &msg)
{
    const auto &optJson = msg.getJson();
    if (!optJson.has_value()) {
        return QJsonObject{};
    }
    return QJsonDocument::fromJson(optJson->toQByteArray()).object();
}

NODISCARD QJsonObject json(const char *const text)
{
    return QJsonDocument::fromJson(QByteArray{text}).object();
}

/// A scripted MUME: what the runner sends waits in `pending` until flush() lets it reach MUME
/// (sig2_sentToMudString, as the proxy reports it, newline and all), and MUME's side is played
/// through the observer's line, pager, reader and prompt signals.
struct NODISCARD Mume final
{
    GameObserver observer;
    QStringList pending;
    QStringList sent;
    std::vector<QJsonObject> ops;
    std::vector<QString> names;
    int64_t clock = 1000;
    int viewerChanges = 0;
    /// What the runner told the parser about its quiet command (sig2_quietCommand), in order:
    /// "begin", "foreign", "end".
    QStringList quiet;
    Signal2Lifetime lifetime;
    std::unique_ptr<TradeOperations> trade;

    Mume()
    {
        observer.sig2_quietCommand.connect(lifetime, [this](const QuietCommandEnum what) {
            switch (what) {
            case QuietCommandEnum::BEGIN:
                quiet << QStringLiteral("begin");
                break;
            case QuietCommandEnum::FOREIGN:
                quiet << QStringLiteral("foreign");
                break;
            case QuietCommandEnum::END:
                quiet << QStringLiteral("end");
                break;
            }
        });
        trade = std::make_unique<TradeOperations>(
            observer,
            [this](const QString &line) {
                pending << line;
                sent << line;
            },
            [this](const GmcpMessage &msg) {
                names.push_back(msg.getName().toQString());
                ops.push_back(payloadOf(msg));
            },
            [this]() { return clock; });
        trade->setViewerChanged([this]() { ++viewerChanges; });
        observer.observeConnected();
        observer.observeGameState(GameStateEnum::PLAYING);
        trade->setDriving(true);
    }

    NODISCARD static TradeOperations::Context context()
    {
        TradeOperations::Context ctx;
        ctx.driving = true;
        ctx.connected = true;
        ctx.echo = true;
        ctx.game = GameStateEnum::PLAYING;
        return ctx;
    }

    void request(const char *const payload) { trade->request(json(payload), context()); }
    void requestQuiet(const char *const payload) { trade->requestQuiet(json(payload), context()); }
    void requestQuiet(const std::string &payload) { requestQuiet(payload.c_str()); }

    /// Everything waiting reaches MUME, in order.
    void flush()
    {
        while (!pending.isEmpty()) {
            observer.observeSentToMud(pending.takeFirst() + QStringLiteral("\n"));
        }
    }
    /// The player types `line` in the driving frontend: FrontendServer::handleInput's order.
    void player(const QString &line)
    {
        trade->beforePlayerLine(line);
        pending << line;
        sent << line;
    }
    void line(const QString &text)
    {
        observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud,
                                           text + QStringLiteral("\n"),
                                           false);
    }
    void pager(const int percent)
    {
        const QString text = QStringLiteral(
                                 "*** Return: continue, b: back, r: redisplay, q: quit (%1%) ***")
                                 .arg(percent);
        observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud, text, true);
        observer.observePager(PagerLine{percent, text});
    }
    void prompt()
    {
        observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud,
                                           QStringLiteral("> "),
                                           true);
        observer.observeRealPrompt();
    }
    /// A line of a quiet command's reply, as the parser reports what it kept from the terminal.
    void quietLine(const QString &text) { observer.observeQuietLine(text + QStringLiteral("\n")); }
    /// The pager of that reply: published, marked hidden, and never sent to the terminal.
    void quietPager(const int percent)
    {
        PagerLine pager;
        pager.percent = percent;
        pager.text
            = QStringLiteral("*** Return: continue, b: back, r: redisplay, q: quit (%1%) ***")
                  .arg(percent);
        pager.hidden = true;
        observer.observePager(pager);
    }
    /// The prompt that ends it, which the parser hid as well: the end, then the prompt.
    void quietPrompt()
    {
        observer.observeQuietEnded();
        observer.observeRealPrompt();
    }

    NODISCARD const QJsonObject &last() const { return ops.back(); }
    NODISCARD QString status() const { return last()["status"].toString(); }
    NODISCARD QString reason() const { return last()["reason"].toString(); }
};

NODISCARD ShopDeal deal(const ShopDealKindEnum kind)
{
    ShopDeal d;
    d.kind = kind;
    return d;
}

NODISCARD GuildPractised practised(const int64_t used, const int64_t most)
{
    GuildPractised p;
    p.used = used;
    p.most = most;
    p.knowledgePct = 50;
    return p;
}

/// MUME, the parser's readers and the runner together: what MumeXmlParser::parse() does with
/// each chunk (see TestTradeLines' Terminal), with the runner's lines reaching MUME at flush().
/// `shown` is the terminal.
struct NODISCARD Whole final
{
    GameObserver observer;
    Signal2Lifetime lifetime;
    TradeReaders readers{observer};
    CharLinesTracker chars;
    QStringList pending;
    QStringList sent;
    QStringList shown;
    QStringList packages;
    std::vector<QJsonObject> replies;
    int64_t clock = 1000;
    std::unique_ptr<TradeOperations> trade;

    Whole()
    {
        observer.sig2_quietCommand.connect(lifetime, [this](const QuietCommandEnum what) {
            readers.receiveQuietCommand(what);
        });
        observer.sig2_sentToMudString.connect(lifetime, [this](const QString &line) {
            std::ignore = readers.receiveCommand(line);
        });
        observer.sig2_charSkills.connect(lifetime, [this](const CharSkills &skills) {
            packages << QStringLiteral("skills:%1:%2").arg(skills.rows.size()).arg(skills.complete);
        });
        trade = std::make_unique<TradeOperations>(
            observer,
            [this](const QString &line) {
                pending << line;
                sent << line;
            },
            [this](const GmcpMessage &msg) {
                // The readers' packages have gone out by the time the reply does.
                packages << msg.getName().toQString();
                replies.push_back(payloadOf(msg));
            },
            [this]() { return clock; });
        observer.observeConnected();
        observer.observeGameState(GameStateEnum::PLAYING);
        trade->setDriving(true);
    }

    void requestQuiet(const char *const payload)
    {
        trade->requestQuiet(json(payload), Mume::context());
    }
    void flush()
    {
        while (!pending.isEmpty()) {
            observer.observeSentToMud(pending.takeFirst() + QStringLiteral("\n"));
        }
    }
    void player(const QString &line)
    {
        trade->beforePlayerLine(line);
        pending << line;
        sent << line;
    }
    void stats(const CharReplies &r)
    {
        for (const CharStat &stat : r.stats) {
            packages << QStringLiteral("stat:%1").arg(stat.ob.value_or(-1));
        }
    }
    void chunk(const bool goAhead,
               const QString &text,
               const QuietTrafficEnum traffic = QuietTrafficEnum::REPLY)
    {
        const MudChunk c = readers.beginChunk(goAhead, false, text, traffic);
        if (c.captured) {
            observer.observeQuietLine(c.plain);
        }
        if (!c.hidden) {
            shown << text;
            observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud, text, goAhead);
        }
        if (c.kind == MudChunkKindEnum::LINE) {
            stats(chars.receiveLine(c.plain));
            readers.receiveLine(c.plain);
        }
        if (c.kind == MudChunkKindEnum::PROMPT) {
            stats(chars.receivePrompt());
            readers.receivePrompt();
        }
        readers.endChunk();
    }
    void lines(const std::initializer_list<const char *> all)
    {
        for (const char *const one : all) {
            chunk(false, QString::fromUtf8(one) + QStringLiteral("\n"));
        }
    }
    /// A line MUME wrapped in an element of other traffic.
    void other(const char *const text)
    {
        chunk(false, QString::fromUtf8(text) + QStringLiteral("\n"), QuietTrafficEnum::OTHER);
    }
    void prompt() { chunk(true, QStringLiteral("o W C Mana:Hot>")); }
    void pager(const int percent)
    {
        chunk(true,
              QStringLiteral("*** Return: continue, b: back, r: redisplay, q: quit (%1%) *** ")
                  .arg(percent));
    }

    NODISCARD const QJsonObject &last() const { return replies.back(); }
    NODISCARD QString status() const { return last()["status"].toString(); }
    NODISCARD QString reason() const { return last()["reason"].toString(); }
    NODISCARD QStringList replyLines() const
    {
        QStringList result;
        for (const QJsonValue &value : last()["lines"].toArray()) {
            result << value.toString();
        }
        return result;
    }
};

// elvenrunes 2020-12-30_..._Aquator.txt:1716-1747, as in TestTradeLines: the table's two pages.
const std::initializer_list<const char *> g_pageOne{
    "You have 1 practice session left.",
    "Skill / Spell          Knowledge  Difficulty  Class       Mana  Casting time",
    "Climb                  Good       Very easy   None       ",
    "Cure blindness         Fair       Normal      Cleric         4  Very short"};
const std::initializer_list<const char *>
    g_pageTwo{"Dispel evil            Superb     Hard        Cleric        17  Very short",
              "Pick                   Poor       Normal      Thief      ",
              ""};

} // namespace

void TestTradeOperations::operationMessageTest()
{
    TradeOperationState state;
    state.id = QStringLiteral("r1");
    state.action = QStringLiteral("shop.sell");
    state.status = TradeStatusEnum::STOPPED;
    state.step = 2;
    state.steps = 3;
    state.reason = QStringLiteral("closed");
    state.text << QStringLiteral("A grocer tells you 'Sorry, we are closed.'");
    const GmcpMessage msg = makeTradeOperation(state);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Trade.Operation"));
    QVERIFY(msg.isMMapperTradeOperation());
    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["id"].toString(), QStringLiteral("r1"));
    QCOMPARE(obj["action"].toString(), QStringLiteral("shop.sell"));
    QCOMPARE(obj["status"].toString(), QStringLiteral("stopped"));
    QCOMPARE(obj["step"].toInt(), 2);
    QCOMPARE(obj["steps"].toInt(), 3);
    QCOMPARE(obj["reason"].toString(), QStringLiteral("closed"));
    QCOMPARE(obj["text"].toArray().size(), 1);

    // Every status has its protocol name.
    QCOMPARE(tradeStatusName(TradeStatusEnum::RUNNING), QStringLiteral("running"));
    QCOMPARE(tradeStatusName(TradeStatusEnum::DONE), QStringLiteral("done"));
    QCOMPARE(tradeStatusName(TradeStatusEnum::REFUSED), QStringLiteral("refused"));
    QCOMPARE(tradeStatusName(TradeStatusEnum::FAILED), QStringLiteral("failed"));
    QCOMPARE(tradeStatusName(TradeStatusEnum::CANCELLED), QStringLiteral("cancelled"));
}

void TestTradeOperations::shopListTest()
{
    Mume mume;
    mume.request(R"({"id":"a","action":"shop.list","filter":"helm"})");
    QCOMPARE(mume.sent, QStringList{"list helm"});
    QCOMPARE(mume.names.back(), QStringLiteral("MMapper.Trade.Operation"));
    QCOMPARE(mume.status(), QStringLiteral("running"));
    QCOMPARE(mume.last()["step"].toInt(), 1);
    QCOMPARE(mume.last()["steps"].toInt(), 1);
    QVERIFY(mume.trade->busy());

    // A prompt, and a list, before the command has even reached MUME answer something else.
    mume.observer.observeShopList(ShopList{});
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("running"));

    mume.flush();
    // A bare prompt was already on its way when the command went: not judged.
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("running"));

    mume.line(QStringLiteral("You can buy:"));
    mume.line(QStringLiteral(" 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold."));
    mume.observer.observeShopList(ShopList{});
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.reason(), QString{});
    QVERIFY(!mume.trade->busy());
    QCOMPARE(mume.sent, QStringList{"list helm"});

    // Without a filter, a bare `list`; a miss is an answer too.
    Mume bare;
    bare.request(R"({"id":"b","action":"shop.list"})");
    QCOMPARE(bare.sent, QStringList{"list"});
    bare.flush();
    bare.line(QStringLiteral("There is no such thing for sale."));
    bare.observer.observeShopDeal(deal(ShopDealKindEnum::MISS));
    bare.prompt();
    QCOMPARE(bare.status(), QStringLiteral("done"));

    // A reply nothing recognised fails at the prompt, with MUME's words.
    Mume odd;
    odd.request(R"({"id":"c","action":"shop.list"})");
    odd.flush();
    odd.line(QStringLiteral("Huh?!"));
    odd.prompt();
    QCOMPARE(odd.status(), QStringLiteral("failed"));
    QCOMPARE(odd.reason(), QStringLiteral("no reply"));
    QCOMPARE(odd.last()["text"].toArray().at(0).toString(), QStringLiteral("Huh?!"));
}

void TestTradeOperations::shopBuyTest()
{
    Mume mume;
    mume.request(R"({"id":"a","action":"shop.buy","number":470,"count":3})");
    QCOMPARE(mume.sent, QStringList{"buy 3 470"});
    mume.flush();
    mume.line(QStringLiteral("Angdil tells you 'May it serve you well!'"));
    mume.observer.observeShopDeal(deal(ShopDealKindEnum::BUY));
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));

    Mume one;
    one.request(R"({"id":"b","action":"shop.buy","number":513})");
    QCOMPARE(one.sent, QStringList{"buy 513"});
    one.flush();
    one.line(QStringLiteral("There is no such thing for sale."));
    one.observer.observeShopDeal(deal(ShopDealKindEnum::MISS));
    one.prompt();
    QCOMPARE(one.status(), QStringLiteral("refused"));
    QCOMPARE(one.reason(), QStringLiteral("no such thing"));
    QCOMPARE(one.last()["step"].toInt(), 1);
    QCOMPARE(one.last()["text"].toArray().at(0).toString(),
             QStringLiteral("There is no such thing for sale."));

    // Out of range or of the wrong type: nothing is sent.
    Mume bad;
    bad.request(R"({"id":"c","action":"shop.buy","number":470,"count":21})");
    QCOMPARE(bad.status(), QStringLiteral("refused"));
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    bad.request(R"({"id":"d","action":"shop.buy","number":"470"})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    bad.request(R"({"id":"e","action":"shop.buy","number":4.5})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    QVERIFY(bad.sent.isEmpty());
}

void TestTradeOperations::shopSellTest()
{
    Mume mume;
    mume.request(R"({"id":"s","action":"shop.sell","items":["2.sword","helm","all.metal"]})");
    QCOMPARE(mume.last()["steps"].toInt(), 3);
    // One at a time: the next only after the first is judged.
    QCOMPARE(mume.sent, QStringList{"sell 2.sword"});
    mume.flush();
    mume.line(QStringLiteral("An armourer tells you 'Here you have 38 gold for that.'"));
    mume.line(QStringLiteral("You sell a sword."));
    mume.observer.observeShopDeal(deal(ShopDealKindEnum::SELL));
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("running"));
    QCOMPARE(mume.last()["step"].toInt(), 2);
    QCOMPARE(mume.sent, (QStringList{"sell 2.sword", "sell helm"}));

    // A refusal on a later step stops what has not gone out.
    mume.flush();
    mume.line(QStringLiteral("A grocer tells you 'Sorry, we are closed.'"));
    mume.observer.observeShopDeal(deal(ShopDealKindEnum::CLOSED));
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("stopped"));
    QCOMPARE(mume.reason(), QStringLiteral("closed"));
    QCOMPARE(mume.last()["step"].toInt(), 2);
    QCOMPARE(mume.sent.size(), 2);

    // A selector that would end the line, or an empty list, is refused.
    Mume bad;
    bad.request(R"({"id":"t","action":"shop.sell","items":["sword\nquit"]})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    bad.request(R"({"id":"u","action":"shop.sell","items":[]})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    QVERIFY(bad.sent.isEmpty());
}

void TestTradeOperations::guildListTest()
{
    Mume mume;
    mume.request(R"({"id":"g","action":"guild.list"})");
    QCOMPARE(mume.sent, QStringList{"prac"});
    mume.flush();
    mume.line(QStringLiteral("You have eleven practice sessions left."));
    GuildTeacher teacher;
    teacher.sessionsLeft = 11;
    mume.observer.observeGuildTeacher(teacher);
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));

    // Away from a teacher, `prac` lists the character's own skills: an answer as well.
    mume.request(R"({"id":"h","action":"guild.list"})");
    mume.flush();
    mume.line(QStringLiteral("You know the following skills:"));
    mume.observer.observeCharSkills(CharSkills{});
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
}

void TestTradeOperations::practiseTimesTest()
{
    Mume mume;
    mume.request(R"({"id":"p","action":"guild.practise","name":"block door","times":3})");
    QCOMPARE(mume.last()["steps"].toInt(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(mume.sent.size(), i + 1);
        QCOMPARE(mume.sent.back(), QStringLiteral("prac block door"));
        mume.flush();
        mume.line(QStringLiteral("You took some sessions in this skill."));
        mume.observer.observeGuildPractised(practised(4 + i, 11));
        mume.prompt();
    }
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.reason(), QString{});
    QCOMPARE(mume.last()["step"].toInt(), 3);
    QCOMPARE(mume.sent.size(), 3);

    // It stops at the teacher's most, before `times`.
    Mume most;
    most.request(R"({"id":"q","action":"guild.practise","name":"portal","times":5})");
    most.flush();
    most.line(QStringLiteral("You took 10 out of 11 sessions in this skill."));
    most.observer.observeGuildPractised(practised(10, 11));
    most.prompt();
    QCOMPARE(most.status(), QStringLiteral("running"));
    most.flush();
    most.line(QStringLiteral("You took 11 out of 11 sessions in this skill."));
    most.observer.observeGuildPractised(practised(11, 11));
    most.prompt();
    QCOMPARE(most.status(), QStringLiteral("done"));
    QCOMPARE(most.reason(), QStringLiteral("limit reached"));
    QCOMPARE(most.sent.size(), 2);
}

void TestTradeOperations::practiseLimitTest()
{
    // "limit": as far as the teacher goes, with no count known ahead.
    Mume mume;
    mume.request(R"({"id":"p","action":"guild.practise","name":"portal","times":"limit"})");
    QCOMPARE(mume.last()["steps"].toInt(), 0);
    int64_t used = 8;
    while (mume.trade->busy()) {
        mume.flush();
        mume.line(QStringLiteral("You took sessions in this skill."));
        mume.observer.observeGuildPractised(practised(++used, 11));
        mume.prompt();
        QVERIFY(mume.sent.size() <= 3);
    }
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.reason(), QStringLiteral("limit reached"));
    QCOMPARE(mume.sent.size(), 3);

    // With the sessions left known from the teacher's table, it stops when they are used up.
    Mume sessions;
    GuildTeacher teacher;
    teacher.sessionsLeft = 2;
    sessions.observer.observeGuildTeacher(teacher);
    sessions.request(R"({"id":"s","action":"guild.practise","name":"portal","times":"limit"})");
    for (int i = 0; i < 2; ++i) {
        sessions.flush();
        sessions.line(QStringLiteral("You took sessions in this skill."));
        sessions.observer.observeGuildPractised(practised(1 + i, 22));
        sessions.prompt();
    }
    QCOMPARE(sessions.status(), QStringLiteral("done"));
    QCOMPARE(sessions.reason(), QStringLiteral("limit reached"));
    QCOMPARE(sessions.sent.size(), 2);

    // A refusal after the first step is the limit too.
    Mume refused;
    refused.request(R"({"id":"r","action":"guild.practise","name":"portal","times":"limit"})");
    refused.flush();
    refused.line(QStringLiteral("You took 3 out of 22 sessions in this skill."));
    refused.observer.observeGuildPractised(practised(3, 22));
    refused.prompt();
    refused.flush();
    refused.line(QStringLiteral("You cannot practise any more."));
    GuildPractised no;
    no.refused = QStringLiteral("You cannot practise any more.");
    refused.observer.observeGuildPractised(no);
    refused.prompt();
    QCOMPARE(refused.status(), QStringLiteral("done"));
    QCOMPARE(refused.reason(), QStringLiteral("limit reached"));
}

void TestTradeOperations::practiseRefusalTest()
{
    Mume mume;
    mume.request(R"({"id":"p","action":"guild.practise","name":"portal","times":4})");
    mume.flush();
    mume.line(QStringLiteral("You have to stand in order to practice anything."));
    GuildPractised no;
    no.refused = QStringLiteral("You have to stand in order to practice anything.");
    mume.observer.observeGuildPractised(no);
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("refused"));
    QCOMPARE(mume.reason(), QStringLiteral("refused"));
    QCOMPARE(mume.last()["text"].toArray().at(0).toString(), no.refused);
    QCOMPARE(mume.sent.size(), 1);

    // After a first success, a refusal with a count asked for stops the rest.
    Mume later;
    later.request(R"({"id":"q","action":"guild.practise","name":"portal","times":4})");
    later.flush();
    later.line(QStringLiteral("You took 3 out of 22 sessions in this skill."));
    later.observer.observeGuildPractised(practised(3, 22));
    later.prompt();
    later.flush();
    later.line(no.refused);
    later.observer.observeGuildPractised(no);
    later.prompt();
    QCOMPARE(later.status(), QStringLiteral("stopped"));
    QCOMPARE(later.reason(), QStringLiteral("refused"));
    QCOMPARE(later.sent.size(), 2);

    Mume bad;
    bad.request(R"({"id":"r","action":"guild.practise","name":"portal","times":0})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    bad.request(R"({"id":"s","action":"guild.practise","times":2})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
}

void TestTradeOperations::innOfferAndRentTest()
{
    Mume mume;
    mume.request(R"({"id":"o","action":"inn.offer"})");
    QCOMPARE(mume.sent, QStringList{"offer"});
    mume.flush();
    mume.line(QStringLiteral("Erienal tells you 'It will cost you 12 gold coins per day.'"));
    mume.observer.observeInnOffer(InnOffer{});
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));

    // Renting is done when the character leaves the game.
    mume.request(R"({"id":"r","action":"inn.rent"})");
    QCOMPARE(mume.sent.back(), QStringLiteral("rent"));
    mume.flush();
    mume.line(
        QStringLiteral("Erienal stores your stuff in the safe, and helps you into your chamber."));
    mume.observer.observeGameState(GameStateEnum::RENTED);
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.last()["action"].toString(), QStringLiteral("inn.rent"));

    // A reply without the rent is a rent that did not happen.
    Mume no;
    no.request(R"({"id":"s","action":"inn.rent"})");
    no.flush();
    no.line(QStringLiteral("You can't rent here."));
    no.prompt();
    QCOMPARE(no.status(), QStringLiteral("failed"));
    QCOMPARE(no.reason(), QStringLiteral("not rented"));
}

void TestTradeOperations::innRetireTest()
{
    Mume mume;
    mume.request(R"({"id":"x","action":"inn.retire"})");
    QCOMPARE(mume.last()["steps"].toInt(), 2);
    QCOMPARE(mume.sent, QStringList{"rent retire"});
    mume.flush();
    mume.line(QStringLiteral("If you really want to retire, please repeat that request."));
    InnOffer repeat;
    repeat.retireAsksRepeat = true;
    mume.observer.observeInnOffer(repeat);
    // The repeat goes only once the prompt has closed MUME's request for it.
    QCOMPARE(mume.sent.size(), 1);
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("running"));
    QCOMPARE(mume.last()["step"].toInt(), 2);
    QCOMPARE(mume.sent, (QStringList{"rent retire", "rent retire"}));
    mume.flush();
    mume.line(
        QStringLiteral("Erienal stores your stuff in the safe, and helps you into your chamber."));
    mume.observer.observeGameState(GameStateEnum::RENTED);
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.last()["step"].toInt(), 2);

    // Without MUME's request to repeat, the second is never sent.
    Mume other;
    other.request(R"({"id":"y","action":"inn.retire"})");
    other.flush();
    other.line(QStringLiteral("You can't retire here."));
    other.prompt();
    QCOMPARE(other.status(), QStringLiteral("failed"));
    QCOMPARE(other.sent.size(), 1);

    // Leaving the game on the first step is not what was asked: it stops.
    Mume early;
    early.request(R"({"id":"z","action":"inn.retire"})");
    early.flush();
    early.observer.observeGameState(GameStateEnum::RENTED);
    QCOMPARE(early.status(), QStringLiteral("stopped"));
    QCOMPARE(early.reason(), QStringLiteral("left the game"));
}

void TestTradeOperations::trophiesPagerTest()
{
    Mume mume;
    mume.request(R"({"id":"t","action":"char.trophies"})");
    QCOMPARE(mume.sent, QStringList{"trop"});
    mume.flush();
    for (int i = 0; i < 10; ++i) {
        mume.line(QStringLiteral("   1,  1%,  goblin %1  |").arg(i));
    }
    mume.pager(30);
    // Answered with Return, once.
    QCOMPARE(mume.sent, (QStringList{"trop", ""}));
    QVERIFY(mume.trade->pagerOpen());
    mume.flush();
    QVERIFY(!mume.trade->pagerOpen());

    // Something happens meanwhile, and MUME shows the same pager again: not a new page.
    mume.line(QStringLiteral("Gandalf arrives from the north."));
    mume.pager(30);
    QCOMPARE(mume.sent.size(), 2);

    // The next page.
    for (int i = 0; i < 10; ++i) {
        mume.line(QStringLiteral("   2,  5%,  orc %1  |").arg(i));
    }
    mume.pager(60);
    QCOMPARE(mume.sent, (QStringList{"trop", "", ""}));
    mume.flush();
    mume.line(QStringLiteral("Total kills: 63 (53 distinct)."));
    mume.observer.observeCharTrophies(CharTrophies{});
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QVERIFY(!mume.trade->pagerOpen());

    // A pager in the player's own reply is the player's: not answered.
    mume.player(QStringLiteral("help"));
    mume.flush();
    mume.pager(20);
    QCOMPARE(mume.sent.size(), 4);
}

void TestTradeOperations::quietReplyMessageTest()
{
    QuietReplyState state;
    state.id = QStringLiteral("k1");
    state.text = QStringLiteral("prac");
    state.status = TradeStatusEnum::DONE;
    state.lines << QStringLiteral("You have 0 practice sessions left.") << QStringLiteral("Skill");
    state.paged = true;
    state.complete = true;
    const GmcpMessage msg = makeQuietReply(state);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Input.Reply"));
    QVERIFY(msg.isMMapperInputReply());
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Input.Reply {"complete":true,"id":"k1","lines":)"
                        R"(["You have 0 practice sessions left.","Skill"],"paged":true,)"
                        R"("reason":"","status":"done","text":"prac"})"));
}

void TestTradeOperations::quietHiddenTest()
{
    // One MMapper.Terminal.Hidden for a quiet command whose reply was kept from the terminal:
    // which command, whose request, how many lines. The lines are in the reply, not here too.
    QuietReplyState state;
    state.id = QStringLiteral("k1");
    state.text = QStringLiteral("prac");
    state.lines << QStringLiteral("You have 0 practice sessions left.") << QStringLiteral("Skill");
    const GmcpMessage msg = makeQuietHidden(state);
    QVERIFY(msg.isMMapperTerminalHidden());
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Terminal.Hidden {"command":"prac","count":2,"id":"k1",)"
                        R"("kind":"quiet.command"})"));

    Mume mume;
    mume.requestQuiet(R"({"id":"k","text":"prac"})");
    mume.flush();
    mume.quietLine(QStringLiteral("You have 0 practice sessions left."));
    mume.quietLine(QStringLiteral("Skill / Spell        Knowledge  Difficulty  Class"));
    mume.quietPrompt();
    QCOMPARE(mume.names,
             (std::vector<QString>{QStringLiteral("MMapper.Terminal.Hidden"),
                                   QStringLiteral("MMapper.Input.Reply")}));
    QCOMPARE(mume.ops.front()["kind"].toString(), QStringLiteral("quiet.command"));
    QCOMPARE(mume.ops.front()["id"].toString(), QStringLiteral("k"));
    QCOMPARE(mume.ops.front()["command"].toString(), QStringLiteral("prac"));
    QCOMPARE(mume.ops.front()["count"].toInt(), 2);
    QVERIFY(!mume.ops.front().contains("lines"));
    QVERIFY(!mume.ops.front().contains("text"));

    // Stopped with a part of the reply hidden: said as well.
    Mume held;
    held.requestQuiet(R"({"id":"l","text":"prac"})");
    held.flush();
    held.quietLine(QStringLiteral("You have 0 practice sessions left."));
    held.quietPager(40);
    held.player(QStringLiteral("flee"));
    QCOMPARE(held.status(), QStringLiteral("stopped"));
    QCOMPARE(held.names.front(), QStringLiteral("MMapper.Terminal.Hidden"));
    QCOMPARE(held.ops.front()["count"].toInt(), 1);

    // Nothing was hidden: nothing is said. A timeout with no line, and a refusal.
    Mume late;
    late.requestQuiet(R"({"id":"m","text":"prac"})");
    late.flush();
    late.clock += TradeOperations::QUIET_TIMEOUT_MS;
    late.trade->expire();
    QCOMPARE(late.names, std::vector<QString>{QStringLiteral("MMapper.Input.Reply")});
    Mume refused;
    refused.requestQuiet(R"({"id":"n","text":""})");
    QCOMPARE(refused.names, std::vector<QString>{QStringLiteral("MMapper.Input.Reply")});
}

void TestTradeOperations::quietCommandTest()
{
    // The quiet command: MMapper sends the frontend's line itself and tells the parser once
    // it has reached MUME; the reply is what the parser reports of it.
    Mume mume;
    mume.requestQuiet(R"({"id":"k","text":"prac"})");
    QCOMPARE(mume.sent, QStringList{"prac"});
    // Nothing is published until it is over.
    QVERIFY(mume.ops.empty());
    QVERIFY(mume.trade->busy());
    QVERIFY(mume.quiet.isEmpty());
    mume.flush();
    QCOMPARE(mume.quiet, QStringList{"begin"});

    // A prompt, and lines the terminal was shown, are other traffic to it: it waits.
    mume.line(QStringLiteral("Gandalf tells you 'hi'"));
    mume.prompt();
    QVERIFY(mume.ops.empty());

    mume.quietLine(QStringLiteral(""));
    mume.quietLine(QStringLiteral("You have 0 practice sessions left."));
    mume.quietLine(QStringLiteral("Skill / Spell        Knowledge  Difficulty  Class"));
    mume.quietLine(QStringLiteral("Bandage              Average    Easy        None"));
    mume.quietLine(QStringLiteral(""));
    mume.quietPrompt();
    // What was hidden is said first, then the reply.
    QCOMPARE(mume.ops.size(), size_t{2});
    QCOMPARE(mume.names.front(), QStringLiteral("MMapper.Terminal.Hidden"));
    QCOMPARE(mume.names.back(), QStringLiteral("MMapper.Input.Reply"));
    QCOMPARE(mume.last()["id"].toString(), QStringLiteral("k"));
    QCOMPARE(mume.last()["text"].toString(), QStringLiteral("prac"));
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.reason(), QString{});
    QVERIFY(!mume.last()["paged"].toBool());
    QVERIFY(mume.last()["complete"].toBool());
    // The blank lines around the reply are spacing, and left out; those inside it are kept.
    const QJsonArray lines = mume.last()["lines"].toArray();
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0).toString(), QStringLiteral("You have 0 practice sessions left."));
    QCOMPARE(lines.at(2).toString(),
             QStringLiteral("Bandage              Average    Easy        None"));
    QCOMPARE(mume.quiet, (QStringList{"begin", "end"}));
    QVERIFY(!mume.trade->busy());
    QCOMPARE(mume.sent, QStringList{"prac"});

    // Any line: the runner knows nothing of the command. Here `stat`, right after.
    mume.clock += TradeOperations::QUIET_IDLE_MS;
    mume.requestQuiet(R"({"id":"l","text":"stat"})");
    QCOMPARE(mume.sent.back(), QStringLiteral("stat"));
    mume.flush();
    mume.quietLine(
        QStringLiteral("OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy."));
    mume.quietLine(QStringLiteral("Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal."));
    mume.quietPrompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.last()["text"].toString(), QStringLiteral("stat"));
    QCOMPARE(mume.last()["lines"].toArray().size(), 2);

    // A trade operation's id is its own: Trade.Cancel does not reach a quiet command.
    mume.clock += TradeOperations::QUIET_IDLE_MS;
    mume.requestQuiet(R"({"id":"m","text":"info"})");
    mume.trade->cancel(QStringLiteral("m"));
    QVERIFY(mume.trade->busy());
}

void TestTradeOperations::quietAllowedTest()
{
    // A command the player never sees may only read. Each of these is sent.
    for (const char *const line :
         {"prac", "practice", "sc", "score", "inf", "info", "info MMXP %l %x %X %t %T", "stat",
          "eq", "equipment", "i", "inv", "inventory", "time", "who", "trop", "trophy", "exits",
          "exa sword", "examine sword", "l", "look", "look in pack", "list", "list helm", "PRAC",
          "  Look north  "}) {
        Mume mume;
        mume.trade->requestQuiet(QJsonObject{{"id", "k"}, {"text", QString::fromUtf8(line)}},
                                 Mume::context());
        QVERIFY2(mume.ops.empty(), line);
        // As it was asked, without the space around it.
        QCOMPARE(mume.sent, QStringList{QString::fromUtf8(line).trimmed()});
        QVERIFY2(mume.trade->busy(), line);
    }

    // Anything else is refused whole, as `not allowed`, and nothing of it goes to MUME: not
    // an action, not a second command joined to an allowed one, not an empty line.
    for (const char *const line :
         {"drop all", "say x", "kill orc", "north", "cast 'armour'", "rent", "buy bread",
          "prac bash", "prac; drop all", "prac\ndrop all", "look\r\nflee", "", "   ", " drop all",
          "_help", "practise"}) {
        Mume mume;
        mume.trade->requestQuiet(QJsonObject{{"id", "k"}, {"text", QString::fromUtf8(line)}},
                                 Mume::context());
        QCOMPARE(mume.ops.size(), size_t{1});
        QCOMPARE(mume.names.back(), QStringLiteral("MMapper.Input.Reply"));
        QCOMPARE(mume.status(), QStringLiteral("refused"));
        QCOMPARE(mume.reason(), QStringLiteral("not allowed"));
        QCOMPARE(mume.last()["text"].toString(), QString::fromUtf8(line).trimmed());
        QVERIFY2(mume.sent.isEmpty(), line);
        QVERIFY2(mume.pending.isEmpty(), line);
        QVERIFY2(!mume.trade->busy(), line);
        QVERIFY2(mume.quiet.isEmpty(), line);
        // Nor later: there is nothing waiting to be sent when MUME is idle.
        mume.clock += TradeOperations::QUIET_IDLE_MS;
        mume.trade->expire();
        mume.prompt();
        QVERIFY2(mume.sent.isEmpty(), line);
    }
}

void TestTradeOperations::quietIdleTest()
{
    // It is sent only when no reply of the player's is still to come. Here the player's `look`
    // has gone out and its prompt has not come: the command waits.
    Mume mume;
    mume.player(QStringLiteral("look"));
    mume.flush();
    mume.requestQuiet(R"({"id":"k","text":"prac"})");
    QCOMPARE(mume.sent, QStringList{"look"});
    QVERIFY(mume.trade->busy());
    mume.line(QStringLiteral("A dark cave"));
    mume.trade->expire();
    QCOMPARE(mume.sent, QStringList{"look"});
    // Its prompt: every line has had one. Half a second later the command goes.
    mume.prompt();
    QCOMPARE(mume.sent, QStringList{"look"});
    mume.clock += TradeOperations::QUIET_SETTLE_MS;
    mume.trade->expire();
    QCOMPARE(mume.sent, (QStringList{"look", "prac"}));
    QVERIFY(mume.ops.empty());

    // Two lines out and one prompt back: still one to come.
    Mume two;
    two.player(QStringLiteral("north"));
    two.player(QStringLiteral("look"));
    two.flush();
    two.requestQuiet(R"({"id":"l","text":"prac"})");
    two.line(QStringLiteral("A dark cave"));
    two.prompt();
    two.clock += TradeOperations::QUIET_SETTLE_MS;
    two.trade->expire();
    QCOMPARE(two.sent.size(), 2);
    two.line(QStringLiteral("A dark cave"));
    two.prompt();
    two.clock += TradeOperations::QUIET_SETTLE_MS;
    two.trade->expire();
    QCOMPARE(two.sent.back(), QStringLiteral("prac"));

    // MUME is in the middle of saying something (no prompt since): it waits; and a player who
    // never stops gets it refused, with nothing sent.
    Mume never;
    never.player(QStringLiteral("look"));
    never.flush();
    never.requestQuiet(R"({"id":"m","text":"prac"})");
    for (int i = 0; i < 9; ++i) {
        never.clock += 1000;
        never.line(QStringLiteral("You hit a wolf."));
        never.trade->expire();
    }
    QCOMPARE(never.sent, QStringList{"look"});
    QCOMPARE(never.status(), QStringLiteral("refused"));
    QCOMPARE(never.reason(), QStringLiteral("not idle"));
    QCOMPARE(never.last()["text"].toString(), QStringLiteral("prac"));
    QVERIFY(never.quiet.isEmpty());
    QVERIFY(!never.trade->busy());

    // A line that never gets its prompt does not block for ever: after three seconds with
    // nothing sent and nothing said, MUME is idle.
    Mume silent;
    silent.player(QStringLiteral("look"));
    silent.flush();
    silent.requestQuiet(R"({"id":"n","text":"prac"})");
    silent.clock += TradeOperations::QUIET_IDLE_MS - 1;
    silent.trade->expire();
    QCOMPARE(silent.sent, QStringList{"look"});
    silent.clock += 1;
    silent.trade->expire();
    QCOMPARE(silent.sent, (QStringList{"look", "prac"}));

    // The player types while it waits: the line goes, and the wait starts again.
    Mume typing;
    typing.player(QStringLiteral("look"));
    typing.flush();
    typing.requestQuiet(R"({"id":"o","text":"prac"})");
    typing.prompt();
    typing.player(QStringLiteral("north"));
    QCOMPARE(typing.pending, QStringList{"north"});
    typing.flush();
    typing.clock += TradeOperations::QUIET_SETTLE_MS;
    typing.trade->expire();
    QCOMPARE(typing.sent, (QStringList{"look", "north"}));
    QVERIFY(typing.trade->busy());
}

void TestTradeOperations::quietPagerTest()
{
    // The pager the parser hid is the runner's: answered with Return, page by page.
    Mume mume;
    mume.requestQuiet(R"({"id":"k","text":"prac"})");
    mume.flush();
    for (int i = 0; i < 10; ++i) {
        mume.quietLine(QStringLiteral("Skill %1   Good   Easy   None").arg(i));
    }
    mume.quietPager(40);
    QCOMPARE(mume.sent, (QStringList{"prac", ""}));
    QVERIFY(mume.trade->pagerOpen());
    mume.flush();
    // The same pager again after other output is not a new page.
    mume.line(QStringLiteral("Gandalf arrives from the north."));
    mume.quietPager(40);
    QCOMPARE(mume.sent.size(), 2);
    for (int i = 0; i < 10; ++i) {
        mume.quietLine(QStringLiteral("Spell %1   Good   Easy   Cleric").arg(i));
    }
    mume.quietPager(80);
    QCOMPARE(mume.sent, (QStringList{"prac", "", ""}));
    mume.flush();
    mume.quietLine(QStringLiteral("Wilderness   Good   Easy   None"));
    mume.quietPrompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QVERIFY(mume.last()["paged"].toBool());
    QVERIFY(mume.last()["complete"].toBool());
    QCOMPARE(mume.last()["lines"].toArray().size(), 21);
    QCOMPARE(mume.sent.size(), 3);

    // Each page has its own four seconds.
    Mume slow;
    slow.requestQuiet(R"({"id":"l","text":"prac"})");
    slow.flush();
    slow.clock += TradeOperations::QUIET_TIMEOUT_MS - 1;
    slow.quietLine(QStringLiteral("Skill   Good   Easy   None"));
    slow.quietPager(40);
    slow.flush();
    slow.clock += TradeOperations::QUIET_TIMEOUT_MS - 1;
    slow.trade->expire();
    QVERIFY(slow.trade->busy());

    // A pager the terminal was shown is in a reply of the player's, whatever the runner waits
    // for: not answered.
    Mume other;
    other.requestQuiet(R"({"id":"m","text":"prac"})");
    other.flush();
    other.line(QStringLiteral("   1,  1%,  goblin  |"));
    other.pager(30);
    QCOMPARE(other.sent, QStringList{"prac"});

    // More pages than a reply may have: quit, and what came is handed over.
    Mume many;
    many.requestQuiet(R"({"id":"n","text":"prac"})");
    many.flush();
    for (int i = 0; i <= TradeOperations::MAX_PAGES; ++i) {
        many.quietLine(QStringLiteral("row %1").arg(i));
        many.quietPager(i + 1);
        many.flush();
    }
    QCOMPARE(many.status(), QStringLiteral("stopped"));
    QCOMPARE(many.reason(), QStringLiteral("too many pages"));
    QCOMPARE(many.sent.back(), QStringLiteral("q"));
    QVERIFY(many.last()["paged"].toBool());
    QVERIFY(!many.last()["complete"].toBool());
    QCOMPARE(many.quiet, (QStringList{"begin", "end"}));
}

void TestTradeOperations::quietOverlapTest()
{
    // The player's own lines, and anything else going to MUME, do not stop a quiet command:
    // MUME answers in order. The parser is told, so that its window ends at the next prompt.
    Mume mume;
    mume.requestQuiet(R"({"id":"k","text":"prac"})");
    mume.flush();
    mume.player(QStringLiteral("kill wolf"));
    QCOMPARE(mume.pending, QStringList{"kill wolf"});
    mume.flush();
    QCOMPARE(mume.quiet, (QStringList{"begin", "foreign"}));
    QVERIFY(mume.trade->busy());
    mume.quietLine(QStringLiteral("You have 0 practice sessions left."));
    mume.quietPrompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
    // The player's reply follows; nothing more is said about it.
    const size_t published = mume.ops.size();
    mume.line(QStringLiteral("You hit a wolf."));
    mume.prompt();
    QCOMPARE(mume.ops.size(), published);

    // The window closed with no line of the reply (the prompt after another line went out):
    // no reply.
    Mume none;
    none.requestQuiet(R"({"id":"l","text":"prac"})");
    none.flush();
    none.observer.observeSentToMud(QStringLiteral("inventory\n"));
    none.quietPrompt();
    QCOMPARE(none.status(), QStringLiteral("failed"));
    QCOMPARE(none.reason(), QStringLiteral("no reply"));
    QVERIFY(!none.last()["complete"].toBool());

    // While the runner holds the hidden pager, the player's line would answer it: `q` first,
    // and the command stops with what had come.
    Mume held;
    held.requestQuiet(R"({"id":"m","text":"prac"})");
    held.flush();
    held.quietLine(QStringLiteral("You have 0 practice sessions left."));
    held.quietPager(40);
    held.flush();
    held.line(QStringLiteral("A black wolf arrives from the north."));
    held.quietPager(40);
    held.player(QStringLiteral("flee"));
    QCOMPARE(held.pending, (QStringList{"q", "flee"}));
    QCOMPARE(held.status(), QStringLiteral("stopped"));
    QCOMPARE(held.reason(), QStringLiteral("player input"));
    QVERIFY(held.last()["paged"].toBool());
    QVERIFY(!held.last()["complete"].toBool());
    QCOMPARE(held.last()["lines"].toArray().size(), 1);
    QCOMPARE(held.quiet, (QStringList{"begin", "end"}));

    // Another line reached MUME between the command being submitted and its reaching MUME:
    // that reply comes first, so the window is never opened, and MUME's answer is shown.
    Mume raced;
    raced.requestQuiet(R"({"id":"n","text":"prac"})");
    raced.observer.observeSentToMud(QStringLiteral("look\n"));
    raced.flush();
    QCOMPARE(raced.status(), QStringLiteral("failed"));
    QCOMPARE(raced.reason(), QStringLiteral("overlapping command"));
    QVERIFY(raced.quiet.isEmpty());

    // One thing at a time, either way round.
    Mume busy;
    busy.requestQuiet(R"({"id":"o","text":"prac"})");
    busy.request(R"({"id":"p","action":"shop.list"})");
    QCOMPARE(busy.names.back(), QStringLiteral("MMapper.Trade.Operation"));
    QCOMPARE(busy.status(), QStringLiteral("refused"));
    QCOMPARE(busy.reason(), QStringLiteral("busy"));
    busy.requestQuiet(R"({"id":"q","text":"stat"})");
    QCOMPARE(busy.names.back(), QStringLiteral("MMapper.Input.Reply"));
    QCOMPARE(busy.reason(), QStringLiteral("busy"));
    QCOMPARE(busy.last()["id"].toString(), QStringLiteral("q"));
    QCOMPARE(busy.sent, QStringList{"prac"});
    Mume shop;
    shop.request(R"({"id":"r","action":"shop.list"})");
    shop.requestQuiet(R"({"id":"s","text":"prac"})");
    QCOMPARE(shop.reason(), QStringLiteral("busy"));
    QCOMPARE(shop.last()["id"].toString(), QStringLiteral("s"));
    QCOMPARE(shop.sent, QStringList{"list"});

    // The player's reply is at the pager: the command would answer it.
    Mume paging;
    paging.player(QStringLiteral("trop"));
    paging.flush();
    paging.pager(20);
    paging.requestQuiet(R"({"id":"t","text":"prac"})");
    QCOMPARE(paging.status(), QStringLiteral("refused"));
    QCOMPARE(paging.reason(), QStringLiteral("pager open"));
    QCOMPARE(paging.sent, QStringList{"trop"});
}

void TestTradeOperations::quietFailuresTest()
{
    // No reply in time: failed, and the parser is told at once that it is over.
    Mume mume;
    mume.requestQuiet(R"({"id":"k","text":"prac"})");
    mume.flush();
    mume.clock += TradeOperations::QUIET_TIMEOUT_MS - 1;
    mume.trade->expire();
    QVERIFY(mume.ops.empty());
    mume.clock += 1;
    mume.trade->expire();
    QCOMPARE(mume.status(), QStringLiteral("failed"));
    QCOMPARE(mume.reason(), QStringLiteral("timeout"));
    QVERIFY(mume.last()["lines"].toArray().isEmpty());
    QVERIFY(!mume.last()["complete"].toBool());
    QCOMPARE(mume.quiet, (QStringList{"begin", "end"}));
    QVERIFY(!mume.trade->busy());

    // Timing out at the hidden pager quits it, and hands over what came.
    Mume paged;
    paged.requestQuiet(R"({"id":"l","text":"prac"})");
    paged.flush();
    paged.quietLine(QStringLiteral("You have 0 practice sessions left."));
    paged.quietPager(40);
    paged.flush();
    paged.quietPager(40);
    paged.clock += TradeOperations::QUIET_TIMEOUT_MS;
    paged.trade->expire();
    QCOMPARE(paged.status(), QStringLiteral("failed"));
    QCOMPARE(paged.sent.back(), QStringLiteral("q"));
    QCOMPARE(paged.last()["lines"].toArray().at(0).toString(),
             QStringLiteral("You have 0 practice sessions left."));

    // A line that never reaches MUME (MMapper kept or rewrote it on the way): the window is
    // never opened, it times out, and what is sent afterwards is still matched.
    Mume lost;
    lost.requestQuiet(R"({"id":"m","text":"look"})");
    lost.pending.clear();
    lost.clock += TradeOperations::QUIET_TIMEOUT_MS;
    lost.trade->expire();
    QCOMPARE(lost.reason(), QStringLiteral("timeout"));
    QVERIFY(lost.quiet.isEmpty());
    lost.requestQuiet(R"({"id":"n","text":"prac"})");
    lost.flush();
    QCOMPARE(lost.quiet, QStringList{"begin"});

    // What is no line at all, or cannot go now.
    Mume bad;
    bad.requestQuiet(R"({"id":"a","text":"info )" + std::string(100, 'x') + R"("})");
    QCOMPARE(bad.status(), QStringLiteral("refused"));
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    bad.requestQuiet(R"({"id":"c"})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    bad.requestQuiet(R"({"id":"d","text":7})");
    QCOMPARE(bad.reason(), QStringLiteral("invalid arguments"));
    QVERIFY(bad.sent.isEmpty());
    TradeOperations::Context ctx = Mume::context();
    ctx.game = GameStateEnum::MENU;
    bad.trade->requestQuiet(json(R"({"id":"e","text":"prac"})"), ctx);
    QCOMPARE(bad.reason(), QStringLiteral("not in the game"));
    ctx = Mume::context();
    ctx.echo = false;
    bad.trade->requestQuiet(json(R"({"id":"f","text":"prac"})"), ctx);
    QCOMPARE(bad.reason(), QStringLiteral("echo off"));
    ctx = Mume::context();
    ctx.connected = false;
    bad.trade->requestQuiet(json(R"({"id":"g","text":"prac"})"), ctx);
    QCOMPARE(bad.reason(), QStringLiteral("not connected"));
    ctx = Mume::context();
    ctx.driving = false;
    bad.trade->requestQuiet(json(R"({"id":"h","text":"prac"})"), ctx);
    QCOMPARE(bad.reason(), QStringLiteral("observing"));
    QVERIFY(bad.sent.isEmpty());

    // The link lost, and the session released.
    Mume gone;
    gone.requestQuiet(R"({"id":"o","text":"prac"})");
    gone.flush();
    gone.observer.observeDisconnected();
    QCOMPARE(gone.status(), QStringLiteral("stopped"));
    QCOMPARE(gone.quiet, (QStringList{"begin", "end"}));
    Mume released;
    released.requestQuiet(R"({"id":"p","text":"prac"})");
    released.flush();
    released.trade->setDriving(false);
    QCOMPARE(released.reason(), QStringLiteral("session released"));
    QCOMPARE(released.quiet, (QStringList{"begin", "end"}));
}

void TestTradeOperations::quietWholeTest()
{
    // The readers and the runner together, as in MMapper: a short `prac`. Nothing of it
    // reaches the terminal; MMapper.Char.Skills is published as for a typed one, before the
    // reply.
    {
        Whole mume;
        mume.prompt();
        mume.requestQuiet(R"({"id":"k","text":"prac"})");
        mume.flush();
        mume.lines({"You have 0 practice sessions left.",
                    "Skill / Spell        Knowledge  Difficulty  Class       Mana  Casting time",
                    "Bandage              Average    Easy        None       ",
                    "Cure light           Average    Easy        Cleric        13  Very short",
                    ""});
        mume.prompt();
        QCOMPARE(mume.shown, QStringList{"o W C Mana:Hot>"});
        QCOMPARE(mume.packages,
                 (QStringList{"skills:2:1", "MMapper.Terminal.Hidden", "MMapper.Input.Reply"}));
        QCOMPARE(mume.status(), QStringLiteral("done"));
        QCOMPARE(mume.replyLines().size(), qsizetype{4});
        QCOMPARE(mume.replyLines().front(), QStringLiteral("You have 0 practice sessions left."));
        QVERIFY(mume.last()["complete"].toBool());
        QCOMPARE(mume.sent, QStringList{"prac"});
        QVERIFY(!mume.readers.quietOpen());
    }

    // A table longer than the player's page: the pager is answered and never seen, and the
    // table comes whole.
    {
        Whole mume;
        mume.requestQuiet(R"({"id":"k","text":"prac"})");
        mume.flush();
        mume.lines(g_pageOne);
        mume.pager(73);
        QCOMPARE(mume.sent, (QStringList{"prac", ""}));
        mume.flush();
        mume.lines(g_pageTwo);
        mume.prompt();
        QVERIFY(mume.shown.isEmpty());
        QCOMPARE(mume.packages,
                 (QStringList{"skills:4:1", "MMapper.Terminal.Hidden", "MMapper.Input.Reply"}));
        QCOMPARE(mume.status(), QStringLiteral("done"));
        QVERIFY(mume.last()["paged"].toBool());
        QVERIFY(mume.last()["complete"].toBool());
        QCOMPARE(mume.replyLines().size(), qsizetype{6});
        // The next line the player sends is a command again, not a pager's answer.
        QVERIFY(mume.readers.receiveCommand(QStringLiteral("look")));
    }

    // A second command, `stat` (powwow/logs/stolb.balrog.txt, as in TestCharLines): hidden,
    // handed over, and MMapper.Char.Stat's reader still reads it.
    {
        Whole mume;
        mume.requestQuiet(R"({"id":"s","text":"stat"})");
        mume.flush();
        mume.lines({"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.",
                    "Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal.",
                    ""});
        mume.prompt();
        QVERIFY(mume.shown.isEmpty());
        QCOMPARE(mume.packages,
                 (QStringList{"stat:131", "MMapper.Terminal.Hidden", "MMapper.Input.Reply"}));
        QCOMPARE(mume.replyLines(),
                 (QStringList{"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.",
                              "Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal."}));
        QCOMPARE(mume.last()["text"].toString(), QStringLiteral("stat"));
    }

    // A tell while the reply waits at the pager is shown and is no part of the reply; the
    // table still comes whole.
    {
        Whole mume;
        mume.requestQuiet(R"({"id":"k","text":"prac"})");
        mume.flush();
        mume.lines(g_pageOne);
        mume.pager(73);
        mume.lines({""});
        mume.other("Gandalf tells you 'are you there?'");
        mume.pager(73);
        mume.flush();
        mume.lines(g_pageTwo);
        mume.prompt();
        QCOMPARE(mume.shown,
                 (QStringList{"Gandalf tells you 'are you there?'\n", "o W C Mana:Hot>"}));
        QCOMPARE(mume.sent, (QStringList{"prac", ""}));
        QCOMPARE(mume.packages,
                 (QStringList{"skills:4:1", "MMapper.Terminal.Hidden", "MMapper.Input.Reply"}));
        QCOMPARE(mume.status(), QStringLiteral("done"));
        QVERIFY(!mume.replyLines().contains(QStringLiteral("Gandalf tells you 'are you there?'")));
    }

    // The player types right after the command went: the player's reply comes after the
    // command's prompt, and is shown whole.
    {
        Whole mume;
        mume.requestQuiet(R"({"id":"k","text":"stat"})");
        mume.flush();
        mume.player(QStringLiteral("inventory"));
        mume.flush();
        mume.lines({"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.", ""});
        mume.prompt();
        QCOMPARE(mume.status(), QStringLiteral("done"));
        mume.lines({"You are carrying:", "a lantern", ""});
        mume.prompt();
        QCOMPARE(mume.shown,
                 (QStringList{"You are carrying:\n", "a lantern\n", "\n", "o W C Mana:Hot>"}));
    }

    // The player types while the runner holds the pager: `q`, then the player's line. The
    // window is closed at once, and the player's reply is shown.
    {
        Whole mume;
        mume.requestQuiet(R"({"id":"k","text":"prac"})");
        mume.flush();
        mume.lines(g_pageOne);
        mume.pager(73);
        mume.flush();
        mume.pager(73);
        mume.player(QStringLiteral("look"));
        QCOMPARE(mume.pending, (QStringList{"q", "look"}));
        QCOMPARE(mume.status(), QStringLiteral("stopped"));
        QVERIFY(!mume.readers.quietOpen());
        mume.flush();
        mume.prompt();
        mume.lines({"A dark cave"});
        mume.prompt();
        QCOMPARE(mume.shown,
                 (QStringList{"o W C Mana:Hot>", "A dark cave\n", "o W C Mana:Hot>"}));
    }

    // The player's own `prac` is the player's: shown, its pager shown and left alone.
    {
        Whole mume;
        mume.player(QStringLiteral("prac"));
        mume.flush();
        mume.lines(g_pageOne);
        mume.pager(73);
        QCOMPARE(mume.shown.size(), qsizetype{5});
        QCOMPARE(mume.sent, QStringList{"prac"});
        QVERIFY(mume.readers.pagerOpen());
        mume.player(QStringLiteral(""));
        mume.flush();
        mume.lines(g_pageTwo);
        mume.prompt();
        QCOMPARE(mume.shown.size(), qsizetype{9});
        QCOMPARE(mume.packages, QStringList{"skills:4:1"});
        QVERIFY(mume.replies.empty());
    }

    // The reply never comes: after four seconds the window is closed, and whatever MUME says
    // from then on is shown, the late reply included.
    {
        Whole mume;
        mume.requestQuiet(R"({"id":"k","text":"prac"})");
        mume.flush();
        mume.clock += TradeOperations::QUIET_TIMEOUT_MS;
        mume.trade->expire();
        QCOMPARE(mume.status(), QStringLiteral("failed"));
        QCOMPARE(mume.reason(), QStringLiteral("timeout"));
        QVERIFY(!mume.readers.quietOpen());
        mume.lines(g_pageOne);
        QCOMPARE(mume.shown.size(), qsizetype{4});
    }
}

void TestTradeOperations::tooManyPagesTest()
{
    Mume mume;
    mume.request(R"({"id":"t","action":"char.trophies"})");
    mume.flush();
    for (int page = 1; page <= TradeOperations::MAX_PAGES; ++page) {
        mume.line(QStringLiteral("row"));
        mume.pager(page * 4);
        QCOMPARE(mume.sent.size(), page + 1);
        QCOMPARE(mume.sent.back(), QString{});
        mume.flush();
    }
    mume.line(QStringLiteral("row"));
    mume.pager(90);
    QCOMPARE(mume.sent.back(), QStringLiteral("q"));
    QCOMPARE(mume.status(), QStringLiteral("stopped"));
    QCOMPARE(mume.reason(), QStringLiteral("too many pages"));
    mume.flush();
    QVERIFY(mume.ops.size() == 2);
}

void TestTradeOperations::playerPreemptionTest()
{
    Mume mume;
    mume.request(R"({"id":"t","action":"char.trophies"})");
    mume.flush();
    mume.line(QStringLiteral("row"));
    mume.pager(30);
    mume.flush();
    // The pager is showing again and the runner holds it when the player types.
    mume.line(QStringLiteral("Gandalf arrives from the north."));
    mume.pager(30);
    mume.player(QStringLiteral("north"));
    QCOMPARE(mume.pending, (QStringList{"q", "north"}));
    QCOMPARE(mume.status(), QStringLiteral("stopped"));
    QCOMPARE(mume.reason(), QStringLiteral("player input"));
    const size_t published = mume.ops.size();
    mume.flush();
    QCOMPARE(mume.ops.size(), published);
    QVERIFY(!mume.trade->busy());

    // Without a pager held, the player's line goes alone, and still stops the operation.
    Mume plain;
    plain.request(R"({"id":"u","action":"shop.list"})");
    plain.flush();
    plain.player(QStringLiteral("look"));
    QCOMPARE(plain.pending, QStringList{"look"});
    QCOMPARE(plain.status(), QStringLiteral("stopped"));
    QCOMPARE(plain.reason(), QStringLiteral("player input"));
}

void TestTradeOperations::foreignLineTest()
{
    Mume mume;
    mume.request(R"({"id":"s","action":"shop.sell","items":["sword","helm"]})");
    mume.flush();
    // Something the runner did not send goes to MUME: an alias, a quiet refresh.
    mume.observer.observeSentToMud(QStringLiteral("inventory\n"));
    QCOMPARE(mume.status(), QStringLiteral("stopped"));
    QCOMPARE(mume.reason(), QStringLiteral("overlapping command"));
    QVERIFY(!mume.trade->busy());
    mume.observer.observeShopDeal(deal(ShopDealKindEnum::SELL));
    mume.prompt();
    QCOMPARE(mume.sent.size(), 1);

    // The runner's own lines, in flight while the foreign one overtakes nothing, are its own:
    // a new operation right after is not disturbed by the old line's echo.
    Mume order;
    order.request(R"({"id":"a","action":"shop.list"})");
    order.flush();
    order.line(QStringLiteral("You can buy:"));
    order.observer.observeShopList(ShopList{});
    order.prompt();
    order.request(R"({"id":"b","action":"char.trophies"})");
    QCOMPARE(order.status(), QStringLiteral("running"));
    order.flush();
    QCOMPARE(order.status(), QStringLiteral("running"));
}

void TestTradeOperations::timeoutTest()
{
    Mume mume;
    mume.request(R"({"id":"t","action":"char.trophies"})");
    mume.flush();
    mume.clock += TradeOperations::STEP_TIMEOUT_MS - 1;
    mume.trade->expire();
    QCOMPARE(mume.status(), QStringLiteral("running"));
    mume.clock += 2;
    mume.trade->expire();
    QCOMPARE(mume.status(), QStringLiteral("failed"));
    QCOMPARE(mume.reason(), QStringLiteral("timeout"));
    QVERIFY(!mume.trade->busy());

    // A pager answer gives the step its time again; timing out while holding a pager quits it.
    Mume paged;
    paged.request(R"({"id":"u","action":"char.trophies"})");
    paged.flush();
    paged.clock += 10000;
    paged.line(QStringLiteral("row"));
    paged.pager(40);
    paged.flush();
    paged.clock += 10000;
    paged.trade->expire();
    QCOMPARE(paged.status(), QStringLiteral("running"));
    paged.line(QStringLiteral("Someone shouts 'hi'"));
    paged.pager(40);
    paged.clock += 6000;
    paged.trade->expire();
    QCOMPARE(paged.status(), QStringLiteral("failed"));
    QCOMPARE(paged.sent.back(), QStringLiteral("q"));
}

void TestTradeOperations::refusalsTest()
{
    Mume mume;
    mume.request(R"({"id":"a","action":"shop.value","item":"sword"})");
    QCOMPARE(mume.status(), QStringLiteral("refused"));
    QCOMPARE(mume.reason(), QStringLiteral("unsupported"));
    QCOMPARE(mume.last()["id"].toString(), QStringLiteral("a"));
    QCOMPARE(mume.last()["step"].toInt(), 0);
    mume.request(R"({"id":"b","action":"dance"})");
    QCOMPARE(mume.reason(), QStringLiteral("unsupported"));

    auto ctx = Mume::context();
    ctx.connected = false;
    mume.trade->request(json(R"({"id":"c","action":"shop.list"})"), ctx);
    QCOMPARE(mume.reason(), QStringLiteral("not connected"));
    ctx = Mume::context();
    ctx.game = GameStateEnum::MENU;
    mume.trade->request(json(R"({"id":"d","action":"shop.list"})"), ctx);
    QCOMPARE(mume.reason(), QStringLiteral("not in the game"));
    ctx = Mume::context();
    ctx.driving = false;
    mume.trade->request(json(R"({"id":"e","action":"shop.list"})"), ctx);
    QCOMPARE(mume.reason(), QStringLiteral("observing"));
    ctx = Mume::context();
    ctx.echo = false;
    mume.trade->request(json(R"({"id":"f","action":"shop.list"})"), ctx);
    QCOMPARE(mume.reason(), QStringLiteral("echo off"));
    QVERIFY(mume.sent.isEmpty());

    // One at a time overall.
    mume.request(R"({"id":"g","action":"shop.list"})");
    mume.request(R"({"id":"h","action":"char.trophies"})");
    QCOMPARE(mume.last()["id"].toString(), QStringLiteral("h"));
    QCOMPARE(mume.reason(), QStringLiteral("busy"));
    QCOMPARE(mume.sent, QStringList{"list"});
    mume.flush();
    mume.observer.observeShopList(ShopList{});
    mume.line(QStringLiteral("You can buy:"));
    mume.prompt();
    QCOMPARE(mume.last()["id"].toString(), QStringLiteral("g"));
    QCOMPARE(mume.status(), QStringLiteral("done"));

    // The player's reply is at a pager: whatever the runner sent would answer it.
    mume.player(QStringLiteral("help"));
    mume.flush();
    mume.pager(10);
    mume.request(R"({"id":"i","action":"shop.list"})");
    QCOMPARE(mume.reason(), QStringLiteral("pager open"));
    // Once the player answered it, the way is free.
    mume.player(QStringLiteral("q"));
    mume.flush();
    mume.request(R"({"id":"j","action":"shop.list"})");
    QCOMPARE(mume.status(), QStringLiteral("running"));

    // Nothing can be sent while no frontend drives.
    Mume released;
    released.trade->setDriving(false);
    released.request(R"({"id":"k","action":"shop.list"})");
    QCOMPARE(released.reason(), QStringLiteral("observing"));
}

void TestTradeOperations::cancelTest()
{
    Mume mume;
    mume.request(R"({"id":"s","action":"shop.sell","items":["sword","helm"]})");
    mume.flush();
    mume.trade->cancel(QStringLiteral("other"));
    QCOMPARE(mume.status(), QStringLiteral("running"));
    mume.trade->cancel(QStringLiteral("s"));
    QCOMPARE(mume.status(), QStringLiteral("cancelled"));
    QVERIFY(!mume.trade->busy());
    // The reply to what already went out comes, and nothing more is sent.
    mume.observer.observeShopDeal(deal(ShopDealKindEnum::SELL));
    mume.line(QStringLiteral("You sell a sword."));
    mume.prompt();
    QCOMPARE(mume.sent, QStringList{"sell sword"});
    // A second cancel is ignored: the last status has gone out.
    const size_t published = mume.ops.size();
    mume.trade->cancel(QStringLiteral("s"));
    QCOMPARE(mume.ops.size(), published);

    // Cancelling while the runner holds a pager quits it.
    Mume paged;
    paged.request(R"({"id":"t","action":"char.trophies"})");
    paged.flush();
    paged.line(QStringLiteral("row"));
    paged.pager(30);
    paged.flush();
    paged.line(QStringLiteral("Someone arrives."));
    paged.pager(30);
    paged.trade->cancel(QStringLiteral("t"));
    QCOMPARE(paged.sent.back(), QStringLiteral("q"));
    QCOMPARE(paged.status(), QStringLiteral("cancelled"));
}

void TestTradeOperations::abortTest()
{
    {
        Mume mume;
        mume.request(R"({"id":"a","action":"shop.list"})");
        mume.observer.observeDisconnected();
        QCOMPARE(mume.status(), QStringLiteral("stopped"));
        QCOMPARE(mume.reason(), QStringLiteral("disconnected"));
    }
    {
        Mume mume;
        mume.request(R"({"id":"b","action":"shop.list"})");
        mume.flush();
        mume.observer.observeGameState(GameStateEnum::QUIT);
        QCOMPARE(mume.reason(), QStringLiteral("left the game"));
    }
    {
        Mume mume;
        mume.request(R"({"id":"c","action":"shop.list"})");
        mume.observer.observeToggledEchoMode(false);
        QCOMPARE(mume.reason(), QStringLiteral("echo changed"));
    }
    {
        Mume mume;
        mume.request(R"({"id":"d","action":"shop.list"})");
        mume.trade->setDriving(false);
        QCOMPARE(mume.reason(), QStringLiteral("session released"));
        mume.trade->abort(QStringLiteral("again"));
        QCOMPARE(mume.reason(), QStringLiteral("session released"));
    }
}

void TestTradeOperations::viewerClaimTest()
{
    FrontendSubscriptions viewer;
    QVERIFY(viewer.applySupports(GmcpMessage::fromRawBytes(
        QByteArray{R"(Core.Supports.Set ["MMapper.View 1", "MMapper.Trade 1"])"})));
    FrontendSubscriptions other;
    QVERIFY(other.applySupports(
        GmcpMessage::fromRawBytes(QByteArray{R"(Core.Supports.Set ["MMapper.Trade 1"])"})));

    QVERIFY(viewer.subscribes("MMapper.View"));
    QVERIFY(viewer.subscribes("mmapper.view"));
    QVERIFY(!viewer.subscribes("MMapper"));
    QVERIFY(!other.subscribes("MMapper.View"));

    // Only the driving frontend claims the viewer, so an observer never takes viewed texts away
    // from a telnet player.
    QVERIFY(claimsViewer(viewer, true));
    QVERIFY(!claimsViewer(viewer, false));
    QVERIFY(!claimsViewer(other, true));

    QVERIFY(viewer.applySupports(
        GmcpMessage::fromRawBytes(QByteArray{R"(Core.Supports.Remove ["MMapper.View"])"})));
    QVERIFY(!claimsViewer(viewer, true));
}

void TestTradeOperations::viewerSettingTest()
{
    Mume mume;
    // Not claimed: never set.
    mume.prompt();
    QVERIFY(mume.sent.isEmpty());
    QCOMPARE(mume.trade->viewerState(), QStringLiteral("unknown"));

    // Claimed: set at the first real prompt in the game, once.
    mume.observer.setViewerClaimed(true);
    mume.prompt();
    QCOMPARE(mume.sent, QStringList{"change viewer external"});
    QCOMPARE(mume.trade->viewerState(), QStringLiteral("external"));
    QCOMPARE(mume.viewerChanges, 1);
    mume.flush();
    mume.prompt();
    QCOMPARE(mume.sent.size(), 1);

    // A new login sets it again.
    mume.observer.observeGameState(GameStateEnum::MENU);
    mume.prompt();
    QCOMPARE(mume.sent.size(), 1);
    mume.observer.observeGameState(GameStateEnum::PLAYING);
    mume.prompt();
    QCOMPARE(mume.sent.size(), 2);
    mume.flush();

    // Not while an operation runs, nor at the player's pager: it waits for a free prompt.
    mume.observer.observeGameState(GameStateEnum::MENU);
    mume.observer.observeGameState(GameStateEnum::PLAYING);
    mume.request(R"({"id":"a","action":"shop.list"})");
    mume.flush();
    mume.prompt();
    QCOMPARE(mume.sent.back(), QStringLiteral("list"));
    // The prompt that ends the operation is free again.
    mume.line(QStringLiteral("You can buy:"));
    mume.observer.observeShopList(ShopList{});
    mume.prompt();
    QCOMPARE(mume.status(), QStringLiteral("done"));
    QCOMPARE(mume.sent.back(), QStringLiteral("change viewer external"));
    mume.flush();

    // Nor while the player's reply is at a pager.
    mume.observer.observeGameState(GameStateEnum::MENU);
    mume.observer.observeGameState(GameStateEnum::PLAYING);
    mume.player(QStringLiteral("help"));
    mume.flush();
    mume.pager(10);
    QCOMPARE(mume.sent.back(), QStringLiteral("help"));

    // The player turns it back: recorded, and never set again in this connection.
    mume.player(QStringLiteral("cha vie simp"));
    mume.flush();
    QCOMPARE(mume.trade->viewerState(), QStringLiteral("simple"));
    mume.observer.observeGameState(GameStateEnum::MENU);
    mume.observer.observeGameState(GameStateEnum::PLAYING);
    mume.prompt();
    QCOMPARE(mume.sent.back(), QStringLiteral("cha vie simp"));
    mume.player(QStringLiteral("change viewer off"));
    mume.flush();
    QCOMPARE(mume.trade->viewerState(), QStringLiteral("off"));
    // Not a value MUME knows: nothing changes.
    mume.player(QStringLiteral("change viewer purple"));
    mume.flush();
    QCOMPARE(mume.trade->viewerState(), QStringLiteral("off"));

    // A new connection starts again from nothing.
    mume.observer.observeConnected();
    QCOMPARE(mume.trade->viewerState(), QStringLiteral("unknown"));
    mume.observer.observeGameState(GameStateEnum::PLAYING);
    mume.prompt();
    QCOMPARE(mume.sent.back(), QStringLiteral("change viewer external"));
}

QTEST_MAIN(TestTradeOperations)
