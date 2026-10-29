// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestLineReaders.h"

#include "../src/observer/gameobserver.h"
#include "../src/parser/AccountLines.h"
#include "../src/parser/CharConsider.h"
#include "../src/parser/CharFollowers.h"
#include "../src/parser/CharLines.h"
#include "../src/parser/CharRefused.h"
#include "../src/parser/ChunkFrame.h"
#include "../src/parser/CombatLines.h"
#include "../src/parser/ContainerLines.h"
#include "../src/parser/ExitLooks.h"
#include "../src/parser/ItemLines.h"
#include "../src/parser/LineReader.h"
#include "../src/parser/LineTags.h"
#include "../src/parser/LineText.h"
#include "../src/parser/LoginLines.h"
#include "../src/parser/RoomContents.h"
#include "../src/parser/RoomDoors.h"
#include "../src/parser/TradeLines.h"
#include "../src/parser/WeatherLines.h"

#include <array>
#include <tuple>

#include <QtTest/QtTest>

namespace {

using R = ResetReason;

const std::array<R, 6> ALL_REASONS{R::DISCONNECTED,
                                   R::CONNECTED,
                                   R::LEFT_GAME,
                                   R::ENTERED_GAME,
                                   R::ECHO_OFF,
                                   R::XML_LOST};

/// Every reader MumeXmlParser registers, in the order it registers them.
struct NODISCARD AllReaders final
{
    GameObserver observer;
    ChunkFrame frame{observer};
    TradeLinesTracker trade;
    ConsiderReader consider;
    ContainerTracker container;
    RoomDoorTracker doors;
    ExitLookTracker exitLooks;
    CharFollowersTracker followers;
    ItemBlockTracker items;
    ItemCommandTracker itemCommands;
    CharLinesTracker chars;
    AccountLinesTracker account;
    LoginLinesTracker login;
    GroundTracker ground;
    RoomContentsTracker roomContents;
    OwnCastTracker ownCast;
    LineTagger tagger;
    LineReaders readers;

    AllReaders()
    {
        readers.add(frame);
        readers.add(trade);
        readers.add(consider);
        readers.add(container);
        readers.add(doors);
        readers.add(exitLooks);
        readers.add(followers);
        readers.add(items);
        readers.add(itemCommands);
        readers.add(chars);
        readers.add(account);
        readers.add(login);
        readers.add(ground);
        readers.add(roomContents);
        readers.add(ownCast);
        readers.add(tagger);
    }
};

/// A reader that counts what it is given.
class NODISCARD Counter final : public LineReader
{
public:
    std::vector<QString> lines;
    int resets = 0;

    NODISCARD ResetSet resetsOn() const override { return {R::XML_LOST}; }
    void reset() override { ++resets; }
    void receiveCommand(const SentCommand &command) override { lines.push_back(command.line); }
};

} // namespace

void TestLineReaders::everyReaderResetsOnDisconnectTest()
{
    // A container reply queue or an own-cast tracker never survives a reconnect.
    AllReaders all;
    QCOMPARE(all.readers.all().size(), size_t{16});
    for (const LineReader *const reader : all.readers.all()) {
        QVERIFY(reader->resetsOn().has(R::DISCONNECTED));
    }
}

void TestLineReaders::resetOnlyWhereDeclaredTest()
{
    // The declared sets are the ones MumeXmlParser reset by hand before, and the four that were
    // never reset on a disconnect.
    AllReaders all;
    QVERIFY(all.container.resetsOn().has(R::XML_LOST));
    QVERIFY(all.ground.resetsOn().has(R::XML_LOST));
    QVERIFY(all.roomContents.resetsOn().has(R::XML_LOST));
    QVERIFY(all.ownCast.resetsOn().has(R::XML_LOST));
    QVERIFY(all.items.resetsOn().has(R::ECHO_OFF));
    QVERIFY(all.chars.resetsOn().has(R::ECHO_OFF));
    QVERIFY(all.trade.resetsOn().has(R::ECHO_OFF));
    QVERIFY(all.frame.resetsOn().has(R::ECHO_OFF));
    QVERIFY(all.exitLooks.resetsOn().has(R::ECHO_OFF));
    QVERIFY(all.exitLooks.resetsOn().has(R::CONNECTED));
    QVERIFY(all.followers.resetsOn().has(R::LEFT_GAME));
    QVERIFY(all.doors.resetsOn().has(R::LEFT_GAME));
    QVERIFY(all.login.resetsOn().has(R::CONNECTED));
    QVERIFY(all.login.resetsOn().has(R::ENTERED_GAME));
    QVERIFY(!all.followers.resetsOn().has(R::ECHO_OFF));
    QVERIFY(!all.followers.resetsOn().has(R::XML_LOST));
    QVERIFY(!all.login.resetsOn().has(R::LEFT_GAME));

    // A reason resets what declares it, and only that.
    Counter counter;
    LineReaders readers;
    readers.add(counter);
    for (const R reason : ALL_REASONS) {
        readers.reset(reason);
    }
    QCOMPARE(counter.resets, 1);
}

void TestLineReaders::stateIsForgottenTest()
{
    // What each reader holds is gone after the reasons it declares; a probe for each reader
    // that has one that can be read from outside.
    for (const R reason : ALL_REASONS) {
        AllReaders all;
        all.readers.receiveCommand(SentCommand{QStringLiteral("look north"), 1000});
        all.readers.receiveCommand(SentCommand{QStringLiteral("consider orc"), 1000});
        all.readers.receiveCommand(SentCommand{QStringLiteral("get helmet"), 1000});
        std::ignore = all.frame.begin(true,
                                      false,
                                      QString::fromUtf8("*** Return: continue, b: "
                                                        "back, r: redisplay, q: "
                                                        "quit (73%) ***"));
        QVERIFY(all.frame.pagerOpen());
        QVERIFY(all.exitLooks.waiting());
        all.readers.reset(reason);
        QCOMPARE(all.frame.pagerOpen(), !all.frame.resetsOn().has(reason));
        QCOMPARE(all.exitLooks.waiting(), !all.exitLooks.resetsOn().has(reason));
    }

    // The own cast: "You start to concentrate..." until the prompt.
    for (const R reason : ALL_REASONS) {
        OwnCastTracker cast;
        const auto started = parseCombatLine(QStringLiteral("You start to concentrate..."));
        QVERIFY(started.has_value());
        cast.receiveEvent(*started);
        QVERIFY(cast.casting());
        LineReaders readers;
        readers.add(cast);
        readers.reset(reason);
        QCOMPARE(cast.casting(), !cast.resetsOn().has(reason));
    }

    // A consider waiting for its verdict is let go on a disconnect: the next lone advice line
    // is read as one.
    ConsiderReader consider;
    consider.receiveCommand(SentCommand{QStringLiteral("consider orc"), 0});
    LineReaders readers;
    readers.add(consider);
    readers.reset(R::DISCONNECTED);
    const auto lone = consider.receiveLine(QStringLiteral("You ARE mad!"));
    QVERIFY(lone.has_value());

    // The commands that waited end without a prompt, and are handed to the parser.
    ItemCommandTracker commands;
    commands.receiveCommand(SentCommand{QStringLiteral("get helmet"), 5});
    const auto began = commands.takeReady();
    QCOMPARE(began.size(), size_t{1});
    commands.reset();
    const auto ended = commands.takeReady();
    QCOMPARE(ended.size(), size_t{1});
    QCOMPARE(ended.front().id, began.front().id);
    QVERIFY(commands.finish(true).empty());
}

void TestLineReaders::splitSentCommandsTest()
{
    const auto two = splitSentCommands(QStringLiteral("open chest\nget all chest"), 7);
    QCOMPARE(two.size(), size_t{2});
    QCOMPARE(two[0].line, QStringLiteral("open chest"));
    QCOMPARE(two[0].verb, QStringLiteral("open"));
    QCOMPARE(two[0].args, QStringLiteral("chest"));
    QCOMPARE(two[1].line, QStringLiteral("get all chest"));
    QCOMPARE(two[1].verb, QStringLiteral("get"));
    QCOMPARE(two[1].args, QStringLiteral("all chest"));
    QCOMPARE(two[1].nowMs, int64_t{7});

    // A line ending is not a line; CR LF is a line ending.
    QCOMPARE(splitSentCommands(QStringLiteral("look\r\n"), 0).size(), size_t{1});
    QCOMPARE(splitSentCommands(QStringLiteral("look\r\n"), 0).front().line, QStringLiteral("look"));
    // Return, which answers a pager, is one empty line, as is an empty string.
    QCOMPARE(splitSentCommands(QString{}, 0).size(), size_t{1});
    QCOMPARE(splitSentCommands(QStringLiteral("\n"), 0).size(), size_t{1});
    QVERIFY(splitSentCommands(QStringLiteral("\n"), 0).front().line.isEmpty());
    // The verb is lowercase and the spacing simplified.
    const SentCommand shouted{QStringLiteral("  GET   Sword  "), 0};
    QCOMPARE(shouted.verb, QStringLiteral("get"));
    QCOMPARE(shouted.args, QStringLiteral("Sword"));
}

void TestLineReaders::everyReaderSeesEveryLineTest()
{
    // An alias send `open chest\nget all chest` is seen as two commands by every reader.
    Counter a;
    Counter b;
    ItemCommandTracker commands;
    ItemBlockTracker listings;
    LineReaders readers;
    readers.add(a);
    readers.add(commands);
    readers.add(listings);
    readers.add(b);
    for (const SentCommand &command :
         splitSentCommands(QStringLiteral("open chest\nget all chest"), 100)) {
        readers.receiveCommand(command);
    }
    QCOMPARE(a.lines.size(), size_t{2});
    QCOMPARE(b.lines.size(), size_t{2});
    const auto observations = commands.takeReady();
    QCOMPARE(observations.size(), size_t{2});
    QCOMPARE(observations[0].action, QStringLiteral("open"));
    QCOMPARE(observations[1].action, QStringLiteral("get"));

    // As two commands, neither is taken for the one command that a reply could be matched to.
    commands.reset();
    ItemCommandTracker one;
    one.receiveCommand(SentCommand{QStringLiteral("get helmet"), 0});
    QCOMPARE(one.takeReady().size(), size_t{1});
}

void TestLineReaders::pagerAnswerIsNoCommandTest()
{
    GameObserver observer;
    ChunkFrame frame{observer};
    QVERIFY(!frame.takesPagerAnswer());
    const MudChunk pager = frame.begin(true,
                                       false,
                                       QStringLiteral("*** Return: continue, b: back, r: "
                                                      "redisplay, q: quit (73%) ***"));
    QCOMPARE(pager.kind, MudChunkKindEnum::PAGER);
    QVERIFY(frame.pagerOpen());
    frame.end();
    QVERIFY(frame.takesPagerAnswer());
    QVERIFY(!frame.takesPagerAnswer());
}

void TestLineReaders::twiddlersTest()
{
    const auto removed = [](const QString &line) {
        QString text = line;
        line_text::removeTwiddlers(text);
        return text;
    };
    QCOMPARE(removed(QStringLiteral("/-\\|/-Ok.")), QStringLiteral("Ok."));
    QCOMPARE(removed(QStringLiteral("\\|*click*")), QStringLiteral("*click*"));
    QCOMPARE(removed(QStringLiteral("|\bOk.")), QStringLiteral("Ok."));
    // Before a lowercase letter they are no twiddlers: "-sword" is not one.
    QCOMPARE(removed(QStringLiteral("-sword")), QStringLiteral("-sword"));
    QString items = QStringLiteral("\\a gold ring");
    line_text::removeTwiddlersBeforeLowercase(items);
    QCOMPARE(items, QStringLiteral("a gold ring"));
}

void TestLineReaders::sharedCleanersTest()
{
    // The cases every reader's cleaner is made of: colour, spacing, twiddlers.
    const QString coloured = QStringLiteral("\x1b[31m/-\\Ok.   Done\x1b[0m");
    QCOMPARE(line_text::plainOf(coloured), QStringLiteral("/-\\Ok.   Done"));
    QCOMPARE(line_text::cleaned(coloured), QStringLiteral("/-\\Ok. Done"));
    QCOMPARE(line_text::cleanedWithoutTwiddlers(coloured), QStringLiteral("Ok. Done"));
    QCOMPARE(line_text::trimmedWithoutTwiddlers(QStringLiteral("  /-Ok.  ")), QStringLiteral("Ok."));
    // The one deliberate difference: an item listing's lowercase line.
    QCOMPARE(line_text::cleanedWithoutTwiddlers(QStringLiteral("\\a gold ring")),
             QStringLiteral("\\a gold ring"));
    QCOMPARE(line_text::cleanedForItems(QStringLiteral("\\a gold ring")),
             QStringLiteral("a gold ring"));
    QCOMPARE(line_text::rawLine(QStringLiteral("\x1b[1ma   b\r\n")), QStringLiteral("a   b"));
    QCOMPARE(line_text::toNumber(QStringLiteral("1,136,776")).value_or(-1), int64_t{1136776});
    QVERIFY(!line_text::toNumber(QStringLiteral("many")).has_value());
    QVERIFY(line_text::isItemLine(QStringLiteral("a gold ring")));
    QVERIFY(line_text::isItemLine(QStringLiteral("Something.")));
    QVERIFY(!line_text::isItemLine(QStringLiteral("You are using:")));

    // Every reader that strips twiddlers does so the same way.
    const QString twiddled = QStringLiteral("/-\\|/");
    QCOMPARE(parseConsiderLine(twiddled + QStringLiteral("You ARE mad!"))->verdict,
             parseConsiderLine(QStringLiteral("You ARE mad!"))->verdict);
    QVERIFY(parseRefusedLine(twiddled + QStringLiteral("You are unable to leave this place."))
                .has_value());
    QVERIFY(parseFollowerLine(twiddled + QStringLiteral("Grayelf now follows you.")).has_value());
    QVERIFY(parseCombatLine(twiddled + QStringLiteral("You start to concentrate...")).has_value());
}

QTEST_MAIN(TestLineReaders)
