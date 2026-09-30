// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestTradeOperations.h"

#include "../src/frontend/FrontendSubscriptions.h"
#include "../src/frontend/TradeOperations.h"
#include "../src/observer/gameobserver.h"
#include "../src/parser/GameStateLines.h"
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
    std::unique_ptr<TradeOperations> trade;

    Mume()
    {
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
