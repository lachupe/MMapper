// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "MapReshapeScaling.h"

#include "ExitDirection.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <stdexcept>

namespace {

/// Connected groups of movable rooms, over horizontal exits.
///
/// The same notion of a group the solver uses when it moves a whole storey:
/// rooms that belong together because they are joined sideways.
NODISCARD std::vector<std::vector<size_t>> connectedGroups(const MapReshapeGraph &graph)
{
    const std::vector<LayoutRoom> &rooms = graph.getRooms();
    std::vector<std::vector<size_t>> adjacency(rooms.size());
    for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
        adjacency[edge.fromIndex].push_back(edge.toIndex);
        adjacency[edge.toIndex].push_back(edge.fromIndex);
    }

    std::vector<std::vector<size_t>> groups;
    std::vector<bool> seen(rooms.size(), false);
    for (size_t start = 0; start < rooms.size(); ++start) {
        if (seen[start] || !isMovable(rooms[start].role)) {
            continue;
        }
        std::vector<size_t> group;
        std::deque<size_t> queue{start};
        seen[start] = true;
        while (!queue.empty()) {
            const size_t current = queue.front();
            queue.pop_front();
            group.push_back(current);
            for (const size_t next : adjacency[current]) {
                if (!seen[next] && isMovable(rooms[next].role)) {
                    seen[next] = true;
                    queue.push_back(next);
                }
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

NODISCARD int ceilToInt(const double value)
{
    return static_cast<int>(std::ceil(value - 1e-9));
}

} // namespace

int ScalingCandidate::scaledWidth() const
{
    return std::max(1, ceilToInt(spanWidth * suggestedScale()));
}

int ScalingCandidate::scaledHeight() const
{
    return std::max(1, ceilToInt(spanHeight * suggestedScale()));
}

std::vector<ScalingCandidate> MapReshapeScaling::findCandidates(const MapReshapeGraph &graph,
                                                                const LayoutPositions &positions,
                                                                const double stretchThreshold,
                                                                const size_t minimumRooms)
{
    if (positions.size() != graph.getRooms().size()) {
        throw std::invalid_argument("MapReshapeScaling: positions do not match the graph");
    }

    std::vector<ScalingCandidate> candidates;
    for (const std::vector<size_t> &group : connectedGroups(graph)) {
        if (group.size() < minimumRooms) {
            continue;
        }

        std::vector<bool> inGroup(graph.getRooms().size(), false);
        for (const size_t index : group) {
            inGroup[index] = true;
        }

        // Only exits with both ends inside the group say anything about how
        // cramped the group itself is; one leaving it is just a long road.
        int64_t totalLength = 0;
        int64_t edgeCount = 0;
        for (const LayoutEdge &edge : graph.getHorizontalEdges()) {
            if (!inGroup[edge.fromIndex] || !inGroup[edge.toIndex]) {
                continue;
            }
            const Coordinate &from = positions[edge.fromIndex];
            const Coordinate &to = positions[edge.toIndex];
            totalLength += std::abs(to.x - from.x) + std::abs(to.y - from.y);
            ++edgeCount;
        }
        if (edgeCount == 0) {
            continue;
        }

        const double average = static_cast<double>(totalLength) / static_cast<double>(edgeCount);
        if (average < stretchThreshold) {
            continue;
        }

        Coordinate lo = positions[group.front()];
        Coordinate hi = lo;
        for (const size_t index : group) {
            lo = Coordinate::min(lo, positions[index]);
            hi = Coordinate::max(hi, positions[index]);
        }

        ScalingCandidate candidate;
        candidate.rooms.reserve(group.size());
        for (const size_t index : group) {
            candidate.rooms.push_back(graph.getRooms()[index].id);
        }
        candidate.spanWidth = hi.x - lo.x + 1;
        candidate.spanHeight = hi.y - lo.y + 1;
        candidate.averageEdgeLength = average;
        candidates.push_back(std::move(candidate));
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const ScalingCandidate &a, const ScalingCandidate &b) {
                  return a.rooms.size() > b.rooms.size();
              });
    return candidates;
}

void MapReshapeScaling::printReport(const MapReshapeGraph &graph,
                                    const LayoutPositions &positions,
                                    std::ostream &os)
{
    const std::vector<ScalingCandidate> candidates = findCandidates(graph, positions);
    if (candidates.empty()) {
        return;
    }
    os << "groups that would read better drawn smaller: " << candidates.size() << "\n";
    for (const ScalingCandidate &candidate : candidates) {
        os << "  " << candidate.rooms.size() << " rooms spanning " << candidate.spanWidth << "x"
           << candidate.spanHeight << ", exits averaging " << candidate.averageEdgeLength
           << " cells; at " << candidate.suggestedScale() << " scale it would fit "
           << candidate.scaledWidth() << "x" << candidate.scaledHeight() << "\n";
    }
}
