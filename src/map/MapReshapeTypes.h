#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "ExitDirection.h"
#include "RoomIdSet.h"
#include "coordinate.h"
#include "roomid.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Layout reshaping treats (x, y, z) as an editable visualization of the room
// graph rather than as authoritative geography. Coordinate is reused as the
// layout coordinate type: it is already the {int x, y, z} the reshaper needs,
// and reusing it keeps conversion out of the solver.
//
// The first solver milestone keeps z fixed, but nothing here encodes that:
// z is carried everywhere so flatness/minimum-layer optimization can be added
// without reshaping these types.

/// How freely the reshaper may move a room.
enum class NODISCARD LayoutRoomRoleEnum : uint8_t {
    /// Selected area or explicit selection. Freely movable.
    Core,
    /// Nearby room pulled in so displacement has somewhere to go.
    /// Movable, but increasingly expensive with marginDistance.
    Margin,
    /// Explicitly held in place by the user.
    Pinned,
    /// Outside the margin. Never moves; acts as a collision obstacle and,
    /// where an exit crosses the boundary, as a direction anchor.
    FixedExternal
};

NODISCARD extern std::string_view to_string_view(LayoutRoomRoleEnum role);

/// Can the solver propose a new position for a room in this role?
NODISCARD inline bool isMovable(const LayoutRoomRoleEnum role)
{
    return role == LayoutRoomRoleEnum::Core || role == LayoutRoomRoleEnum::Margin;
}

struct NODISCARD LayoutRoom final
{
    RoomId id = INVALID_ROOMID;
    Coordinate original;
    Coordinate current;
    LayoutRoomRoleEnum role = LayoutRoomRoleEnum::FixedExternal;
    /// Graph hops from the nearest core room. 0 for core rooms.
    int marginDistance = 0;
};

/// A directional exit between two rooms that are both in scope.
///
/// Compass directions express preferred relative direction, not a mandatory
/// unit displacement: an EAST edge wants xTo > xFrom and yTo close to yFrom,
/// and only weakly wants xTo == xFrom + 1. One graph edge may legitimately
/// render as several grid cells.
struct NODISCARD LayoutEdge final
{
    /// Index into MapReshapeGraph::getRooms(), not a RoomId.
    size_t fromIndex = 0;
    size_t toIndex = 0;
    ExitDirEnum dir = ExitDirEnum::NONE;
    /// True when exactly one endpoint is FixedExternal, so the edge is an
    /// anchor: its length is elastic but its direction still matters.
    bool crossesBoundary = false;
};

/// An exit the layout solver cannot express as a horizontal constraint.
/// Recorded rather than silently dropped, so diagnostics can report what was
/// ignored and later milestones can pick these up.
struct NODISCARD LayoutUnsupportedExit final
{
    RoomId from = INVALID_ROOMID;
    RoomId to = INVALID_ROOMID;
    ExitDirEnum dir = ExitDirEnum::NONE;
    std::string reason;
};

struct NODISCARD ReshapeOptions final
{
    /// Graph rings of neighbours pulled in as movable margin. Ring 0 is the
    /// core itself; marginRings == 1 adds direct neighbours, and so on.
    int marginRings = 1;
    /// Rooms the user has explicitly frozen, even inside the core.
    RoomIdSet pinned;
    /// Extra cells of surrounding geometry scanned for fixed collision
    /// obstacles beyond the scope's bounding box.
    int obstaclePadding = 8;
};

enum class NODISCARD ReshapeStatusEnum : uint8_t {
    Improved,
    Unchanged,
    InfeasibleWithCurrentBoundary
};

NODISCARD extern std::string_view to_string_view(ReshapeStatusEnum status);

struct NODISCARD RoomMove final
{
    RoomId room = INVALID_ROOMID;
    Coordinate from;
    Coordinate to;
};

struct NODISCARD ReshapeConflict final
{
    RoomId roomA = INVALID_ROOMID;
    RoomId roomB = INVALID_ROOMID;
    std::string description;
};

struct NODISCARD ReshapeStatistics final
{
    size_t roomsProcessed = 0;
    size_t coreRooms = 0;
    size_t marginRooms = 0;
    size_t pinnedRooms = 0;
    size_t fixedExternalRooms = 0;
    size_t roomsMoved = 0;

    size_t conflictsBefore = 0;
    size_t conflictsAfter = 0;

    /// Identical before/after while the solver keeps z fixed.
    size_t zLevelsBefore = 0;
    size_t zLevelsAfter = 0;

    int64_t scoreBefore = 0;
    int64_t scoreAfter = 0;

    size_t iterations = 0;
};

struct NODISCARD ReshapeResult final
{
    ReshapeStatusEnum status = ReshapeStatusEnum::Unchanged;

    std::vector<RoomMove> moves;
    std::vector<ReshapeConflict> conflicts;

    ReshapeStatistics stats;

    NODISCARD bool improved() const { return status == ReshapeStatusEnum::Improved; }
};
