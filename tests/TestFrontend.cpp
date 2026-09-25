// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestFrontend.h"

#include "../src/frontend/FrontendMessages.h"
#include "../src/frontend/FrontendReplayCache.h"
#include "../src/frontend/FrontendSubscriptions.h"
#include "../src/global/progresscounter.h"
#include "../src/map/Map.h"
#include "../src/map/RawRoom.h"
#include "../src/map/RoomHandle.h"
#include "../src/map/coordinate.h"
#include "../src/map/mmapper2room.h"
#include "../src/map/roomid.h"
#include "../src/parser/ContainerLines.h"
#include "../src/parser/ItemLines.h"
#include "../src/parser/RoomContents.h"
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
             QByteArray(R"(MMapper.Char.Container {"closed":false,"items":[)"
                        R"({"condition":null,"count":2,"flags":[],"name":"azure scrolls",)"
                        R"("text":"two azure scrolls"},)"
                        R"({"condition":"satisfactory","count":1,"flags":[],)"
                        R"("name":"a black pair of padded boots",)"
                        R"j("text":"a black pair of padded boots (satisfactory)"}],)j"
                        R"("keyword":"backpack","where":"used"})"));
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_CHAR_CONTAINER);

    // A closed one, never listed: no items, and no place.
    const auto closed = listed({QStringLiteral("A large cabinet is closed.")},
                               QStringLiteral("look in cabinet"));
    QCOMPARE(closed.size(), size_t{1});
    QCOMPARE(
        frontend_messages::makeCharContainer(closed.front()).toRawBytes(),
        QByteArray(
            R"(MMapper.Char.Container {"closed":true,"items":[],"keyword":"cabinet","where":null})"));
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

QTEST_MAIN(TestFrontend)
