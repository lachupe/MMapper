// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestExitLooks.h"

#include "../src/parser/ExitLooks.h"

#include <QtTest/QtTest>

// The answers are MUME's own, from the powwow logs under /home/aza/data/powwow/logs/archives;
// each case names where it was found, with that directory left off.

namespace {

using L = LineKindEnum;
using K = ExitLookKindEnum;

/// Sends `command`, gives MUME's `lines` and a prompt, and returns what that prompt closed.
std::vector<ExitLook> exchange(ExitLookTracker &tracker,
                               const char *const command,
                               std::initializer_list<std::pair<const char *, L>> lines)
{
    if (command != nullptr) {
        tracker.receiveCommand(QString::fromUtf8(command));
    }
    for (const auto &[text, kind] : lines) {
        tracker.receiveLine(QString::fromUtf8(text), kind);
    }
    return tracker.receivePrompt();
}

QStringList list(std::initializer_list<const char *> lines)
{
    QStringList result;
    for (const char *line : lines) {
        result.append(QString::fromUtf8(line));
    }
    return result;
}

} // namespace

QTEST_MAIN(TestExitLooks)

void TestExitLooks::directionTest()
{
    QCOMPARE(lookedDirection("l n"), QStringLiteral("north"));
    QCOMPARE(lookedDirection("look north"), QStringLiteral("north"));
    QCOMPARE(lookedDirection("lo e"), QStringLiteral("east"));
    QCOMPARE(lookedDirection("LOOK Down"), QStringLiteral("down"));
    QCOMPARE(lookedDirection("l u\n"), QStringLiteral("up"));
    QVERIFY(lookedDirection("look").isEmpty());
    QVERIFY(lookedDirection("look door").isEmpty());
    QVERIFY(lookedDirection("look in chest").isEmpty());
    QVERIFY(lookedDirection("examine north").isEmpty());
    QVERIFY(lookedDirection("north").isEmpty());
}

void TestExitLooks::answerKindsTest()
{
    ExitLookTracker tracker;
    tracker.receiveRoom(1101);

    // log-2005.09.03-02.28.34.txt:11403-11406
    auto done = exchange(tracker, "l n", {{"The giant, thick wooden is open.", L::TEXT}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].room, std::optional<int64_t>{1101});
    QCOMPARE(done[0].dir, QStringLiteral("north"));
    QCOMPARE(done[0].command, QStringLiteral("l n"));
    QCOMPARE(done[0].kind, K::DOOR);
    QCOMPARE(done[0].door, QStringLiteral("giant, thick wooden"));
    QCOMPARE(done[0].doorState, QStringLiteral("open"));
    QVERIFY(!done[0].uncertain);

    // log-2005.09.03-02.28.34.txt:11500-11502
    done = exchange(tracker, "l n", {{"The giant, strong pizdecdoor is closed.", L::TEXT}});
    QCOMPARE(done[0].kind, K::DOOR);
    QCOMPARE(done[0].doorState, QStringLiteral("closed"));

    // log-2005.09.05-19.23.16.txt:29369
    done = exchange(tracker, "l s", {{"A broken rockdoor.", L::TEXT}});
    QCOMPARE(done[0].kind, K::DOOR);
    QCOMPARE(done[0].door, QStringLiteral("rockdoor"));
    QCOMPARE(done[0].doorState, QStringLiteral("broken"));

    // A description and its door's line: log-2005.09.05-19.23.16.txt:108228-108230
    done = exchange(tracker,
                    "l w",
                    {{"A really ugly wall of thorns. It seems to be very harmful to touch.", L::TEXT},
                     {"The thorns is open.", L::TEXT}});
    QCOMPARE(done[0].kind, K::DESCRIPTION);
    QCOMPARE(done[0].text,
             QStringLiteral("A really ugly wall of thorns. It seems to be very harmful to "
                            "touch.\nThe thorns is open."));
    QCOMPARE(done[0].door, QStringLiteral("thorns"));
    QCOMPARE(done[0].doorState, QStringLiteral("open"));

    // The commonest answer: 82 of the 251 looks in the logs.
    done = exchange(tracker, "look east", {{"You see nothing special there...", L::TEXT}});
    QCOMPARE(done[0].kind, K::NOTHING);
    QCOMPARE(done[0].dir, QStringLiteral("east"));

    done = exchange(tracker, "l d", {{"It is pitch black...", L::TEXT}});
    QCOMPARE(done[0].kind, K::DARK);

    done = exchange(tracker, "l u", {{"Arglebargle, glop-glyf!?!", L::TEXT}});
    QCOMPARE(done[0].kind, K::OTHER);
    QVERIFY(done[0].uncertain);
    QCOMPARE(done[0].reasons, list({"other"}));

    // Blank lines are no part of an answer.
    done = exchange(tracker, "l n", {{"", L::TEXT}, {"A narrow path.", L::TEXT}, {"  ", L::TEXT}});
    QCOMPARE(done[0].lines, list({"A narrow path."}));
    QVERIFY(!tracker.waiting());
}

void TestExitLooks::asyncInsideTest()
{
    // A say inside a look's answer: log-2005.09.03-02.28.34.txt:11384-11388.
    ExitLookTracker tracker;
    const auto done = exchange(tracker,
                               "l n",
                               {{"The giant, thick wooden is open.", L::TEXT},
                                {"*Stolb the Orc* [stolb] says 'yawn'", L::ASYNC}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].text, QStringLiteral("The giant, thick wooden is open."));
    QCOMPARE(done[0].dropped, list({"*Stolb the Orc* [stolb] says 'yawn'"}));
    QCOMPARE(done[0].raw,
             list({"The giant, thick wooden is open.", "*Stolb the Orc* [stolb] says 'yawn'"}));
    QVERIFY(!done[0].uncertain);
}

void TestExitLooks::asyncPromptTest()
{
    // A narrate with a prompt of its own (log-2006.05.06-20.16.22.txt:827-829) between two looks
    // sent at once: it closes neither.
    ExitLookTracker tracker;
    tracker.receiveCommand("l n");
    tracker.receiveCommand("l e");
    auto done = exchange(tracker,
                         nullptr,
                         {{"Apollo narrates 'insane' in Orkish.", L::ASYNC}});
    QVERIFY(done.empty());
    done = exchange(tracker, nullptr, {{"You see nothing special there...", L::TEXT}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].dir, QStringLiteral("north"));
    QCOMPARE(done[0].kind, K::NOTHING);
    QVERIFY(done[0].uncertain);
    QCOMPARE(done[0].reasons, list({"extra-prompt"}));
    QCOMPARE(done[0].raw,
             list({"Apollo narrates 'insane' in Orkish.", "You see nothing special there..."}));
    done = exchange(tracker, nullptr, {{"A wide road leads east.", L::TEXT}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].dir, QStringLiteral("east"));
    QVERIFY(!done[0].uncertain);
}

void TestExitLooks::fightTest()
{
    // A blow with its prompt before the answer: log-2005.09.02-23.20.17.txt:7213-7218.
    ExitLookTracker tracker;
    auto done = exchange(
        tracker,
        "l n",
        {{"You evade *a Dreadful Orc*'s bash, causing him to fall flat on his face.", L::COMBAT}});
    QVERIFY(done.empty());
    done = exchange(tracker, nullptr, {{"The giant, strong wooden is closed.", L::TEXT}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].kind, K::DOOR);
    QVERIFY(done[0].uncertain);
    QVERIFY(done[0].reasons.contains(QStringLiteral("fight")));
    QVERIFY(done[0].reasons.contains(QStringLiteral("extra-prompt")));
    QCOMPARE(done[0].text, QStringLiteral("The giant, strong wooden is closed."));
}

void TestExitLooks::otherCommandsTest()
{
    // The player's own say is answered in speech markup: it is closed by its prompt all the
    // same, and the look after it gets the next answer.
    ExitLookTracker tracker;
    tracker.receiveCommand("say hi");
    tracker.receiveCommand("l w");
    auto done = exchange(tracker, nullptr, {{"You say 'hi'", L::ASYNC}});
    QVERIFY(done.empty());
    QVERIFY(tracker.waiting());
    done = exchange(tracker, nullptr, {{"A large patch of thorny brush grows west of here.", L::TEXT}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].dir, QStringLiteral("west"));
    QVERIFY(!done[0].uncertain);

    // Any other line is counted and never published.
    done = exchange(tracker, "score", {{"You have 120/120 hit points.", L::TEXT}});
    QVERIFY(done.empty());
}

void TestExitLooks::severalLooksTest()
{
    // `_lookexits`, or an alias: the looks go out together and are answered in order.
    ExitLookTracker tracker;
    tracker.receiveRoom(42);
    tracker.receiveCommand("look north");
    tracker.receiveCommand("look east");
    tracker.receiveCommand("look down");
    QVERIFY(tracker.waiting());
    auto a = exchange(tracker, nullptr, {{"A narrow path winds up the hill.", L::TEXT}});
    auto b = exchange(tracker, nullptr, {{"The gate is closed.", L::TEXT}});
    auto c = exchange(tracker, nullptr, {{"You see nothing special there...", L::TEXT}});
    QCOMPARE(a.size() + b.size() + c.size(), size_t{3});
    QCOMPARE(a[0].dir, QStringLiteral("north"));
    QCOMPARE(b[0].dir, QStringLiteral("east"));
    QCOMPARE(b[0].door, QStringLiteral("gate"));
    QCOMPARE(c[0].dir, QStringLiteral("down"));
    QVERIFY(!tracker.waiting());
}

void TestExitLooks::movedTest()
{
    ExitLookTracker tracker;
    tracker.receiveRoom(1);
    tracker.receiveCommand("l n");
    tracker.receiveRoom(1); // the same room again: a plain look
    tracker.receiveRoom(2);
    const auto done = exchange(tracker, nullptr, {{"A narrow path.", L::TEXT}});
    QCOMPARE(done[0].room, std::optional<int64_t>{1});
    QCOMPARE(done[0].reasons, list({"moved"}));
}

void TestExitLooks::roomDisplayTest()
{
    // A room display nobody asked for while a look waits (the character was led, dragged or fell):
    // it answers no line, and the look's answer after it is uncertain.
    ExitLookTracker tracker;
    auto done = exchange(tracker,
                         "l n",
                         {{"The Prancing Pony", L::ROOM},
                          {"You are in the common room.", L::ROOM},
                          {"Exits: north, east.", L::ROOM}});
    QVERIFY(done.empty());
    done = exchange(tracker, nullptr, {{"A narrow corridor.", L::TEXT}});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].kind, K::DESCRIPTION);
    QVERIFY(done[0].reasons.contains(QStringLiteral("room-display")));
    QVERIFY(done[0].reasons.contains(QStringLiteral("extra-prompt")));
    QCOMPARE(done[0].dropped.size(), qsizetype{3});

    // A room display inside the answer's own window is left out of it.
    done = exchange(tracker,
                    "l e",
                    {{"A wide road.", L::TEXT}, {"The Prancing Pony", L::ROOM}});
    QCOMPARE(done[0].text, QStringLiteral("A wide road."));
    QCOMPARE(done[0].reasons, list({"room-display"}));
}

void TestExitLooks::emptyAndLostTest()
{
    ExitLookTracker tracker;
    auto done = exchange(tracker, "l n", {});
    QCOMPARE(done.size(), size_t{1});
    QCOMPARE(done[0].kind, K::EMPTY);
    QCOMPARE(done[0].reasons, list({"empty"}));

    // Only MUME's own lines, prompt after prompt: given up after MAX_PROMPTS windows.
    tracker.receiveCommand("l e");
    for (int i = 1; i < ExitLookTracker::MAX_PROMPTS; ++i) {
        QVERIFY(exchange(tracker, nullptr, {{"Gandalf narrates 'hi'", L::ASYNC}}).empty());
    }
    done = exchange(tracker, nullptr, {{"Gandalf narrates 'hi'", L::ASYNC}});
    QCOMPARE(done.size(), size_t{1});
    QVERIFY(done[0].reasons.contains(QStringLiteral("lost")));

    // A prompt with nothing waiting closes nothing; reset forgets the looks.
    QVERIFY(exchange(tracker, nullptr, {{"The sun rises in the east.", L::ASYNC}}).empty());
    tracker.receiveCommand("l w");
    tracker.reset();
    QVERIFY(!tracker.waiting());
}
