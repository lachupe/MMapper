// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "MapReshapeApply.h"

#include "ChangeTypes.h"
#include "RoomIdSet.h"
#include "coordinate.h"

#include <algorithm>
#include <cstdlib>

namespace map_reshape {

namespace {

/// Somewhere no room can already be, and that no room's destination can
/// reach either.
///
/// Staging above the map's northern edge by the map's own height clears
/// everything that currently exists; adding the largest single displacement
/// in the reshape clears every destination too, so the staging area cannot
/// collide with where rooms are about to land.
NODISCARD Coordinate stagingOffset(const Map &map, const std::vector<RoomMove> &moves)
{
    const auto bounds = map.getBounds();
    const int mapHeight = bounds ? (bounds->max.y - bounds->min.y) : 0;

    int largestShift = 0;
    for (const RoomMove &move : moves) {
        largestShift = std::max(largestShift, std::abs(move.to.y - move.from.y));
    }
    return Coordinate{0, mapHeight + largestShift + 2, 0};
}

} // namespace

ChangeList buildChanges(const Map &map, const std::vector<RoomMove> &moves)
{
    ChangeList changes;
    if (moves.empty()) {
        return changes;
    }

    const Coordinate staging = stagingOffset(map, moves);

    RoomIdSet movers;
    for (const RoomMove &move : moves) {
        movers.insert(move.room);
    }
    changes.add(room_change_types::MoveRelative2{movers, staging});

    for (const RoomMove &move : moves) {
        // Each room is parked at from + staging, so this is what remains to
        // reach its destination.
        changes.add(room_change_types::MoveRelative{move.room, move.to - move.from - staging});
    }
    return changes;
}

} // namespace map_reshape
