// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "MapReshapeScorer.h"

#include "ExitDirection.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <unordered_map>

std::string_view to_string_view(const LayoutIssueEnum kind)
{
    switch (kind) {
    case LayoutIssueEnum::WrongDirection:
        return "wrong direction";
    case LayoutIssueEnum::Misaligned:
        return "misaligned";
    case LayoutIssueEnum::Overlong:
        return "overlong";
    case LayoutIssueEnum::LayerMismatch:
        return "layer mismatch";
    case LayoutIssueEnum::Collision:
        return "collision";
    case LayoutIssueEnum::ImmovableMoved:
        return "immovable room moved";
    }
    return "unknown";
}

namespace {

/// How a horizontal edge sits relative to the direction it claims.
///
/// Decomposing the offset against the direction's unit vector avoids a switch
/// over NORTH/SOUTH/EAST/WEST and keeps all four consistent:
///   along -- displacement in the exit's own direction. Must be positive;
///            one cell is ideal, more is merely stretched.
///   perp  -- displacement across it. Should be zero, so that north/south
///            exits share x and east/west exits share y.
struct NODISCARD EdgeGeometry final
{
    int along = 0;
    int perp = 0;
    int layers = 0;
};

NODISCARD EdgeGeometry measure(const ExitDirEnum dir, const Coordinate &from, const Coordinate &to)
{
    const Coordinate unit = exitDir(dir);
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    EdgeGeometry g;
    g.along = dx * unit.x + dy * unit.y;
    g.perp = std::abs(dx * unit.y - dy * unit.x);
    g.layers = std::abs(to.z - from.z);
    return g;
}

NODISCARD int64_t movementWeightFor(const LayoutRoom &room, const ReshapeWeights &weights)
{
    switch (room.role) {
    case LayoutRoomRoleEnum::Core:
        return weights.coreMovement;
    case LayoutRoomRoleEnum::Margin:
        // Resistance grows with distance from the core, so displacement is
        // absorbed as close to the core as possible.
        return weights.marginMovementBase * std::max(1, room.marginDistance);
    case LayoutRoomRoleEnum::Pinned:
    case LayoutRoomRoleEnum::FixedExternal:
        return weights.immovableMoved;
    }
    return weights.immovableMoved;
}

/// Is `cell` owned by a fixed room that is not part of the scope?
///
/// Such a room is authoritative: it never moves, so it is the candidate
/// placement that has to give way.
NODISCARD bool hasExternalObstacle(const MapReshapeGraph &graph,
                                   const Coordinate &cell,
                                   const RoomId self)
{
    const auto occupant = graph.findOccupant(cell);
    return occupant && *occupant != self && !graph.findIndex(*occupant);
}

void requireMatchingSize(const MapReshapeGraph &graph, const LayoutPositions &positions)
{
    if (positions.size() != graph.getRooms().size()) {
        throw std::invalid_argument("MapReshapeScorer: positions do not match the graph");
    }
}

} // namespace

LayoutPositions MapReshapeScorer::currentPositions(const MapReshapeGraph &graph)
{
    LayoutPositions positions;
    positions.reserve(graph.getRooms().size());
    for (const LayoutRoom &room : graph.getRooms()) {
        positions.push_back(room.current);
    }
    return positions;
}

int64_t MapReshapeScorer::edgeCost(const LayoutEdge &edge,
                                   const Coordinate &from,
                                   const Coordinate &to,
                                   const ReshapeWeights &weights)
{
    const EdgeGeometry g = measure(edge.dir, from, to);
    int64_t cost = 0;
    if (g.along <= 0) {
        // Graded, not a flat charge. A binary penalty makes every wrong-side
        // position cost the same, so unit-move local search sees a plateau:
        // stepping an east neighbour from two cells west to one cell west
        // changes nothing and is never taken, leaving the exit backwards
        // forever. Charging by how wrong it is gives the search a gradient to
        // follow, and "less backwards" genuinely is better.
        cost += weights.wrongDirection * (1 - g.along);
    } else {
        // Spacing is elastic, so only the excess beyond one cell costs, and a
        // boundary edge is allowed to stretch far more cheaply.
        cost += (edge.crossesBoundary ? weights.boundaryEdgeLength : weights.edgeLength)
                * (g.along - 1);
    }
    cost += weights.alignment * g.perp;
    // A horizontal exit crossing layers is a defect regardless of how many
    // layers the area uses overall.
    cost += weights.layerMismatch * g.layers;
    return cost;
}

int64_t MapReshapeScorer::movementCost(const LayoutRoom &room,
                                       const Coordinate &pos,
                                       const ReshapeWeights &weights)
{
    const int64_t dx = std::abs(pos.x - room.original.x);
    const int64_t dy = std::abs(pos.y - room.original.y);
    const int64_t dz = std::abs(pos.z - room.original.z);
    if (!isMovable(room.role)) {
        return (dx != 0 || dy != 0 || dz != 0) ? weights.immovableMoved : 0;
    }
    return movementWeightFor(room, weights) * (dx + dy);
}

LayoutScore MapReshapeScorer::score(const MapReshapeGraph &graph,
                                    const LayoutPositions &positions,
                                    const ReshapeWeights &weights)
{
    requireMatchingSize(graph, positions);

    LayoutScore score;
    const std::vector<LayoutRoom> &rooms = graph.getRooms();

    for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
        const EdgeGeometry g = measure(edge.dir, positions[edge.fromIndex], positions[edge.toIndex]);

        if (g.along <= 0) {
            score.direction += weights.wrongDirection * (1 - g.along);
        } else {
            const int64_t excess = g.along - 1;
            if (edge.crossesBoundary) {
                score.boundary += weights.boundaryEdgeLength * excess;
            } else {
                score.edgeLength += weights.edgeLength * excess;
            }
        }
        score.alignment += weights.alignment * g.perp;
        score.layerMismatch += weights.layerMismatch * g.layers;
    }

    std::set<int> layersInUse;
    for (size_t i = 0; i < rooms.size(); ++i) {
        const LayoutRoom &room = rooms[i];
        const Coordinate &pos = positions[i];
        const int64_t dz = std::abs(pos.z - room.original.z);

        if (isMovable(room.role)) {
            score.movement += movementCost(room, pos, weights);
            layersInUse.insert(pos.z);
        } else {
            score.immovableMoved += movementCost(room, pos, weights);
        }
        score.zMovement += weights.zMovement * dz;
    }

    if (layersInUse.size() > 1) {
        score.zLayers += weights.extraZLayer * static_cast<int64_t>(layersInUse.size() - 1);
    }

    // Collisions, both among scope rooms and against fixed rooms that were
    // never part of the scope. An external occupant is authoritative: the
    // candidate placement is the thing that has to give way.
    // Counted per unordered pair sharing a cell, plus one per room landing on
    // an external fixed room. Pairwise rather than "everyone after the first"
    // so the count does not depend on iteration order -- the solver needs an
    // order-independent delta when it moves a single room.
    std::unordered_map<Coordinate, int64_t> cellCounts;
    cellCounts.reserve(rooms.size());
    for (size_t i = 0; i < rooms.size(); ++i) {
        ++cellCounts[positions[i]];
        if (hasExternalObstacle(graph, positions[i], rooms[i].id)) {
            score.collision += weights.collision;
        }
    }
    for (const auto &[cell, count] : cellCounts) {
        if (count > 1) {
            score.collision += weights.collision * (count * (count - 1) / 2);
        }
    }

    // Compactness uses the bounding-box perimeter rather than its area: it
    // grows smoothly as the layout spreads, and cannot overflow on a large
    // selection the way an area product can.
    bool any = false;
    Coordinate lo;
    Coordinate hi;
    for (size_t i = 0; i < rooms.size(); ++i) {
        if (!isMovable(rooms[i].role)) {
            continue;
        }
        const Coordinate &pos = positions[i];
        if (!any) {
            lo = pos;
            hi = pos;
            any = true;
            continue;
        }
        lo = Coordinate::min(lo, pos);
        hi = Coordinate::max(hi, pos);
    }
    if (any) {
        const int64_t width = hi.x - lo.x;
        const int64_t height = hi.y - lo.y;
        score.compactness += weights.compactness * (width + height);
    }

    return score;
}

std::vector<LayoutIssue> MapReshapeScorer::findIssues(const MapReshapeGraph &graph,
                                                      const LayoutPositions &positions)
{
    requireMatchingSize(graph, positions);

    std::vector<LayoutIssue> issues;
    const std::vector<LayoutRoom> &rooms = graph.getRooms();

    for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
        const Coordinate &from = positions[edge.fromIndex];
        const Coordinate &to = positions[edge.toIndex];
        const EdgeGeometry g = measure(edge.dir, from, to);
        const Coordinate delta = to - from;

        LayoutIssue base;
        base.from = rooms[edge.fromIndex].id;
        base.to = rooms[edge.toIndex].id;
        base.dir = edge.dir;
        base.delta = delta;

        if (g.along <= 0) {
            LayoutIssue issue = base;
            issue.kind = LayoutIssueEnum::WrongDirection;
            issue.amount = 1 - g.along;
            issues.push_back(issue);
        } else if (g.along > 1) {
            LayoutIssue issue = base;
            issue.kind = LayoutIssueEnum::Overlong;
            issue.amount = g.along - 1;
            issues.push_back(issue);
        }
        if (g.perp != 0) {
            LayoutIssue issue = base;
            issue.kind = LayoutIssueEnum::Misaligned;
            issue.amount = g.perp;
            issues.push_back(issue);
        }
        if (g.layers != 0) {
            LayoutIssue issue = base;
            issue.kind = LayoutIssueEnum::LayerMismatch;
            issue.amount = g.layers;
            issues.push_back(issue);
        }
    }

    std::unordered_map<Coordinate, RoomId> taken;
    taken.reserve(rooms.size());
    for (size_t i = 0; i < rooms.size(); ++i) {
        const LayoutRoom &room = rooms[i];
        const Coordinate &pos = positions[i];

        if (!isMovable(room.role) && pos != room.original) {
            LayoutIssue issue;
            issue.kind = LayoutIssueEnum::ImmovableMoved;
            issue.from = room.id;
            issue.delta = pos - room.original;
            issues.push_back(issue);
        }

        const auto [it, inserted] = taken.emplace(pos, room.id);
        if (!inserted) {
            LayoutIssue issue;
            issue.kind = LayoutIssueEnum::Collision;
            issue.from = it->second;
            issue.to = room.id;
            issues.push_back(issue);
            continue;
        }
        if (hasExternalObstacle(graph, pos, room.id)) {
            LayoutIssue issue;
            issue.kind = LayoutIssueEnum::Collision;
            issue.from = room.id;
            issue.to = *graph.findOccupant(pos);
            issues.push_back(issue);
        }
    }

    return issues;
}

void MapReshapeScorer::printReport(const MapReshapeGraph &graph,
                                   const LayoutPositions &positions,
                                   std::ostream &os,
                                   const ReshapeWeights &weights)
{
    const LayoutScore s = score(graph, positions, weights);
    const std::vector<LayoutIssue> issues = findIssues(graph, positions);

    os << "layout score: " << s.total() << "\n";
    const auto line = [&os](const char *const name, const int64_t value) {
        if (value != 0) {
            os << "  " << name << ": " << value << "\n";
        }
    };
    line("collision", s.collision);
    line("extra z layers", s.zLayers);
    line("z movement", s.zMovement);
    line("layer mismatch", s.layerMismatch);
    line("wrong direction", s.direction);
    line("alignment", s.alignment);
    line("room movement", s.movement);
    line("edge length", s.edgeLength);
    line("boundary stretch", s.boundary);
    line("compactness", s.compactness);
    line("immovable moved", s.immovableMoved);

    std::unordered_map<int, size_t> counts;
    for (const LayoutIssue &issue : issues) {
        ++counts[static_cast<int>(issue.kind)];
    }
    os << "issues: " << issues.size() << "\n";
    for (const LayoutIssueEnum kind : {LayoutIssueEnum::WrongDirection,
                                       LayoutIssueEnum::Misaligned,
                                       LayoutIssueEnum::Overlong,
                                       LayoutIssueEnum::LayerMismatch,
                                       LayoutIssueEnum::Collision,
                                       LayoutIssueEnum::ImmovableMoved}) {
        const auto it = counts.find(static_cast<int>(kind));
        if (it != counts.end()) {
            os << "  " << to_string_view(kind) << ": " << it->second << "\n";
        }
    }
}
