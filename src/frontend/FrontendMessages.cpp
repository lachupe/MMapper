// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendMessages.h"

#include "../global/TextUtils.h"
#include "../map/RoomFingerprint.h"
#include "../map/coordinate.h"
#include "../map/mmapper2room.h"
#include "../map/roomid.h"

#include <algorithm>
#include <array>
#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>

namespace {

NODISCARD QString precisionName(const MumeClockPrecisionEnum precision)
{
    switch (precision) {
    case MumeClockPrecisionEnum::UNSET:
        return QStringLiteral("unset");
    case MumeClockPrecisionEnum::DAY:
        return QStringLiteral("day");
    case MumeClockPrecisionEnum::HOUR:
        return QStringLiteral("hour");
    case MumeClockPrecisionEnum::MINUTE:
        return QStringLiteral("minute");
    }
    return QStringLiteral("unset");
}

NODISCARD QString timeOfDayName(const MumeTimeEnum time)
{
    switch (time) {
    case MumeTimeEnum::DAWN:
        return QStringLiteral("dawn");
    case MumeTimeEnum::DAY:
        return QStringLiteral("day");
    case MumeTimeEnum::DUSK:
        return QStringLiteral("dusk");
    case MumeTimeEnum::NIGHT:
        return QStringLiteral("night");
    case MumeTimeEnum::UNKNOWN:
        break;
    }
    return QStringLiteral("unknown");
}

NODISCARD QString seasonName(const MumeSeasonEnum season)
{
    switch (season) {
    case MumeSeasonEnum::WINTER:
        return QStringLiteral("winter");
    case MumeSeasonEnum::SPRING:
        return QStringLiteral("spring");
    case MumeSeasonEnum::SUMMER:
        return QStringLiteral("summer");
    case MumeSeasonEnum::AUTUMN:
        return QStringLiteral("autumn");
    case MumeSeasonEnum::UNKNOWN:
        break;
    }
    return QStringLiteral("unknown");
}

NODISCARD QString moonPhaseName(const MumeMoonPhaseEnum phase)
{
    switch (phase) {
    case MumeMoonPhaseEnum::WAXING_CRESCENT:
        return QStringLiteral("waxing-crescent");
    case MumeMoonPhaseEnum::FIRST_QUARTER:
        return QStringLiteral("first-quarter");
    case MumeMoonPhaseEnum::WAXING_GIBBOUS:
        return QStringLiteral("waxing-gibbous");
    case MumeMoonPhaseEnum::FULL_MOON:
        return QStringLiteral("full");
    case MumeMoonPhaseEnum::WANING_GIBBOUS:
        return QStringLiteral("waning-gibbous");
    case MumeMoonPhaseEnum::THIRD_QUARTER:
        return QStringLiteral("third-quarter");
    case MumeMoonPhaseEnum::WANING_CRESCENT:
        return QStringLiteral("waning-crescent");
    case MumeMoonPhaseEnum::NEW_MOON:
        return QStringLiteral("new");
    case MumeMoonPhaseEnum::UNKNOWN:
        break;
    }
    return QStringLiteral("unknown");
}

NODISCARD QString moonPositionName(const MumeMoonPositionEnum position)
{
    switch (position) {
    case MumeMoonPositionEnum::INVISIBLE:
        return QStringLiteral("below");
    case MumeMoonPositionEnum::EAST:
        return QStringLiteral("east");
    case MumeMoonPositionEnum::SOUTHEAST:
        return QStringLiteral("southeast");
    case MumeMoonPositionEnum::SOUTH:
        return QStringLiteral("south");
    case MumeMoonPositionEnum::SOUTHWEST:
        return QStringLiteral("southwest");
    case MumeMoonPositionEnum::WEST:
        return QStringLiteral("west");
    case MumeMoonPositionEnum::UNKNOWN:
        break;
    }
    return QStringLiteral("unknown");
}

NODISCARD QString moonVisibilityName(const MumeMoonVisibilityEnum visibility)
{
    switch (visibility) {
    case MumeMoonVisibilityEnum::INVISIBLE:
        return QStringLiteral("invisible");
    case MumeMoonVisibilityEnum::DIM:
        return QStringLiteral("dim");
    case MumeMoonVisibilityEnum::BRIGHT:
        return QStringLiteral("bright");
    case MumeMoonVisibilityEnum::UNKNOWN:
        break;
    }
    return QStringLiteral("unknown");
}

NODISCARD QJsonObject toJson(const XmlElement &element)
{
    QJsonObject obj;
    obj["tag"] = mmqt::toQStringUtf8(element.name);
    obj["category"] = mmqt::toQStringUtf8(to_string_view(toXmlCategory(element.tag)));
    obj["text"] = element.text;

    if (!element.attributes.empty()) {
        QJsonObject attributes;
        for (const auto &attribute : element.attributes) {
            attributes[mmqt::toQStringUtf8(attribute.first)] = mmqt::toQStringUtf8(attribute.second);
        }
        obj["attributes"] = attributes;
    }

    if (!element.children.empty()) {
        QJsonArray children;
        for (const XmlElement &child : element.children) {
            children.append(toJson(child));
        }
        obj["children"] = children;
    }

    // Only stated when true, so that the ordinary case stays quiet on the wire.
    if (element.truncated) {
        obj["truncated"] = true;
    }

    // MMapper's reading of a movement line: where someone came from (move_in) or went
    // (move_out), or MUME's own `dir` for the player's movement. Kept out of `attributes`,
    // which stay exactly what MUME sent, and omitted when there is no direction to state.
    if (!element.direction.empty()) {
        obj["direction"] = mmqt::toQStringUtf8(element.direction);
    }
    return obj;
}

NODISCARD GmcpJson toGmcpJson(const QJsonObject &obj)
{
    const QString json = QJsonDocument{obj}.toJson(QJsonDocument::Compact);
    return GmcpJson{json};
}

} // namespace

namespace frontend_messages {

std::string_view toProtocolString(const SendToUserSourceEnum source)
{
    switch (source) {
    case SendToUserSourceEnum::FromMud:
        return "mud";
    case SendToUserSourceEnum::DuplicatePrompt:
        return "duplicate-prompt";
    case SendToUserSourceEnum::SimulatedPrompt:
        return "simulated-prompt";
    case SendToUserSourceEnum::SimulatedOutput:
        return "simulated-output";
    case SendToUserSourceEnum::FromMMapper:
        return "mmapper";
    case SendToUserSourceEnum::NoLongerPrompted:
        return "no-longer-prompted";
    }
    return "unknown";
}

GmcpMessage makeTerminalOutput(const SendToUserSourceEnum source,
                               const QString &text,
                               const bool goAhead)
{
    QJsonObject obj;
    obj["text"] = text;
    obj["format"] = QStringLiteral("ansi");
    obj["encoding"] = QStringLiteral("utf-8");
    obj["source"] = mmqt::toQStringUtf8(toProtocolString(source));
    obj["goAhead"] = goAhead;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TERMINAL_OUTPUT, toGmcpJson(obj)};
}

GmcpMessage makeSessionState(const bool upstreamConnected,
                             const MapIdentity &map,
                             const bool echo,
                             const bool driving,
                             const GameStateEnum game,
                             const QString &viewer)
{
    QJsonObject obj;
    obj["itemCommands"] = 6;
    obj["trade"] = 1;
    obj["viewer"] = viewer;
    obj["upstream"] = upstreamConnected ? QStringLiteral("connected")
                                        : QStringLiteral("disconnected");
    obj["mapLoaded"] = map.rooms != 0;
    obj["mapName"] = map.name;
    obj["mapRooms"] = map.rooms;
    obj["mapGeneration"] = map.generation;
    obj["echo"] = echo;
    obj["role"] = driving ? QStringLiteral("driving") : QStringLiteral("observing");
    // Only in the game while MUME is there at all.
    obj["game"] = mmqt::toQStringUtf8(
        gameStateName(upstreamConnected ? game : GameStateEnum::UNKNOWN));
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_SESSION_STATE, toGmcpJson(obj)};
}

GmcpMessage makeError(const QString &code, const QString &message)
{
    QJsonObject obj;
    obj["code"] = code;
    obj["message"] = message;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_SESSION_ERROR, toGmcpJson(obj)};
}

GmcpMessage makeMapPosition(const RoomHandle &room)
{
    QJsonObject obj;

    // Session-local handle; clients must not persist this.
    obj["id"] = static_cast<qint64>(room.getId().asUint32());
    obj["externalId"] = static_cast<qint64>(room.getIdExternal().asUint32());

    // Omitted rather than zeroed when MUME never supplied one, so that a client can tell
    // "unknown" apart from a legitimate id.
    if (const ServerRoomId serverId = room.getServerId(); serverId != INVALID_SERVER_ROOMID) {
        obj["serverId"] = static_cast<qint64>(serverId.asUint32());
    }

    obj["name"] = room.getName().toQString();
    obj["area"] = room.getArea().toQString();
    obj["terrain"] = mmqt::toQStringUtf8(to_string_view(room.getTerrainType()));

    // The same as the XML export's, so that a frontend can find the room in its own export
    // even when it has no serverId and the export numbers its rooms differently.
    obj["fingerprint"] = room_fingerprint::compute(room);

    const Coordinate &pos = room.getPosition();
    QJsonObject layout;
    layout["kind"] = QStringLiteral("mmapper-grid");
    layout["x"] = pos.x;
    layout["y"] = pos.y;
    layout["z"] = pos.z;
    obj["layout"] = layout;

    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_MAP_POSITION, toGmcpJson(obj)};
}

GmcpMessage makeCombatEvent(const CombatEvent &event)
{
    QJsonObject obj;
    obj["kind"] = mmqt::toQStringUtf8(to_string_view(event.kind));
    obj["text"] = event.text;
    // Quiet on the wire: a field is only sent when the line said something for it.
    const auto put = [&obj](const char *const key, const QString &value) {
        if (!value.isEmpty()) {
            obj[key] = value;
        }
    };
    put("actor", event.actor);
    put("target", event.target);
    if (event.kind == CombatKindEnum::BLOW) {
        obj["outcome"] = mmqt::toQStringUtf8(to_string_view(event.outcome));
    }
    if (event.phase != CombatPhaseEnum::NONE) {
        obj["phase"] = mmqt::toQStringUtf8(to_string_view(event.phase));
    }
    put("verb", event.verb);
    put("part", event.part);
    put("quality", event.quality);
    put("severity", event.severity);
    put("effect", event.effect);
    put("detail", event.detail);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_COMBAT_EVENT, toGmcpJson(obj)};
}

GmcpMessage makeXmlElement(const XmlElement &element)
{
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_XML_ELEMENT, toGmcpJson(toJson(element))};
}

GmcpMessage makeTimeState(const MumeMoment &moment, const MumeClockPrecisionEnum precision)
{
    QJsonObject obj;
    obj["year"] = moment.year;
    obj["month"] = moment.month + 1;
    obj["monthName"] = QString::fromLatin1(
        QMetaEnum::fromType<MumeClock::WestronMonthNamesEnum>().valueToKey(
            static_cast<quint64>(std::max(moment.month, 0))));
    obj["day"] = moment.day + 1;
    obj["weekday"] = QString::fromLatin1(
        QMetaEnum::fromType<MumeClock::WestronWeekDayNamesEnum>().valueToKey(
            static_cast<quint64>(std::max(moment.weekDay(), 0))));
    obj["hour"] = moment.hour;
    obj["minute"] = moment.minute;
    obj["precision"] = precisionName(precision);
    // One MUME minute passes each real second (MumeClock::slot_tick).
    obj["secondsPerHour"] = MUME_MINUTES_PER_HOUR;
    obj["season"] = seasonName(moment.toSeason());

    const MumeClock::DawnDusk dawnDusk = MumeClock::getDawnDusk(moment.month);
    obj["dawnHour"] = dawnDusk.dawnHour;
    obj["duskHour"] = dawnDusk.duskHour;
    obj["phase"] = timeOfDayName(moment.toTimeOfDay());

    QJsonObject moon;
    moon["phase"] = moonPhaseName(moment.moonPhase());
    moon["level"] = moment.moonLevel();
    moon["waxing"] = moment.isMoonWaxing();
    moon["position"] = moonPositionName(moment.moonPosition());
    moon["visibility"] = moonVisibilityName(moment.moonVisibility());
    // When in the day the moon is highest, as a minute of the day: MMapper's model has it
    // rise six hours before and set six hours after, which is what lets a renderer move it
    // smoothly instead of in `position`'s five steps.
    moon["zenithMinute"] = moment.moonZenithMinutes();
    obj["moon"] = moon;

    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_TIME_STATE, toGmcpJson(obj)};
}

GmcpMessage makeWeatherEvent(const WeatherLine &line)
{
    QJsonObject obj;
    obj["kind"] = mmqt::toQStringUtf8(to_string_view(line.kind));
    if (!line.level.empty()) {
        obj["level"] = mmqt::toQStringUtf8(line.level);
        if (line.changing) {
            obj["changing"] = true;
        }
    }
    if (!line.direction.empty()) {
        obj["direction"] = mmqt::toQStringUtf8(line.direction);
    }
    if (!line.precipitation.empty()) {
        obj["precipitation"] = mmqt::toQStringUtf8(line.precipitation);
    }
    if (line.magic) {
        obj["magic"] = true;
    }
    obj["text"] = line.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_WEATHER_EVENT, toGmcpJson(obj)};
}

GmcpMessage makeGroundState(const GroundState &state, const RoomHandle *const room)
{
    QJsonObject obj;
    obj["snow"] = mmqt::toQStringUtf8(state.snow);
    obj["frost"] = mmqt::toQStringUtf8(state.frost);
    obj["ice"] = mmqt::toQStringUtf8(state.ice);
    obj["entered"] = state.entered;
    if (room != nullptr && *room) {
        QJsonObject where;
        where["externalId"] = static_cast<qint64>(room->getIdExternal().asUint32());
        if (const ServerRoomId serverId = room->getServerId(); serverId != INVALID_SERVER_ROOMID) {
            where["serverId"] = static_cast<qint64>(serverId.asUint32());
        }
        obj["room"] = where;
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_WEATHER_GROUND, toGmcpJson(obj)};
}

GmcpMessage makeRoomContents(const RoomContentsSnapshot &contents, const RoomHandle *const room)
{
    const auto maybe = [](const std::optional<bool> &value) -> QJsonValue {
        return value.has_value() ? QJsonValue{*value} : QJsonValue{QJsonValue::Null};
    };
    const auto orNull = [](const QString &text) -> QJsonValue {
        return text.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{text};
    };

    QJsonObject obj;
    if (room != nullptr && *room) {
        obj["externalId"] = static_cast<qint64>(room->getIdExternal().asUint32());
        if (const ServerRoomId serverId = room->getServerId(); serverId != INVALID_SERVER_ROOMID) {
            obj["serverId"] = static_cast<qint64>(serverId.asUint32());
        }
    }
    if (!contents.snapshotId.isEmpty()) {
        obj["snapshotId"] = contents.snapshotId;
        obj["roomKey"] = contents.roomKey;
    }
    obj["seen"] = contents.seen;
    obj["entered"] = contents.entered;

    QJsonArray objects;
    for (const RoomObject &object : contents.objects) {
        QJsonObject entry;
        entry["index"] = object.index;
        if (!contents.snapshotId.isEmpty()) {
            entry["handle"] = contents.snapshotId + QLatin1Char(':')
                              + QString::number(object.index);
            entry["selector"] = QJsonValue::Null;
            entry["targetEvidence"] = QStringLiteral("display-order-unverified");
        }
        entry["line"] = object.line;
        entry["name"] = orNull(object.name);
        entry["count"] = object.count;
        entry["container"] = object.container;
        entry["keyword"] = orNull(object.keyword);
        entry["target"] = orNull(object.target);
        QJsonObject state;
        state["open"] = maybe(object.state.open);
        state["locked"] = maybe(object.state.locked);
        state["pickproof"] = maybe(object.state.pickproof);
        state["empty"] = maybe(object.state.empty);
        state["known"] = static_cast<qint64>(object.state.known);
        entry["state"] = state;
        objects.append(entry);
    }
    obj["objects"] = objects;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ROOM_CONTENTS, toGmcpJson(obj)};
}

GmcpMessage makeContainerEvent(const ContainerEvent &event)
{
    QJsonObject obj;
    obj["target"] = event.command.target;
    obj["action"] = mmqt::toQStringUtf8(to_string_view(event.command.action));
    obj["result"] = mmqt::toQStringUtf8(to_string_view(event.result));
    if (!event.items.empty()) {
        QJsonArray items;
        for (const ContainerItem &item : event.items) {
            QJsonObject entry;
            entry["name"] = item.name;
            entry["count"] = item.count;
            entry["text"] = item.text;
            items.append(entry);
        }
        obj["items"] = items;
    }
    if (event.index >= 0) {
        obj["index"] = event.index;
    }
    obj["text"] = event.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ROOM_CONTAINER, toGmcpJson(obj)};
}

namespace {

NODISCARD QJsonArray listedItems(const std::vector<ListedItem> &items,
                                 const bool equipment,
                                 const QString &snapshotId)
{
    QJsonArray result;
    int index = 0;
    for (const ListedItem &item : items) {
        QJsonObject entry;
        if (!snapshotId.isEmpty()) {
            entry["handle"] = snapshotId + QLatin1Char(':') + QString::number(index);
            entry["index"] = index;
            entry["selector"] = QJsonValue::Null;
        }
        ++index;
        if (equipment) {
            entry["slot"] = item.slot;
            entry["label"] = item.label;
            if (item.twoHanded) {
                entry["twoHanded"] = true;
            }
        }
        entry["name"] = item.name;
        entry["count"] = item.count;
        entry["condition"] = item.condition.isEmpty() ? QJsonValue{QJsonValue::Null}
                                                      : QJsonValue{item.condition};
        entry["flags"] = QJsonArray::fromStringList(item.flags);
        entry["text"] = item.text;
        result.append(entry);
    }
    return result;
}

} // namespace

GmcpMessage makeCharEquipment(const ItemBlock &block)
{
    QJsonObject obj;
    if (!block.snapshotId.isEmpty()) {
        obj["snapshotId"] = block.snapshotId;
        obj["text"] = block.text;
    }
    obj["owner"] = block.owner;
    obj["items"] = listedItems(block.items, true, block.snapshotId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_EQUIPMENT, toGmcpJson(obj)};
}

GmcpMessage makeCharInventory(const ItemBlock &block)
{
    QJsonObject obj;
    if (!block.snapshotId.isEmpty()) {
        obj["snapshotId"] = block.snapshotId;
        obj["text"] = block.text;
    }
    obj["owner"] = block.owner.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{block.owner};
    obj["peek"] = block.peek;
    obj["items"] = listedItems(block.items, false, block.snapshotId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_INVENTORY, toGmcpJson(obj)};
}

GmcpMessage makeCharContainer(const ItemBlock &block)
{
    QJsonObject obj;
    if (!block.snapshotId.isEmpty()) {
        obj["snapshotId"] = block.snapshotId;
        obj["text"] = block.text;
    }
    if (!block.target.isEmpty()) {
        obj["target"] = block.target;
    }
    obj["listingMode"] = block.listingMode;
    obj["contentsKnown"] = !block.closed;
    obj["keyword"] = block.keyword;
    obj["where"] = block.where.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{block.where};
    obj["closed"] = block.closed;
    obj["items"] = listedItems(block.items, false, block.snapshotId);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_CONTAINER, toGmcpJson(obj)};
}

GmcpMessage makeCharItem(const ItemEvent &event)
{
    QJsonObject obj;
    obj["action"] = mmqt::toQStringUtf8(to_string_view(event.action));
    const auto optional = [&obj](const char *const key, const QString &value) {
        if (!value.isEmpty()) {
            obj[key] = value;
        }
    };
    optional("item", event.item);
    optional("container", event.container);
    optional("place", event.place);
    optional("slot", event.slot);
    optional("other", event.other);
    optional("reason", event.reason);
    obj["text"] = event.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_ITEM, toGmcpJson(obj)};
}

namespace {

void putNumber(QJsonObject &obj, const char *const key, const std::optional<int64_t> &value)
{
    if (value.has_value()) {
        obj[key] = static_cast<qint64>(*value);
    }
}

void putText(QJsonObject &obj, const char *const key, const QString &value)
{
    if (!value.isEmpty()) {
        obj[key] = value;
    }
}

} // namespace

GmcpMessage makeAccountMenu(const AccountMenu &menu)
{
    QJsonArray commands;
    for (const AccountMenuCommand &command : menu.commands) {
        QJsonObject c;
        c["name"] = command.name;
        c["usage"] = command.usage;
        putText(c, "help", command.help);
        commands.append(c);
    }
    QJsonObject obj;
    obj["commands"] = commands;
    if (!menu.sorts.isEmpty()) {
        obj["sorts"] = QJsonArray::fromStringList(menu.sorts);
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ACCOUNT_MENU, toGmcpJson(obj)};
}

GmcpMessage makeAccountChars(const AccountChars &chars)
{
    QJsonArray rows;
    for (const AccountChar &row : chars.chars) {
        QJsonObject c;
        c["name"] = row.name;
        putText(c, "race", row.race);
        c["lvl"] = row.lvl;
        putText(c, "class", row.cls);
        putNumber(c, "level", row.level);
        putText(c, "logon", row.logon);
        c["playing"] = row.playing;
        putText(c, "area", row.area);
        putText(c, "rent", row.rent);
        putText(c, "delete", row.deletion);
        rows.append(c);
    }
    QJsonObject obj;
    putText(obj, "account", chars.account);
    obj["chars"] = rows;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ACCOUNT_CHARS, toGmcpJson(obj)};
}

GmcpMessage makeAccountReply(const AccountReply &reply)
{
    QJsonObject obj;
    obj["kind"] = QString::fromUtf8(accountReplyKindName(reply.kind));
    putNumber(obj, "seconds", reply.seconds);
    putText(obj, "command", reply.command);
    obj["text"] = reply.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ACCOUNT_REPLY, toGmcpJson(obj)};
}

GmcpMessage makeCharStat(const CharStat &stat)
{
    QJsonObject obj;
    putNumber(obj, "ob", stat.ob);
    putNumber(obj, "db", stat.db);
    putNumber(obj, "pb", stat.pb);
    putNumber(obj, "armour", stat.armour);
    putNumber(obj, "wimpy", stat.wimpy);
    putText(obj, "mood", stat.mood);
    putText(obj, "alert", stat.alert);
    putNumber(obj, "neededXp", stat.neededXp);
    putNumber(obj, "neededTp", stat.neededTp);
    putNumber(obj, "gold", stat.gold);
    putNumber(obj, "wp", stat.wp);
    if (!stat.condition.isEmpty()) {
        obj["condition"] = QJsonArray::fromStringList(stat.condition);
    }
    obj["affects"] = QJsonArray::fromStringList(stat.affects);
    obj["wounds"] = QJsonArray::fromStringList(stat.wounds);
    obj["text"] = stat.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_STAT, toGmcpJson(obj)};
}

GmcpMessage makeCharScore(const CharScore &score)
{
    static const std::array<const char *, 7>
        abilityKeys{"str", "int", "wis", "dex", "con", "wil", "per"};
    QJsonObject obj;
    putText(obj, "reply", score.reply);
    QJsonObject abilities;
    for (size_t i = 0; i < abilityKeys.size(); ++i) {
        putNumber(abilities, abilityKeys[i], score.abilities[i]);
    }
    if (!abilities.isEmpty()) {
        obj["abilities"] = abilities;
    }
    putNumber(obj, "ob", score.ob);
    putNumber(obj, "db", score.db);
    putNumber(obj, "pb", score.pb);
    putNumber(obj, "armour", score.armour);
    putNumber(obj, "hp", score.hp);
    putNumber(obj, "maxhp", score.maxhp);
    putNumber(obj, "mana", score.mana);
    putNumber(obj, "maxmana", score.maxmana);
    putNumber(obj, "mp", score.mp);
    putNumber(obj, "maxmp", score.maxmp);
    putText(obj, "mood", score.mood);
    putNumber(obj, "wimpy", score.wimpy);
    putNumber(obj, "xp", score.xp);
    putNumber(obj, "tp", score.tp);
    putText(obj, "renown", score.renown);
    putNumber(obj, "wp", score.wp);
    putNumber(obj, "neededXp", score.neededXp);
    putNumber(obj, "neededTp", score.neededTp);
    putNumber(obj, "gold", score.gold);
    putNumber(obj, "silver", score.silver);
    putNumber(obj, "copper", score.copper);
    putText(obj, "language", score.language);
    putText(obj, "swim", score.swim);
    putText(obj, "climb", score.climb);
    if (score.effectsKnown) {
        obj["effects"] = QJsonArray::fromStringList(score.effects);
        obj["wounds"] = QJsonArray::fromStringList(score.wounds);
    }
    obj["text"] = score.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_SCORE, toGmcpJson(obj)};
}

GmcpMessage makeCharBurden(const CharBurden &burden)
{
    QJsonObject obj;
    obj["pounds"] = static_cast<qint64>(burden.pounds);
    putText(obj, "word", burden.word);
    obj["text"] = burden.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_BURDEN, toGmcpJson(obj)};
}

GmcpMessage makeCharLevel(const CharLevel &level)
{
    QJsonObject obj;
    obj["level"] = static_cast<qint64>(level.level);
    putNumber(obj, "xp", level.xp);
    putNumber(obj, "neededXp", level.neededXp);
    putNumber(obj, "tp", level.tp);
    putNumber(obj, "neededTp", level.neededTp);
    obj["text"] = level.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_LEVEL, toGmcpJson(obj)};
}

GmcpMessage makeCharCommand(const ItemCommandObservation &command)
{
    QJsonObject obj;
    obj["id"] = command.id;
    obj["command"] = command.command;
    obj["action"] = command.action;
    obj["status"] = command.status;
    obj["successes"] = command.successes;
    obj["refusals"] = command.refusals;
    QJsonArray replies;
    for (const auto &reply : command.replies) {
        QJsonObject evidence;
        evidence["action"] = mmqt::toQStringUtf8(to_string_view(reply.action));
        evidence["item"] = reply.item;
        evidence["container"] = reply.container;
        evidence["other"] = reply.other;
        evidence["slot"] = reply.slot;
        evidence["reason"] = reply.reason;
        evidence["text"] = reply.text;
        replies.append(evidence);
    }
    obj["replies"] = replies;
    obj["text"] = QJsonArray::fromStringList(command.text);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_CHAR_COMMAND, toGmcpJson(obj)};
}

} // namespace frontend_messages
