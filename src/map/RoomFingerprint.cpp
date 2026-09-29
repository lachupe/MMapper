// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "RoomFingerprint.h"

#include "../global/TextUtils.h"
#include "ExitDirection.h"
#include "RoomHandle.h"
#include "roomid.h"

#include <algorithm>
#include <optional>
#include <utility>

#include <QCryptographicHash>
#include <QStringList>

namespace room_fingerprint {

namespace {

NODISCARD char letterFor(const ExitDirEnum dir)
{
    switch (dir) {
    case ExitDirEnum::NORTH:
        return 'N';
    case ExitDirEnum::SOUTH:
        return 'S';
    case ExitDirEnum::EAST:
        return 'E';
    case ExitDirEnum::WEST:
        return 'W';
    case ExitDirEnum::UP:
        return 'U';
    case ExitDirEnum::DOWN:
        return 'D';
    case ExitDirEnum::UNKNOWN:
    case ExitDirEnum::NONE:
        break;
    }
    return '?';
}

} // namespace

QString terrainWord(const RoomTerrainEnum terrain)
{
    // The XML export never writes UNDEFINED: a room without a terrain has no <terrain> at all.
    if (terrain == RoomTerrainEnum::UNDEFINED) {
        return QString{};
    }
    return mmqt::toQStringUtf8(to_string_view(terrain));
}

Fields fieldsOf(const RoomHandle &room)
{
    Fields fields;
    fields.name = room.getName().toQString();
    fields.description = room.getDescription().toQString();
    fields.terrain = terrainWord(room.getTerrainType());

    const Map map = room.getMap();
    for (const ExitDirEnum dir : ALL_EXITS_NESWUD) {
        const RawExit &exit = room.getExit(dir);
        if (!exit.exitIsExit()) {
            continue;
        }
        Exit entry;
        entry.letter = letterFor(dir);
        // The target with the lowest externalId, which is the first <to> the XML export writes;
        // the session-local ids here need not be in the same order.
        std::optional<ExternalRoomId> lowest;
        for (const RoomId to : exit.getOutgoingSet()) {
            if (const RoomHandle target = map.findRoomHandle(to)) {
                const ExternalRoomId id = target.getIdExternal();
                if (!lowest.has_value() || id < *lowest) {
                    lowest = id;
                    entry.target = target.getName().toQString();
                }
            }
        }
        fields.exits.push_back(std::move(entry));
    }
    return fields;
}

QByteArray canonicalText(const Fields &fields)
{
    std::vector<Exit> exits = fields.exits;
    std::stable_sort(exits.begin(), exits.end(), [](const Exit &a, const Exit &b) {
        return a.letter < b.letter;
    });

    QStringList letters;
    QStringList targets;
    for (const Exit &exit : exits) {
        letters.append(QString{QLatin1Char(exit.letter)});
        targets.append(exit.target);
    }

    const QLatin1Char newline{'\n'};
    const QString text = fields.name + newline + fields.description + newline + fields.terrain
                         + newline + letters.join(QLatin1Char(',')) + newline
                         + targets.join(newline);
    return text.toUtf8();
}

QString compute(const Fields &fields)
{
    const QByteArray digest = QCryptographicHash::hash(canonicalText(fields),
                                                       QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().left(DIGITS));
}

QString compute(const RoomHandle &room)
{
    return compute(fieldsOf(room));
}

} // namespace room_fingerprint
