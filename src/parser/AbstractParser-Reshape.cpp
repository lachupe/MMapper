// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/AnsiOstream.h"
#include "../map/ChangeList.h"
#include "../map/Map.h"
#include "../map/MapReshapeApply.h"
#include "../map/MapReshapeGraph.h"
#include "../map/MapReshapeScorer.h"
#include "../map/MapReshapeSolver.h"
#include "../map/MapReshapeTypes.h"
#include "../map/RoomHandle.h"
#include "../map/mmapper2room.h"
#include "../mapdata/mapdata.h"
#include "abstractparser.h"

#include <optional>
#include <sstream>
#include <string>

namespace {

/// The area to reshape: the one named, or the one the player is standing in.
NODISCARD std::optional<RoomArea> resolveArea(MapData &mapData,
                                              const std::optional<std::string> &requested,
                                              AnsiOstream &os)
{
    if (requested) {
        return makeRoomArea(*requested);
    }
    const RoomHandle here = mapData.getCurrentRoom();
    if (!here) {
        os << "You are not in a known room, so there is no area to reshape.\n";
        return std::nullopt;
    }
    return here.getArea();
}

void describeScope(AnsiOstream &os, const MapReshapeGraph &graph)
{
    const ReshapeStatistics stats = graph.computeStatistics();
    os << "Rooms in scope: " << stats.roomsProcessed << " (core " << stats.coreRooms << ", margin "
       << stats.marginRooms << ", pinned " << stats.pinnedRooms << ", fixed "
       << stats.fixedExternalRooms << ")\n";
}

/// Forward a std::ostream-based report into the user's AnsiOstream.
void forwardReport(AnsiOstream &os, const std::function<void(std::ostream &)> &writer)
{
    std::ostringstream buffer;
    writer(buffer);
    os << buffer.str();
}

} // namespace

void AbstractParser::doMapAreaReshape(AnsiOstream &os,
                                      const std::optional<std::string> &requestedArea,
                                      const bool applyResult,
                                      const ReshapeModeEnum mode)
{
    MapData &mapData = m_mapData;

    const std::optional<RoomArea> area = resolveArea(mapData, requestedArea, os);
    if (!area) {
        return;
    }

    const Map map = mapData.getCurrentMap();
    ReshapeOptions options;
    const auto graph = MapReshapeGraph::buildForArea(map, *area, options);
    if (!graph) {
        os << "No rooms found in area \"" << area->getStdStringViewUtf8() << "\".\n";
        return;
    }

    os << "Area: \"" << area->getStdStringViewUtf8() << "\" (" << to_string_view(mode) << ")\n";
    describeScope(os, *graph);

    if (!applyResult) {
        forwardReport(os, [&graph](std::ostream &out) {
            MapReshapeScorer::printReport(*graph, MapReshapeScorer::currentPositions(*graph), out);
        });
        const auto &ignored = graph->getUnsupportedExits();
        if (!ignored.empty()) {
            os << "ignored exits: " << ignored.size()
               << " (random, special or unmapped exits say nothing reliable about geometry)\n";
        }

        // The z terms usually dominate the total, and which of them can be
        // acted on depends entirely on the mode asked for, so say so rather
        // than leaving the reader to guess why a big number barely moved.
        const LayoutScore score = MapReshapeScorer::score(*graph,
                                                          MapReshapeScorer::currentPositions(*graph),
                                                          makeWeights(mode));
        os << "z levels in use: " << graph->countZLevels() << "\n";
        const int64_t layerCost = score.zLayers + score.layerMismatch;
        if (layerCost != 0) {
            os << "Of the score, " << layerCost << " comes from layer usage. \"reshape\" "
               << "collapses layers where it can; \"reshape3d\" leaves them and honours "
               << "up/down exits instead.\n";
        }
        return;
    }

    ReshapeResult result;
    const LayoutPositions solved
        = MapReshapeSolver::solvePositions(*graph, ReshapeSolverOptions::forMode(mode), result);

    switch (result.status) {
    case ReshapeStatusEnum::Unchanged:
        os << "No better layout found; the area is left alone.\n";
        return;

    case ReshapeStatusEnum::InfeasibleWithCurrentBoundary:
        // Reporting the problem beats returning a malformed layout.
        os << "Cannot reshape this area while preserving the surrounding rooms.\n";
        os << "Unresolved collisions: " << result.stats.conflictsAfter << " (was "
           << result.stats.conflictsBefore << ")\n";
        os << "Try increasing the reshape margin, or including the neighbouring area.\n";
        return;

    case ReshapeStatusEnum::Improved:
        break;
    }

    const ChangeList changes = map_reshape::buildChanges(map, result.moves);
    if (!mapData.applyChanges(changes)) {
        os << "Failed to apply the reshape; the map is unchanged.\n";
        return;
    }

    os << "Rooms moved: " << result.stats.roomsMoved << "\n";
    os << "Layout score: " << result.stats.scoreBefore << " -> " << result.stats.scoreAfter << "\n";
    os << "Z levels: " << result.stats.zLevelsBefore << " -> " << result.stats.zLevelsAfter << "\n";
    os << "This counts as a single undo step.\n";
}
