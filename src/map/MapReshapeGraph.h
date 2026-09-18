#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "Map.h"
#include "MapReshapeTypes.h"
#include "RoomIdSet.h"
#include "coordinate.h"
#include "mmapper2room.h"
#include "roomid.h"

#include <cstddef>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <vector>

/// Converts MMapper map data into the structures the layout solver works on.
///
/// This layer performs no optimization and mutates nothing: it copies
/// coordinates out of the map and hands the solver a self-contained snapshot.
/// Only after a solve succeeds does anything get applied back to the map.
class NODISCARD MapReshapeGraph final
{
private:
    std::vector<LayoutRoom> m_rooms;
    std::unordered_map<RoomId, size_t> m_indexById;

    std::vector<LayoutEdge> m_horizontalEdges;
    /// UP/DOWN exits in scope. Collected but not turned into constraints yet:
    /// MUME's up/down does not imply a fixed z delta, so the solver must not
    /// assume one. Kept so Z optimization can use them later.
    std::vector<LayoutEdge> m_verticalEdges;
    std::vector<LayoutUnsupportedExit> m_unsupportedExits;

    /// Every occupied cell in and around the scope, including rooms that are
    /// not part of the scope at all. Fixed occupants are authoritative: a
    /// candidate placement that lands on one must be rejected.
    std::unordered_map<Coordinate, RoomId> m_occupancy;

    ReshapeOptions m_options;

public:
    MapReshapeGraph() = default;

public:
    /// Build a scope around the given core rooms.
    ///
    /// Core rooms come either from an MMapper area or from an explicit
    /// selection; both reduce to a RoomIdSet so the two workflows share one
    /// solver. Rooms within options.marginRings graph hops become movable
    /// margin; the next ring out, plus anything occupying nearby space,
    /// becomes fixed.
    NODISCARD static MapReshapeGraph build(const Map &map,
                                           const RoomIdSet &coreRooms,
                                           const ReshapeOptions &options);

    /// Core is every room within `radius` graph steps of `origin`.
    ///
    /// The way in for maps whose rooms all sit in one enormous area, or none
    /// at all: reshaping such a map by area would mean reshaping the whole
    /// world at once, which is neither fast nor reviewable.
    NODISCARD static RoomIdSet collectWithinRadius(const Map &map, RoomId origin, int radius);

    /// Every room of the named area, empty when there is no such area.
    ///
    /// Shared so the command and the menu action gather rooms identically;
    /// the two had separate copies of this and could disagree about what an
    /// area contains.
    NODISCARD static RoomIdSet collectArea(const Map &map, const RoomArea &area);

    /// Convenience overload: core is every room of the named area.
    /// Returns nullopt when the area does not exist or is empty.
    NODISCARD static std::optional<MapReshapeGraph> buildForArea(const Map &map,
                                                                 const RoomArea &area,
                                                                 const ReshapeOptions &options);

public:
    NODISCARD const std::vector<LayoutRoom> &getRooms() const { return m_rooms; }
    NODISCARD const std::vector<LayoutEdge> &getHorizontalEdges() const
    {
        return m_horizontalEdges;
    }
    NODISCARD const std::vector<LayoutEdge> &getVerticalEdges() const { return m_verticalEdges; }
    NODISCARD const std::vector<LayoutUnsupportedExit> &getUnsupportedExits() const
    {
        return m_unsupportedExits;
    }
    NODISCARD const std::unordered_map<Coordinate, RoomId> &getOccupancy() const
    {
        return m_occupancy;
    }
    NODISCARD const ReshapeOptions &getOptions() const { return m_options; }

public:
    NODISCARD bool empty() const { return m_rooms.empty(); }
    NODISCARD size_t size() const { return m_rooms.size(); }

    /// Index into getRooms(), or nullopt when the room is out of scope.
    NODISCARD std::optional<size_t> findIndex(RoomId id) const;
    NODISCARD const LayoutRoom &getRoom(size_t index) const;

    /// The room occupying a cell, whether or not it is in scope.
    NODISCARD std::optional<RoomId> findOccupant(const Coordinate &coord) const;

public:
    NODISCARD ReshapeStatistics computeStatistics() const;
    /// Distinct z values used by rooms in scope.
    NODISCARD size_t countZLevels() const;
    /// Pairs of in-scope rooms sharing a coordinate. MMapper tolerates
    /// overlaps, so a scope can already contain some before any solving.
    NODISCARD std::vector<ReshapeConflict> findCollisions() const;

public:
    /// Human-readable dump for `_map area reshape`-style diagnostics.
    void printDiagnostics(std::ostream &os) const;
};
