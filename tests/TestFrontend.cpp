// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestFrontend.h"

#include "../src/frontend/FrontendMessages.h"
#include "../src/frontend/FrontendSubscriptions.h"
#include "../src/proxy/GmcpModule.h"
#include "../src/global/progresscounter.h"
#include "../src/map/Map.h"
#include "../src/map/RawRoom.h"
#include "../src/map/RoomHandle.h"
#include "../src/map/coordinate.h"
#include "../src/map/mmapper2room.h"
#include "../src/map/roomid.h"

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

void TestFrontend::terminalOutputTest()
{
    const QString text = QStringLiteral("\x1b[32mA forest path\x1b[0m\r\n");
    const GmcpMessage msg
        = frontend_messages::makeTerminalOutput(SendToUserSourceEnum::FromMud, text, false);

    QCOMPARE(msg.getName().toQByteArray(), QByteArray("MMapper.Terminal.Output"));

    const QJsonObject obj = payloadOf(msg);
    // ANSI must survive: a frontend is expected to render colour.
    QCOMPARE(obj["text"].toString(), text);
    QCOMPARE(obj["format"].toString(), QStringLiteral("ansi"));
    QCOMPARE(obj["source"].toString(), QStringLiteral("mud"));
    QCOMPARE(obj["goAhead"].toBool(), false);

    // A prompt is distinguished by goAhead, not by its text.
    const GmcpMessage prompt
        = frontend_messages::makeTerminalOutput(SendToUserSourceEnum::FromMud, "HP:Fine> ", true);
    QCOMPARE(payloadOf(prompt)["goAhead"].toBool(), true);

    // MMapper's own output is distinguishable from the game's.
    const GmcpMessage own
        = frontend_messages::makeTerminalOutput(SendToUserSourceEnum::FromMMapper, "hi", false);
    QCOMPARE(payloadOf(own)["source"].toString(), QStringLiteral("mmapper"));
}

void TestFrontend::sessionStateTest()
{
    const GmcpMessage connected = frontend_messages::makeSessionState(true, true, true, true);
    QCOMPARE(connected.getName().toQByteArray(), QByteArray("MMapper.Session.State"));
    QCOMPARE(payloadOf(connected)["upstream"].toString(), QStringLiteral("connected"));
    QCOMPARE(payloadOf(connected)["mapLoaded"].toBool(), true);

    const GmcpMessage offline = frontend_messages::makeSessionState(false, false, false, false);
    QCOMPARE(payloadOf(offline)["upstream"].toString(), QStringLiteral("disconnected"));
    QCOMPARE(payloadOf(offline)["echo"].toBool(), false);

    // Only one frontend may drive MMapper's single downstream session; the rest observe it,
    // and each is told which it is.
    QCOMPARE(payloadOf(connected)["role"].toString(), QStringLiteral("driving"));
    QCOMPARE(payloadOf(offline)["role"].toString(), QStringLiteral("observing"));
}

void TestFrontend::inputSubscriptionTest()
{
    // Input is client-to-server, so a frontend never subscribes to receive it; but the
    // module has to be known, or "MMapper.Input" would be treated as an unrecognised module
    // and proxied upstream to MUME by UserTelnet's supports filter.
    const GmcpModule mod{std::string{"mmapper.input"}};
    QVERIFY(mod.isSupported());
    QCOMPARE(mod.getType(), GmcpModuleTypeEnum::MMAPPER_INPUT);

    const GmcpMessage command
        = GmcpMessage::fromRawBytes(QByteArray(R"(MMapper.Input.Command {"text":"north"})"));
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
}

QTEST_MAIN(TestFrontend)
