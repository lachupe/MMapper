// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestLoginLines.h"

#include "../src/parser/LoginLines.h"

#include <tuple>
#include <vector>

#include <QtTest/QtTest>

// MUME's lines here are copied from the powwow logs; the file each comes from is named above
// it. Nothing a player typed is in them: MUME's output never holds the pass phrase.

namespace {

/// Feeds a transcript, and returns every prompt the tracker reported, in order.
NODISCARD std::vector<LoginPrompt> feed(LoginLinesTracker &tracker,
                                        const std::vector<const char *> &lines)
{
    std::vector<LoginPrompt> all;
    for (const char *const line : lines) {
        if (const auto prompt = tracker.receiveLine(QString::fromUtf8(line))) {
            all.push_back(*prompt);
        }
    }
    return all;
}

} // namespace

void TestLoginLines::promptLinesTest()
{
    const auto kind = [](const char *const text) {
        return parseLoginPromptLine(QString::fromUtf8(text));
    };
    // powwow/logs/ennoring.mov:342 and :24
    QVERIFY(kind("By what name do you wish to be known? ") == LoginPromptKindEnum::NAME);
    QVERIFY(kind("Account pass phrase: ") == LoginPromptKindEnum::PASSWORD);
    // powwow/logs/archives/log-2006.05.02-15.16.26.mov:117-125, a character's own login
    QVERIFY(kind("Password: ") == LoginPromptKindEnum::PASSWORD);
    QVERIFY(kind("Password:") == LoginPromptKindEnum::PASSWORD);
    QVERIFY(kind("Please enter your account password: ") == LoginPromptKindEnum::PASSWORD);
    // powwow/logs/old/!best/fn_dt.txt:3285
    QVERIFY(kind("Character password:") == LoginPromptKindEnum::PASSWORD);
    // powwow/logs/old/my/linkless.ded:335: the name prompt came without a GO-AHEAD.
    QVERIFY(kind("By what name do you wish to be known? Account pass phrase: ")
            == LoginPromptKindEnum::PASSWORD);

    // Not prompts: the banner, the menu's line about the pass phrase, a prompt with text after.
    QVERIFY(kind("or ? for help. Otherwise, type your account or character name.")
            == LoginPromptKindEnum::NONE);
    QVERIFY(kind("  Pass          - Change account pass phrase") == LoginPromptKindEnum::NONE);
    QVERIFY(kind("By what name do you wish to be known? Illegal name, please try another.")
            == LoginPromptKindEnum::NONE);
    QVERIFY(kind("Account> ") == LoginPromptKindEnum::NONE);
    QVERIFY(kind("") == LoginPromptKindEnum::NONE);

    QCOMPARE(loginPromptKindName(LoginPromptKindEnum::NONE), std::string_view{"none"});
    QCOMPARE(loginPromptKindName(LoginPromptKindEnum::NAME), std::string_view{"name"});
    QCOMPARE(loginPromptKindName(LoginPromptKindEnum::PASSWORD), std::string_view{"password"});
}

void TestLoginLines::refusalLinesTest()
{
    const auto reason = [](const char *const text) {
        return parseLoginRefusalLine(QString::fromUtf8(text));
    };
    // powwow/logs/archives/log-2005.10.26-01.18.32.mov:865
    QCOMPARE(reason("Wrong password."), QStringLiteral("wrong-password"));
    // powwow/logs/archives/log-2006.05.02-15.16.26.mov:123
    QCOMPARE(reason("Wrong password buddy, try again!"), QStringLiteral("wrong-password"));
    // powwow/logs/archives/log-2005.11.17-03.51.00.mov:2679-2680
    QCOMPARE(reason("No character or account by that name."), QStringLiteral("no-such-name"));
    QCOMPARE(reason("Type NEW if you have no other characters on MUME."),
             QStringLiteral("no-such-name"));
    // powwow/logs/archives/log-2005.12.24-02.05.16.mov:3, edit/full/log-2005.07.11-13.12.44.mov:26
    QCOMPARE(reason("Illegal name, please try another."), QStringLiteral("illegal-name"));
    QCOMPARE(reason("Illegal name, try again."), QStringLiteral("illegal-name"));

    QVERIFY(reason("That character is already playing.").isEmpty());
    QVERIFY(reason("Account menu").isEmpty());
    QVERIFY(reason("").isEmpty());
}

// powwow/logs/trolls_roots.mov:14-28 and moria.gjurza.mov:10: the banner, the name prompt twice
// (MUME repeats it once it has switched to XML), the pass phrase prompt, the menu.
void TestLoginLines::loginTest()
{
    LoginLinesTracker tracker;
    const auto seen = feed(tracker,
                           {
                               "If you have never played MUME before, type NEW to create a new "
                               "character,",
                               "or ? for help. Otherwise, type your account or character name.",
                               "",
                               "",
                               "By what name do you wish to be known? ",
                               "",
                               "By what name do you wish to be known? ",
                               "Account pass phrase: ",
                               "",
                               "",
                               "Account menu",
                               "  Play <name>   - Log the character <name> into MUME",
                           });
    QCOMPARE(seen.size(), static_cast<size_t>(4));
    QVERIFY(seen[0].kind == LoginPromptKindEnum::NAME);
    QCOMPARE(seen[0].text, QStringLiteral("By what name do you wish to be known?"));
    QVERIFY(seen[0].refusedReason.isEmpty());
    QCOMPARE(seen[0].serial, 1);
    // The same prompt again is told again, under the next serial.
    QVERIFY(seen[1].kind == LoginPromptKindEnum::NAME);
    QCOMPARE(seen[1].text, seen[0].text);
    QCOMPARE(seen[1].serial, 2);
    QVERIFY(seen[1] != seen[0]);
    QVERIFY(seen[2].kind == LoginPromptKindEnum::PASSWORD);
    QCOMPARE(seen[2].text, QStringLiteral("Account pass phrase:"));
    QVERIFY(seen[2].refusedReason.isEmpty());
    QCOMPARE(seen[2].serial, 3);
    // The first line that is no part of the login ends it, once.
    QVERIFY(seen[3].kind == LoginPromptKindEnum::NONE);
    QVERIFY(seen[3].text.isEmpty());
    QCOMPARE(seen[3].serial, 0);
    QVERIFY(tracker.prompt().kind == LoginPromptKindEnum::NONE);
}

// powwow/logs/archives/log-2006.07.21-01.35.51.mov:28-54: the pass phrase refused twice.
void TestLoginLines::wrongPasswordTest()
{
    LoginLinesTracker tracker;
    const auto seen = feed(tracker,
                           {
                               "By what name do you wish to be known? ",
                               "Account pass phrase: ",
                               "",
                               "",
                               "Wrong password.",
                               "",
                               "Account pass phrase: ",
                               "",
                               "",
                               "Wrong password.",
                               "",
                               "Account pass phrase: ",
                           });
    QCOMPARE(seen.size(), static_cast<size_t>(4));
    QVERIFY(seen[1].kind == LoginPromptKindEnum::PASSWORD);
    QVERIFY(seen[1].refusedReason.isEmpty());
    QVERIFY(seen[2].kind == LoginPromptKindEnum::PASSWORD);
    QCOMPARE(seen[2].refusedReason, QStringLiteral("wrong-password"));
    QCOMPARE(seen[2].refusedText, QStringLiteral("Wrong password."));
    // The second refusal is told anew, not left over and not doubled.
    QVERIFY(seen[3].kind == LoginPromptKindEnum::PASSWORD);
    QCOMPARE(seen[3].refusedReason, seen[2].refusedReason);
    QCOMPARE(seen[3].refusedText, seen[2].refusedText);
    QCOMPARE(seen[3].serial, seen[2].serial + 1);
    QVERIFY(tracker.prompt().kind == LoginPromptKindEnum::PASSWORD);

    // The name prompt run into the pass phrase prompt: the prompt's own text is the tail.
    LoginLinesTracker glued;
    const auto one = feed(glued, {"By what name do you wish to be known? Account pass phrase: "});
    QCOMPARE(one.size(), static_cast<size_t>(1));
    QVERIFY(one[0].kind == LoginPromptKindEnum::PASSWORD);
    QCOMPARE(one[0].text, QStringLiteral("Account pass phrase:"));
}

// powwow/logs/archives/log-2005.11.17-03.51.00.mov:2679-2682 and log-2005.12.24-02.05.16.mov:3-5.
void TestLoginLines::wrongNameTest()
{
    LoginLinesTracker tracker;
    const auto seen = feed(tracker,
                           {
                               "By what name do you wish to be known? ",
                               "No character or account by that name.",
                               "Type NEW if you have no other characters on MUME.",
                               "",
                               "By what name do you wish to be known? ",
                               "Illegal name, please try another.",
                               "",
                               "By what name do you wish to be known? ",
                           });
    QCOMPARE(seen.size(), static_cast<size_t>(3));
    QVERIFY(seen[1].kind == LoginPromptKindEnum::NAME);
    QCOMPARE(seen[1].refusedReason, QStringLiteral("no-such-name"));
    QCOMPARE(seen[1].refusedText,
             QStringLiteral("No character or account by that name. Type NEW if you have no other "
                            "characters on MUME."));
    QVERIFY(seen[2].kind == LoginPromptKindEnum::NAME);
    QCOMPARE(seen[2].refusedReason, QStringLiteral("illegal-name"));
    QCOMPARE(seen[2].refusedText, QStringLiteral("Illegal name, please try another."));
}

void TestLoginLines::notLoginTest()
{
    // A pass phrase asked for with no name prompt before it (the account menu's `pass`, or a
    // line that only ends that way) is no login; nor is a refusal with no prompt standing.
    LoginLinesTracker tracker;
    QVERIFY(feed(tracker,
                 {
                     "Account> ",
                     "Enter your current account pass phrase: ",
                     "Wrong password.",
                     "Someone says 'what is the password:",
                 })
                .empty());
    QVERIFY(tracker.prompt().kind == LoginPromptKindEnum::NONE);

    // After the login has gone on, a pass phrase prompt is not one of it any more.
    const auto seen = feed(tracker,
                           {
                               "By what name do you wish to be known? ",
                               "Account pass phrase: ",
                               "Account menu",
                               "New pass phrase: ",
                           });
    QCOMPARE(seen.size(), static_cast<size_t>(3));
    QVERIFY(seen[2].kind == LoginPromptKindEnum::NONE);
}

void TestLoginLines::resetTest()
{
    LoginLinesTracker tracker;
    tracker.reset();
    QVERIFY(tracker.prompt().kind == LoginPromptKindEnum::NONE);
    std::ignore = feed(tracker, {"By what name do you wish to be known? ", "Wrong password."});
    QVERIFY(tracker.prompt().kind == LoginPromptKindEnum::NAME);
    tracker.reset();
    QVERIFY(tracker.prompt().kind == LoginPromptKindEnum::NONE);
    // The refusal that was waiting went with it; the serial goes on, and never repeats.
    const auto seen = feed(tracker, {"By what name do you wish to be known? "});
    QCOMPARE(seen.size(), static_cast<size_t>(1));
    QVERIFY(seen[0].refusedReason.isEmpty());
    QCOMPARE(seen[0].serial, 2);
}

QTEST_MAIN(TestLoginLines)
