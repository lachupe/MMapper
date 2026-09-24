// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendMessages.h"

#include "../global/TextUtils.h"
#include "../map/coordinate.h"
#include "../map/mmapper2room.h"
#include "../map/roomid.h"

#include <algorithm>

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
                             const bool mapLoaded,
                             const bool echo,
                             const bool driving)
{
    QJsonObject obj;
    obj["upstream"] = upstreamConnected ? QStringLiteral("connected")
                                        : QStringLiteral("disconnected");
    obj["mapLoaded"] = mapLoaded;
    obj["echo"] = echo;
    obj["role"] = driving ? QStringLiteral("driving") : QStringLiteral("observing");
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

} // namespace frontend_messages
