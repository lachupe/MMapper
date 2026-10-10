// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestLogRules.h"

#include "../src/frontend/ImportantLog.h"
#include "../src/frontend/LogRules.h"
#include "../src/observer/gameobserver.h"
#include "../src/parser/CharLines.h"
#include "../src/parser/CombatLines.h"
#include "../src/parser/ItemLines.h"
#include "../src/parser/LineTags.h"
#include "../src/parser/TradeLines.h"
#include "../src/parser/XmlElement.h"
#include "../src/proxy/GmcpMessage.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

NODISCARD QByteArray rulesDoc(const char *const rules)
{
    return QByteArray{R"({"version":1,"rules":)"} + QByteArray{rules} + QByteArray{"}"};
}

NODISCARD QJsonObject object(const char *const json)
{
    return QJsonDocument::fromJson(QByteArray{json}).object();
}

NODISCARD LineTagSet tagsOf(const QStringList &names)
{
    LineTagSet tags;
    for (const QString &name : names) {
        const auto tag = lineTagFromName(name);
        if (tag.has_value()) {
            tags.insert(*tag);
        }
    }
    return tags;
}

NODISCARD LogLine lineOf(const QString &plain,
                         const QStringList &tags = {},
                         const QString &about = QStringLiteral("other"))
{
    LogLine line;
    line.plain = plain;
    line.tags = tagsOf(tags);
    line.about = about;
    return line;
}

NODISCARD bool matches(const QString &templateText, const QString &line)
{
    const auto compiled = compileLogTemplate(templateText);
    return compiled.has_value()
           && QRegularExpression{compiled->expression}.match(line.trimmed()).hasMatch();
}

NODISCARD QJsonObject payloadOf(const GmcpMessage &msg)
{
    const auto &json = msg.getJson();
    return json.has_value() ? QJsonDocument::fromJson(json->toQByteArray()).object()
                            : QJsonObject{};
}

NODISCARD GmcpMessage gmcp(const char *const raw)
{
    return GmcpMessage::fromRawBytes(QByteArray{raw});
}

NODISCARD QByteArray readAll(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray{};
    }
    return file.readAll();
}

/// The readers as MumeXmlParser::parse() drives them, and the record it makes of each chunk:
/// the same order, the same claims (see the `facts` lines there).
struct NODISCARD Tagging final
{
    GameObserver observer;
    TradeReaders readers{observer};
    CharLinesTracker chars;
    ItemBlockTracker items;
    LineTagger tagger;
    std::vector<LineFacts> records;

    void chunk(const bool goAhead,
               const QString &text,
               const bool inRoom = false,
               const bool description = false,
               const std::vector<XmlElement> &elements = {},
               const bool commOpen = false)
    {
        const MudChunk c = readers.beginChunk(goAhead, false, text);
        LineFacts facts;
        const bool wants = !c.hidden && c.kind != MudChunkKindEnum::TWIDDLER
                           && !c.plain.trimmed().isEmpty();
        if (c.kind == MudChunkKindEnum::PROMPT) {
            facts.tags.insert(LineTagEnum::PROMPT);
        } else if (c.kind == MudChunkKindEnum::PAGER) {
            facts.tags.insert(LineTagEnum::PAGER);
        }
        if (c.captured) {
            facts.tags.insert(LineTagEnum::QUIET);
        }
        if (inRoom) {
            facts.tags.insert(LineTagEnum::ROOM);
            if (description) {
                facts.tags.insert(LineTagEnum::ROOM_DESC);
            }
        }
        if (const auto combat = parseCombatLine(c.plain)) {
            tagCombat(facts, *combat);
        }
        if (!goAhead) {
            std::ignore = items.receiveLine(c.plain);
            if (items.lastLineClaimed()) {
                facts.tags.insert(LineTagEnum::ITEMS);
            }
            std::ignore = chars.receiveLine(c.plain);
            if (chars.lastLineKind() == CharLineKindEnum::STAT) {
                facts.tags.insert(LineTagEnum::STAT);
            }
            readers.receiveLine(c.plain);
            if (readers.lastLineKind() == TradeLineKindEnum::SHOP) {
                facts.tags.insert(LineTagEnum::SHOP);
            }
        }
        if (c.kind == MudChunkKindEnum::PROMPT) {
            std::ignore = items.receivePrompt();
            std::ignore = chars.receivePrompt();
            readers.receivePrompt();
        }
        if (wants) {
            tagElements(facts, elements);
            facts.text = text;
            facts.plain = c.plain.trimmed();
            for (LineFacts &record : tagger.finish(std::move(facts), commOpen)) {
                records.push_back(std::move(record));
            }
        }
        readers.endChunk();
    }
    void line(const QString &text) { chunk(false, text + QStringLiteral("\n")); }
    void prompt() { chunk(true, QStringLiteral("o HP:Fine>")); }
    NODISCARD QStringList tagsOfLast() const
    {
        return records.empty() ? QStringList{} : records.back().tags.names();
    }
};

} // namespace

void TestLogRules::templateTest()
{
    QVERIFY(matches("<N> pats you on the head.", "Kili pats you on the head."));
    QVERIFY(matches("<N> pats you on the head.", "  Kili pats you on the head.  "));
    // Anchored at both ends.
    QVERIFY(!matches("<N> pats you on the head.", "Kili pats you on the head. Again."));
    QVERIFY(!matches("You flee <O>.", "Then You flee west."));
    // `*` is anything; a run of spaces any whitespace.
    QVERIFY(matches("The sun * east.", "The sun rises in the east."));
    QVERIFY(matches("You  flee <O>.", "You flee\t west."));
    QVERIFY(!matches("You flee <O>.", "Youflee west."));
    // `\` takes the next character as it is: a star, a placeholder's bracket.
    QVERIFY(matches("\\*<N>\\* is dead! R.I.P.", "*an Orc* is dead! R.I.P."));
    QVERIFY(!matches("\\*<N>\\* is dead! R.I.P.", "an Orc is dead! R.I.P."));
    QVERIFY(matches("a \\<N> b", "a <N> b"));
    QVERIFY(!matches("a \\<N> b", "a Kili b"));
    // Regular expression characters are literal.
    QVERIFY(matches("(buh) [x] 1+1?", "(buh) [x] 1+1?"));
    QVERIFY(!matches("1+1?", "11"));
    // Numbers and quoted words.
    QVERIFY(matches("You have <#> gold.", "You have 1,234 gold."));
    QVERIFY(!matches("You have <#> gold.", "You have many gold."));
    QVERIFY(matches("<N> says '<Q>'", "Kili says 'hi there'"));
    QVERIFY(!matches("<N> says '<Q>'", "Kili says 'it's'"));
    QVERIFY(!compileLogTemplate("   ").has_value());

    // What the index keys on.
    const auto first = compileLogTemplate("You flee <O>.");
    QCOMPARE(first->firstWord, QStringLiteral("You"));
    QVERIFY(first->lastWord.isEmpty());
    const auto last = compileLogTemplate("<N> pats you on the head.");
    QVERIFY(last->firstWord.isEmpty());
    QCOMPARE(last->lastWord, QStringLiteral("head."));
    QCOMPARE(compileLogTemplate("Ok.")->firstWord, QStringLiteral("Ok."));
}

void TestLogRules::precedenceTest()
{
    QTemporaryDir dir;
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(rulesDoc(R"([
        {"id":"d.say","tags":["comm.say"],"priority":6},
        {"id":"d.text","text":"<N> says '<Q>'","priority":3},
        {"id":"d.fight","text":"<N> says '<Q>'","when":{"inFight":true},"priority":5},
        {"id":"d.long","text":"Kili says '<Q>'","priority":7},
        {"id":"d.newer","text":"<N> says '<Q>'","priority":4},
        {"id":"d.regex","regex":"tells the group","priority":8},
        {"id":"d.not","tags":["comm"],"notTags":["comm.say"],"priority":1}])")));
    rules.loadUserFile(dir.filePath(QStringLiteral("rules.json")));
    const QStringList say{QStringLiteral("comm"), QStringLiteral("comm.say")};
    const LogContext calm;
    LogContext fight;
    fight.inFight = true;

    // A line no rule knows: 2, no rule.
    QCOMPARE(rules.classify(lineOf("Something odd."), calm).priority, 2);
    QVERIFY(rules.classify(lineOf("Something odd."), calm).rule == nullptr);
    // Tags only.
    QCOMPARE(rules.classify(lineOf("Bob sings."), calm).priority, 2);
    QCOMPARE(rules.classify(lineOf("Bob mumbles.", say), calm).rule->id, QStringLiteral("d.say"));
    // notTags.
    QCOMPARE(rules.classify(lineOf("Bob shouts.", {QStringLiteral("comm")}), calm).priority, 1);
    // A text rule beats a tags-only one; of equal texts, the newer.
    QCOMPARE(rules.classify(lineOf("Bob says 'x'", say), calm).rule->id, QStringLiteral("d.newer"));
    // The longer literal text.
    QCOMPARE(rules.classify(lineOf("Kili says 'x'", say), calm).rule->id, QStringLiteral("d.long"));
    // More `when` conditions beat the longer text.
    QCOMPARE(rules.classify(lineOf("Kili says 'x'", say), fight).rule->id,
             QStringLiteral("d.fight"));
    // A regex is unanchored.
    QCOMPARE(rules.classify(lineOf("Kili tells the group 'heal'"), calm).priority, 8);

    // A default switched off.
    QVERIFY(rules.deleteRule(QStringLiteral("d.newer")).ok);
    QCOMPARE(rules.disabled(), QStringList{QStringLiteral("d.newer")});
    QCOMPARE(rules.classify(lineOf("Bob says 'x'", say), calm).rule->id, QStringLiteral("d.text"));

    // The user's layer beats the defaults, even a tags-only rule of the user's.
    const auto edit = rules.setRule(object(R"({"tags":["comm.say"],"priority":9})"),
                                    QStringLiteral("2026-10-10T21:04:00Z"));
    QVERIFY(edit.ok);
    QCOMPARE(edit.id, QStringLiteral("u1"));
    QCOMPARE(rules.classify(lineOf("Kili says 'x'", say), fight).priority, 9);
    // Within the user's layer, the same order.
    QVERIFY(rules
                .setRule(object(R"({"text":"Kili says '<Q>'","priority":10,"route":"screen"})"),
                         QStringLiteral("2026-10-10T21:05:00Z"))
                .ok);
    const LogResult kili = rules.classify(lineOf("Kili says 'x'", say), fight);
    QCOMPARE(kili.priority, 10);
    QCOMPARE(kili.route, LogRouteEnum::SCREEN);
    QCOMPARE(kili.rule->id, QStringLiteral("u2"));
    // Replacing a rule by its id.
    QVERIFY(rules
                .setRule(object(R"({"id":"u2","text":"Kili says '<Q>'","priority":1})"),
                         QStringLiteral("2026-10-10T21:06:00Z"))
                .ok);
    QCOMPARE(rules.userRules().size(), size_t{2});
    QCOMPARE(rules.classify(lineOf("Kili says 'x'", say), fight).priority, 1);
    // Deleting a user rule.
    QVERIFY(rules.deleteRule(QStringLiteral("u2")).ok);
    QCOMPARE(rules.classify(lineOf("Kili says 'x'", say), fight).priority, 9);
}

void TestLogRules::whenTest()
{
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(rulesDoc(R"([
        {"id":"d.grouped","text":"Help!","when":{"grouped":true},"priority":7},
        {"id":"d.low","text":"Ouch.","when":{"hpBelow":0.5},"priority":6},
        {"id":"d.you","tags":["combat"],"when":{"who":["you","group"]},"priority":4},
        {"id":"d.blow","tags":["combat.blow"],"priority":0},
        {"id":"d.swim","text":"Glub.","when":{"swimming":true},"priority":9},
        {"id":"d.tag","tags":["no.such.tag"],"priority":9}])")));
    LogContext context;
    QCOMPARE(rules.classify(lineOf("Help!"), context).priority, 2);
    context.grouped = true;
    QCOMPARE(rules.classify(lineOf("Help!"), context).priority, 7);
    QCOMPARE(rules.classify(lineOf("Ouch."), context).priority, 2);
    context.hpFraction = 0.4;
    QCOMPARE(rules.classify(lineOf("Ouch."), context).priority, 6);
    const QStringList blow{QStringLiteral("combat"), QStringLiteral("combat.blow")};
    QCOMPARE(rules.classify(lineOf("An orc hits Bob.", blow), context).priority, 0);
    QCOMPARE(rules.classify(lineOf("An orc hits you.", blow, QStringLiteral("you")), context)
                 .priority,
             4);
    QCOMPARE(rules.classify(lineOf("An orc hits Kili.", blow, QStringLiteral("group")), context)
                 .priority,
             4);
    // A state MMapper does not know: loaded, listed, never matched.
    QCOMPARE(rules.classify(lineOf("Glub."), context).priority, 2);
    const QJsonArray inactive = rules.toRulesJson()["inactive"].toArray();
    QCOMPARE(inactive.size(), 2);
    QCOMPARE(inactive.at(0).toObject()["id"].toString(), QStringLiteral("d.swim"));
    QCOMPARE(inactive.at(0).toObject()["reason"].toString(), QStringLiteral("when.swimming"));
    QCOMPARE(inactive.at(1).toObject()["reason"].toString(), QStringLiteral("tag.no.such.tag"));
    QCOMPARE(rules.toRulesJson()["defaults"].toInt(), 6);
}

void TestLogRules::sunriseTest()
{
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(rulesDoc(R"([
        {"id":"d.sun","text":"The sun rises in the east.","priority":3},
        {"id":"d.sun.troll","text":"The sun rises in the east.",
         "when":{"race":["troll"],"outdoors":true},"priority":10,"route":"screen"}])")));
    LogContext troll;
    troll.race = QStringLiteral("troll");
    troll.outdoors = true;
    const LogResult outside = rules.classify(lineOf("The sun rises in the east."), troll);
    QCOMPARE(outside.priority, 10);
    QCOMPARE(outside.route, LogRouteEnum::SCREEN);
    troll.outdoors = false;
    QCOMPARE(rules.classify(lineOf("The sun rises in the east."), troll).priority, 3);
    LogContext elf;
    elf.race = QStringLiteral("elf");
    elf.outdoors = true;
    QCOMPARE(rules.classify(lineOf("The sun rises in the east."), elf).priority, 3);
    // A race not known yet is no troll.
    LogContext unknown;
    unknown.outdoors = true;
    QCOMPARE(rules.classify(lineOf("The sun rises in the east."), unknown).priority, 3);

    // Through the Log: the race from Char.StatusVars, outdoors from the map's flag.
    std::vector<GmcpMessage> sent;
    ImportantLog log{[&sent](const GmcpMessage &msg) { sent.push_back(msg); }};
    log.loadDefaults(rulesDoc(R"([
        {"id":"d.sun.troll","text":"The sun rises in the east.",
         "when":{"race":["troll"],"outdoors":true},"priority":10,"route":"screen"}])"));
    bool outdoors = true;
    log.setOutdoors([&outdoors]() { return outdoors; });
    log.receiveGmcp(gmcp(R"(Char.StatusVars {"name":"Ugluk","race":"Troll"})"));
    LineFacts facts;
    facts.plain = QStringLiteral("The sun rises in the east.");
    facts.text = facts.plain;
    facts.tags.insert(LineTagEnum::TEXT);
    log.receiveFacts(facts);
    log.receivePrompt();
    QCOMPARE(sent.size(), size_t{1});
    QCOMPARE(payloadOf(sent.back())["priority"].toInt(), 10);
    QCOMPARE(payloadOf(sent.back())["route"].toString(), QStringLiteral("screen"));
    outdoors = false;
    log.receiveFacts(facts);
    log.receivePrompt();
    QCOMPARE(payloadOf(sent.back())["priority"].toInt(), 2);
}

void TestLogRules::hpDropTest()
{
    std::vector<GmcpMessage> sent;
    ImportantLog log{[&sent](const GmcpMessage &msg) { sent.push_back(msg); }};
    log.loadDefaults(rulesDoc(R"([
        {"id":"d.drop","tags":["text"],"when":{"hpDrop":0.3},"priority":9}])"));
    LineFacts facts;
    facts.plain = QStringLiteral("You feel a sharp pain.");
    facts.text = facts.plain;
    facts.tags.insert(LineTagEnum::TEXT);

    log.receiveGmcp(gmcp(R"(Char.Vitals {"hp":100,"maxhp":100})"));
    log.receivePrompt();
    // The line comes first, the hit points it cost after it, before the prompt: the window is
    // ranked at the prompt, with the whole loss.
    log.receiveFacts(facts);
    log.receiveGmcp(gmcp(R"(Char.Vitals {"hp":50})"));
    QVERIFY(sent.empty());
    log.receivePrompt();
    QCOMPARE(sent.size(), size_t{1});
    QCOMPARE(payloadOf(sent.back())["priority"].toInt(), 9);
    QCOMPARE(payloadOf(sent.back())["rule"].toString(), QStringLiteral("d.drop"));
    // Measured from the previous prompt: a small loss now.
    log.receiveFacts(facts);
    log.receiveGmcp(gmcp(R"(Char.Vitals {"hp":45})"));
    log.receivePrompt();
    QCOMPARE(payloadOf(sent.back())["priority"].toInt(), 2);
    QVERIFY(payloadOf(sent.back())["rule"].isNull());
    // The window also closes by itself.
    log.receiveFacts(facts);
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), size_t{3}, 2000);
}

void TestLogRules::errorCodesTest()
{
    QTemporaryDir dir;
    const QString now = QStringLiteral("2026-10-10T21:04:00Z");
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(rulesDoc("[]")));
    rules.loadUserFile(dir.filePath(QStringLiteral("rules.json")));
    for (const char *const bad : {R"({"text":"Ok."})",
                                  R"({"text":"Ok.","priority":11})",
                                  R"({"text":"Ok.","priority":2.5})",
                                  R"({"text":"","priority":1})",
                                  R"({"regex":"Ok","priority":1})",
                                  R"({"priority":1})",
                                  R"({"tags":["no.such"],"priority":1})",
                                  R"({"text":"Ok.","priority":1,"route":"sky"})",
                                  R"({"text":"Ok.","priority":1,"when":{"swimming":true}})",
                                  R"({"text":"Ok.","priority":1,"when":{"hpBelow":3}})",
                                  R"({"text":"Ok.","regex":"Ok","priority":1})"}) {
        const auto edit = rules.setRule(object(bad), now);
        QVERIFY2(!edit.ok, bad);
        QCOMPARE(edit.error, QStringLiteral("invalid-rule"));
    }
    QCOMPARE(rules.deleteRule(QStringLiteral("d.none")).error, QStringLiteral("invalid-rule"));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("rules.json"))));

    // A file that cannot be written: its folder is a file.
    QFile blocker{dir.filePath(QStringLiteral("blocker"))};
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    LogRuleSet stuck;
    QVERIFY(stuck.loadDefaults(rulesDoc(R"([{"id":"d.ok","text":"Ok.","priority":0}])")));
    stuck.loadUserFile(dir.filePath(QStringLiteral("blocker/rules.json")));
    const auto unwritable = stuck.setRule(object(R"({"text":"Ok.","priority":1})"), now);
    QCOMPARE(unwritable.error, QStringLiteral("unwritable"));
    QVERIFY(stuck.userRules().empty());
    QCOMPARE(stuck.deleteRule(QStringLiteral("d.ok")).error, QStringLiteral("unwritable"));
    QVERIFY(stuck.disabled().isEmpty());

    // A newer file.
    const QString newer = dir.filePath(QStringLiteral("newer.json"));
    {
        QFile file{newer};
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"version":2,"rules":[]})");
    }
    LogRuleSet future;
    future.loadUserFile(newer);
    QCOMPARE(future.setRule(object(R"({"text":"Ok.","priority":1})"), now).error,
             QStringLiteral("newer-file"));
    QCOMPARE(future.deleteRule(QStringLiteral("u1")).error, QStringLiteral("newer-file"));

    // And the same through the package, with the request's id.
    ImportantLog log{[](const GmcpMessage &) {}};
    log.loadDefaults(rulesDoc("[]"));
    log.loadUserFile(dir.filePath(QStringLiteral("blocker/log.json")));
    const auto reply = log.handle(
        gmcp(R"(MMapper.Log.SetRule {"requestId":"r5","rule":{"text":"Ok.","priority":1}})"), true);
    QCOMPARE(reply.toClient.size(), size_t{1});
    const QJsonObject saved = payloadOf(reply.toClient.front());
    QCOMPARE(reply.toClient.front().getName().toQString(), QStringLiteral("MMapper.Log.RuleSaved"));
    QCOMPARE(saved["requestId"].toString(), QStringLiteral("r5"));
    QCOMPARE(saved["ok"].toBool(), false);
    QCOMPARE(saved["error"].toString(), QStringLiteral("unwritable"));
    QVERIFY(!reply.rulesChanged);
    const auto invalid
        = log.handle(gmcp(R"(MMapper.Log.SetRule {"requestId":"r6","rule":{"priority":1}})"), true);
    QCOMPARE(payloadOf(invalid.toClient.front())["error"].toString(),
             QStringLiteral("invalid-rule"));
    QCOMPARE(payloadOf(invalid.toClient.front())["requestId"].toString(), QStringLiteral("r6"));
}

void TestLogRules::saveLoadTest()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("sub/MMapper-log-rules.json"));
    const QByteArray defaults = rulesDoc(R"([{"id":"d.ok","text":"Ok.","priority":0}])");
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(defaults));
    rules.loadUserFile(path);
    QVERIFY(
        rules
            .setRule(object(R"({"text":"<N> pats you on the head.","priority":1,"note":"kept"})"),
                     QStringLiteral("2026-10-10T21:04:00Z"))
            .ok);
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
    QVERIFY(rules
                .setRule(object(R"({"tags":["comm.social"],"priority":0,"when":{"grouped":true}})"),
                         QStringLiteral("2026-10-10T21:05:00Z"))
                .ok);
    QVERIFY(rules.deleteRule(QStringLiteral("d.ok")).ok);
    // The file as it was before the last change is kept.
    QVERIFY(QFile::exists(path + QStringLiteral(".bak")));

    const QJsonObject file = QJsonDocument::fromJson(readAll(path)).object();
    QCOMPARE(file["version"].toInt(), 1);
    QCOMPARE(file["disabled"].toArray().size(), 1);
    QCOMPARE(file["rules"].toArray().size(), 2);
    const QJsonObject first = file["rules"].toArray().at(0).toObject();
    QCOMPARE(first["id"].toString(), QStringLiteral("u1"));
    QCOMPARE(first["created"].toString(), QStringLiteral("2026-10-10T21:04:00Z"));
    QCOMPARE(first["note"].toString(), QStringLiteral("kept"));

    LogRuleSet again;
    QVERIFY(again.loadDefaults(defaults));
    again.loadUserFile(path);
    QCOMPARE(again.userRules().size(), size_t{2});
    QCOMPARE(again.userRules().at(1).id, QStringLiteral("u2"));
    QCOMPARE(again.disabled(), QStringList{QStringLiteral("d.ok")});
    QCOMPARE(again.toRulesJson()["user"].toArray(), rules.toRulesJson()["user"].toArray());
    QCOMPARE(again.classify(lineOf("Kili pats you on the head."), LogContext{}).priority, 1);
    QCOMPARE(again.classify(lineOf("Ok."), LogContext{}).priority, 2);
    // The next rule is named after the highest.
    QCOMPARE(again.setRule(object(R"({"text":"Ok.","priority":3})"), QStringLiteral("x")).id,
             QStringLiteral("u3"));
}

void TestLogRules::newerFileTest()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("MMapper-log-rules.json"));
    const QByteArray future
        = R"({"version":2,"future":true,"rules":[{"id":"u1","text":"Ok.","priority":5}]})";
    {
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(future);
    }
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(rulesDoc(R"([{"id":"d.ok","text":"Ok.","priority":0}])")));
    rules.loadUserFile(path);
    QVERIFY(rules.newerFile());
    // Read and used...
    QCOMPARE(rules.classify(lineOf("Ok."), LogContext{}).priority, 5);
    QCOMPARE(rules.toRulesJson()["fileVersion"].toInt(), 2);
    // ...and never written.
    QCOMPARE(rules.setRule(object(R"({"text":"Hi.","priority":1})"), QStringLiteral("x")).error,
             QStringLiteral("newer-file"));
    QCOMPARE(rules.deleteRule(QStringLiteral("d.ok")).error, QStringLiteral("newer-file"));
    QCOMPARE(readAll(path), future);
    QVERIFY(!QFile::exists(path + QStringLiteral(".bak")));
}

void TestLogRules::suggestTest()
{
    const QStringList kili{QStringLiteral("Kili")};
    QCOMPARE(suggestLogTemplate("Kili tells you 'meet me at 5'", kili),
             QStringLiteral("<N> tells you '<Q>'"));
    QCOMPARE(suggestLogTemplate("*an Orc* hits you hard and shatters it.", {}),
             QStringLiteral("<N> hits you hard and shatters it."));
    QCOMPARE(suggestLogTemplate("474/474 hits, 80/80 mana, and 154/154 moves.", {}),
             QStringLiteral("<#>/<#> hits, <#>/<#> mana, and <#>/<#> moves."));
    QCOMPARE(suggestLogTemplate("You have 1,234 gold coins.", {}),
             QStringLiteral("You have <#> gold coins."));
    // A name inside a longer word stays; "you" is never a name.
    QCOMPARE(suggestLogTemplate("Kilian waves at you.",
                                {QStringLiteral("Kili"), QStringLiteral("you")}),
             QStringLiteral("Kilian waves at you."));
    // The longest name first.
    QCOMPARE(suggestLogTemplate("Kili Oakenshield nods.",
                                {QStringLiteral("Kili"), QStringLiteral("Kili Oakenshield")}),
             QStringLiteral("<N> nods."));
    // Template characters in the line are escaped, and the template matches the line.
    const QString odd = QStringLiteral(R"(A * star and a \ and <N>.)");
    const QString escaped = suggestLogTemplate(odd, {});
    QCOMPARE(escaped, QStringLiteral(R"(A \* star and a \\ and \<N>.)"));
    QVERIFY(matches(escaped, odd));
    QVERIFY(matches(suggestLogTemplate("Kili tells you 'x'", kili), "Bob tells you 'y'"));
}

void TestLogRules::performanceTest()
{
    // 2000 rules of the shapes users and the defaults make, ranked against 10,000 lines.
    QJsonArray array;
    for (int i = 0; i < 2000; ++i) {
        QJsonObject rule;
        rule["id"] = QStringLiteral("d.p%1").arg(i);
        rule["priority"] = i % 11;
        switch (i % 5) {
        case 0:
            rule["text"] = QStringLiteral("Word%1 strikes <N> with <O>.").arg(i);
            break;
        case 1:
            rule["text"] = QStringLiteral("<N> looks at you strangely%1.").arg(i);
            break;
        case 2:
            rule["text"] = QStringLiteral("<N> sings%1 <O>").arg(i);
            break;
        case 3:
            rule["text"] = QStringLiteral("<N> tells you 'code %1 <Q>'").arg(i);
            rule["when"] = QJsonObject{{"grouped", true}};
            break;
        default:
            rule["text"] = QStringLiteral("The <O> of %1 falls.").arg(i);
            break;
        }
        array.append(rule);
    }
    for (const char *const tag : {"room", "prompt", "comm.tell", "comm.say", "combat.blow"}) {
        array.append(QJsonObject{{"id", QStringLiteral("d.t.%1").arg(tag)},
                                 {"tags", QJsonArray{tag}},
                                 {"priority", 0}});
    }
    QJsonObject doc;
    doc["version"] = 1;
    doc["rules"] = array;
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(QJsonDocument{doc}.toJson()));

    std::vector<LogLine> lines;
    for (int i = 0; i < 10000; ++i) {
        switch (i % 6) {
        case 0:
            lines.push_back(
                lineOf(QStringLiteral("Word%1 strikes Kili with a sword.").arg(i % 2000)));
            break;
        case 1:
            lines.push_back(lineOf(QStringLiteral("Kili looks at you strangely%1.").arg(i % 2000)));
            break;
        case 2:
            lines.push_back(lineOf(QStringLiteral("Kili tells you 'code %1 now'").arg(i % 2000),
                                   {QStringLiteral("comm"), QStringLiteral("comm.tell")}));
            break;
        case 3:
            lines.push_back(lineOf(QStringLiteral("The grey wolf of %1 falls.").arg(i % 2000)));
            break;
        case 4:
            lines.push_back(lineOf(QStringLiteral("A dirty uruk barely slashes your body."),
                                   {QStringLiteral("combat"), QStringLiteral("combat.blow")},
                                   QStringLiteral("you")));
            break;
        default:
            lines.push_back(lineOf(QStringLiteral("You see nothing special about line %1.").arg(i),
                                   {QStringLiteral("room")}));
            break;
        }
    }
    LogContext context;
    context.grouped = true;
    QElapsedTimer timer;
    timer.start();
    int64_t sum = 0;
    for (const LogLine &line : lines) {
        sum += rules.classify(line, context).priority;
    }
    const qint64 ms = timer.elapsed();
    qInfo() << "2000 rules x 10000 lines:" << ms << "ms";
    QVERIFY(sum > 0);
    QVERIFY2(ms < 2000, qPrintable(QStringLiteral("%1 ms").arg(ms)));
    // And they matched what they should.
    QCOMPARE(rules.classify(lineOf("Word10 strikes Kili with a sword."), context).rule->id,
             QStringLiteral("d.p10"));
    QCOMPARE(rules.classify(lineOf("Kili sings7 a song"), context).rule->id, QStringLiteral("d.p7"));
}

void TestLogRules::shippedDefaultsTest()
{
    // The rules MMapper ships (mume3d's tools/research/log_rules_seed.py made them): every one
    // read, the table's priorities as section 37 has them.
    QFile file{QStringLiteral(MMAPPER_LOG_RULES_JSON)};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray json = file.readAll();
    const auto written = QJsonDocument::fromJson(json).object()["rules"].toArray().size();
    LogRuleSet rules;
    QVERIFY(rules.loadDefaults(json));
    QCOMPARE(static_cast<qsizetype>(rules.defaultCount()), written);
    for (const QJsonValue &inactive : rules.toRulesJson()["inactive"].toArray()) {
        // Only states MMapper does not know.
        QVERIFY2(inactive.toObject()["reason"].toString().startsWith(QStringLiteral("when.")),
                 qPrintable(inactive.toObject()["id"].toString()));
    }
    const LogContext calm;
    const auto rank = [&rules, &calm](const char *const plain, const QStringList &tags) {
        return rules.classify(lineOf(QString::fromUtf8(plain), tags), calm).priority;
    };
    QCOMPARE(rank("Kili tells you 'hi'", {QStringLiteral("comm"), QStringLiteral("comm.tell")}), 10);
    QCOMPARE(rank("Kili tells the group 'heal'",
                  {QStringLiteral("comm"), QStringLiteral("comm.gtell")}),
             10);
    QCOMPARE(rank("Kili narrates 'hi'", {QStringLiteral("comm"), QStringLiteral("comm.narrate")}),
             8);
    QCOMPARE(rank("Kili says 'hi'", {QStringLiteral("comm"), QStringLiteral("comm.say")}), 6);
    QCOMPARE(rank("Ok.", {QStringLiteral("text")}), 0);
    QCOMPARE(rank("The Prancing Pony", {QStringLiteral("room")}), 0);
    QCOMPARE(rank("o HP:Fine>", {QStringLiteral("prompt")}), 0);
    QCOMPARE(rank("A crow caws somewhere far away.", {QStringLiteral("text")}), 2);

    // 10,000 lines against them.
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 10000; ++i) {
        std::ignore = rank(i % 2 == 0 ? "A dirty uruk barely slashes your body."
                                      : "Kili tells you 'meet me at the ford'",
                           {QStringLiteral("text")});
    }
    qInfo() << rules.defaultCount() << "shipped rules x 10000 lines:" << timer.elapsed() << "ms";
}

void TestLogRules::roomTagsTest()
{
    Tagging t;
    t.chunk(false, QStringLiteral("The Prancing Pony\n"), true);
    t.chunk(false, QStringLiteral("A warm common room.\n"), true, true);
    QCOMPARE(t.records.size(), size_t{2});
    QCOMPARE(t.records.at(0).tags.names(), QStringList{QStringLiteral("room")});
    QCOMPARE(t.records.at(1).tags.names(),
             (QStringList{QStringLiteral("room"), QStringLiteral("room.desc")}));
    // A blank line makes no record.
    t.line(QString{});
    QCOMPARE(t.records.size(), size_t{2});
}

void TestLogRules::promptTagsTest()
{
    Tagging t;
    t.prompt();
    QCOMPARE(t.tagsOfLast(), QStringList{QStringLiteral("prompt")});
    t.line(QStringLiteral("Something happens."));
    QCOMPARE(t.tagsOfLast(), QStringList{QStringLiteral("text")});
}

void TestLogRules::blowTagsTest()
{
    Tagging t;
    t.line(QStringLiteral("A dirty uruk barely slashes your body."));
    QCOMPARE(t.tagsOfLast(),
             (QStringList{QStringLiteral("combat"),
                          QStringLiteral("combat.blow"),
                          QStringLiteral("combat.hit")}));
    QCOMPARE(t.records.back().about, LineAboutEnum::YOU);
    QCOMPARE(t.records.back().names, QStringList{QStringLiteral("A dirty uruk")});
}

void TestLogRules::shopAcrossPagerTest()
{
    Tagging t;
    t.line(QStringLiteral("You can buy:"));
    t.line(QString{});
    t.line(QStringLiteral(
        " 470. forty-one blood-encrusted helms (flawless, new) up to 4 gold 17 silver."));
    t.chunk(true, QStringLiteral("*** Return: continue, b: back, r: redisplay, q: quit (50%) *** "));
    t.line(QStringLiteral(" 513. fourteen great helms (flawless, new) up to 8 gold 15 silver."));
    t.line(QString{});
    t.prompt();
    QCOMPARE(t.records.size(), size_t{5});
    QCOMPARE(t.records.at(0).tags.names(), QStringList{QStringLiteral("shop")});
    QCOMPARE(t.records.at(1).tags.names(), QStringList{QStringLiteral("shop")});
    QCOMPARE(t.records.at(2).tags.names(), QStringList{QStringLiteral("pager")});
    // The list goes on after the pager.
    QCOMPARE(t.records.at(3).tags.names(), QStringList{QStringLiteral("shop")});
    QCOMPARE(t.records.at(4).tags.names(), QStringList{QStringLiteral("prompt")});
}

void TestLogRules::statBlockTest()
{
    Tagging t;
    t.line(QStringLiteral("OB: 60%, DB: 62%, PB: 60%, Armour: 66%. Wimpy: 120. Mood: wimpy."));
    t.line(QStringLiteral("Needed: 1,136,776 xp, 0 tp. Gold: 103. Alert: normal."));
    t.line(QString{});
    t.prompt();
    QCOMPARE(t.records.size(), size_t{3});
    QCOMPARE(t.records.at(0).tags.names(), QStringList{QStringLiteral("stat")});
    QCOMPARE(t.records.at(1).tags.names(), QStringList{QStringLiteral("stat")});
}

void TestLogRules::narrateJoinedTest()
{
    Tagging t;
    XmlElement narrate;
    narrate.tag = XmlTagEnum::NARRATE;
    narrate.text = QStringLiteral(
        "Kili narrates 'the orcs are at the ford, come quickly\nand bring rope'");
    t.chunk(false,
            QStringLiteral("Kili narrates 'the orcs are at the ford, come quickly\n"),
            false,
            false,
            {},
            true);
    QVERIFY(t.records.empty());
    t.chunk(false, QStringLiteral("and bring rope'\n"), false, false, {narrate}, false);
    QCOMPARE(t.records.size(), size_t{1});
    const LineFacts &record = t.records.front();
    QCOMPARE(record.plain,
             QStringLiteral(
                 "Kili narrates 'the orcs are at the ford, come quickly and bring rope'"));
    QCOMPARE(record.tags.names(),
             (QStringList{QStringLiteral("comm"), QStringLiteral("comm.narrate")}));
    QCOMPARE(record.names, QStringList{QStringLiteral("Kili")});
    QVERIFY(record.text.contains(QLatin1Char('\n')));

    // A group tell.
    XmlElement gtell;
    gtell.tag = XmlTagEnum::TELL;
    gtell.text = QStringLiteral("Kili tells the group 'heal'");
    t.chunk(false, QStringLiteral("Kili tells the group 'heal'\n"), false, false, {gtell});
    QCOMPARE(t.records.back().tags.names(),
             (QStringList{QStringLiteral("comm"), QStringLiteral("comm.gtell")}));
    QCOMPARE(t.records.back().about, LineAboutEnum::GROUP);
}

void TestLogRules::quietHiddenTest()
{
    Tagging t;
    t.prompt();
    const size_t before = t.records.size();
    // A quiet command's reply is kept from the terminal: no record either.
    t.readers.receiveQuietCommand(QuietCommandEnum::BEGIN);
    t.line(QStringLiteral("You have 0 practice sessions left."));
    t.prompt();
    QCOMPARE(t.records.size(), before);
    t.readers.receiveQuietCommand(QuietCommandEnum::END);
    t.line(QStringLiteral("You have 0 practice sessions left."));
    QCOMPARE(t.records.size(), before + 1);
}

QTEST_MAIN(TestLogRules)
