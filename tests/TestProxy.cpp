// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2019 The MMapper Authors

#include "TestProxy.h"

#include "../src/global/TextUtils.h"
#include "../src/proxy/GmcpMessage.h"
#include "../src/proxy/GmcpModule.h"
#include "../src/proxy/GmcpUtils.h"
#include "../src/proxy/PromptNewlines.h"
#include "../src/proxy/telnetfilter.h"

#include <QDebug>
#include <QtTest/QtTest>

void TestProxy::escapeTest()
{
    QCOMPARE(GmcpUtils::escapeGmcpStringData(R"(12345)"), QString(R"(12345)"));
    QCOMPARE(GmcpUtils::escapeGmcpStringData(R"(1.0)"), QString(R"(1.0)"));
    QCOMPARE(GmcpUtils::escapeGmcpStringData(R"(true)"), QString(R"(true)"));
    QCOMPARE(GmcpUtils::escapeGmcpStringData(R"("Hello")"), QString(R"(\"Hello\")"));
    QCOMPARE(GmcpUtils::escapeGmcpStringData("\\\n\r\b\f\t"), QString(R"(\\\n\r\b\f\t)"));
}

void TestProxy::gmcpMessageDeserializeTest()
{
    GmcpMessage gmcp1 = GmcpMessage::fromRawBytes(R"(Core.Hello { "Hello": "world" })");
    QCOMPARE(gmcp1.getName().toQByteArray(), QByteArray("Core.Hello"));
    QCOMPARE(gmcp1.getJson()->toQString(), mmqt::toQStringUtf8(R"({ "Hello": "world" })"));

    GmcpMessage gmcp2 = GmcpMessage::fromRawBytes(R"(Core.Goodbye)");
    QCOMPARE(gmcp2.getName().toQByteArray(), QByteArray("Core.Goodbye"));
    QVERIFY(!gmcp2.getJson());

    GmcpMessage gmcp3 = GmcpMessage::fromRawBytes(R"(External.Discord.Hello)");
    QCOMPARE(gmcp3.getName().toQByteArray(), QByteArray("External.Discord.Hello"));
    QVERIFY(!gmcp3.getJson());
}

void TestProxy::gmcpMessageSerializeTest()
{
    GmcpMessage gmcp1(GmcpMessageTypeEnum::CORE_HELLO);
    QCOMPARE(gmcp1.toRawBytes(), QByteArray("Core.Hello"));

    GmcpMessage gmcp2(GmcpMessageTypeEnum::CORE_HELLO, GmcpJson{"{}"});
    QCOMPARE(gmcp2.toRawBytes(), QByteArray("Core.Hello {}"));
}

void TestProxy::gmcpModuleTest()
{
    GmcpModule module1("Char 1");
    QCOMPARE(mmqt::toQByteArrayUtf8(module1.getNormalizedName()), QByteArray("char"));
    QCOMPARE(module1.getVersion().asUint32(), 1u);
    QVERIFY(module1.isSupported());

    GmcpModule module2("Char.Skills 1");
    QCOMPARE(mmqt::toQByteArrayUtf8(module2.getNormalizedName()), QByteArray("char.skills"));
    QCOMPARE(module2.getVersion().asUint32(), 1u);
    QVERIFY(!module2.isSupported());

    GmcpModule module3("Room");
    QCOMPARE(mmqt::toQByteArrayUtf8(module3.getNormalizedName()), QByteArray("room"));
    QCOMPARE(module3.getVersion().asUint32(), 0u);
    QVERIFY(module3.isSupported());
}

// The newline the proxy inserts after a prompt (mume3d FIX.215): the text that follows a prompt
// starts on a line of its own whether or not MUME sent a newline first, so the client's Line rules,
// which match from a line's start, never see the prompt in front of it.
void TestProxy::promptNewlinesTest()
{
    const auto prompt = QStringLiteral("o HP:Fine Mana:Burst>");
    const auto twiddler = QStringLiteral("|\b");

    {
        // Text after a prompt with no newline in front of it: one is missing.
        PromptNewlines lines;
        lines.sent(false, false, u"You are hungry.\n");
        lines.notePrompt();
        QVERIFY(!lines.isMissingNewline(true, false, prompt));
        lines.sent(true, false, prompt);
        QVERIFY(lines.wasPrompt());
        QVERIFY(lines.isMissingNewline(false, false, u"You flee head over heels.\n"));
        // MUME's own newline, with or without a carriage return: none missing.
        QVERIFY(!lines.isMissingNewline(false, false, u"\nYou flee head over heels.\n"));
        QVERIFY(!lines.isMissingNewline(false, false, u"\r\nYou flee head over heels.\n"));
        // A twiddler after the prompt stays on its line.
        QVERIFY(!lines.isMissingNewline(false, true, twiddler));
    }
    {
        // A command sent after the prompt: the user's own Enter ended the line.
        PromptNewlines lines;
        lines.notePrompt();
        lines.sent(true, false, prompt);
        lines.clearPrompt();
        QVERIFY(!lines.wasPrompt());
        QVERIFY(!lines.isMissingNewline(false, false, u"You flee head over heels.\n"));
    }
    {
        // A prompt after a line left open: it goes on a line of its own.
        PromptNewlines lines;
        lines.sent(false, false, u"Somebody tells you 'hi'");
        lines.notePrompt();
        QVERIFY(lines.isMissingNewline(true, false, prompt));
    }
    {
        // A prompt after a whole line (compact mode) or after a blank line: none missing.
        PromptNewlines compact;
        compact.sent(false, false, u"You are hungry.\n");
        compact.notePrompt();
        QVERIFY(!compact.isMissingNewline(true, false, prompt));
        PromptNewlines spaced;
        spaced.sent(false, false, u"You are hungry.\r\n\r\n");
        spaced.notePrompt();
        QVERIFY(!spaced.isMissingNewline(true, false, prompt));
    }
    QVERIFY(PromptNewlines::startsWithNewline(u"\r\nx"));
    QVERIFY(PromptNewlines::startsWithNewline(u"\nx"));
    QVERIFY(!PromptNewlines::startsWithNewline(u"x\n"));
    QVERIFY(!PromptNewlines::startsWithNewline(u""));
}

void TestProxy::telnetFilterTest()
{
    test::test_telnetfilter();
}

QTEST_MAIN(TestProxy)
