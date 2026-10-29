// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/TextUtils.h"
#include "../global/parserutils.h"
#include "../map/ExitDirection.h"
#include "../map/RoomFingerprint.h"
#include "../map/coordinate.h"
#include "../map/mmapper2room.h"
#include "../map/roomid.h"
#include "FrontendJson.h"
#include "FrontendMessages.h"

#include <algorithm>
#include <array>
#include <optional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>

using frontend_json::maybeBool;
using frontend_json::orNull;
using frontend_json::putNumber;
using frontend_json::putRoomRef;
using frontend_json::putText;
using frontend_json::roomRef;
using frontend_json::toGmcpJson;

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

} // namespace

namespace frontend_messages {

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
        obj["room"] = roomRef(*room);
    }
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_WEATHER_GROUND, toGmcpJson(obj)};
}

} // namespace frontend_messages
