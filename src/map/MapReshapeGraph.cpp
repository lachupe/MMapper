// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "MapReshapeGraph.h"

#include "../global/utils.h"
#include "ExitDirection.h"
#include "ExitFlags.h"
#include "RoomHandle.h"
#include "World.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

std::string_view to_string_view(const LayoutRoomRoleEnum role)
{
    switch (role) {
    case LayoutRoomRoleEnum::Core:
        return "core";
    case LayoutRoomRoleEnum::Margin:
        return "margin";
    case LayoutRoomRoleEnum::Pinned:
        return "pinned";
    case LayoutRoomRoleEnum::FixedExternal:
        return "fixed";
    }
    return "unknown";
}

std::string_view to_string_view(const ReshapeStatusEnum status)
{
    switch (status) {
    case ReshapeStatusEnum::Improved:
        return "improved";
    case ReshapeStatusEnum::Unchanged:
        return "unchanged";
    case ReshapeStatusEnum::InfeasibleWithCurrentBoundary:
        return "infeasible-with-current-boundary";
    }
    return "unknown";
}

namespace {

/// Exits whose geometry says nothing reliable about where the target sits.
/// Random exits go somewhere arbitrary; special/unmapped exits have no
/// dependable spatial meaning. Constraining the layout on them would be worse
/// than ignoring them.
NODISCARD const char *classifyUnusableExit(const RawExit &exit)
{
    const ExitFlags flags = exit.getExitFlags();
    if (flags.isRandom()) {
        return "random exit";
    }
    if (flags.isSpecial()) {
        return "special exit";
    }
    if (flags.isUnmapped()) {
        return "unmapped exit";
    }
    return nullptr;
}

/// Every room directly connected to `id` in either direction, over all
/// NESWUD exits. Used only to grow margin rings, so direction does not matter
/// here: what matters is graph proximity to the core.
void forEachGraphNeighbor(const Map &map, const RoomId id, const std::function<void(RoomId)> &fn)
{
    const RoomHandle room = map.findRoomHandle(id);
    if (!room) {
        return;
    }
    for (const ExitDirEnum dir : ALL_EXITS_NESWUD) {
        const RawExit &exit = room.getExit(dir);
        for (const RoomId other : exit.getOutgoingSet()) {
            fn(other);
        }
        for (const RoomId other : exit.getIncomingSet()) {
            fn(other);
        }
    }
}

} // namespace

std::optional<size_t> MapReshapeGraph::findIndex(const RoomId id) const
{
    const auto it = m_indexById.find(id);
    if (it == m_indexById.end()) {
        return std::nullopt;
    }
    return it->second;
}

const LayoutRoom &MapReshapeGraph::getRoom(const size_t index) const
{
    if (index >= m_rooms.size()) {
        throw std::out_of_range("MapReshapeGraph::getRoom: index out of range");
    }
    return m_rooms[index];
}

std::optional<RoomId> MapReshapeGraph::findOccupant(const Coordinate &coord) const
{
    const auto it = m_occupancy.find(coord);
    if (it == m_occupancy.end()) {
        return std::nullopt;
    }
    return it->second;
}

MapReshapeGraph MapReshapeGraph::build(const Map &map,
                                       const RoomIdSet &coreRooms,
                                       const ReshapeOptions &options)
{
    MapReshapeGraph graph;
    graph.m_options = options;

    // Ring 0 is the core. Rings 1..marginRings are movable margin. The ring
    // beyond that is kept as FixedExternal so cross-boundary exits still have
    // a direction anchor to point at; anything further away is not collected
    // as a room at all, though it can still show up as a collision obstacle.
    const int lastCollectedRing = std::max(0, options.marginRings) + 1;

    std::unordered_map<RoomId, int> ringOf;
    std::deque<RoomId> queue;

    for (const RoomId id : coreRooms) {
        if (!map.findRoomHandle(id)) {
            continue; // stale selection; silently skip missing rooms
        }
        if (ringOf.emplace(id, 0).second) {
            queue.push_back(id);
        }
    }

    while (!queue.empty()) {
        const RoomId current = queue.front();
        queue.pop_front();
        const int ring = ringOf.at(current);
        if (ring >= lastCollectedRing) {
            continue;
        }
        forEachGraphNeighbor(map, current, [&](const RoomId neighbor) {
            if (!map.findRoomHandle(neighbor)) {
                return;
            }
            if (ringOf.emplace(neighbor, ring + 1).second) {
                queue.push_back(neighbor);
            }
        });
    }

    graph.m_rooms.reserve(ringOf.size());
    for (const auto &[id, ring] : ringOf) {
        const RoomHandle room = map.findRoomHandle(id);
        LayoutRoom entry;
        entry.id = id;
        entry.original = room.getPosition();
        entry.current = entry.original;
        entry.marginDistance = ring;
        if (options.pinned.contains(id)) {
            entry.role = LayoutRoomRoleEnum::Pinned;
        } else if (ring == 0) {
            entry.role = LayoutRoomRoleEnum::Core;
        } else if (ring <= options.marginRings) {
            entry.role = LayoutRoomRoleEnum::Margin;
        } else {
            entry.role = LayoutRoomRoleEnum::FixedExternal;
        }
        graph.m_rooms.push_back(entry);
    }

    // Stable order keeps solver behaviour and diagnostics reproducible, since
    // the BFS above walked an unordered map.
    std::sort(graph.m_rooms.begin(),
              graph.m_rooms.end(),
              [](const LayoutRoom &a, const LayoutRoom &b) { return a.id < b.id; });

    for (size_t i = 0; i < graph.m_rooms.size(); ++i) {
        graph.m_indexById[graph.m_rooms[i].id] = i;
    }

    // Edges. An edge is only worth recording when the solver could actually
    // act on it, i.e. when at least one endpoint can move.
    for (size_t fromIndex = 0; fromIndex < graph.m_rooms.size(); ++fromIndex) {
        const LayoutRoom &fromRoom = graph.m_rooms[fromIndex];
        const RoomHandle handle = map.findRoomHandle(fromRoom.id);
        if (!handle) {
            continue;
        }
        for (const ExitDirEnum dir : ALL_EXITS_NESWUD) {
            const RawExit &exit = handle.getExit(dir);
            const char *const unusable = classifyUnusableExit(exit);
            for (const RoomId toId : exit.getOutgoingSet()) {
                if (unusable != nullptr) {
                    graph.m_unsupportedExits.push_back(
                        LayoutUnsupportedExit{fromRoom.id, toId, dir, unusable});
                    continue;
                }
                const auto toIndex = graph.findIndex(toId);
                if (!toIndex) {
                    // Target lies beyond the collected rings; both ends are
                    // effectively immovable relative to the scope.
                    continue;
                }
                const LayoutRoom &toRoom = graph.m_rooms[*toIndex];
                if (!isMovable(fromRoom.role) && !isMovable(toRoom.role)) {
                    continue;
                }
                const bool crossesBoundary = (fromRoom.role == LayoutRoomRoleEnum::FixedExternal)
                                             != (toRoom.role == LayoutRoomRoleEnum::FixedExternal);
                const LayoutEdge edge{fromIndex, *toIndex, dir, crossesBoundary};
                if (isNESW(dir)) {
                    graph.m_horizontalEdges.push_back(edge);
                } else {
                    graph.m_verticalEdges.push_back(edge);
                }
            }
        }
    }

    // Occupancy. Fixed rooms are authoritative obstacles even when they have
    // no graph connection to the scope, so scan the surrounding space rather
    // than only the rooms collected above.
    if (!graph.m_rooms.empty()) {
        Bounds bounds{graph.m_rooms.front().current, graph.m_rooms.front().current};
        for (const LayoutRoom &room : graph.m_rooms) {
            bounds.insert(room.current);
        }
        const int pad = std::max(0, options.obstaclePadding);
        // Only x/y are padded: the first solver milestone keeps z fixed, so
        // obstacles on other layers cannot be collided with yet.
        const Coordinate lo{bounds.min.x - pad, bounds.min.y - pad, bounds.min.z};
        const Coordinate hi{bounds.max.x + pad, bounds.max.y + pad, bounds.max.z};
        const Bounds padded{lo, hi};

        for (const RoomId id : map.getRooms()) {
            const RoomHandle room = map.findRoomHandle(id);
            if (!room) {
                continue;
            }
            const Coordinate pos = room.getPosition();
            if (padded.contains(pos)) {
                graph.m_occupancy.emplace(pos, id);
            }
        }
        // In-scope rooms must always be present, even if a pre-existing
        // overlap meant the loop above recorded a different occupant.
        for (const LayoutRoom &room : graph.m_rooms) {
            graph.m_occupancy.emplace(room.current, room.id);
        }
    }

    return graph;
}

std::optional<MapReshapeGraph> MapReshapeGraph::buildForArea(const Map &map,
                                                             const RoomArea &area,
                                                             const ReshapeOptions &options)
{
    const ImmUnorderedRoomIdSet *const areaRooms = map.getWorld().findAreaRoomSet(area);
    if (areaRooms == nullptr || areaRooms->size() == 0) {
        return std::nullopt;
    }
    RoomIdSet core;
    areaRooms->for_each([&core](const RoomId id) { core.insert(id); });
    return build(map, core, options);
}

size_t MapReshapeGraph::countZLevels() const
{
    std::set<int> levels;
    for (const LayoutRoom &room : m_rooms) {
        if (room.role != LayoutRoomRoleEnum::FixedExternal) {
            levels.insert(room.current.z);
        }
    }
    return levels.size();
}

std::vector<ReshapeConflict> MapReshapeGraph::findCollisions() const
{
    std::vector<ReshapeConflict> conflicts;
    std::unordered_map<Coordinate, RoomId> seen;
    seen.reserve(m_rooms.size());
    for (const LayoutRoom &room : m_rooms) {
        const auto [it, inserted] = seen.emplace(room.current, room.id);
        if (!inserted) {
            conflicts.push_back(
                ReshapeConflict{it->second, room.id, "rooms share the same coordinate"});
        }
    }
    return conflicts;
}

ReshapeStatistics MapReshapeGraph::computeStatistics() const
{
    ReshapeStatistics stats;
    stats.roomsProcessed = m_rooms.size();
    for (const LayoutRoom &room : m_rooms) {
        switch (room.role) {
        case LayoutRoomRoleEnum::Core:
            ++stats.coreRooms;
            break;
        case LayoutRoomRoleEnum::Margin:
            ++stats.marginRooms;
            break;
        case LayoutRoomRoleEnum::Pinned:
            ++stats.pinnedRooms;
            break;
        case LayoutRoomRoleEnum::FixedExternal:
            ++stats.fixedExternalRooms;
            break;
        }
        if (room.current != room.original) {
            ++stats.roomsMoved;
        }
    }
    stats.conflictsBefore = findCollisions().size();
    stats.conflictsAfter = stats.conflictsBefore;
    stats.zLevelsBefore = countZLevels();
    stats.zLevelsAfter = stats.zLevelsBefore;
    return stats;
}

void MapReshapeGraph::printDiagnostics(std::ostream &os) const
{
    const ReshapeStatistics stats = computeStatistics();
    os << "rooms in scope: " << stats.roomsProcessed << "\n";
    os << "  core:   " << stats.coreRooms << "\n";
    os << "  margin: " << stats.marginRooms << "\n";
    os << "  pinned: " << stats.pinnedRooms << "\n";
    os << "  fixed:  " << stats.fixedExternalRooms << "\n";
    os << "horizontal edges: " << m_horizontalEdges.size() << "\n";
    os << "vertical edges:   " << m_verticalEdges.size() << "\n";

    size_t boundaryEdges = 0;
    for (const LayoutEdge &edge : m_horizontalEdges) {
        if (edge.crossesBoundary) {
            ++boundaryEdges;
        }
    }
    os << "  crossing the boundary: " << boundaryEdges << "\n";
    os << "ignored exits:    " << m_unsupportedExits.size() << "\n";
    os << "occupied cells:   " << m_occupancy.size() << "\n";
    os << "z levels:         " << stats.zLevelsBefore << "\n";
    os << "existing collisions: " << stats.conflictsBefore << "\n";
}
