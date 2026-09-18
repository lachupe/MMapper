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

/// What the reshaper is trying to achieve on the z axis.
///
/// These are not two algorithms, only two weightings of the same one. The
/// distinction matters because MUME maps are drawn by people who already
/// flatten small interiors by hand for readability: a stair landing with
/// four rooms is routinely laid out side by side on one layer rather than
/// stacked, precisely so it can be seen. A reshaper that treats every such
/// place as a mistake to be re-stacked would undo deliberate work.
enum class NODISCARD ReshapeModeEnum : uint8_t {
    /// Collapse onto as few layers as possible, and let up/down exits be
    /// drawn as ordinary horizontal neighbours. The readable default.
    Flatten,
    /// Keep and honour real verticality: up exits should genuinely lead
    /// upwards, and extra layers cost nothing.
    Volumetric
};

NODISCARD extern std::string_view to_string_view(ReshapeModeEnum mode);

/// Relative cost of everything the layout can get wrong.
///
/// The ordering matters more than the absolute values. The governing
/// relationship is that reasonable x/y distortion must always cost less than
/// unnecessary z separation, so that a layout which can be fixed by stretching
/// horizontally is never "fixed" by moving a room to another layer instead.
/// Keep the numbers here rather than scattering constants through the solver.
struct NODISCARD ReshapeWeights final
{
    /// Two rooms on one cell. Effectively forbidden rather than merely costly.
    int64_t collision = 1'000'000;
    /// Occupying a z layer beyond the first. Extremely expensive: extra layers
    /// are the thing the reshaper exists to avoid.
    int64_t extraZLayer = 100'000;
    /// Per cell of z displacement from a room's original layer.
    int64_t zMovement = 10'000;
    /// Per layer a horizontal exit jumps. Tracked apart from zMovement: "this
    /// area spans two layers" and "this east exit changes layer" are different
    /// defects with different fixes, and diagnostics should not conflate them.
    int64_t layerMismatch = 10'000;
    /// An exit pointing the wrong way, e.g. an east exit whose target is west.
    int64_t wrongDirection = 5'000;
    /// Per cell a NESW exit sits off its axis: a north exit wants equal x.
    int64_t alignment = 500;
    /// Per cell of x/y displacement, multiplied by a margin room's distance
    /// from the core, so resistance grows with distance.
    int64_t marginMovementBase = 50;
    /// Per cell an interior exit is longer than one.
    int64_t edgeLength = 10;
    /// Per cell a boundary-crossing exit is longer than one. Deliberately
    /// cheaper: letting these stretch is what makes area reshaping possible
    /// when the surrounding world is fixed.
    int64_t boundaryEdgeLength = 2;
    /// Per cell of the scope's bounding-box perimeter.
    int64_t compactness = 1;
    /// Per cell of x/y displacement for a core room. Cheap by design: moving
    /// core rooms is the whole point.
    int64_t coreMovement = 1;
    /// An up exit whose target is not above it, or a down exit whose target
    /// is not below. Graded by how wrong it is, like horizontal direction.
    ///
    /// Zero when flattening: there, drawing a staircase as two adjacent
    /// rooms on one layer is the desired answer, not a defect.
    int64_t verticalDirection = 0;

    /// Moving a room that must not move. A bug guard, not a trade-off.
    int64_t immovableMoved = 1'000'000;
};

/// Weights for a mode. Individual fields can still be overridden afterwards;
/// the mode only picks a sensible starting point.
NODISCARD extern ReshapeWeights makeWeights(ReshapeModeEnum mode);

/// Per-component score breakdown. Kept split so diagnostics can say *why* a
/// layout scores badly, and so weights can be tuned against real areas.
struct NODISCARD LayoutScore final
{
    int64_t collision = 0;
    int64_t zLayers = 0;
    int64_t zMovement = 0;
    int64_t layerMismatch = 0;
    int64_t verticalDirection = 0;
    int64_t direction = 0;
    int64_t alignment = 0;
    int64_t movement = 0;
    int64_t edgeLength = 0;
    int64_t boundary = 0;
    int64_t compactness = 0;
    int64_t immovableMoved = 0;

    NODISCARD int64_t total() const
    {
        return collision + zLayers + zMovement + layerMismatch + verticalDirection + direction
               + alignment + movement + edgeLength + boundary + compactness + immovableMoved;
    }
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

    /// The search stopped because it ran out of budget rather than because
    /// it had nothing left to improve.
    ///
    /// Worth surfacing: a solve that gives up partway leaves some of the
    /// area reshaped and the rest untouched, which looks like a broken
    /// result rather than an unfinished one.
    bool hitLimits = false;
};

struct NODISCARD ReshapeResult final
{
    ReshapeStatusEnum status = ReshapeStatusEnum::Unchanged;

    std::vector<RoomMove> moves;
    std::vector<ReshapeConflict> conflicts;

    ReshapeStatistics stats;

    NODISCARD bool improved() const { return status == ReshapeStatusEnum::Improved; }
};
