// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestFrontend.h"

#include "../src/frontend/FrontendMapIdentity.h"
#include "../src/frontend/FrontendMessages.h"
#include "../src/frontend/FrontendRenderPause.h"
#include "../src/frontend/FrontendReplayCache.h"
#include "../src/frontend/FrontendSubscriptions.h"
#include "../src/global/HideQDebug.h"
#include "../src/global/progresscounter.h"
#include "../src/map/Change.h"
#include "../src/map/ChangeTypes.h"
#include "../src/map/Map.h"
#include "../src/map/RawRoom.h"
#include "../src/map/RoomFingerprint.h"
#include "../src/map/RoomHandle.h"
#include "../src/map/coordinate.h"
#include "../src/map/mmapper2room.h"
#include "../src/map/roomid.h"
#include "../src/observer/gameobserver.h"
#include "../src/parser/AccountLines.h"
#include "../src/parser/CharAffects.h"
#include "../src/parser/CharRefused.h"
#include "../src/parser/CombatLines.h"
#include "../src/parser/ContainerLines.h"
#include "../src/parser/LoginLines.h"
#include "../src/parser/ExitLooks.h"
#include "../src/parser/GameStateLines.h"
#include "../src/parser/ItemLines.h"
#include "../src/parser/RoomContents.h"
#include "../src/parser/RoomDoors.h"
#include "../src/parser/WeatherLines.h"
#include "../src/parser/XmlElement.h"
#include "../src/proxy/GmcpMessage.h"
#include "../src/proxy/GmcpModule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

namespace {

NODISCARD GmcpMessage parse(const char *const raw)
{
    return GmcpMessage::fromRawBytes(QByteArray{raw});
}

NODISCARD QJsonObject payloadOf(const GmcpMessage &msg)
{
    const auto &optJson = msg.getJson();
    if (!optJson.has_value()) {
        return QJsonObject{};
    }
    return QJsonDocument::fromJson(optJson->toQByteArray()).object();
}

/// The payload `cache` would replay for `type`, or an empty document if it would replay none.
/// The listings ItemBlockTracker makes of `lines` and the prompt after them.
NODISCARD std::vector<ItemBlock> listed(const QStringList &lines, const QString &command = {})
{
    ItemBlockTracker tracker;
    if (!command.isEmpty()) {
        tracker.receiveCommand(command);
    }
    std::vector<ItemBlock> blocks;
    for (const QString &line : lines) {
        for (ItemBlock &block : tracker.receiveLine(line)) {
            blocks.push_back(std::move(block));
        }
    }
    for (ItemBlock &block : tracker.receivePrompt()) {
        blocks.push_back(std::move(block));
    }
    return blocks;
}

NODISCARD QJsonDocument replayed(const FrontendReplayCache &cache, const GmcpMessageTypeEnum type)
{
    const auto &messages = cache.messages();
    const auto it = messages.find(type);
    if (it == messages.end() || !it->second.getJson().has_value()) {
        return QJsonDocument{};
    }
    return QJsonDocument::fromJson(it->second.getJson()->toQByteArray());
}

/// Builds a one-room map so that room serialization can be tested without a live session.
NODISCARD Map makeOneRoomMap(const ServerRoomId serverId)
{
    ExternalRawRoom room;
    room.setId(ExternalRoomId{1});
    room.setServerId(serverId);
    room.setPosition(Coordinate{12, -34, 2});
    room.setName(RoomName{"A forest path"});
    room.setArea(RoomArea{"The Shire"});
    room.setTerrainType(RoomTerrainEnum::FOREST);
    room.status = RoomStatusEnum::Permanent;

    ProgressCounter pc;
    return Map::fromRooms(pc, {room}, {}).modified;
}

} // namespace

void TestFrontend::subscriptionSetTest()
{
    FrontendSubscriptions subs;
    QVERIFY(subs.empty());

    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Char 1", "MMapper.Map 1" ])")));
    QCOMPARE(subs.size(), static_cast<size_t>(2));

    QVERIFY(subs.wants(parse(R"(Char.Vitals { "hp": 1 })")));
    QVERIFY(subs.wants(parse(R"(MMapper.Map.Position { "id": 1 })")));

    // Not subscribed.
    QVERIFY(!subs.wants(parse(R"(Group.Set [])")));
    QVERIFY(!subs.wants(parse(R"(MMapper.Terminal.Output { "text": "x" })")));

    // Set replaces rather than merges.
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Group 1" ])")));
    QCOMPARE(subs.size(), static_cast<size_t>(1));
    QVERIFY(subs.wants(parse(R"(Group.Set [])")));
    QVERIFY(!subs.wants(parse(R"(Char.Vitals { "hp": 1 })")));
}

void TestFrontend::subscriptionAddRemoveTest()
{
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Char 1" ])")));

    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Add [ "MMapper.Session 1" ])")));
    QVERIFY(subs.wants(parse(R"(Char.Vitals {})")));
    QVERIFY(subs.wants(parse(R"(MMapper.Session.State {})")));

    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Remove [ "Char 1" ])")));
    QVERIFY(!subs.wants(parse(R"(Char.Vitals {})")));
    QVERIFY(subs.wants(parse(R"(MMapper.Session.State {})")));
}

void TestFrontend::subscriptionSplitModuleTest()
{
    // The MMapper.* modules are split so that a client can take position updates without
    // also receiving the whole terminal stream.
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Map 1" ])")));

    QVERIFY(subs.wants(parse(R"(MMapper.Map.Position {})")));
    QVERIFY(!subs.wants(parse(R"(MMapper.Session.State {})")));
    QVERIFY(!subs.wants(parse(R"(MMapper.Terminal.Output {})")));

    // Module matching is case insensitive, as it is for MUME's own modules.
    FrontendSubscriptions lower;
    QVERIFY(lower.applySupports(parse(R"(Core.Supports.Set [ "mmapper.terminal 1" ])")));
    QVERIFY(lower.wants(parse(R"(MMapper.Terminal.Output {})")));
}

void TestFrontend::subscriptionMalformedTest()
{
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Char 1" ])")));

    // Not a supports message at all.
    QVERIFY(!subs.applySupports(parse(R"(Char.Vitals { "hp": 1 })")));
    // Supports message whose payload is not an array; must not disturb the existing set.
    QVERIFY(!subs.applySupports(parse(R"(Core.Supports.Set { "Char": 1 })")));
    QVERIFY(!subs.applySupports(parse(R"(Core.Supports.Set)")));
    QCOMPARE(subs.size(), static_cast<size_t>(1));
    QVERIFY(subs.wants(parse(R"(Char.Vitals {})")));

    // A package with no module prefix belongs to no module.
    QVERIFY(!subs.wants(parse(R"(Bogus {})")));
}

void TestFrontend::relayFilterTest()
{
    // Any module name is accepted, one MMapper will never relay included...
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Core 1", "MUME.Client 1" ])")));
    QVERIFY(subs.wants(parse(R"(Core.Goodbye {})")));

    // ...so it is the relay that keeps Core and MUME.Client away from a frontend, a
    // MUME.Client message the proxy does not know by name included.
    QVERIFY(!FrontendSubscriptions::isRelayable(parse(R"(Core.Goodbye {})")));
    QVERIFY(!FrontendSubscriptions::isRelayable(parse(R"(core.ping)")));
    QVERIFY(!FrontendSubscriptions::isRelayable(parse(R"(MUME.Client.Edit {})")));
    QVERIFY(!FrontendSubscriptions::isRelayable(parse(R"(MUME.Client.Unheard {})")));

    // Everything else MUME sends is relayed.
    QVERIFY(FrontendSubscriptions::isRelayable(parse(R"(Char.Vitals {})")));
    QVERIFY(FrontendSubscriptions::isRelayable(parse(R"(Comm.Channel.Text {})")));
    QVERIFY(FrontendSubscriptions::isRelayable(parse(R"(Room.Chars.Set [])")));
}

void TestFrontend::terminalOutputTest()
{
    const QString text = QStringLiteral("\x1b[32mA forest path\x1b[0m\r\n");
    const GmcpMessage msg = frontend_messages::makeTerminalOutput(SendToUserSourceEnum::FromMud,
                                                                  text,
                                                                  false);

    QCOMPARE(msg.getName().toQByteArray(), QByteArray("MMapper.Terminal.Output"));

    const QJsonObject obj = payloadOf(msg);
    // ANSI must survive: a frontend is expected to render colour.
    QCOMPARE(obj["text"].toString(), text);
    QCOMPARE(obj["format"].toString(), QStringLiteral("ansi"));
    QCOMPARE(obj["source"].toString(), QStringLiteral("mud"));
    QCOMPARE(obj["goAhead"].toBool(), false);

    // A prompt is distinguished by goAhead, not by its text.
    const GmcpMessage prompt = frontend_messages::makeTerminalOutput(SendToUserSourceEnum::FromMud,
                                                                     "HP:Fine> ",
                                                                     true);
    QCOMPARE(payloadOf(prompt)["goAhead"].toBool(), true);

    // MMapper's own output is distinguishable from the game's.
    const GmcpMessage own = frontend_messages::makeTerminalOutput(SendToUserSourceEnum::FromMMapper,
                                                                  "hi",
                                                                  false);
    QCOMPARE(payloadOf(own)["source"].toString(), QStringLiteral("mmapper"));
}

namespace {

/// The chunks MumeXmlParser::parse() makes of `document`, with the parts it marks as a room's
/// description: a chunk ends at each newline, the tags are taken out, and a run of text inside
/// <description> is noted with where it stands in the chunk. What is left at the end is the
/// prompt.
NODISCARD std::vector<TerminalOutput> chunksOf(const QString &document)
{
    std::vector<TerminalOutput> chunks;
    TerminalOutput chunk;
    QString run;
    bool inDescription = false;
    const auto endRun = [&chunk, &run, &inDescription]() {
        if (!run.isEmpty() && inDescription) {
            chunk.roomDescription.push_back(TerminalSpan{chunk.text.size(), run.size()});
        }
        chunk.text += run;
        run.clear();
    };
    qsizetype i = 0;
    while (i < document.size()) {
        const QChar c = document.at(i);
        if (c == QLatin1Char('<')) {
            endRun();
            const qsizetype end = document.indexOf(QLatin1Char('>'), i);
            const QString tag = document.mid(i + 1, end - i - 1);
            if (tag == QStringLiteral("description")) {
                inDescription = true;
            } else if (tag == QStringLiteral("/description")) {
                inDescription = false;
            }
            i = end + 1;
            continue;
        }
        run += c;
        ++i;
        if (c == QLatin1Char('\n')) {
            endRun();
            chunks.push_back(std::exchange(chunk, TerminalOutput{}));
        }
    }
    endRun();
    if (!chunk.text.isEmpty()) {
        chunk.goAhead = true;
        chunks.push_back(chunk);
    }
    return chunks;
}

/// What a frontend with `filter` is sent of `chunks`: each message's text, in order.
NODISCARD QStringList textsFor(const std::vector<TerminalOutput> &chunks,
                               const frontend_messages::TerminalFilter &filter)
{
    QStringList texts;
    for (const TerminalOutput &chunk : chunks) {
        if (const auto msg = frontend_messages::makeTerminalOutput(chunk, filter)) {
            texts << payloadOf(*msg)["text"].toString();
        }
    }
    return texts;
}

} // namespace

void TestFrontend::terminalFilterTest()
{
    frontend_messages::TerminalFilter off;
    frontend_messages::TerminalFilter on;
    on.roomDescriptions = true;

    // A room as MUME sends it in XML mode (the shape of TestWeatherLines' rooms: MUME's colour
    // and the line end are inside the elements), its description two lines long, with an
    // object lying in it and the prompt after it.
    const QString room = QStringLiteral(
        "<room><name>\x1b[32mRolling Hills\x1b[0m\n</name>"
        "<description>\x1b[34mThe land gently rises and falls around here creating some\n"
        "pleasant hills.\x1b[0m\n</description>A lantern lies here.\n</room>"
        "<prompt>*. CRW HP:Fine></prompt>");
    const std::vector<TerminalOutput> chunks = chunksOf(room);
    QCOMPARE(chunks.size(), size_t{5});
    QVERIFY(chunks[0].roomDescription.empty());
    QCOMPARE(chunks[1].roomDescription.size(), size_t{1});
    QCOMPARE(chunks[2].roomDescription.size(), size_t{1});
    QVERIFY(chunks[3].roomDescription.empty());
    QVERIFY(chunks[4].goAhead);

    // The filter off: every chunk goes, byte for byte what it was before there was a filter.
    for (const TerminalOutput &chunk : chunks) {
        const auto msg = frontend_messages::makeTerminalOutput(chunk, off);
        QVERIFY(msg.has_value());
        QCOMPARE(msg->toRawBytes(),
                 frontend_messages::makeTerminalOutput(chunk.source, chunk.text, chunk.goAhead)
                     .toRawBytes());
    }

    // The filter on: the name, the object and the prompt, and nothing where the description
    // was. Its two lines arrived as two chunks; both are gone, and no empty line stands in.
    const QStringList spared = textsFor(chunks, on);
    QCOMPARE(spared,
             (QStringList{QStringLiteral("\x1b[32mRolling Hills\x1b[0m\n"),
                          QStringLiteral("A lantern lies here.\n"),
                          QStringLiteral("*. CRW HP:Fine>")}));
    QVERIFY(!spared.join(QString{}).contains(QStringLiteral("\n\n")));
    // What stays is sent as it was: the same message as without the filter.
    QCOMPARE(frontend_messages::makeTerminalOutput(chunks[0], on)->toRawBytes(),
             frontend_messages::makeTerminalOutput(chunks[0], off)->toRawBytes());

    // Brief mode, or a room MUME gives no description of: nothing is marked, nothing changes.
    const std::vector<TerminalOutput> brief = chunksOf(QStringLiteral(
        "<room><name>Rolling Hills\n</name>A lantern lies here.\n</room><prompt>*></prompt>"));
    QCOMPARE(textsFor(brief, on), textsFor(brief, off));
    QCOMPARE(textsFor(brief, on).size(), qsizetype{3});

    // The closing tag after the line's end, or before it: either way the line goes whole.
    const std::vector<TerminalOutput> closed = chunksOf(QStringLiteral(
        "<room><name>A Cave\n</name><description>Dark and damp.</description>\n"
        "A bat hangs here.\n</room>"));
    QCOMPARE(textsFor(closed, on),
             (QStringList{QStringLiteral("A Cave\n"), QStringLiteral("A bat hangs here.\n")}));
    // And with MUME's line end as "\r\n", or colour left around the description.
    TerminalOutput crlf;
    crlf.text = QStringLiteral("Dark and damp.\r\n");
    crlf.roomDescription.push_back(TerminalSpan{0, 14});
    QVERIFY(!frontend_messages::makeTerminalOutput(crlf, on).has_value());
    TerminalOutput coloured;
    coloured.text = QStringLiteral("\x1b[34mDark and damp.\x1b[0m\n");
    coloured.roomDescription.push_back(TerminalSpan{5, 14});
    QVERIFY(!frontend_messages::makeTerminalOutput(coloured, on).has_value());
    QVERIFY(frontend_messages::makeTerminalOutput(coloured, off).has_value());

    // A description that shares its line with something else: only its own words go.
    TerminalOutput shared;
    shared.text = QStringLiteral("A Cave: Dark and damp. Exits: north.\n");
    shared.roomDescription.push_back(TerminalSpan{8, 15});
    QCOMPARE(payloadOf(*frontend_messages::makeTerminalOutput(shared, on))["text"].toString(),
             QStringLiteral("A Cave: Exits: north.\n"));

    // Parts that do not fit the text are cut to it, not trusted.
    TerminalOutput odd;
    odd.text = QStringLiteral("A Cave\n");
    odd.roomDescription.push_back(TerminalSpan{50, 10});
    odd.roomDescription.push_back(TerminalSpan{2, -4});
    QCOMPARE(payloadOf(*frontend_messages::makeTerminalOutput(odd, on))["text"].toString(),
             QStringLiteral("A Cave\n"));
}

void TestFrontend::terminalFilterPayloadTest()
{
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"MMapper.Terminal.Filter"}).getType(),
             GmcpMessageTypeEnum::MMAPPER_TERMINAL_FILTER);
    const auto apply = [](const char *const raw, frontend_messages::TerminalFilter &filter) {
        return frontend_messages::applyTerminalFilter(GmcpMessage::fromRawBytes(QByteArray{raw}),
                                                      filter);
    };

    // Nothing is spared to begin with.
    frontend_messages::TerminalFilter filter;
    QVERIFY(!filter.roomDescriptions);
    QCOMPARE(frontend_messages::makeTerminalFilter(filter).toRawBytes(),
             QByteArray(R"(MMapper.Terminal.Filter {"roomDescriptions":false})"));

    QVERIFY(apply(R"(MMapper.Terminal.Filter {"roomDescriptions":true})", filter));
    QVERIFY(filter.roomDescriptions);
    QCOMPARE(frontend_messages::makeTerminalFilter(filter).toRawBytes(),
             QByteArray(R"(MMapper.Terminal.Filter {"roomDescriptions":true})"));
    // A key it does not have is left as it is; one not known is ignored.
    QVERIFY(apply(R"(MMapper.Terminal.Filter {})", filter));
    QVERIFY(filter.roomDescriptions);
    QVERIFY(apply(R"(MMapper.Terminal.Filter {"weather":true})", filter));
    QVERIFY(filter.roomDescriptions);
    QVERIFY(apply(R"(MMapper.Terminal.Filter {"roomDescriptions":false})", filter));
    QVERIFY(!filter.roomDescriptions);

    // Anything else is refused, and changes nothing.
    filter.roomDescriptions = true;
    QVERIFY(!apply(R"(MMapper.Terminal.Filter {"roomDescriptions":"yes"})", filter));
    QVERIFY(!apply(R"(MMapper.Terminal.Filter {"roomDescriptions":1})", filter));
    QVERIFY(!apply(R"(MMapper.Terminal.Filter [true])", filter));
    QVERIFY(!apply(R"(MMapper.Terminal.Filter)", filter));
    QVERIFY(filter.roomDescriptions);
}

void TestFrontend::terminalHiddenTest()
{
    QCOMPARE(GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TERMINAL_HIDDEN}.getName().toQByteArray(),
             QByteArray("MMapper.Terminal.Hidden"));

    // A room's description, with the text itself and the room it is of.
    TerminalHidden description;
    description.kind = TerminalHiddenEnum::ROOM_DESCRIPTION;
    description.text = QStringLiteral("The land gently rises and falls.\nPleasant hills.");
    description.roomId = 1234567;
    QCOMPARE(frontend_messages::makeTerminalHidden(description).toRawBytes(),
             QByteArray(R"(MMapper.Terminal.Hidden {"kind":"room.description","roomId":1234567,)"
                        R"("text":"The land gently rises and falls.\nPleasant hills."})"));
    // Without a room id when MMapper has none.
    description.roomId.reset();
    QVERIFY(!payloadOf(frontend_messages::makeTerminalHidden(description)).contains("roomId"));

    // The MMXP line, which no terminal is sent.
    TerminalHidden level;
    level.kind = TerminalHiddenEnum::CHAR_LEVEL;
    level.text = QStringLiteral("MMXP 56 45370716 1029284 271013 0");
    QCOMPARE(frontend_messages::makeTerminalHidden(level).toRawBytes(),
             QByteArray(R"(MMapper.Terminal.Hidden {"kind":"char.level",)"
                        R"("text":"MMXP 56 45370716 1029284 271013 0"})"));

    // A frontend is told of a description only if it was spared it: the others were sent the
    // text. What nobody was sent, everybody is told of.
    frontend_messages::TerminalFilter off;
    frontend_messages::TerminalFilter on;
    on.roomDescriptions = true;
    description.kind = TerminalHiddenEnum::ROOM_DESCRIPTION;
    QVERIFY(!frontend_messages::wantsTerminalHidden(description, off));
    QVERIFY(frontend_messages::wantsTerminalHidden(description, on));
    QVERIFY(frontend_messages::wantsTerminalHidden(level, off));
    QVERIFY(frontend_messages::wantsTerminalHidden(level, on));

    // The package is MMapper.Terminal's: a frontend that reads the terminal gets it.
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(
        GmcpMessage::fromRawBytes(QByteArray{R"(Core.Supports.Set ["MMapper.Terminal 1"])"})));
    QVERIFY(subs.wants(frontend_messages::makeTerminalHidden(level)));
    QVERIFY(subs.wants(frontend_messages::makeTerminalFilter(on)));
}

void TestFrontend::terminalSpansTest()
{
    // What the parser says of a chunk reaches the chunk's signal beside the output path.
    GameObserver observer;
    Signal2Lifetime lifetime;
    std::vector<TerminalOutput> seen;
    observer.sig2_sentToUserTerminal.connect(lifetime, [&seen](const TerminalOutput &out) {
        seen.push_back(out);
    });
    const QString line = QStringLiteral("Dark and damp.\n");

    // The newline MMapper adds after a prompt comes between, and takes nothing.
    observer.observeTerminalSpans(line, {TerminalSpan{0, 15}});
    observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMMapper,
                                       QStringLiteral("\n"),
                                       false);
    observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud, line, false);
    QCOMPARE(seen.size(), size_t{2});
    QVERIFY(seen[0].roomDescription.empty());
    QCOMPARE(seen[1].roomDescription.size(), size_t{1});
    QCOMPARE(seen[1].roomDescription[0].length, qsizetype{15});

    // Once: the same text again is another chunk.
    observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud, line, false);
    QVERIFY(seen[2].roomDescription.empty());

    // A chunk that never reached the terminal (the proxy dropped it): what was said of it is
    // forgotten at the next chunk of MUME's, and marks nothing later.
    observer.observeTerminalSpans(line, {TerminalSpan{0, 15}});
    observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud,
                                       QStringLiteral("Other\n"),
                                       false);
    observer.observeSentToUserTerminal(SendToUserSourceEnum::FromMud, line, false);
    QVERIFY(seen[3].roomDescription.empty());
    QVERIFY(seen[4].roomDescription.empty());
}

void TestFrontend::sessionStateTest()
{
    frontend_messages::MapIdentity arda;
    arda.name = QStringLiteral("arda.mm2");
    arda.rooms = 31000;
    arda.generation = 7;
    const GmcpMessage connected = frontend_messages::makeSessionState(true, arda, true, true);
    QCOMPARE(connected.getName().toQByteArray(), QByteArray("MMapper.Session.State"));
    QCOMPARE(payloadOf(connected)["upstream"].toString(), QStringLiteral("connected"));
    QCOMPARE(payloadOf(connected)["mapLoaded"].toBool(), true);
    QCOMPARE(payloadOf(connected)["itemCommands"].toInt(), 6);
    // Trade operations are offered, and MUME's viewer setting is not known until MMapper or
    // the player sets it.
    QCOMPARE(payloadOf(connected)["trade"].toInt(), 1);
    // And the quiet command, MMapper.Input.Quiet.
    QCOMPARE(payloadOf(connected)["quiet"].toInt(), 1);
    // And MMapper.Terminal.Filter with MMapper.Terminal.Hidden.
    QCOMPARE(payloadOf(connected)["hidden"].toInt(), 1);
    QCOMPARE(payloadOf(connected)["viewer"].toString(), QStringLiteral("unknown"));
    QCOMPARE(payloadOf(frontend_messages::makeSessionState(true,
                                                           arda,
                                                           true,
                                                           true,
                                                           GameStateEnum::PLAYING,
                                                           QStringLiteral("external")))["viewer"]
                 .toString(),
             QStringLiteral("external"));

    // MUME's login prompt is a field of the state, only while MUME waits at one: what it asks
    // for, its own words, a serial, and why it asks again. Never what the player answered.
    QVERIFY(!payloadOf(connected).contains(QStringLiteral("login")));
    LoginPrompt asked;
    asked.kind = LoginPromptKindEnum::PASSWORD;
    asked.text = QStringLiteral("Account pass phrase:");
    asked.serial = 3;
    const QJsonObject first = payloadOf(frontend_messages::makeSessionState(true,
                                                                            arda,
                                                                            false,
                                                                            true,
                                                                            GameStateEnum::MENU,
                                                                            QStringLiteral("unknown"),
                                                                            asked))["login"]
                                  .toObject();
    QCOMPARE(first["kind"].toString(), QStringLiteral("password"));
    QCOMPARE(first["text"].toString(), QStringLiteral("Account pass phrase:"));
    QCOMPARE(first["serial"].toInt(), 3);
    QVERIFY(!first.contains(QStringLiteral("refused")));
    asked.refusedReason = QStringLiteral("wrong-password");
    asked.refusedText = QStringLiteral("Wrong password.");
    asked.serial = 4;
    const QJsonObject again = payloadOf(frontend_messages::makeSessionState(true,
                                                                            arda,
                                                                            false,
                                                                            true,
                                                                            GameStateEnum::MENU,
                                                                            QStringLiteral("unknown"),
                                                                            asked))["login"]
                                  .toObject();
    QCOMPARE(again["serial"].toInt(), 4);
    QCOMPARE(again["refused"].toObject()["reason"].toString(), QStringLiteral("wrong-password"));
    QCOMPARE(again["refused"].toObject()["text"].toString(), QStringLiteral("Wrong password."));
    // With MUME gone there is no prompt to answer, whatever was last seen.
    QVERIFY(!payloadOf(frontend_messages::makeSessionState(false,
                                                           arda,
                                                           false,
                                                           true,
                                                           GameStateEnum::MENU,
                                                           QStringLiteral("unknown"),
                                                           asked))
                 .contains(QStringLiteral("login")));

    // The map is named, so that a frontend reading an export of its own can tell whether that
    // is of this map, and whether this map has changed since.
    QCOMPARE(payloadOf(connected)["mapName"].toString(), QStringLiteral("arda.mm2"));
    QCOMPARE(payloadOf(connected)["mapRooms"].toInteger(), static_cast<qint64>(31000));
    QCOMPARE(payloadOf(connected)["mapGeneration"].toInteger(), static_cast<qint64>(7));

    const GmcpMessage offline = frontend_messages::makeSessionState(false,
                                                                    frontend_messages::MapIdentity{},
                                                                    false,
                                                                    false);
    QCOMPARE(payloadOf(offline)["upstream"].toString(), QStringLiteral("disconnected"));
    QCOMPARE(payloadOf(offline)["echo"].toBool(), false);

    // Without a map the fields are still there, empty.
    QCOMPARE(payloadOf(offline)["mapLoaded"].toBool(), false);
    QVERIFY(payloadOf(offline)["mapName"].isString());
    QCOMPARE(payloadOf(offline)["mapName"].toString(), QString{});
    QCOMPARE(payloadOf(offline)["mapRooms"].toInteger(-1), static_cast<qint64>(0));
    QCOMPARE(payloadOf(offline)["mapGeneration"].toInteger(-1), static_cast<qint64>(0));

    // Only one frontend may drive MMapper's single downstream session; the rest observe it,
    // and each is told which it is.
    QCOMPARE(payloadOf(connected)["role"].toString(), QStringLiteral("driving"));
    QCOMPARE(payloadOf(offline)["role"].toString(), QStringLiteral("observing"));
}

void TestFrontend::gameStateTest()
{
    const auto line = [](const char *const text) {
        return parseGameStateLine(QString::fromUtf8(text));
    };

    // MUME's own lines, from the powwow logs. The innkeeper is named, and differs.
    QVERIFY(line("Erienal stores your stuff in the safe, and helps you into your chamber.")
            == GameStateEnum::RENTED);
    QVERIFY(line("Takhr the orkish warden stores your stuff in the safe, and helps you into your "
                 "chamber.")
            == GameStateEnum::RENTED);
    QVERIFY(line("You finish building your camp and crawl into your tent to rest.")
            == GameStateEnum::RENTED);
    QVERIFY(line("Goodbye, friend.. Come back soon!") == GameStateEnum::QUIT);
    // The account menu's prompt, alone at a GO-AHEAD or run into the line after it.
    QVERIFY(line("Account> ") == GameStateEnum::MENU);
    QVERIFY(line("Account> Characters in account \"dmitry\"") == GameStateEnum::MENU);
    QVERIFY(line("By what name do you wish to be known? ") == GameStateEnum::MENU);

    // What only looks like them: the cost of renting, the command, someone's speech.
    QVERIFY(!line("Renting will cost you 20 gold 15 silver 30 copper per day.").has_value());
    QVERIFY(!line("You will be able to rent for 1 month.").has_value());
    QVERIFY(!line("You start making your camp here.").has_value());
    QVERIFY(!line("Erienal tells you 'I am sorry, but you can't rent now...'").has_value());
    QVERIFY(!line("Gandalf narrates 'Goodbye, friend.. Come back soon!'").has_value());
    QVERIFY(!line("Characters in account \"dmitry\"").has_value());
    QVERIFY(!line("").has_value());

    // The menu after a rent or a quit keeps the rent or the quit.
    QCOMPARE(nextGameState(GameStateEnum::RENTED, GameStateEnum::MENU), GameStateEnum::RENTED);
    QCOMPARE(nextGameState(GameStateEnum::QUIT, GameStateEnum::MENU), GameStateEnum::QUIT);
    QCOMPARE(nextGameState(GameStateEnum::PLAYING, GameStateEnum::MENU), GameStateEnum::MENU);
    QCOMPARE(nextGameState(GameStateEnum::RENTED, GameStateEnum::PLAYING), GameStateEnum::PLAYING);

    // The observer announces changes only, and connecting starts again from nothing.
    GameObserver observer;
    std::vector<GameStateEnum> seen;
    Signal2Lifetime lifetime;
    observer.sig2_gameStateChanged.connect(lifetime,
                                           [&seen](const GameStateEnum s) { seen.push_back(s); });
    observer.observeConnected();
    observer.observeGameState(GameStateEnum::MENU);
    observer.observeGameState(GameStateEnum::PLAYING);
    observer.observeGameState(GameStateEnum::PLAYING);
    observer.observeGameState(GameStateEnum::RENTED);
    observer.observeGameState(GameStateEnum::MENU);
    QCOMPARE(observer.getGameState(), GameStateEnum::RENTED);
    QCOMPARE(seen,
             (std::vector<GameStateEnum>{GameStateEnum::MENU,
                                         GameStateEnum::PLAYING,
                                         GameStateEnum::RENTED}));
    observer.observeDisconnected();
    QCOMPARE(observer.getGameState(), GameStateEnum::UNKNOWN);

    // Published as `game`, and "unknown" whenever MUME is not there.
    frontend_messages::MapIdentity map;
    const auto game = [&map](const bool upstream, const GameStateEnum state) {
        return payloadOf(
                   frontend_messages::makeSessionState(upstream, map, true, true, state))["game"]
            .toString();
    };
    QCOMPARE(game(true, GameStateEnum::PLAYING), QStringLiteral("playing"));
    QCOMPARE(game(true, GameStateEnum::RENTED), QStringLiteral("rented"));
    QCOMPARE(game(true, GameStateEnum::QUIT), QStringLiteral("quit"));
    QCOMPARE(game(true, GameStateEnum::MENU), QStringLiteral("menu"));
    QCOMPARE(game(true, GameStateEnum::UNKNOWN), QStringLiteral("unknown"));
    QCOMPARE(game(false, GameStateEnum::PLAYING), QStringLiteral("unknown"));
    QCOMPARE(payloadOf(frontend_messages::makeSessionState(true, map, true, true))["game"].toString(),
             QStringLiteral("unknown"));
}

void TestFrontend::inputSubscriptionTest()
{
    // Input is client-to-server, so a frontend never subscribes to receive it; but the
    // module has to be known, or "MMapper.Input" would be treated as an unrecognised module
    // and proxied upstream to MUME by UserTelnet's supports filter.
    const GmcpModule mod{std::string{"mmapper.input"}};
    QVERIFY(mod.isSupported());
    QCOMPARE(mod.getType(), GmcpModuleTypeEnum::MMAPPER_INPUT);

    const GmcpMessage command = GmcpMessage::fromRawBytes(
        QByteArray(R"(MMapper.Input.Command {"text":"north"})"));
    QVERIFY(command.isMMapperInputCommand());
    QCOMPARE(command.getJsonDocument()->getObject()->getString("text").value(),
             QStringLiteral("north"));
}

void TestFrontend::errorTest()
{
    const GmcpMessage msg = frontend_messages::makeError("read-only", "nope");
    QCOMPARE(msg.getName().toQByteArray(), QByteArray("MMapper.Session.Error"));
    QCOMPARE(payloadOf(msg)["code"].toString(), QStringLiteral("read-only"));
    QCOMPARE(payloadOf(msg)["message"].toString(), QStringLiteral("nope"));
}

void TestFrontend::mapPositionTest()
{
    const Map map = makeOneRoomMap(ServerRoomId{812345});
    const RoomHandle room = map.findRoomHandle(ExternalRoomId{1});
    QVERIFY(room.exists());

    const GmcpMessage msg = frontend_messages::makeMapPosition(room);
    QCOMPARE(msg.getName().toQByteArray(), QByteArray("MMapper.Map.Position"));

    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["serverId"].toInteger(), static_cast<qint64>(812345));
    QCOMPARE(obj["externalId"].toInteger(), static_cast<qint64>(1));
    QVERIFY(obj.contains("id"));
    QCOMPARE(obj["name"].toString(), QStringLiteral("A forest path"));
    QCOMPARE(obj["area"].toString(), QStringLiteral("The Shire"));
    QCOMPARE(obj["terrain"].toString(), QStringLiteral("FOREST"));

    // The room's fingerprint, the one the XML export writes on it. The room has a name, no
    // description, terrain FOREST and no exits, so, worked out apart from MMapper:
    //   printf 'A forest path\n\nFOREST\n\n' | sha256sum
    //   a7793e150db4f403243b1ae80bdf1d9355d3382bc4533bde2e3b133cf363f8c2  -
    QCOMPARE(obj["fingerprint"].toString(), QStringLiteral("a7793e150db4"));
    QCOMPARE(obj["fingerprint"].toString(), room_fingerprint::compute(room));

    // Coordinates are published as an explicitly named layout, not as bare fields, so that
    // a renderer is not led to treat them as canonical physical geometry.
    const QJsonObject layout = obj["layout"].toObject();
    QCOMPARE(layout["kind"].toString(), QStringLiteral("mmapper-grid"));
    QCOMPARE(layout["x"].toInt(), 12);
    QCOMPARE(layout["y"].toInt(), -34);
    QCOMPARE(layout["z"].toInt(), 2);
}

void TestFrontend::mapPositionWithoutServerIdTest()
{
    // MUME does not always supply a room id. The field is then omitted rather than zeroed,
    // so a client can tell "unknown" apart from a real id and fall back to externalId.
    const Map map = makeOneRoomMap(INVALID_SERVER_ROOMID);
    const RoomHandle room = map.findRoomHandle(ExternalRoomId{1});
    QVERIFY(room.exists());

    const QJsonObject obj = payloadOf(frontend_messages::makeMapPosition(room));
    QVERIFY(!obj.contains("serverId"));
    QCOMPARE(obj["externalId"].toInteger(), static_cast<qint64>(1));

    // Then the fingerprint is the key to find the room by, and MUME's id is no part of it.
    QCOMPARE(obj["fingerprint"].toString(), QStringLiteral("a7793e150db4"));
}

void TestFrontend::mapIdentityTest()
{
    mmqt::HideQDebug forThisTest; // Map reports each change it applies.
    const Map map = makeOneRoomMap(ServerRoomId{812345});
    FrontendMapIdentity identity;
    identity.loaded(map, QStringLiteral("/home/someone/maps/arda.mm2"));
    // The file's name, not where it is.
    QCOMPARE(identity.get().name, QStringLiteral("arda.mm2"));
    QCOMPARE(identity.get().rooms, static_cast<qint64>(1));
    QCOMPARE(identity.get().generation, static_cast<qint64>(0));

    // MapData reports changes that changed nothing, and those do not count: neither the same
    // map again nor a new one with the same rooms in it.
    QVERIFY(!identity.changed(map));
    QVERIFY(!identity.changed(makeOneRoomMap(ServerRoomId{812345})));
    QCOMPARE(identity.get().generation, static_cast<qint64>(0));

    // A room added counts, and so does taking it away again.
    ProgressCounter pc;
    const Change addRoom{room_change_types::AddPermanentRoom{Coordinate{13, -34, 2}}};
    const Map bigger = map.applySingleChange(pc, addRoom).map;
    QVERIFY(identity.changed(bigger));
    QCOMPARE(identity.get().rooms, static_cast<qint64>(2));
    QCOMPARE(identity.get().generation, static_cast<qint64>(1));
    QVERIFY(identity.changed(map));
    QCOMPARE(identity.get().rooms, static_cast<qint64>(1));
    QCOMPARE(identity.get().generation, static_cast<qint64>(2));

    // A save under another name renames the map, and is not a change to it.
    QVERIFY(!identity.renamed(QStringLiteral("/home/someone/maps/arda.mm2")));
    QVERIFY(identity.renamed(QStringLiteral("/tmp/arda-copy.mm2")));
    QCOMPARE(identity.get().name, QStringLiteral("arda-copy.mm2"));
    QCOMPARE(identity.get().generation, static_cast<qint64>(2));

    // Loading starts the count again; a new map has no file, so no name.
    identity.loaded(bigger, QString{});
    QCOMPARE(identity.get().name, QString{});
    QCOMPARE(identity.get().rooms, static_cast<qint64>(2));
    QCOMPARE(identity.get().generation, static_cast<qint64>(0));
}

void TestFrontend::mapIdentityPacingTest()
{
    QCOMPARE(FrontendMapIdentity::ANNOUNCE_INTERVAL_MS, static_cast<int64_t>(1000));

    FrontendMapIdentity identity;
    // Nothing announced yet: at once.
    QCOMPARE(identity.announceDelayMs(5000), static_cast<int64_t>(0));
    identity.announced(5000);
    // A change within the second waits for the rest of it...
    QCOMPARE(identity.announceDelayMs(5000), static_cast<int64_t>(1000));
    QCOMPARE(identity.announceDelayMs(5300), static_cast<int64_t>(700));
    // ...and one after it goes at once.
    QCOMPARE(identity.announceDelayMs(6000), static_cast<int64_t>(0));
    QCOMPARE(identity.announceDelayMs(9000), static_cast<int64_t>(0));
    identity.announced(9000);
    QCOMPARE(identity.announceDelayMs(9999), static_cast<int64_t>(1));
}

void TestFrontend::renderPauseTest()
{
    // Nothing attached: MMapper draws its map, whatever the setting says.
    FrontendRenderPause pause;
    QVERIFY(pause.isEnabled());
    QVERIFY(!pause.isPaused());
    QVERIFY(!pause.setClientCount(0));

    // The first frontend to attach pauses, a second changes nothing, and only the last one
    // to leave resumes. The setters report exactly the changes of isPaused().
    QVERIFY(pause.setClientCount(1));
    QVERIFY(pause.isPaused());
    QVERIFY(!pause.setClientCount(2));
    QVERIFY(pause.isPaused());
    QVERIFY(!pause.setClientCount(1));
    QVERIFY(pause.isPaused());
    QVERIFY(pause.setClientCount(0));
    QVERIFY(!pause.isPaused());

    // Turned off in the preferences: frontends come and go and the map is drawn throughout.
    QVERIFY(!pause.setEnabled(false));
    QVERIFY(!pause.setClientCount(1));
    QVERIFY(!pause.isPaused());
    QCOMPARE(pause.clientCount(), size_t{1});

    // Turned on (and off again) while a frontend is attached takes effect at once.
    QVERIFY(pause.setEnabled(true));
    QVERIFY(pause.isPaused());
    QVERIFY(!pause.setEnabled(true));
    QVERIFY(pause.setEnabled(false));
    QVERIFY(!pause.isPaused());
    QVERIFY(!pause.setClientCount(0));
    QVERIFY(!pause.setEnabled(true));
    QVERIFY(!pause.isPaused());
}

void TestFrontend::xmlElementTest()
{
    XmlElement character;
    character.tag = XmlTagEnum::CHARACTER;
    character.name = "character";
    character.text = QStringLiteral("A dirty uruk");

    XmlElement hit;
    hit.tag = XmlTagEnum::HIT;
    hit.name = "hit";
    hit.text = QStringLiteral("A dirty uruk barely slashes your body.");
    hit.attributes.emplace_back("dir", "north");
    hit.children.emplace_back(character);

    const GmcpMessage msg = frontend_messages::makeXmlElement(hit);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Xml.Element"));

    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["tag"].toString(), QStringLiteral("hit"));
    // The category is what lets a client take an interest in combat without listing tags.
    QCOMPARE(obj["category"].toString(), QStringLiteral("combat"));
    QCOMPARE(obj["text"].toString(), QStringLiteral("A dirty uruk barely slashes your body."));
    QCOMPARE(obj["attributes"].toObject()["dir"].toString(), QStringLiteral("north"));

    const QJsonArray children = obj["children"].toArray();
    QCOMPARE(children.size(), 1);
    QCOMPARE(children[0].toObject()["tag"].toString(), QStringLiteral("character"));
    QCOMPARE(children[0].toObject()["category"].toString(), QStringLiteral("entity"));

    // Quiet on the wire in the ordinary case.
    QVERIFY(!obj.contains("truncated"));
    // A blow has no direction, whatever its attributes say.
    QVERIFY(!obj.contains("direction"));

    // Someone arriving: MMapper's reading of the line travels beside MUME's own attributes,
    // never inside them.
    XmlElement scholar;
    scholar.tag = XmlTagEnum::CHARACTER;
    scholar.name = "character";
    scholar.text = QStringLiteral("A scholar");
    XmlElement arrival;
    arrival.tag = XmlTagEnum::MOVE_IN;
    arrival.name = "move_in";
    arrival.text = QStringLiteral("A scholar has arrived from the south.");
    arrival.children.emplace_back(scholar);
    arrival.direction = deriveMovementDirection(arrival);
    const QJsonObject arrivalObj = payloadOf(frontend_messages::makeXmlElement(arrival));
    QCOMPARE(arrivalObj["category"].toString(), QStringLiteral("movement"));
    QCOMPARE(arrivalObj["direction"].toString(), QStringLiteral("south"));
    QVERIFY(!arrivalObj.contains("attributes"));
    QVERIFY(!arrivalObj["children"].toArray()[0].toObject().contains("direction"));

    // A tag this build does not know is still identified by the name MUME used.
    XmlElement unknown;
    unknown.tag = XmlTagEnum::UNKNOWN;
    unknown.name = "somethingnew";
    unknown.truncated = true;
    const QJsonObject unknownObj = payloadOf(frontend_messages::makeXmlElement(unknown));
    QCOMPARE(unknownObj["tag"].toString(), QStringLiteral("somethingnew"));
    QCOMPARE(unknownObj["category"].toString(), QStringLiteral("unknown"));
    QVERIFY(unknownObj["truncated"].toBool());
    QVERIFY(!unknownObj.contains("children"));
}

void TestFrontend::combatEventTest()
{
    const std::optional<CombatEvent> blow = parseCombatLine(
        QStringLiteral("You pound the one-eyed orc's left arm extremely hard and shatter it."));
    QVERIFY(blow.has_value());
    const GmcpMessage msg = frontend_messages::makeCombatEvent(*blow);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Combat.Event"));
    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["kind"].toString(), QStringLiteral("blow"));
    QCOMPARE(obj["outcome"].toString(), QStringLiteral("hit"));
    QCOMPARE(obj["actor"].toString(), QStringLiteral("you"));
    QCOMPARE(obj["target"].toString(), QStringLiteral("the one-eyed orc"));
    QCOMPARE(obj["part"].toString(), QStringLiteral("left arm"));
    QCOMPARE(obj["severity"].toString(), QStringLiteral("extremely hard"));
    QCOMPARE(obj["effect"].toString(), QStringLiteral("shatter"));
    // A field the line said nothing for is left out rather than sent empty.
    QVERIFY(!obj.contains("quality"));
    QVERIFY(!obj.contains("phase"));

    const std::optional<CombatEvent> flee = parseCombatLine(
        QStringLiteral("PANIC! You couldn't escape!"));
    QVERIFY(flee.has_value());
    const QJsonObject fled = payloadOf(frontend_messages::makeCombatEvent(*flee));
    QCOMPARE(fled["kind"].toString(), QStringLiteral("flee"));
    QCOMPARE(fled["phase"].toString(), QStringLiteral("failed"));
    QVERIFY(!fled.contains("outcome"));

    // A refused move says why, and names what was in the way when there was something.
    const std::optional<CombatEvent> door = parseCombatLine(
        QStringLiteral("The door seems to be closed."));
    QVERIFY(door.has_value());
    QCOMPARE(frontend_messages::makeCombatEvent(*door).toRawBytes(),
             QByteArray(R"(MMapper.Combat.Event {"actor":"you","detail":"door-closed",)"
                        R"("kind":"refused","target":"door",)"
                        R"("text":"The door seems to be closed."})"));
    const std::optional<CombatEvent> engaged = parseCombatLine(
        QStringLiteral("No way! You are fighting for your life!"));
    QVERIFY(engaged.has_value());
    const QJsonObject engagedObj = payloadOf(frontend_messages::makeCombatEvent(*engaged));
    QCOMPARE(engagedObj["detail"].toString(), QStringLiteral("fighting"));
    QVERIFY(!engagedObj.contains("target"));

    // New phases go out as words, like the old ones.
    const std::optional<CombatEvent> dodged = parseCombatLine(
        QStringLiteral("You dodge a bash from an ugly forest troll who loses his balance."));
    QVERIFY(dodged.has_value());
    const QJsonObject dodgedObj = payloadOf(frontend_messages::makeCombatEvent(*dodged));
    QCOMPARE(dodgedObj["kind"].toString(), QStringLiteral("bash"));
    QCOMPARE(dodgedObj["phase"].toString(), QStringLiteral("dodged"));
    QCOMPARE(dodgedObj["actor"].toString(), QStringLiteral("an ugly forest troll"));
    QCOMPARE(dodgedObj["target"].toString(), QStringLiteral("you"));

    // The player's own spell going off has no line of its own: an empty text, no actor words.
    OwnCastTracker tracker;
    tracker.receiveEvent(*parseCombatLine(QStringLiteral("You start to concentrate...")));
    const std::optional<CombatEvent> done = tracker.receivePrompt();
    QVERIFY(done.has_value());
    QCOMPARE(frontend_messages::makeCombatEvent(*done).toRawBytes(),
             QByteArray(
                 R"(MMapper.Combat.Event {"actor":"you","kind":"cast","phase":"done","text":""})"));
}

/// Every module MUME's own "help gmcp" page lists. If MUME adds one, the omission should
/// show up here rather than as a feed that silently never arrives.
void TestFrontend::mumeModuleCoverageTest()
{
    static const char *const documented[] = {"Char",
                                             "Client",
                                             "Comm.Channel",
                                             "Event",
                                             "External.Discord",
                                             "Group",
                                             "MUME.Client",
                                             "Room",
                                             "Room.Chars",
                                             "Room.Known"};

    for (const char *const name : documented) {
        const GmcpModule mod{std::string{name}};
        QVERIFY2(mod.isSupported(), name);
    }

    // Core is the one documented module a frontend never receives, and that is deliberate
    // rather than an oversight: MMapper terminates Core itself. It answers a frontend's
    // Core.Hello and Core.Supports on its own behalf and builds its own upstream supports
    // set, and it reports the connection through MMapper.Session.State; relayFilterTest
    // shows that a subscription to it gets nothing. The individual Core message types are
    // still recognised; see mumeMessageCoverageTest.
    const GmcpModule core{std::string{"Core"}};
    QVERIFY(!core.isSupported());

    // A module MUME does not define is not one MMapper knows. A frontend may still subscribe
    // to it: the subscription is accepted and simply never matches, since MUME only sends
    // what MMapper asked it for.
    const GmcpModule nonsense{std::string{"Nonsense"}};
    QVERIFY(!nonsense.isSupported());
}

void TestFrontend::timeStateTest()
{
    // 3:12pm on the 18th of Halimath, year 3030: summer, and in Halimath dawn is at 5am and
    // dusk at 9pm (MUME's `help calendars`). MumeMoment counts month and day from zero.
    const MumeMoment moment{3030, 8, 17, 15, 12};
    const GmcpMessage msg = frontend_messages::makeTimeState(moment, MumeClockPrecisionEnum::MINUTE);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Time.State"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_TIME_STATE);

    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["year"].toInt(), 3030);
    QCOMPARE(obj["month"].toInt(), 9);
    QCOMPARE(obj["monthName"].toString(), QStringLiteral("Halimath"));
    QCOMPARE(obj["day"].toInt(), 18);
    QCOMPARE(obj["hour"].toInt(), 15);
    QCOMPARE(obj["minute"].toInt(), 12);
    QCOMPARE(obj["precision"].toString(), QStringLiteral("minute"));
    QCOMPARE(obj["secondsPerHour"].toInt(), 60);
    QCOMPARE(obj["season"].toString(), QStringLiteral("summer"));
    QCOMPARE(obj["dawnHour"].toInt(), 5);
    QCOMPARE(obj["duskHour"].toInt(), 21);
    QCOMPARE(obj["phase"].toString(), QStringLiteral("day"));
    QVERIFY(!obj["weekday"].toString().isEmpty());

    const QJsonObject moon = obj["moon"].toObject();
    QVERIFY(!moon["phase"].toString().isEmpty());
    QVERIFY(moon["level"].toInt() >= 0 && moon["level"].toInt() <= 12);
    QVERIFY(moon["waxing"].isBool());
    QVERIFY(!moon["position"].toString().isEmpty());
    QVERIFY(!moon["visibility"].toString().isEmpty());
    // The moon is highest at a minute of the day, which lets a renderer move it smoothly.
    QVERIFY(moon["zenithMinute"].isDouble());
    QVERIFY(moon["zenithMinute"].toInt() >= 0 && moon["zenithMinute"].toInt() < 24 * 60);

    // An unsynchronised clock still publishes its guess, and says that it is one.
    const QJsonObject guess = payloadOf(
        frontend_messages::makeTimeState(moment, MumeClockPrecisionEnum::UNSET));
    QCOMPARE(guess["precision"].toString(), QStringLiteral("unset"));

    // The dawn and dusk hours give the phases their names.
    const QJsonObject dusk = payloadOf(
        frontend_messages::makeTimeState(MumeMoment{3030, 8, 17, 21, 30},
                                         MumeClockPrecisionEnum::HOUR));
    QCOMPARE(dusk["phase"].toString(), QStringLiteral("dusk"));
    const QJsonObject winterNight = payloadOf(
        frontend_messages::makeTimeState(MumeMoment{3030, 0, 0, 3, 0},
                                         MumeClockPrecisionEnum::HOUR));
    QCOMPARE(winterNight["phase"].toString(), QStringLiteral("night"));
    QCOMPARE(winterNight["season"].toString(), QStringLiteral("winter"));
    QCOMPARE(winterNight["month"].toInt(), 1);
    QCOMPARE(winterNight["day"].toInt(), 1);

    // It belongs to a module of its own, so a frontend that wants no clock is not sent one.
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Time 1" ])")));
    QVERIFY(subs.wants(msg));
    FrontendSubscriptions other;
    QVERIFY(other.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Map 1" ])")));
    QVERIFY(!other.wants(msg));
}

void TestFrontend::weatherEventTest()
{
    const GmcpMessage msg = frontend_messages::makeWeatherEvent(
        parseWeatherLine(QStringLiteral("You see some fog coming from the north.")));
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Weather.Event"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_WEATHER_EVENT);
    const QJsonObject fog = payloadOf(msg);
    QCOMPARE(fog["kind"].toString(), QStringLiteral("fog"));
    QCOMPARE(fog["level"].toString(), QStringLiteral("light"));
    QCOMPARE(fog["changing"].toBool(), true);
    QCOMPARE(fog["direction"].toString(), QStringLiteral("north"));
    QCOMPARE(fog["text"].toString(), QStringLiteral("You see some fog coming from the north."));
    // Quiet on the wire: only what the line said.
    QVERIFY(!fog.contains("precipitation"));
    QVERIFY(!fog.contains("magic"));

    const QJsonObject storm = payloadOf(frontend_messages::makeWeatherEvent(parseWeatherLine(
        QStringLiteral("The weather suddenly becomes extremely stormy. How very strange!"))));
    QCOMPARE(storm["kind"].toString(), QStringLiteral("storm"));
    QCOMPARE(storm["magic"].toBool(), true);
    QVERIFY(!storm.contains("level"));
    QVERIFY(!storm.contains("changing"));

    const QJsonObject flash = payloadOf(frontend_messages::makeWeatherEvent(parseWeatherLine(
        QStringLiteral("A flare of lightning branches out into several small streaks above the "
                       "mountains."))));
    QCOMPARE(flash["kind"].toString(), QStringLiteral("lightning"));
    QVERIFY(!flash.contains("level"));
    QVERIFY(!flash.contains("direction"));

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Weather 1" ])")));
    QVERIFY(subs.wants(msg));
    FrontendSubscriptions other;
    QVERIFY(other.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Time 1" ])")));
    QVERIFY(!other.wants(msg));
}

void TestFrontend::groundStateTest()
{
    GroundState ground;
    ground.snow = "lot";
    ground.frost = "very";
    ground.entered = true;

    const Map map = makeOneRoomMap(ServerRoomId{4321});
    const RoomHandle room = map.findRoomHandle(ExternalRoomId{1});
    QVERIFY(room.exists());

    const GmcpMessage msg = frontend_messages::makeGroundState(ground, &room);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Weather.Ground"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_WEATHER_GROUND);
    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["snow"].toString(), QStringLiteral("lot"));
    QCOMPARE(obj["frost"].toString(), QStringLiteral("very"));
    QCOMPARE(obj["ice"].toString(), QStringLiteral("none"));
    QCOMPARE(obj["entered"].toBool(), true);
    // The same identities MMapper.Map.Position gives the room, so the two can be matched.
    const QJsonObject where = obj["room"].toObject();
    QCOMPARE(where["externalId"].toInt(), 1);
    QCOMPARE(where["serverId"].toInt(), 4321);

    // Without a current room there is nothing to name.
    const QJsonObject nowhere = payloadOf(
        frontend_messages::makeGroundState(GroundState{}, nullptr));
    QVERIFY(!nowhere.contains("room"));
    QCOMPARE(nowhere["snow"].toString(), QStringLiteral("none"));
    QCOMPARE(nowhere["entered"].toBool(), false);

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Weather 1" ])")));
    QVERIFY(subs.wants(msg));
}

void TestFrontend::roomContentsTest()
{
    RoomContentsSnapshot contents;
    contents.roomKey = QStringLiteral("4321");
    RoomObject chest;
    chest.index = 0;
    chest.line = QStringLiteral("A wooden chest stands in the corner.");
    chest.keyword = QStringLiteral("chest");
    chest.target = QStringLiteral("chest");
    chest.container = true;
    chest.state.locked = true;
    chest.state.open = false;
    chest.state.known = 1790000000;
    contents.objects.push_back(chest);
    RoomObject torch;
    torch.index = 1;
    torch.line = QStringLiteral("A large torch lies here among the dust.");
    contents.objects.push_back(torch);

    const Map map = makeOneRoomMap(ServerRoomId{4321});
    const RoomHandle room = map.findRoomHandle(ExternalRoomId{1});
    QVERIFY(room.exists());

    const GmcpMessage msg = frontend_messages::makeRoomContents(contents, &room);
    // The whole frame, as the spec shows it and mume3d's tests/room_contents_smoke.gd feeds it.
    QCOMPARE(msg.toRawBytes(),
             QByteArray(
                 R"(MMapper.Room.Contents {"entered":true,"externalId":1,"objects":[)"
                 R"({"container":true,"count":1,"index":0,"keyword":"chest",)"
                 R"("line":"A wooden chest stands in the corner.","name":null,)"
                 R"("state":{"empty":null,"known":1790000000,"locked":true,"open":false,)"
                 R"("pickproof":null},"target":"chest"},)"
                 R"({"container":false,"count":1,"index":1,"keyword":null,)"
                 R"("line":"A large torch lies here among the dust.","name":null,)"
                 R"("state":{"empty":null,"known":0,"locked":null,"open":null,"pickproof":null},)"
                 R"("target":null}],"seen":true,"serverId":4321})"));
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Room.Contents"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_ROOM_CONTENTS);
    const QJsonObject obj = payloadOf(msg);
    // The same identities MMapper.Map.Position gives the room, at the top level.
    QCOMPARE(obj["serverId"].toInt(), 4321);
    QCOMPARE(obj["externalId"].toInt(), 1);
    QCOMPARE(obj["seen"].toBool(), true);
    QCOMPARE(obj["entered"].toBool(), true);
    const QJsonArray objects = obj["objects"].toArray();
    QCOMPARE(objects.size(), 2);

    const QJsonObject first = objects.at(0).toObject();
    QCOMPARE(first["index"].toInt(), 0);
    QCOMPARE(first["line"].toString(), QStringLiteral("A wooden chest stands in the corner."));
    QVERIFY(first["name"].isNull());
    QCOMPARE(first["count"].toInt(), 1);
    QCOMPARE(first["container"].toBool(), true);
    QCOMPARE(first["keyword"].toString(), QStringLiteral("chest"));
    QCOMPARE(first["target"].toString(), QStringLiteral("chest"));
    const QJsonObject state = first["state"].toObject();
    QCOMPARE(state["open"].toBool(true), false);
    QCOMPARE(state["locked"].toBool(), true);
    // Unknown is null, not false.
    QVERIFY(state["pickproof"].isNull());
    QVERIFY(state["empty"].isNull());
    QCOMPARE(state["known"].toInteger(), static_cast<qint64>(1790000000));

    const QJsonObject second = objects.at(1).toObject();
    QCOMPARE(second["container"].toBool(), false);
    QVERIFY(second["keyword"].isNull());
    QVERIFY(second["target"].isNull());
    QCOMPARE(second["state"].toObject()["known"].toInteger(), static_cast<qint64>(0));

    // Without a current room there is nothing to name.
    const QJsonObject nowhere = payloadOf(frontend_messages::makeRoomContents(contents, nullptr));
    QVERIFY(!nowhere.contains("serverId"));
    QVERIFY(!nowhere.contains("externalId"));

    // Its own module, not MUME's Room.
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Room 1" ])")));
    QVERIFY(!subs.wants(msg));
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Room 1" ])")));
    QVERIFY(subs.wants(msg));
}

void TestFrontend::containerEventTest()
{
    // A room with a chest, the command, and MUME's replies as the logs have them.
    RoomContentsSnapshot contents;
    contents.roomKey = QStringLiteral("4321");
    RoomObject chest;
    chest.line = QStringLiteral("A wooden chest stands in the corner.");
    chest.keyword = QStringLiteral("chest");
    chest.target = QStringLiteral("chest");
    chest.container = true;
    contents.objects.push_back(chest);
    ContainerTracker tracker;
    tracker.decorate(contents);

    tracker.receiveCommand(QStringLiteral("exami chest"));
    std::ignore = tracker.receiveLine(QStringLiteral("chest (here) : "), 1790000000);
    std::ignore = tracker.receiveLine(QStringLiteral("a gold ring"), 1790000000);
    std::ignore = tracker.receiveLine(QStringLiteral("three azure scrolls"), 1790000000);
    const std::vector<ContainerEvent> events = tracker.receivePrompt(1790000000);
    QCOMPARE(events.size(), size_t{1});

    const GmcpMessage msg = frontend_messages::makeContainerEvent(events.front());
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Room.Container {"action":"look","index":0,"items":[)"
                        R"({"count":1,"name":"a gold ring","text":"a gold ring"},)"
                        R"({"count":3,"name":"azure scrolls","text":"three azure scrolls"}],)"
                        R"("result":"contents","target":"chest",)"
                        R"("text":"chest (here) :\na gold ring\nthree azure scrolls"})"));
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Room.Container"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_ROOM_CONTAINER);
    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["target"].toString(), QStringLiteral("chest"));
    QCOMPARE(obj["action"].toString(), QStringLiteral("look"));
    QCOMPARE(obj["result"].toString(), QStringLiteral("contents"));
    QCOMPARE(obj["index"].toInt(), 0);
    QCOMPARE(obj["text"].toString(),
             QStringLiteral("chest (here) :\na gold ring\nthree azure scrolls"));
    const QJsonArray items = obj["items"].toArray();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(1).toObject()["name"].toString(), QStringLiteral("azure scrolls"));
    QCOMPARE(items.at(1).toObject()["count"].toInt(), 3);
    QCOMPARE(items.at(1).toObject()["text"].toString(), QStringLiteral("three azure scrolls"));

    // A reply with nothing inside says so without an empty list, and one that did not reach an
    // object in the room has no index.
    tracker.receiveCommand(QStringLiteral("open cabinet"));
    const std::vector<ContainerEvent> missing
        = tracker.receiveLine(QStringLiteral("You don't see any cabinet here."), 1790000000);
    QCOMPARE(missing.size(), size_t{1});
    const QJsonObject notFound = payloadOf(frontend_messages::makeContainerEvent(missing.front()));
    QCOMPARE(notFound["result"].toString(), QStringLiteral("not-found"));
    QVERIFY(!notFound.contains("items"));
    QVERIFY(!notFound.contains("index"));

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Room 1" ])")));
    QVERIFY(subs.wants(msg));
}

void TestFrontend::charEquipmentTest()
{
    // The player's own "equipment", lines as the logs have them (azazello.txt).
    const auto blocks = listed(
        {QStringLiteral("You are using:"),
         QStringLiteral("<wielded>            an engraved broadsword (flawless); it glows blue"),
         QStringLiteral(
             "<worn around neck>   a red ruby; it glows blue; it has a soft glowing aura"),
         QStringLiteral("<worn on belt>       a sable pouch"),
         QStringLiteral("")});
    QCOMPARE(blocks.size(), size_t{1});
    const GmcpMessage msg = frontend_messages::makeCharEquipment(blocks.front());
    // The whole frame, as the spec shows it and mume3d's tests/char_items_smoke.gd feeds it.
    QCOMPARE(msg.toRawBytes(),
             QByteArray(
                 R"(MMapper.Char.Equipment {"items":[)"
                 R"({"condition":"flawless","count":1,"flags":["it glows blue"],)"
                 R"("label":"wielded","name":"an engraved broadsword","slot":"wielded",)"
                 R"("text":"<wielded> an engraved broadsword (flawless); it glows blue"},)"
                 R"({"condition":null,"count":1,)"
                 R"("flags":["it glows blue","it has a soft glowing aura"],)"
                 R"("label":"worn around neck","name":"a red ruby","slot":"neck",)"
                 R"("text":"<worn around neck> a red ruby; it glows blue; it has a soft glowing aura"},)"
                 R"({"condition":null,"count":1,"flags":[],"label":"worn on belt",)"
                 R"("name":"a sable pouch","slot":"belt","text":"<worn on belt> a sable pouch"}],)"
                 R"("owner":"you"})"));
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Char.Equipment"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_EQUIPMENT);

    // Someone looked at (stolb.balrog.txt), with a weapon in both hands.
    const auto other = listed(
        {QStringLiteral("Uldor the Damned is using: "),
         QStringLiteral("<wielded two-handed> a great warsword (flawless); it glows blue"),
         QStringLiteral("<worn on forearm>    a metal buckler (flawless)")});
    QCOMPARE(other.size(), size_t{1});
    const QJsonObject obj = payloadOf(frontend_messages::makeCharEquipment(other.front()));
    QCOMPARE(obj["owner"].toString(), QStringLiteral("Uldor the Damned"));
    const QJsonArray items = obj["items"].toArray();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toObject()["slot"].toString(), QStringLiteral("wielded"));
    QCOMPARE(items.at(0).toObject()["label"].toString(), QStringLiteral("wielded two-handed"));
    QCOMPARE(items.at(0).toObject()["twoHanded"].toBool(), true);
    QCOMPARE(items.at(1).toObject()["slot"].toString(), QStringLiteral("shield"));
    QVERIFY(!items.at(1).toObject().contains("twoHanded"));

    // Its own module, not MUME's Char.
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "Char 1" ])")));
    QVERIFY(!subs.wants(msg));
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(msg));
}

void TestFrontend::charInventoryTest()
{
    const auto blocks = listed({QStringLiteral("You are carrying:"),
                                QStringLiteral("a sturdy chain mail hauberk (flawless)"),
                                QStringLiteral("a lembas wafer")});
    QCOMPARE(blocks.size(), size_t{1});
    const GmcpMessage msg = frontend_messages::makeCharInventory(blocks.front());
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Char.Inventory {"items":[)"
                        R"({"condition":"flawless","count":1,"flags":[],)"
                        R"("name":"a sturdy chain mail hauberk",)"
                        R"j("text":"a sturdy chain mail hauberk (flawless)"},)j"
                        R"({"condition":null,"count":1,"flags":[],"name":"a lembas wafer",)"
                        R"("text":"a lembas wafer"}],"owner":"you","peek":false})"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_INVENTORY);

    // A peek at someone whose name the reply did not give (stonedoor.txt:302).
    const auto peek = listed({QStringLiteral("You attempt to peek at the inventory:"),
                              QStringLiteral("a wooden pipe"),
                              QStringLiteral("")});
    QCOMPARE(peek.size(), size_t{1});
    const QJsonObject obj = payloadOf(frontend_messages::makeCharInventory(peek.front()));
    QCOMPARE(obj["peek"].toBool(), true);
    QVERIFY(obj.contains("owner"));
    QVERIFY(obj["owner"].isNull());
    QCOMPARE(obj["items"].toArray().size(), 1);

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(msg));
}

void TestFrontend::charContainerTest()
{
    // azazello.txt:1133, "l in backpack".
    const auto blocks = listed({QStringLiteral("backpack (used) : "),
                                QStringLiteral("two azure scrolls"),
                                QStringLiteral("a black pair of padded boots (satisfactory)"),
                                QStringLiteral("")});
    QCOMPARE(blocks.size(), size_t{1});
    const GmcpMessage msg = frontend_messages::makeCharContainer(blocks.front());
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Char.Container {"closed":false,"contentsKnown":true,"items":[)"
                        R"({"condition":null,"count":2,"flags":[],"name":"azure scrolls",)"
                        R"("text":"two azure scrolls"},)"
                        R"({"condition":"satisfactory","count":1,"flags":[],)"
                        R"("name":"a black pair of padded boots",)"
                        R"j("text":"a black pair of padded boots (satisfactory)"}],)j"
                        R"("keyword":"backpack","listingMode":"unknown","where":"used"})"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_CONTAINER);

    // A closed one, never listed: no items, and no place.
    const auto closed = listed({QStringLiteral("A large cabinet is closed.")},
                               QStringLiteral("look in cabinet"));
    QCOMPARE(closed.size(), size_t{1});
    QCOMPARE(
        frontend_messages::makeCharContainer(closed.front()).toRawBytes(),
        QByteArray(
            R"(MMapper.Char.Container {"closed":true,"contentsKnown":false,"items":[],"keyword":"cabinet","listingMode":"grouped","target":"cabinet","where":null})"));
}

void TestFrontend::charItemTest()
{
    const auto frame = [](const char *const line) {
        const auto event = parseItemEvent(QString::fromUtf8(line));
        return event.has_value() ? frontend_messages::makeCharItem(*event).toRawBytes()
                                 : QByteArray{};
    };
    QCOMPARE(frame("You fasten a sable pouch on your belt."),
             QByteArray(R"(MMapper.Char.Item {"action":"wear","item":"a sable pouch",)"
                        R"("place":"belt","slot":"belt",)"
                        R"("text":"You fasten a sable pouch on your belt."})"));
    QCOMPARE(frame("You get a flask of orkish draught from a leather backpack."),
             QByteArray(R"(MMapper.Char.Item {"action":"get","container":"a leather backpack",)"
                        R"("item":"a flask of orkish draught",)"
                        R"("text":"You get a flask of orkish draught from a leather backpack."})"));
    QCOMPARE(frame("Stolb (S) gives you a red ruby."),
             QByteArray(R"(MMapper.Char.Item {"action":"receive","item":"a red ruby",)"
                        R"("other":"Stolb","text":"Stolb (S) gives you a red ruby."})"));
    QCOMPARE(frame("You are already wearing something on your legs."),
             QByteArray(R"(MMapper.Char.Item {"action":"refused","place":"legs",)"
                        R"("reason":"slot-taken","slot":"legs",)"
                        R"("text":"You are already wearing something on your legs."})"));

    const auto event = parseItemEvent(QStringLiteral("You drop the key."));
    QVERIFY(event.has_value());
    const GmcpMessage msg = frontend_messages::makeCharItem(*event);
    QCOMPARE(msg.getName().toQString(), QStringLiteral("MMapper.Char.Item"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_ITEM);
    const QJsonObject obj = payloadOf(msg);
    // Only what the line says is sent.
    QCOMPARE(obj.keys(),
             (QStringList{QStringLiteral("action"), QStringLiteral("item"), QStringLiteral("text")}));

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(msg));
}

/// MMapper.Char.Stat, .Score and .Burden: each is built from a reply CharLinesTracker read,
/// with numbers as numbers and what the reply did not state left out, and each is replayed to a
/// frontend that connects later.
void TestFrontend::charSheetTest()
{
    CharLinesTracker tracker;
    CharReplies replies;
    // Real replies from the powwow logs: stolb.balrog.txt's `stat` and `score`, and the burden
    // and figures of log-2006.03.06-01.08.16.txt's `info`.
    for (const char *const line :
         {"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.",
          "Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal.",
          "",
          "Your equipment weighs one hundred fourteen pounds. Heavy, but we will manage...",
          "Your base abilities are: Str:19 Int:18 Wis:16 Dex:13 Con:14 Wil:14 Per:12.",
          "Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.",
          "Your armour provides an average protection of 77%.",
          "You have 318/318 hit, 81/117 mana, and 135/135 movement points.",
          "You have scored 49,186,097 experience points and you have 301,832 travel points.",
          "You have 262 gold coins, 14 silver pennies, and 59 copper pennies.",
          "",
          "You are subjected to the following temporary effects:",
          "- shield",
          "- a light wound at the head (clean)",
          ""}) {
        replies.append(tracker.receiveLine(QString::fromUtf8(line)));
    }
    replies.append(tracker.receivePrompt());
    QCOMPARE(replies.stats.size(), size_t{1});
    QCOMPARE(replies.scores.size(), size_t{1});
    QCOMPARE(replies.burdens.size(), size_t{1});

    const GmcpMessage stat = frontend_messages::makeCharStat(replies.stats.front());
    QCOMPARE(stat.getName().toQString(), QStringLiteral("MMapper.Char.Stat"));
    QCOMPARE(stat.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_STAT);
    const QJsonObject s = payloadOf(stat);
    QVERIFY(s["ob"].isDouble());
    QCOMPARE(s["ob"].toInteger(), 131);
    QCOMPARE(s["db"].toInteger(), 24);
    QCOMPARE(s["armour"].toInteger(), 0);
    QCOMPARE(s["wimpy"].toInteger(), 111);
    QCOMPARE(s["mood"].toString(), QStringLiteral("wimpy"));
    QCOMPARE(s["alert"].toString(), QStringLiteral("normal"));
    QCOMPARE(s["neededXp"].toInteger(), 1108995);
    QCOMPARE(s["neededTp"].toInteger(), 0);
    QCOMPARE(s["gold"].toInteger(), 0);
    QVERIFY(s["affects"].isArray());
    QVERIFY(s["affects"].toArray().isEmpty());
    QVERIFY(s["wounds"].toArray().isEmpty());
    QVERIFY(!s.contains("wp"));
    QVERIFY(!s.contains("condition"));

    const GmcpMessage info = frontend_messages::makeCharScore(replies.scores.front());
    QCOMPARE(info.getName().toQString(), QStringLiteral("MMapper.Char.Score"));
    const QJsonObject sc = payloadOf(info);
    QCOMPARE(sc["reply"].toString(), QStringLiteral("info"));
    QCOMPARE(sc["abilities"].toObject()["str"].toInteger(), 19);
    QCOMPARE(sc["abilities"].toObject()["per"].toInteger(), 12);
    QCOMPARE(sc["xp"].toInteger(), 49186097);
    QCOMPARE(sc["maxmana"].toInteger(), 117);
    QCOMPARE(sc["silver"].toInteger(), 14);
    QCOMPARE(sc["effects"].toArray(), QJsonArray{QStringLiteral("shield")});
    QCOMPARE(sc["wounds"].toArray(),
             QJsonArray{QStringLiteral("a light wound at the head (clean)")});
    // Not stated by this reply, so not sent.
    QVERIFY(!sc.contains("mood"));
    QVERIFY(!sc.contains("wimpy"));
    QVERIFY(!sc.contains("language"));
    QVERIFY(!sc.contains("neededXp"));

    const GmcpMessage burden = frontend_messages::makeCharBurden(replies.burdens.front());
    QCOMPARE(burden.toRawBytes(),
             QByteArray(R"(MMapper.Char.Burden {"pounds":114,"text":"Your equipment weighs one )"
                        R"(hundred fourteen pounds. Heavy, but we will manage...",)"
                        R"("word":"Heavy, but we will manage..."})"));

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(stat));
    QVERIFY(subs.wants(info));
    QVERIFY(subs.wants(burden));

    // Replayed: Stat and Burden as last sent; Score merged, so that a one-line `score` after
    // the sheet updates the pools without dropping the sheet's other figures.
    FrontendReplayCache cache;
    cache.remember(stat);
    cache.remember(info);
    cache.remember(burden);
    const std::optional<CharScore> pools = parseScoreLine(
        QStringLiteral("523/523 hits, 53/53 mana, and 155/155 moves."));
    QVERIFY(pools.has_value());
    const GmcpMessage brief = frontend_messages::makeCharScore(*pools);
    QVERIFY(!payloadOf(brief).contains("effects"));
    cache.remember(brief);
    QCOMPARE(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_STAT).object()["ob"].toInteger(),
             131);
    QCOMPARE(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_BURDEN).object()["pounds"].toInteger(),
             114);
    const QJsonObject merged = replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_SCORE).object();
    QCOMPARE(merged["hp"].toInteger(), 523);
    QCOMPARE(merged["maxmana"].toInteger(), 53);
    QCOMPARE(merged["xp"].toInteger(), 49186097);
    QCOMPARE(merged["abilities"].toObject()["str"].toInteger(), 19);
    QCOMPARE(merged["effects"].toArray(), QJsonArray{QStringLiteral("shield")});

    // A later `stat` replaces the earlier one whole.
    CharStat later = replies.stats.front();
    later.ob = 140;
    later.affects = QStringList{QStringLiteral("strength")};
    cache.remember(frontend_messages::makeCharStat(later));
    const QJsonObject again = replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_STAT).object();
    QCOMPARE(again["ob"].toInteger(), 140);
    QCOMPARE(again["affects"].toArray(), QJsonArray{QStringLiteral("strength")});

    // The head of the sheet (the live `info` of 2026-10-02): words as MUME's words, figures as
    // numbers, the age and the perception as objects, the places as an array.
    CharLinesTracker headTracker;
    CharReplies head;
    for (const char *const line :
         {"You are a male Eriadorian.",
          "You are 19 years and 6 months old.",
          "You have played 4 hours (real time). Session: 10 mins.",
          "This ranks you as Idwar the Man Adventurer (level 2).",
          "You are five feet nine and weigh eleven stone and eleven pounds.",
          "Perception: vision 40, hearing -31, smell -60. Alertness: normal.",
          "You are a well-meaning person, always glad to help your friends.",
          "You are welcome in Fornost.",
          "You have scored 1,478 experience points and you have 241 travel points.",
          "You are not known for any acts of war."}) {
        head.append(headTracker.receiveLine(QString::fromUtf8(line)));
    }
    head.append(headTracker.receivePrompt());
    QCOMPARE(head.scores.size(), size_t{1});
    const QJsonObject h = payloadOf(frontend_messages::makeCharScore(head.scores.front()));
    QCOMPARE(h["sex"].toString(), QStringLiteral("male"));
    QCOMPARE(h["race"].toString(), QStringLiteral("Eriadorian"));
    QCOMPARE(h["age"].toObject()["years"].toInteger(), 19);
    QCOMPARE(h["age"].toObject()["months"].toInteger(), 6);
    QVERIFY(!h["age"].toObject().contains("days"));
    QCOMPARE(h["played"].toString(), QStringLiteral("4 hours"));
    QCOMPARE(h["session"].toString(), QStringLiteral("10 mins"));
    QCOMPARE(h["name"].toString(), QStringLiteral("Idwar"));
    QCOMPARE(h["title"].toString(), QStringLiteral("the Man Adventurer"));
    QVERIFY(h["level"].isDouble());
    QCOMPARE(h["level"].toInteger(), 2);
    QCOMPARE(h["height"].toString(), QStringLiteral("five feet nine"));
    QCOMPARE(h["weight"].toString(), QStringLiteral("eleven stone and eleven pounds"));
    QCOMPARE(h["perception"].toObject()["vision"].toInteger(), 40);
    QCOMPARE(h["perception"].toObject()["hearing"].toInteger(), -31);
    QCOMPARE(h["perception"].toObject()["smell"].toInteger(), -60);
    QCOMPARE(h["alertness"].toString(), QStringLiteral("normal"));
    QCOMPARE(h["alignment"].toString(),
             QStringLiteral("You are a well-meaning person, always glad to help your friends."));
    QCOMPARE(h["welcome"].toArray(), QJsonArray{QStringLiteral("Fornost")});
    QCOMPARE(h["war"].toString(), QStringLiteral("You are not known for any acts of war."));
    QCOMPARE(h["renown"].toString(), QStringLiteral("You are not known for any acts of war."));
    // The sheet above had no head, and a one-line `score` has none: neither sends its fields.
    for (const char *const key : {"sex", "race", "age", "played", "session", "name", "title",
                                  "level", "height", "weight", "perception", "alertness",
                                  "alignment", "welcome", "war"}) {
        QVERIFY2(!sc.contains(QString::fromUtf8(key)), key);
        QVERIFY2(!payloadOf(brief).contains(QString::fromUtf8(key)), key);
    }

    // A new game session forgets them, as it forgets Char.Vitals.
    cache.clear();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_SCORE).isNull());
}

void TestFrontend::charLevelTest()
{
    const std::optional<CharLevel> level = parseCharLevelLine(
        QStringLiteral("MMXP 56 45,370,716 1,029,284 271,013 0"));
    QVERIFY(level.has_value());
    const GmcpMessage msg = frontend_messages::makeCharLevel(*level);
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_LEVEL);
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Char.Level {"level":56,"neededTp":0,"neededXp":1029284,)"
                        R"("text":"MMXP 56 45,370,716 1,029,284 271,013 0","tp":271013,)"
                        R"("xp":45370716})"));

    // A figure MUME did not print as a number is left out.
    const std::optional<CharLevel> top = parseCharLevelLine(
        QStringLiteral("MMXP 100 158000000 none 288600 none"));
    QVERIFY(top.has_value());
    const QJsonObject t = payloadOf(frontend_messages::makeCharLevel(*top));
    QVERIFY(!t.contains("neededXp"));
    QVERIFY(!t.contains("neededTp"));
    QCOMPARE(t["xp"].toInteger(), 158000000);

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(msg));

    // Replayed as last sent.
    FrontendReplayCache cache;
    cache.remember(msg);
    cache.remember(frontend_messages::makeCharLevel(*top));
    const QJsonObject replay = replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_LEVEL).object();
    QCOMPARE(replay["level"].toInteger(), 100);
    QVERIFY(!replay.contains("neededXp"));
    cache.clear();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_LEVEL).isNull());
}

void TestFrontend::charWimpyTest()
{
    const std::optional<CharWimpy> set = parseWimpyLine(QStringLiteral("Wimpy set to: 120"));
    QVERIFY(set.has_value());
    const GmcpMessage msg = frontend_messages::makeCharWimpy(*set);
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_WIMPY);
    QCOMPARE(msg.toRawBytes(), QByteArray(R"(MMapper.Char.Wimpy {"wimpy":120})"));
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"MMapper.Char.Wimpy"}).getType(),
             GmcpMessageTypeEnum::MMAPPER_CHAR_WIMPY);

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(msg));

    // State: replayed as last sent, forgotten when the character leaves the game and when
    // MMapper connects again.
    FrontendReplayCache cache;
    cache.remember(msg);
    cache.remember(frontend_messages::makeCharWimpy(CharWimpy{0}));
    const QJsonObject replay = replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_WIMPY).object();
    QVERIFY(replay["wimpy"].isDouble());
    QCOMPARE(replay["wimpy"].toInteger(), 0);
    cache.clearGame();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_WIMPY).isNull());
    cache.remember(msg);
    cache.clear();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_WIMPY).isNull());
}

void TestFrontend::charAffectsTest()
{
    // The one table of names: `stat`'s words and the events' come to the same.
    QCOMPARE(charAffectName(QStringLiteral("poison (type: psylonia)")), QStringLiteral("poison"));
    QCOMPARE(charAffectName(QStringLiteral("poisoned")), QStringLiteral("poison"));
    QCOMPARE(charAffectName(QStringLiteral("blind")), QStringLiteral("blindness"));
    QCOMPARE(charAffectName(QStringLiteral("blindness")), QStringLiteral("blindness"));
    QCOMPARE(charAffectName(QStringLiteral("watch room (xanscasoebb)")),
             QStringLiteral("watch room"));
    QCOMPARE(charAffectName(QStringLiteral("Orkish draught")), QStringLiteral("orkish draught"));
    QCOMPARE(charAffectName(QStringLiteral("disease (type: flu)")), QStringLiteral("disease"));
    QCOMPARE(charAffectName(QStringLiteral("sense life")), QStringLiteral("sense life"));
    QVERIFY(charAffectName(QStringLiteral("stored spell fireball")).isEmpty());

    CharAffectsTracker tracker;
    // What the tracker makes of one of MUME's lines at `now`: true when the list changed.
    const auto line = [&tracker](const char *const text, const int64_t now) {
        const std::optional<CombatEvent> event = parseCombatLine(QString::fromUtf8(text));
        return event.has_value() && tracker.receiveEvent(*event, now);
    };
    const auto names = [&tracker]() {
        QStringList result;
        for (const CharAffect &affect : tracker.affects()) {
            result.append(affect.name);
        }
        return result;
    };
    const auto payload = [&tracker]() {
        return payloadOf(frontend_messages::makeCharAffects(tracker.affects()));
    };

    // Up: the line that says it took hold gives the name and the time.
    QVERIFY(line("A blue transparent wall slowly appears around you.", 1000));
    QVERIFY(line("You feel protected.", 1010));
    const GmcpMessage two = frontend_messages::makeCharAffects(tracker.affects());
    QCOMPARE(two.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_AFFECTS);
    QCOMPARE(two.toRawBytes(),
             QByteArray(R"(MMapper.Char.Affects {"affects":[)"
                        R"({"name":"armour","since":1000,"source":"line"},)"
                        R"({"name":"shield","since":1010,"source":"line"}]})"));

    // Refresh: `refreshed` moves, `since` stays.
    QVERIFY(line("Your magic armour is revitalized.", 1500));
    QJsonObject armour = payload()["affects"].toArray().at(0).toObject();
    QCOMPARE(armour["since"].toInteger(), 1000);
    QCOMPARE(armour["refreshed"].toInteger(), 1500);
    // A refresh of something not known: it is on, since nobody knows when.
    QVERIFY(line("Your aura glows more intensely.", 1510));
    const QJsonObject sanctuary = payload()["affects"].toArray().at(2).toObject();
    QCOMPARE(sanctuary["name"].toString(), QStringLiteral("sanctuary"));
    QVERIFY(!sanctuary.contains("since"));
    QCOMPARE(sanctuary["refreshed"].toInteger(), 1510);
    QCOMPARE(sanctuary["source"].toString(), QStringLiteral("line"));

    // Down: gone. A down for what was not known changes nothing.
    QVERIFY(line("Your magical shield wears off.", 1600));
    QCOMPARE(names(), (QStringList{QStringLiteral("armour"), QStringLiteral("sanctuary")}));
    QVERIFY(!line("You feel weaker.", 1601));

    // Somebody else's effects, heals, the conditions that are no lasting effect, and lines
    // that are no effect at all leave the list alone.
    QVERIFY(!line("Walo is surrounded by a brilliant white aura.", 1602));
    QVERIFY(!line("Your scratches and bruises disappear.", 1603));
    QVERIFY(!line("You bleed from open wounds.", 1604));
    QVERIFY(!line("You fight the web to get free, but just become more entangled.", 1605));
    QVERIFY(!line("You are incapacitated and will slowly die, if not aided.", 1606));
    QVERIFY(!line("Bert the stone-troll seems to be blinded!", 1607));
    QVERIFY(!line("You feel sleepy.", 1608));
    QCOMPARE(names(), (QStringList{QStringLiteral("armour"), QStringLiteral("sanctuary")}));

    // The conditions `stat` lists land under `stat`'s names; the symptom repeating is no
    // change, and the cure's line takes it off.
    QVERIFY(line("You suddenly feel a terrible headache!", 1700));
    QVERIFY(!line("You suddenly feel a terrible headache!", 1760));
    QVERIFY(line("You have been blinded!", 1770));
    QCOMPARE(names(),
             (QStringList{QStringLiteral("armour"),
                          QStringLiteral("sanctuary"),
                          QStringLiteral("poison"),
                          QStringLiteral("blindness")}));
    QCOMPARE(payload()["affects"].toArray().at(2).toObject()["since"].toInteger(), 1700);
    QVERIFY(line("You feel a cloak of blindness dissolve.", 1780));

    // `stat` sets it right, both ways: what it lists and was not known comes in without a
    // time and marked as `stat`'s; what was tracked and it does not list goes (sanctuary's
    // fading was missed); what both know keeps its times. Stored spells are no effects.
    QVERIFY(tracker.receiveStat(QStringList{QStringLiteral("armour"),
                                            QStringLiteral("noquit"),
                                            QStringLiteral("poison (type: psylonia)"),
                                            QStringLiteral("stored spell fireball"),
                                            QStringLiteral("stored spell fireball"),
                                            QStringLiteral("detect magic")}));
    QCOMPARE(names(),
             (QStringList{QStringLiteral("armour"),
                          QStringLiteral("poison"),
                          QStringLiteral("noquit"),
                          QStringLiteral("detect magic")}));
    const QJsonArray after = payload()["affects"].toArray();
    armour = after.at(0).toObject();
    QCOMPARE(armour["since"].toInteger(), 1000);
    QCOMPARE(armour["refreshed"].toInteger(), 1500);
    QCOMPARE(armour["source"].toString(), QStringLiteral("line"));
    QCOMPARE(after.at(1).toObject()["since"].toInteger(), 1700);
    const QJsonObject noquit = after.at(2).toObject();
    QCOMPARE(noquit["source"].toString(), QStringLiteral("stat"));
    QVERIFY(!noquit.contains("since"));
    QVERIFY(!noquit.contains("refreshed"));
    // The same list again changes nothing, so nothing is sent.
    QVERIFY(!tracker.receiveStat(QStringList{QStringLiteral("armour"),
                                             QStringLiteral("noquit"),
                                             QStringLiteral("poison (type: psylonia)"),
                                             QStringLiteral("detect magic")}));
    // An effect `stat` told of, then renewed by a line, then worn off by one.
    QVERIFY(line("Your awareness of magical auras is renewed.", 2000));
    const QJsonObject magic = payload()["affects"].toArray().at(3).toObject();
    QCOMPARE(magic["refreshed"].toInteger(), 2000);
    QCOMPARE(magic["source"].toString(), QStringLiteral("line"));
    QVERIFY(!magic.contains("since"));
    QVERIFY(line("Your perception of magical auras wears off.", 2100));
    QVERIFY(line("A warm feeling runs through your body, you feel better.", 2110));
    QCOMPARE(names(), (QStringList{QStringLiteral("armour"), QStringLiteral("noquit")}));
    // Taking hold again while known: a new time, and the old renewal no longer counts.
    QVERIFY(line("A blue transparent wall slowly appears around you.", 2200));
    armour = payload()["affects"].toArray().at(0).toObject();
    QCOMPARE(armour["since"].toInteger(), 2200);
    QVERIFY(!armour.contains("refreshed"));

    // Replay: the list as last sent, whole; a frontend that attaches late is told at once.
    const GmcpMessage current = frontend_messages::makeCharAffects(tracker.affects());
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(current));
    FrontendReplayCache cache;
    cache.remember(two);
    cache.remember(current);
    const QJsonArray replay
        = replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_AFFECTS).object()["affects"].toArray();
    QCOMPARE(replay.size(), 2);
    QCOMPARE(replay.at(0).toObject()["since"].toInteger(), 2200);
    QCOMPARE(replay.at(1).toObject()["name"].toString(), QStringLiteral("noquit"));

    // Reset: the character left the game (or another connection began). Nothing is replayed
    // and nothing is known; the next character's first `stat` is a statement even when it
    // lists nothing, and only the first.
    cache.clearGame();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_AFFECTS).isNull());
    tracker.reset();
    QVERIFY(tracker.affects().empty());
    QVERIFY(tracker.receiveStat(QStringList{}));
    QCOMPARE(frontend_messages::makeCharAffects(tracker.affects()).toRawBytes(),
             QByteArray(R"(MMapper.Char.Affects {"affects":[]})"));
    QVERIFY(!tracker.receiveStat(QStringList{}));
    cache.remember(current);
    cache.clear();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_AFFECTS).isNull());
}

void TestFrontend::charFollowersTest()
{
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"MMapper.Char.Followers"}).getType(),
             GmcpMessageTypeEnum::MMAPPER_CHAR_FOLLOWERS);

    CharFollowersTracker tracker;
    FrontendReplayCache cache;
    QList<QByteArray> sent;
    // What FrontendServer does with each change the tracker reports: the message goes out
    // whole, and what lasts of it is what a frontend that connects later is told.
    const auto publish = [&cache, &sent](const std::optional<CharFollowers> &change) {
        if (!change.has_value()) {
            return false;
        }
        cache.remember(frontend_messages::makeCharFollowers(lastingFollowers(*change)));
        sent.append(frontend_messages::makeCharFollowers(*change).toRawBytes());
        return true;
    };
    const auto line = [&tracker, &publish](const char *const text, const int64_t now) {
        return publish(tracker.receiveLine(QString::fromUtf8(text), now));
    };
    const auto replay = [&cache]() {
        const auto &messages = cache.messages();
        const auto it = messages.find(GmcpMessageTypeEnum::MMAPPER_CHAR_FOLLOWERS);
        return it == messages.end() ? QByteArray{} : it->second.toRawBytes();
    };

    // A bond made: the follower with what is known of it, and no reply.
    QVERIFY(line("A mother eagle starts following you.", 1000));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Char.Followers {"followers":[)"
                        R"({"here":true,"kind":"charmie","label":"","name":"a mother eagle",)"
                        R"("since":1000,"state":"following"}],"leader":{"you":true},)"
                        R"("players":[]})"));
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Char 1" ])")));
    QVERIFY(subs.wants(GmcpMessage::fromRawBytes(sent.last())));
    QVERIFY(line("A trained horse (my) starts following you.", 1005));

    // An order answered: the reply rides in the message its answer caused, and in none after.
    tracker.receiveCommand(QStringLiteral("label eagle one"), 1010);
    QVERIFY(line("Ok.", 1010));
    QVERIFY(!publish(tracker.receivePrompt(1010)));
    tracker.receiveCommand(QStringLiteral("order followers assist"), 1020);
    QVERIFY(!line("You failed to control a mother eagle (one).", 1020));
    QVERIFY(!line("Ok.", 1020));
    QVERIFY(publish(tracker.receivePrompt(1020)));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Char.Followers {"followers":[)"
                        R"({"here":true,"kind":"charmie","label":"one","lastRefused":"assist",)"
                        R"("name":"a mother eagle","since":1000,"state":"refusing"},)"
                        R"({"here":true,"kind":"mount","label":"my","lastOrder":"assist",)"
                        R"("name":"a trained horse","since":1005,"state":"following"}],)"
                        R"("leader":{"you":true},"players":[],)"
                        R"("reply":{"failed":["a mother eagle"],"order":"assist",)"
                        R"("result":"failed","who":"followers"}})"));
    // Replay: a frontend that attaches late is told the followers, not the answer to an
    // order that is over.
    const QByteArray twoBound
        = QByteArray(R"(MMapper.Char.Followers {"followers":[)"
                     R"({"here":true,"kind":"charmie","label":"one","lastRefused":"assist",)"
                     R"("name":"a mother eagle","since":1000,"state":"refusing"},)"
                     R"({"here":true,"kind":"mount","label":"my","lastOrder":"assist",)"
                     R"("name":"a trained horse","since":1005,"state":"following"}],)"
                     R"("leader":{"you":true},"players":[]})");
    QCOMPARE(replay(), twoBound);

    // The other results, with the followers unchanged.
    tracker.receiveCommand(QStringLiteral("order followers"), 1030);
    QVERIFY(!line("Order who to do what?", 1030));
    QVERIFY(publish(tracker.receivePrompt(1030)));
    QJsonObject reply = payloadOf(GmcpMessage::fromRawBytes(sent.last()))["reply"].toObject();
    QCOMPARE(reply["result"].toString(), QStringLiteral("syntax"));
    QCOMPARE(reply["order"].toString(), QString{});
    QVERIFY(reply["failed"].isArray());
    QVERIFY(reply["failed"].toArray().isEmpty());
    tracker.receiveCommand(QStringLiteral("order followers hit orc"), 1031);
    QVERIFY(!line("You have no loyal subjects here.", 1031));
    QVERIFY(publish(tracker.receivePrompt(1031)));
    reply = payloadOf(GmcpMessage::fromRawBytes(sent.last()))["reply"].toObject();
    QCOMPARE(reply["result"].toString(), QStringLiteral("none-here"));
    tracker.receiveCommand(QStringLiteral("order followers hit orc"), 1032);
    QVERIFY(!line("In your dreams, or what?", 1032));
    QVERIFY(publish(tracker.receivePrompt(1032)));
    reply = payloadOf(GmcpMessage::fromRawBytes(sent.last()))["reply"].toObject();
    QCOMPARE(reply["result"].toString(), QStringLiteral("asleep"));
    tracker.receiveCommand(QStringLiteral("order one stand"), 1033);
    QVERIFY(!line("Ok.", 1033));
    QVERIFY(publish(tracker.receivePrompt(1033)));
    reply = payloadOf(GmcpMessage::fromRawBytes(sent.last()))["reply"].toObject();
    QCOMPARE(reply["result"].toString(), QStringLiteral("ok"));
    QCOMPARE(reply["who"].toString(), QStringLiteral("one"));
    QVERIFY(replay() != twoBound);
    QVERIFY(!replay().contains("reply"));

    // One left behind stays in the list and in the replay; `since` is left out for one whose
    // bond was not seen made.
    QVERIFY(line("ACK! A mother eagle didn't follow you, you lost her.", 1040));
    QVERIFY(line("You failed to control Harle the Hobbit.", 1041));
    const QJsonArray three = payloadOf(GmcpMessage::fromRawBytes(replay()))["followers"].toArray();
    QCOMPARE(three.size(), 3);
    QCOMPARE(three.at(0).toObject()["state"].toString(), QStringLiteral("lost"));
    QCOMPARE(three.at(0).toObject()["here"].toBool(), false);
    const QJsonObject harle = three.at(2).toObject();
    QCOMPARE(harle["kind"].toString(), QStringLiteral("unknown"));
    QCOMPARE(harle["state"].toString(), QStringLiteral("refusing"));
    QVERIFY(!harle.contains("since"));
    QVERIFY(!harle.contains("lastOrder"));
    QVERIFY(!harle.contains("lastRefused"));

    // One that leaves or dies is told once, with that state, and is not replayed.
    QVERIFY(line("A trained horse (my) stops following you.", 1050));
    QJsonArray told = payloadOf(GmcpMessage::fromRawBytes(sent.last()))["followers"].toArray();
    QCOMPARE(told.size(), 3);
    QCOMPARE(told.at(1).toObject()["state"].toString(), QStringLiteral("left"));
    QCOMPARE(payloadOf(GmcpMessage::fromRawBytes(replay()))["followers"].toArray().size(), 2);
    QVERIFY(line("A mother eagle (one) has arrived from the north.", 1055));
    QVERIFY(line("A mother eagle (one) is dead! R.I.P.", 1060));
    told = payloadOf(GmcpMessage::fromRawBytes(sent.last()))["followers"].toArray();
    QCOMPARE(told.size(), 2);
    QCOMPARE(told.at(0).toObject()["state"].toString(), QStringLiteral("dead"));
    QCOMPARE(payloadOf(GmcpMessage::fromRawBytes(replay()))["followers"].toArray().size(), 1);
    // The last one gone: an empty list is sent, and is what is replayed.
    QVERIFY(line("Harle the Hobbit is dead! R.I.P.", 1070));
    QCOMPARE(replay(), QByteArray(R"(MMapper.Char.Followers {"followers":[],"players":[]})"));

    // The other side of following: whom the character follows, who leads, the players that
    // follow it and whom it protects, each in the message and in the replay.
    QVERIFY(line("Budach (B) starts following you.", 1080));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Char.Followers {"followers":[],"leader":{"you":true},)"
                        R"("players":["Budach"]})"));
    QVERIFY(line("You now follow Grayelf.", 1081));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Char.Followers {"followers":[],"following":"Grayelf",)"
                        R"("leader":{"name":"Grayelf","you":false},"players":["Budach"]})"));
    QVERIFY(line("You will now try to protect Kazadoe (K).", 1082));
    const QByteArray led
        = QByteArray(R"(MMapper.Char.Followers {"followers":[],"following":"Grayelf",)"
                     R"("leader":{"name":"Grayelf","you":false},"players":["Budach"],)"
                     R"("protect":{"protecting":["Kazadoe"]}})");
    QCOMPARE(sent.last(), led);
    QCOMPARE(replay(), led);
    QVERIFY(line("You stop following Grayelf.", 1083));
    QVERIFY(line("Budach (B) stops following you.", 1084));
    QVERIFY(line("Very well, you concentrate on your own health.", 1085));
    QCOMPARE(replay(),
             QByteArray(R"(MMapper.Char.Followers {"followers":[],"players":[],)"
                        R"("protect":{"protecting":[]}})"));

    // Reset: the character left the game, or another connection began. Nothing is replayed,
    // nothing is known, and nothing is sent to say so.
    QVERIFY(line("A mother eagle starts following you.", 2000));
    const qsizetype before = sent.size();
    cache.clearGame();
    tracker.reset();
    QVERIFY(replay().isEmpty());
    QVERIFY(tracker.followers().empty());
    QCOMPARE(sent.size(), before);
    QVERIFY(!line("A mother eagle has arrived from the north.", 2001));
    QVERIFY(line("A mother eagle starts following you.", 2002));
    QVERIFY(!replay().isEmpty());
    cache.clear();
    QVERIFY(replay().isEmpty());
}

void TestFrontend::accountTest()
{
    // powwow/logs/moria.gjurza.mov:31-37, hosts replaced.
    AccountLinesTracker tracker;
    AccountReplies out;
    for (const char *const line :
         {"Characters in account \"dmitry\"",
          "Name         Rce Lvl   Logon Area     Rent    Delete Host",
          "Porien            Mc  8 days Valinor    free   never secret-host.example",
          "Rumata       dwa W63 28 days Rivendl    free retired secret-host.example",
          ""}) {
        out.append(tracker.receiveLine(QString::fromUtf8(line)));
    }
    QCOMPARE(out.lists.size(), size_t{1});
    const GmcpMessage chars = frontend_messages::makeAccountChars(out.lists.front());
    QCOMPARE(chars.getType(), GmcpMessageTypeEnum::MMAPPER_ACCOUNT_CHARS);
    QCOMPARE(chars.toRawBytes(),
             QByteArray(R"(MMapper.Account.Chars {"account":"dmitry","chars":[)"
                        R"({"area":"Valinor","delete":"never","logon":"8 days","lvl":"Mc",)"
                        R"("name":"Porien","playing":false,"rent":"free"},)"
                        R"({"area":"Rivendl","class":"W","delete":"retired","level":63,)"
                        R"("logon":"28 days","lvl":"W63","name":"Rumata","playing":false,)"
                        R"("race":"dwa","rent":"free"}]})"));
    QVERIFY(!chars.toRawBytes().contains("secret-host"));

    // A list MUME's pager interrupts says so, and a row's subrace is given where it has one.
    AccountChars paged;
    paged.account = QStringLiteral("dmitry");
    AccountChar gjurza;
    gjurza.name = QStringLiteral("Gjurza");
    gjurza.race = QStringLiteral("orc");
    gjurza.sub = QStringLiteral("tar");
    gjurza.lvl = QStringLiteral("56");
    gjurza.level = 56;
    gjurza.logon = QStringLiteral("3 yrs");
    gjurza.area = QStringLiteral("DolGldr");
    gjurza.rent = QStringLiteral("free");
    gjurza.deletion = QStringLiteral("retired");
    paged.chars.push_back(gjurza);
    paged.more = true;
    paged.percent = 84;
    QCOMPARE(frontend_messages::makeAccountChars(paged).toRawBytes(),
             QByteArray(R"(MMapper.Account.Chars {"account":"dmitry","chars":[)"
                        R"({"area":"DolGldr","delete":"retired","level":56,"logon":"3 yrs",)"
                        R"("lvl":"56","name":"Gjurza","playing":false,"race":"orc",)"
                        R"("rent":"free","sub":"tar"}],"more":true,"percent":84})"));

    AccountMenu menu;
    AccountMenuCommand play;
    play.name = QStringLiteral("play");
    play.usage = QStringLiteral("Play <name>");
    play.help = QStringLiteral("Log the character <name> into MUME");
    menu.commands.push_back(play);
    menu.sorts = QStringList{QStringLiteral("side"), QStringLiteral("race")};
    const GmcpMessage menuMsg = frontend_messages::makeAccountMenu(menu);
    QCOMPARE(menuMsg.toRawBytes(),
             QByteArray(R"(MMapper.Account.Menu {"commands":[{"help":"Log the character <name> )"
                        R"(into MUME","name":"play","usage":"Play <name>"}],)"
                        R"("sorts":["side","race"]})"));

    const auto wait = parseAccountReplyLine(
        QStringLiteral("You must wait  6 mins before you can log in this character!"));
    QVERIFY(wait.has_value());
    const QJsonObject w = payloadOf(frontend_messages::makeAccountReply(*wait));
    QCOMPARE(w["kind"].toString(), QStringLiteral("wait"));
    QCOMPARE(w["seconds"].toInteger(), 360);

    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Account 1" ])")));
    QVERIFY(subs.wants(chars));
    QVERIFY(subs.wants(menuMsg));

    // Replayed as last sent, and kept when the character leaves the game; a new connection
    // forgets them.
    FrontendReplayCache cache;
    cache.remember(chars);
    cache.remember(menuMsg);
    cache.remember(frontend_messages::makeAccountReply(*wait));
    cache.remember(GmcpMessage::fromRawBytes(QByteArray{R"(Char.Name {"name":"Rumata"})"}));
    cache.clearGame();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::CHAR_NAME).isNull());
    QCOMPARE(replayed(cache, GmcpMessageTypeEnum::MMAPPER_ACCOUNT_CHARS)
                 .object()["chars"]
                 .toArray()
                 .size(),
             2);
    QVERIFY(!replayed(cache, GmcpMessageTypeEnum::MMAPPER_ACCOUNT_MENU).isNull());
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_ACCOUNT_REPLY).isNull());
    cache.clear();
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::MMAPPER_ACCOUNT_CHARS).isNull());
}

void TestFrontend::replayChangedFieldsTest()
{
    // MUME sends only what changed, so a frontend that connects mid-fight must be given
    // everything seen so far rather than the last tick: an hp figure with no maximum to draw
    // it against is no use to anyone.
    FrontendReplayCache cache;
    cache.remember(parse(R"(Char.Vitals {"hp":100,"maxhp":120,"mana":40,"maxmana":50})"));
    cache.remember(parse(R"(Char.Vitals {"hp":87})"));
    cache.remember(parse(R"(Char.Vitals {"opponent":"orc"})"));
    cache.remember(parse(R"(Char.Vitals {"opponent":null})"));

    const QJsonObject vitals = replayed(cache, GmcpMessageTypeEnum::CHAR_VITALS).object();
    QCOMPARE(vitals["hp"].toInt(), 87);
    QCOMPARE(vitals["maxhp"].toInt(), 120);
    QCOMPARE(vitals["mana"].toInt(), 40);
    QCOMPARE(vitals["maxmana"].toInt(), 50);
    // A field MUME cleared is replayed cleared, as a watching frontend would have it.
    QVERIFY(vitals.contains("opponent"));
    QVERIFY(vitals["opponent"].isNull());
    // Under MUME's own name, so that a frontend takes it exactly as it takes the original.
    QCOMPARE(cache.messages().at(GmcpMessageTypeEnum::CHAR_VITALS).getName().toQString(),
             QStringLiteral("Char.Vitals"));

    cache.remember(parse(R"(Char.StatusVars {"level":12,"race":"Hobbit"})"));
    cache.remember(parse(R"(Char.StatusVars {"level":13})"));
    const QJsonObject status = replayed(cache, GmcpMessageTypeEnum::CHAR_STATUSVARS).object();
    QCOMPARE(status["level"].toInt(), 13);
    QCOMPARE(status["race"].toString(), QStringLiteral("Hobbit"));

    // A package that restates everything is replayed as the last one sent.
    cache.remember(parse(R"(Event.Sun {"what":"rise"})"));
    cache.remember(parse(R"(Event.Sun {"what":"light"})"));
    QCOMPARE(replayed(cache, GmcpMessageTypeEnum::EVENT_SUN).object()["what"].toString(),
             QStringLiteral("light"));

    // Something that happened is not state.
    cache.remember(parse(R"(Event.Moved {"dir":"north"})"));
    QVERIFY(cache.messages().count(GmcpMessageTypeEnum::EVENT_MOVED) == 0);

    cache.clear();
    QVERIFY(cache.messages().empty());
}

void TestFrontend::replaySetChangesTest()
{
    FrontendReplayCache cache;

    // A change that arrives before any Set has nothing to be a change to.
    cache.remember(parse(R"(Room.Chars.Add {"id":9,"name":"a cat"})"));
    QVERIFY(cache.messages().count(GmcpMessageTypeEnum::ROOM_CHARS_SET) == 0);

    // After a Set, every change is applied to it, so that a frontend connecting in the middle
    // of a fight is shown the room as it is now rather than nobody at all.
    cache.remember(parse(R"(Room.Chars.Set [{"id":1,"name":"an orc","fighting":null},)"
                         R"({"id":2,"name":"a troll"}])"));
    cache.remember(parse(R"(Room.Chars.Add {"id":3,"name":"a wolf"})"));
    cache.remember(parse(R"(Room.Chars.Update {"id":1,"fighting":"you"})"));
    // The bare id, followed by a space, as MUME sends it.
    cache.remember(parse("Room.Chars.Remove 2 "));
    // Described before its arrival was: taken as an arrival, as a frontend takes it.
    cache.remember(parse(R"(Room.Chars.Update {"id":4,"name":"a crow"})"));
    // Someone who is not here leaving changes nothing, and neither does a change to nobody.
    cache.remember(parse("Room.Chars.Remove 99"));
    cache.remember(parse(R"(Room.Chars.Update {"fighting":"you"})"));

    QCOMPARE(cache.messages().at(GmcpMessageTypeEnum::ROOM_CHARS_SET).getName().toQString(),
             QStringLiteral("Room.Chars.Set"));
    const QJsonArray room = replayed(cache, GmcpMessageTypeEnum::ROOM_CHARS_SET).array();
    QCOMPARE(room.size(), 3);
    const QJsonObject orc = room[0].toObject();
    QCOMPARE(orc["id"].toInt(), 1);
    // An Update carries only what changed; the rest of what is known about them stays.
    QCOMPARE(orc["name"].toString(), QStringLiteral("an orc"));
    QCOMPARE(orc["fighting"].toString(), QStringLiteral("you"));
    QCOMPARE(room[1].toObject()["id"].toInt(), 3);
    QCOMPARE(room[2].toObject()["id"].toInt(), 4);

    // A new Set, as on every move, replaces the room outright.
    cache.remember(parse(R"(Room.Chars.Set [])"));
    QVERIFY(replayed(cache, GmcpMessageTypeEnum::ROOM_CHARS_SET).array().isEmpty());

    // The group is kept the same way.
    cache.remember(parse(R"(Group.Set [{"id":5,"name":"Gandalf","hp":100,"maxhp":100}])"));
    cache.remember(parse(R"(Group.Update {"id":5,"hp":60})"));
    cache.remember(parse(R"(Group.Add {"id":6,"name":"Frodo"})"));
    const QJsonObject gandalf = replayed(cache, GmcpMessageTypeEnum::GROUP_SET).array()[0].toObject();
    QCOMPARE(gandalf["hp"].toInt(), 60);
    QCOMPARE(gandalf["maxhp"].toInt(), 100);
    cache.remember(parse("Group.Remove 5"));
    const QJsonArray group = replayed(cache, GmcpMessageTypeEnum::GROUP_SET).array();
    QCOMPARE(group.size(), 1);
    QCOMPARE(group[0].toObject()["name"].toString(), QStringLiteral("Frodo"));
}

/// Every server-sent message those modules define. A message MMapper does not recognise is
/// still relayed, because FrontendSubscriptions::wants() resolves the module from the name
/// rather than from the enum, but MMapper itself cannot then reason about it.
void TestFrontend::mumeMessageCoverageTest()
{
    static const char *const documented[]
        = {"Char.Name",       "Char.StatusVars",    "Char.Vitals",       "Client.GUI",
           "Client.Map",      "Comm.Channel.List",  "Comm.Channel.Text", "Core.Goodbye",
           "Core.Ping",       "Event.Achieved",     "Event.Darkness",    "Event.Moon",
           "Event.Moved",     "Event.Sun",          "Group.Add",         "Group.Remove",
           "Group.Set",       "Group.Update",       "Room.Chars.Add",    "Room.Chars.Remove",
           "Room.Chars.Set",  "Room.Chars.Update",  "Room.Info",         "Room.Known.Add",
           "Room.Known.List", "Room.Known.Updated", "Room.UpdateExits"};

    for (const char *const name : documented) {
        const GmcpMessage msg = GmcpMessage::fromRawBytes(QByteArray{name});
        QVERIFY2(msg.getType() != GmcpMessageTypeEnum::UNKNOWN, name);
    }

    // MUME's help spells it as one word. MMapper once had it as Room.Update.Exits, which
    // matched nothing MUME sends.
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"Room.UpdateExits"}).getType(),
             GmcpMessageTypeEnum::ROOM_UPDATE_EXITS);
    QCOMPARE(GmcpMessage{GmcpMessageTypeEnum::ROOM_UPDATE_EXITS}.getName().toQString(),
             QStringLiteral("Room.UpdateExits"));
}

void TestFrontend::charRefusedTest()
{
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"MMapper.Char.Refused"}).getType(),
             GmcpMessageTypeEnum::MMAPPER_CHAR_REFUSED);

    // A line that names its command: log-2005.09.15-21.01.17.txt:90756.
    QCOMPARE(frontend_messages::makeCharRefused(
                 *parseRefusedLine(QStringLiteral("Rest while fighting? Are you MAD?")))
                 .toRawBytes(),
             QByteArray(R"(MMapper.Char.Refused {"action":"rest","reason":"fighting",)"
                        R"("text":"Rest while fighting? Are you MAD?"})"));
    // One that answers several commands has no action: log-2005.08.31-14.00.43.txt:1353.
    QCOMPARE(frontend_messages::makeCharRefused(
                 *parseRefusedLine(QStringLiteral("You are too afraid.")))
                 .toRawBytes(),
             QByteArray(R"(MMapper.Char.Refused {"reason":"afraid","text":"You are too afraid."})"));
    // One that names somebody: log-2005.11.07-05.28.32.txt:10776.
    QCOMPARE(frontend_messages::makeCharRefused(
                 *parseRefusedLine(QStringLiteral("A pack horse doesn't want to follow you!")))
                 .toRawBytes(),
             QByteArray(R"(MMapper.Char.Refused {"action":"lead","reason":"unwilling",)"
                        R"("target":"A pack horse","text":"A pack horse doesn't want to follow you!"})"));
    // A ride refused, with the side the parser takes from the oldest move still unanswered:
    // log-2005.08.31-14.00.43.txt:1305.
    CharRefused ride = *parseRefusedLine(QStringLiteral("Oops! You cannot go there riding!"));
    ride.dir = QStringLiteral("n");
    QCOMPARE(frontend_messages::makeCharRefused(ride).toRawBytes(),
             QByteArray(R"(MMapper.Char.Refused {"action":"move","dir":"n","reason":"noride",)"
                        R"("text":"Oops! You cannot go there riding!"})"));

    // A door's refusal comes from the answer paired with its command, as ContainerTracker pairs
    // it: `open exit e` / "It seems to be locked." (log-2006.08.03-19.24.37.txt:18504-18505).
    ContainerTracker containers;
    containers.receiveCommand(QStringLiteral("open exit e"));
    QVERIFY(containers.receiveLine(QStringLiteral("It seems to be locked."), 1000).empty());
    const std::vector<DoorReply> replies = containers.takeDoorReplies();
    QCOMPARE(replies.size(), size_t{1});
    QCOMPARE(replies[0].command.direction, QStringLiteral("e"));
    QCOMPARE(frontend_messages::makeCharRefused(*refusedFromDoorReply(replies[0])).toRawBytes(),
             QByteArray(R"(MMapper.Char.Refused {"action":"open","dir":"e","reason":"door-locked",)"
                        R"("text":"It seems to be locked."})"));
    QVERIFY(containers.takeDoorReplies().empty());
    // The same sentence about a chest is MMapper.Room.Container's, and no door reply.
    containers.receiveCommand(QStringLiteral("open chest"));
    QCOMPARE(containers.receiveLine(QStringLiteral("It seems to be locked."), 1001).size(),
             size_t{1});
    QVERIFY(containers.takeDoorReplies().empty());

    // An event: nothing of it is kept for a frontend that connects later.
    FrontendReplayCache cache;
    cache.remember(frontend_messages::makeCharRefused(ride));
    QVERIFY(cache.messages().empty());
}

void TestFrontend::roomLookTest()
{
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"MMapper.Room.Look"}).getType(),
             GmcpMessageTypeEnum::MMAPPER_ROOM_LOOK);

    // A say inside the answer (log-2005.09.03-02.28.34.txt:11384-11388), as MumeXmlParser
    // hands it on.
    ExitLookTracker tracker;
    tracker.receiveRoom(1101);
    tracker.receiveCommand(QStringLiteral("l n"));
    tracker.receiveLine(QStringLiteral("The giant, thick wooden is open."), LineKindEnum::TEXT);
    tracker.receiveLine(QStringLiteral("*Stolb the Orc* [stolb] says 'yawn'"), LineKindEnum::ASYNC);
    const auto done = tracker.receivePrompt();
    QCOMPARE(done.size(), size_t{1});
    const GmcpMessage msg = frontend_messages::makeRoomLook(done[0]);
    QCOMPARE(msg.toRawBytes(),
             QByteArray(R"(MMapper.Room.Look {"command":"l n","dir":"north",)"
                        R"("door":{"name":"giant, thick wooden","state":"open"},)"
                        R"("dropped":["*Stolb the Orc* [stolb] says 'yawn'"],"kind":"door",)"
                        R"("lines":["The giant, thick wooden is open."],)"
                        R"("raw":["The giant, thick wooden is open.",)"
                        R"("*Stolb the Orc* [stolb] says 'yawn'"],"reasons":[],"room":1101,)"
                        R"("text":"The giant, thick wooden is open.","uncertain":false})"));

    // To every subscriber of MMapper.Room, observers too; an event, not replayed.
    FrontendSubscriptions subs;
    QVERIFY(subs.applySupports(parse(R"(Core.Supports.Set [ "MMapper.Room 1" ])")));
    QVERIFY(subs.wants(msg));
    FrontendReplayCache cache;
    cache.remember(msg);
    QVERIFY(cache.messages().empty());
}

void TestFrontend::roomDoorTest()
{
    QCOMPARE(GmcpMessage::fromRawBytes(QByteArray{"MMapper.Room.Door"}).getType(),
             GmcpMessageTypeEnum::MMAPPER_ROOM_DOOR);

    RoomDoorTracker tracker;
    tracker.setNotDoor([](const QString &word) { return namesContainer(word); });
    ContainerTracker containers;
    FrontendReplayCache cache;
    QList<QByteArray> sent;
    // What MumeXmlParser and FrontendServer do with each change the tracker reports.
    const auto publish = [&cache, &sent](const std::optional<RoomDoors> &change) {
        if (!change.has_value()) {
            return false;
        }
        const GmcpMessage msg = frontend_messages::makeRoomDoor(*change);
        cache.remember(msg);
        sent.append(msg.toRawBytes());
        return true;
    };
    const auto gmcp = [](const char *const raw) {
        return QJsonDocument::fromJson(parse(raw).getJson()->toQByteArray()).object();
    };
    const auto command = [&tracker, &containers](const char *const input) {
        containers.receiveCommand(QString::fromLatin1(input));
        tracker.receiveCommand(QString::fromLatin1(input));
    };
    const auto line = [&tracker, &containers, &publish](const char *const text, const int64_t now) {
        bool changed = false;
        std::ignore = containers.receiveLine(QString::fromUtf8(text), now);
        for (const DoorReply &reply : containers.takeDoorReplies()) {
            changed = publish(tracker.receiveDoorReply(reply, now)) || changed;
        }
        return publish(tracker.receiveLine(QString::fromUtf8(text), QString{}, now)) || changed;
    };
    const auto replay = [&cache]() {
        const auto &messages = cache.messages();
        const auto it = messages.find(GmcpMessageTypeEnum::MMAPPER_ROOM_DOOR);
        return it == messages.end() ? QByteArray{} : it->second.toRawBytes();
    };

    // The Backroom of the Unqalome crypt (log-2005.11.02-01.27.58.txt:23462-23494), with the
    // Room.Info MUME sends today (help gmcp_room).
    QVERIFY(publish(tracker.receiveRoomInfo(
        parseRoomInfoDoors(gmcp(R"(Room.Info {"id":5988992,"name":"Backroom","exits":)"
                                R"({"w":{"id":12925987,"name":"door"}}})")),
        1000)));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Room.Door {"doors":[)"
                        R"({"dir":"w","name":"door","since":1000,"state":"open"}],)"
                        R"("room":5988992})"));
    QVERIFY(line("The door slams shut, and a thick layer of ice covers it.", 1010));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Room.Door {"doors":[)"
                        R"({"dir":"w","name":"door","since":1010,"state":"iced"}],)"
                        R"("room":5988992})"));
    QVERIFY(!publish(tracker.receiveUpdateExits(
        parseExitsObject(gmcp(R"(Room.UpdateExits {"w":{"id":12925987,"name":"door",)"
                              R"("flags":["closed"]}})")),
        1010)));
    command("open exit w");
    QVERIFY(!line("The ice layer is too thick and prevents you from reaching it.", 1011));
    command("cast normal 'burning hands' door");
    QVERIFY(!line("You aim your spell at the ice layer.", 1015));
    QVERIFY(!line("Some of the ice melts down.", 1015));
    QVERIFY(line("The ice layer is completely molten!", 1020));
    const QByteArray molten = QByteArray(R"(MMapper.Room.Door {"doors":[)"
                                         R"({"dir":"w","name":"door","since":1020,"state":"molten"}],)"
                                         R"("room":5988992})");
    QCOMPARE(sent.last(), molten);
    // State: a frontend that connects now is told the doors as they are.
    QCOMPARE(replay(), molten);
    // The pending `open exit w` was never answered in a door's words; this one is.
    command("open exit w");
    QVERIFY(line("Ok.", 1021));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Room.Door {"doors":[)"
                        R"({"dir":"w","name":"door","since":1021,"state":"open"}],)"
                        R"("room":5988992})"));

    // A door known by a line alone has no side, and one known by its side alone no name.
    QVERIFY(publish(tracker.receiveRoomInfo(
        parseRoomInfoDoors(gmcp(R"(Room.Info {"id":12925987,"exits":{"e":{"id":5988992,)"
                                R"("flags":["closed"]}}})")),
        1030)));
    QVERIFY(line("The trapdoor gave away under the pressure.", 1031));
    QCOMPARE(sent.last(),
             QByteArray(R"(MMapper.Room.Door {"doors":[)"
                        R"({"dir":"e","since":1030,"state":"closed"},)"
                        R"({"name":"trapdoor","since":1031,"state":"broken"}],)"
                        R"("room":12925987})"));
    // A chest is no door, whoever opens it.
    QVERIFY(!line("Stolb opens the chest.", 1032));

    // A room without a door: one message that says so, which is also what is replayed.
    QVERIFY(publish(tracker.receiveRoomInfo(
        parseRoomInfoDoors(gmcp(R"(Room.Info {"id":77,"exits":{"n":{"id":78}}})")), 1040)));
    QCOMPARE(sent.last(), QByteArray(R"(MMapper.Room.Door {"doors":[],"room":77})"));
    QCOMPARE(replay(), sent.last());

    // Forgotten with the rest of the game's state when the character leaves it.
    cache.clearGame();
    QCOMPARE(replay(), QByteArray{});
}

void TestFrontend::mapPositionRidableTest()
{
    mmqt::HideQDebug forThisTest;
    // A stable (1) with a field north of it (2, ridable), a hall east (3, not ridable) and a
    // cellar below (4, the map does not say); an exit west that leads nowhere on the map.
    const auto makeRoom = [](const uint32_t id, const char *const name, const RoomRidableEnum ride) {
        ExternalRawRoom room;
        room.setId(ExternalRoomId{id});
        room.setPosition(Coordinate{static_cast<int>(id), 0, 0});
        room.setName(RoomName{name});
        room.setRidableType(ride);
        room.status = RoomStatusEnum::Permanent;
        return room;
    };
    ExternalRawRoom stable = makeRoom(1, "A stable", RoomRidableEnum::RIDABLE);
    ExternalRawRoom field = makeRoom(2, "A field", RoomRidableEnum::RIDABLE);
    field.setServerId(ServerRoomId{222});
    ExternalRawRoom hall = makeRoom(3, "A hall", RoomRidableEnum::NOT_RIDABLE);
    ExternalRawRoom cellar = makeRoom(4, "A cellar", RoomRidableEnum::UNDEFINED);
    const auto link = [](ExternalRawRoom &from, const ExitDirEnum dir, const uint32_t to) {
        from.exits[dir].addExitFlags(ExitFlagEnum::EXIT);
        from.exits[dir].outgoing.insert(ExternalRoomId{to});
    };
    link(stable, ExitDirEnum::NORTH, 2);
    link(stable, ExitDirEnum::EAST, 3);
    link(stable, ExitDirEnum::DOWN, 4);
    stable.exits[ExitDirEnum::WEST].addExitFlags(ExitFlagEnum::EXIT);
    // A hidden door on the way east: its name is no part of the package.
    stable.exits[ExitDirEnum::EAST].addExitFlags(ExitFlagEnum::DOOR);
    stable.exits[ExitDirEnum::EAST].addDoorFlags(DoorFlagEnum::HIDDEN);
    stable.exits[ExitDirEnum::EAST].setDoorName(DoorName{"secretpanel"});
    link(field, ExitDirEnum::SOUTH, 1);
    link(hall, ExitDirEnum::WEST, 1);
    link(cellar, ExitDirEnum::UP, 1);

    ProgressCounter pc;
    const Map map = Map::fromRooms(pc, {stable, field, hall, cellar}, {}).modified;

    const GmcpMessage msg = frontend_messages::makeMapPosition(
        map.findRoomHandle(ExternalRoomId{1}));
    const QJsonObject obj = payloadOf(msg);
    QCOMPARE(obj["ridable"], QJsonValue{true});
    const QJsonObject exits = obj["exits"].toObject();
    QCOMPARE(exits.keys(), (QStringList{QStringLiteral("d"), QStringLiteral("e"), QStringLiteral("n")}));
    QCOMPARE(exits["n"].toObject()["ridable"], QJsonValue{true});
    QCOMPARE(exits["n"].toObject()["externalId"].toInteger(), 2);
    QCOMPARE(exits["n"].toObject()["serverId"].toInteger(), 222);
    QCOMPARE(exits["e"].toObject()["ridable"], QJsonValue{false});
    QCOMPARE(exits["e"].toObject()["externalId"].toInteger(), 3);
    QVERIFY(!exits["e"].toObject().contains("serverId"));
    // The map does not say: null, not false.
    QVERIFY(exits["d"].toObject().contains("ridable"));
    QVERIFY(exits["d"].toObject()["ridable"].isNull());
    QVERIFY(!msg.toRawBytes().contains("secretpanel"));

    const QJsonObject inHall = payloadOf(
        frontend_messages::makeMapPosition(map.findRoomHandle(ExternalRoomId{3})));
    QCOMPARE(inHall["ridable"], QJsonValue{false});
    QCOMPARE(inHall["exits"].toObject()["w"].toObject()["ridable"], QJsonValue{true});
    const QJsonObject inCellar = payloadOf(
        frontend_messages::makeMapPosition(map.findRoomHandle(ExternalRoomId{4})));
    QVERIFY(inCellar.contains("ridable"));
    QVERIFY(inCellar["ridable"].isNull());

    // A room with no exits has an empty `exits`, and what the map does not say is null.
    const Map lone = makeOneRoomMap(ServerRoomId{812345});
    const QJsonObject alone = payloadOf(
        frontend_messages::makeMapPosition(lone.findRoomHandle(ExternalRoomId{1})));
    QVERIFY(alone["exits"].toObject().isEmpty());
    QVERIFY(alone["ridable"].isNull());
}

void TestFrontend::charSkillsReplayTest()
{
    // MMapper.Char.Skills is state: each `prac` away from a guild lists the table whole, whoever
    // sent the command, and the last one is what a frontend that connects later is told.
    FrontendReplayCache cache;
    const char *const first = R"(MMapper.Char.Skills {"complete":true,"paged":false,)"
                              R"("rows":[{"class":"Warrior","difficulty":"Hard","knowledge":"Fair",)"
                              R"("name":"Bash","trained":true}],"sessionsLeft":41,"text":"..."})";
    const char *const second = R"(MMapper.Char.Skills {"complete":true,"paged":false,)"
                               R"("rows":[{"class":"Warrior","difficulty":"Hard","knowledge":"Good",)"
                               R"("name":"Bash","trained":true}],"sessionsLeft":40,"text":"..."})";
    cache.remember(parse(first));
    QCOMPARE(replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_SKILLS)
                 .object()["sessionsLeft"]
                 .toInteger(),
             41);
    cache.remember(parse(second));
    const QJsonObject now = replayed(cache, GmcpMessageTypeEnum::MMAPPER_CHAR_SKILLS).object();
    QCOMPARE(now["sessionsLeft"].toInteger(), 40);
    QCOMPARE(now["rows"].toArray().size(), 1);
    QCOMPARE(now["rows"].toArray().at(0).toObject()["knowledge"].toString(), QStringLiteral("Good"));
    // Another character's skills are not this one's.
    cache.clearGame();
    QVERIFY(!cache.messages().contains(GmcpMessageTypeEnum::MMAPPER_CHAR_SKILLS));
}

QTEST_MAIN(TestFrontend)

void TestFrontend::itemObservationMetadataTest()
{
    ItemBlock block;
    block.snapshotId = QStringLiteral("snapshot-a");
    block.items.push_back(parseListedItem(QStringLiteral("a sable pouch")));
    block.items.push_back(parseListedItem(QStringLiteral("a sable pouch")));
    const auto inventory = payloadOf(frontend_messages::makeCharInventory(block));
    const auto items = inventory["items"].toArray();
    QCOMPARE(inventory["snapshotId"].toString(), block.snapshotId);
    QCOMPARE(items[0].toObject()["handle"].toString(), QStringLiteral("snapshot-a:0"));
    QCOMPARE(items[1].toObject()["handle"].toString(), QStringLiteral("snapshot-a:1"));
    QVERIFY(items[0].toObject()["selector"].isNull());
    block.closed = true;
    block.target = QStringLiteral("2.pouch");
    const auto container = payloadOf(frontend_messages::makeCharContainer(block));
    QVERIFY(!container["contentsKnown"].toBool());
    QCOMPARE(container["target"].toString(), block.target);
    QCOMPARE(container["listingMode"].toString(), block.listingMode);
    RoomContentsSnapshot room;
    room.snapshotId = QStringLiteral("room-a");
    room.objects.emplace_back();
    const auto ground = payloadOf(frontend_messages::makeRoomContents(room, nullptr));
    const auto object = ground["objects"].toArray()[0].toObject();
    QVERIFY(object["selector"].isNull());
    QCOMPARE(object["targetEvidence"].toString(), QStringLiteral("display-order-unverified"));
    ItemCommandTracker tracker;
    const auto pending = tracker.receiveCommand(QStringLiteral("get helmet"));
    QVERIFY(pending.has_value());
    const auto start = payloadOf(frontend_messages::makeCharCommand(*pending));
    QCOMPARE(start["status"].toString(), QStringLiteral("pending"));
    const auto end = payloadOf(frontend_messages::makeCharCommand(tracker.finish(true).front()));
    QCOMPARE(end["id"], start["id"]);
    QCOMPARE(end["status"].toString(), QStringLiteral("unknown"));
}
