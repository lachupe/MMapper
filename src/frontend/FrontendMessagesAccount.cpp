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

namespace frontend_messages {

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
        putText(c, "sub", row.sub);
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
    // MUME's pager waits: the list goes on when the player answers it. Absent otherwise.
    if (chars.more) {
        obj["more"] = true;
        putNumber(obj, "percent", chars.percent);
    }
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

} // namespace frontend_messages
