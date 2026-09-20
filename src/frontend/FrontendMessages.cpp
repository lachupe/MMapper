// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendMessages.h"

#include "../global/TextUtils.h"
#include "../map/coordinate.h"
#include "../map/mmapper2room.h"
#include "../map/roomid.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

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

} // namespace frontend_messages
