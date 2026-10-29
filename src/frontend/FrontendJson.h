// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors
#pragma once

// The JSON helpers every frontend message builder shares (ARCH.29): one place for how a package's payload is
// written, so that FrontendMessages.cpp, TradeMessages.cpp and FrontendReplayCache.cpp no longer carry copies.

#include "../global/TextUtils.h"
#include "../global/macros.h"
#include "../map/RoomHandle.h"
#include "../proxy/GmcpMessage.h"

#include <cstdint>
#include <optional>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace frontend_json {

/// A payload as the compact JSON text MMapper writes on the wire.
NODISCARD inline GmcpJson toGmcpJson(const QJsonDocument &doc)
{
    return GmcpJson{mmqt::toStdStringUtf8(doc.toJson(QJsonDocument::Compact))};
}

NODISCARD inline GmcpJson toGmcpJson(const QJsonObject &obj)
{
    return toGmcpJson(QJsonDocument{obj});
}

/// `key` is written only when the value is there ("quiet on the wire").
inline void putNumber(QJsonObject &obj, const char *const key, const std::optional<int64_t> &value)
{
    if (value.has_value()) {
        obj[key] = static_cast<qint64>(*value);
    }
}

/// `key` is written only when the text is not empty.
inline void putText(QJsonObject &obj, const char *const key, const QString &value)
{
    if (!value.isEmpty()) {
        obj[key] = value;
    }
}

/// The text, or JSON null where it is empty: a key that is always there, with nothing known of it.
NODISCARD inline QJsonValue orNull(const QString &text)
{
    return text.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{text};
}

/// The flag, or JSON null where it is not known.
NODISCARD inline QJsonValue maybeBool(const std::optional<bool> &value)
{
    return value.has_value() ? QJsonValue{*value} : QJsonValue{QJsonValue::Null};
}

/// How every package names a room (ARCH.12): MMapper's id in its map file, always, and MUME's own where the game
/// supplied one -- omitted rather than zeroed, so that a client can tell "unknown" from a legitimate id. The
/// session-local `id` is deliberately not part of it, and the fingerprint is added where a package carries it
/// (MMapper.Map.Position).
///
/// Where the object goes is each package's own and unchanged: its top level for MMapper.Map.Position and
/// MMapper.Room.Contents (putRoomRef()), `room` for MMapper.Weather.Ground, and an exit's entry for the room it
/// leads to. MMapper.Room.Door and MMapper.Room.Look carry a bare `room` number of the parser, not a RoomHandle.
NODISCARD inline QJsonObject roomRef(const RoomHandle &room)
{
    QJsonObject ref;
    ref["externalId"] = static_cast<qint64>(room.getIdExternal().asUint32());
    if (const ServerRoomId serverId = room.getServerId(); serverId != INVALID_SERVER_ROOMID) {
        ref["serverId"] = static_cast<qint64>(serverId.asUint32());
    }
    return ref;
}

/// roomRef(room)'s keys written into `obj`.
inline void putRoomRef(QJsonObject &obj, const RoomHandle &room)
{
    const QJsonObject ref = roomRef(room);
    for (auto it = ref.begin(); it != ref.end(); ++it) {
        obj.insert(it.key(), it.value());
    }
}

} // namespace frontend_json
