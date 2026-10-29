// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendMessages.h"

#include "FrontendJson.h"

#include "../global/TextUtils.h"
#include "../global/parserutils.h"
#include "../map/ExitDirection.h"
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

using frontend_json::maybeBool;
using frontend_json::orNull;
using frontend_json::putNumber;
using frontend_json::putRoomRef;
using frontend_json::putText;
using frontend_json::roomRef;
using frontend_json::toGmcpJson;

namespace {

/// What the map says of riding into a room: true, false, or null where it does not say.
NODISCARD QJsonValue ridableValue(const RoomRidableEnum ridable)
{
    switch (ridable) {
    case RoomRidableEnum::RIDABLE:
        return true;
    case RoomRidableEnum::NOT_RIDABLE:
        return false;
    case RoomRidableEnum::UNDEFINED:
        break;
    }
    return QJsonValue::Null;
}


} // namespace

namespace frontend_messages {

GmcpMessage makeMapPosition(const RoomHandle &room)
{
    QJsonObject obj;

    // Session-local handle; clients must not persist this.
    obj["id"] = static_cast<qint64>(room.getId().asUint32());
    putRoomRef(obj, room);

    obj["name"] = room.getName().toQString();
    obj["area"] = room.getArea().toQString();
    obj["terrain"] = mmqt::toQStringUtf8(to_string_view(room.getTerrainType()));

    // Whether a mount may go there, for this room and for the room each exit leads to: MUME
    // refuses the ride only once the move is made ("Oops! You cannot go there riding!"), and
    // the map knows beforehand. Null where the map does not say. No door is named here.
    obj["ridable"] = ridableValue(room.getRidableType());
    QJsonObject exits;
    const Map map = room.getMap();
    for (const ExitDirEnum dir : ALL_EXITS_NESWUD) {
        const RawExit &exit = room.getExit(dir);
        if (!exit.exitIsExit()) {
            continue;
        }
        // The target with the lowest externalId, as the fingerprint takes it.
        std::optional<RoomHandle> target;
        for (const RoomId to : exit.getOutgoingSet()) {
            if (RoomHandle candidate = map.findRoomHandle(to)) {
                if (!target.has_value() || candidate.getIdExternal() < target->getIdExternal()) {
                    target.emplace(std::move(candidate));
                }
            }
        }
        if (!target.has_value()) {
            continue;
        }
        QJsonObject to = roomRef(*target);
        to["ridable"] = ridableValue(target->getRidableType());
        exits[QString(QLatin1Char(lowercaseDirection(dir)[0]))] = to;
    }
    obj["exits"] = exits;

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

GmcpMessage makeRoomContents(const RoomContentsSnapshot &contents, const RoomHandle *const room)
{

    QJsonObject obj;
    if (room != nullptr && *room) {
        putRoomRef(obj, *room);
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
        state["open"] = maybeBool(object.state.open);
        state["locked"] = maybeBool(object.state.locked);
        state["pickproof"] = maybeBool(object.state.pickproof);
        state["empty"] = maybeBool(object.state.empty);
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

GmcpMessage makeRoomHandRaised(const HandRaised &hand)
{
    QJsonObject obj;
    obj["name"] = hand.name;
    obj["keyword"] = hand.keyword;
    obj["text"] = hand.text;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ROOM_HAND_RAISED, toGmcpJson(obj)};
}

GmcpMessage makeRoomDoor(const RoomDoors &doors)
{
    QJsonArray rows;
    for (const RoomDoor &door : doors.doors) {
        QJsonObject row;
        putText(row, "dir", door.dir);
        putText(row, "name", door.name);
        row["state"] = mmqt::toQStringUtf8(to_string_view(door.state));
        row["since"] = static_cast<qint64>(door.since);
        putText(row, "by", door.by);
        rows.append(row);
    }
    QJsonObject obj;
    putNumber(obj, "room", doors.room);
    obj["doors"] = rows;
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ROOM_DOOR, toGmcpJson(obj)};
}

GmcpMessage makeRoomLook(const ExitLook &look)
{
    QJsonObject obj;
    putNumber(obj, "room", look.room);
    obj["dir"] = look.dir;
    obj["command"] = look.command;
    obj["kind"] = mmqt::toQStringUtf8(to_string_view(look.kind));
    obj["text"] = look.text;
    obj["lines"] = QJsonArray::fromStringList(look.lines);
    if (!look.door.isEmpty()) {
        QJsonObject door;
        door["name"] = look.door;
        putText(door, "state", look.doorState);
        obj["door"] = door;
    }
    obj["uncertain"] = look.uncertain;
    obj["reasons"] = QJsonArray::fromStringList(look.reasons);
    obj["dropped"] = QJsonArray::fromStringList(look.dropped);
    obj["raw"] = QJsonArray::fromStringList(look.raw);
    return GmcpMessage{GmcpMessageTypeEnum::MMAPPER_ROOM_LOOK, toGmcpJson(obj)};
}

} // namespace frontend_messages
