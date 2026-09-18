// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/AnsiOstream.h"
#include "../global/AsyncTasks.h"
#include "../global/SendToUser.h"
#include "../global/progresscounter.h"
#include "../global/thread_utils.h"
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

#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <QPointer>
#include <QString>

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

void describeScope(std::ostream &os, const MapReshapeGraph &graph)
{
    const ReshapeStatistics stats = graph.computeStatistics();
    os << "Rooms in scope: " << stats.roomsProcessed << " (core " << stats.coreRooms << ", margin "
       << stats.marginRooms << ", pinned " << stats.pinnedRooms << ", fixed "
       << stats.fixedExternalRooms << ")\n";
}

/// Everything one invocation needs, so the background thread can work from a
/// snapshot and the main thread can finish up afterwards.
///
/// The Map is an immutable persistent structure, so handing a copy to another
/// thread is both safe and cheap. Only applying the result has to come back
/// to the main thread.
struct NODISCARD ReshapeJob final
{
    Map map;
    RoomArea area;
    ReshapeModeEnum mode = ReshapeModeEnum::Flatten;
    bool apply = false;

    ReshapeResult result;
    ChangeList changes;
    std::string report;
    bool hasChanges = false;
};

void runInBackground(ReshapeJob &job, ProgressCounter &pc)
{
    std::ostringstream os;

    ReshapeOptions options;
    const auto graph = MapReshapeGraph::buildForArea(job.map, job.area, options);
    if (!graph) {
        os << "No rooms found in area \"" << job.area.getStdStringViewUtf8() << "\".\n";
        job.report = os.str();
        return;
    }

    os << "Area: \"" << job.area.getStdStringViewUtf8() << "\" (" << to_string_view(job.mode)
       << ")\n";
    describeScope(os, *graph);

    const ReshapeWeights weights = makeWeights(job.mode);

    if (!job.apply) {
        const LayoutPositions positions = MapReshapeScorer::currentPositions(*graph);
        MapReshapeScorer::printReport(*graph, positions, os, weights);

        const auto &ignored = graph->getUnsupportedExits();
        if (!ignored.empty()) {
            os << "ignored exits: " << ignored.size()
               << " (random, special or unmapped exits say nothing reliable about geometry)\n";
        }
        os << "z levels in use: " << graph->countZLevels() << "\n";

        const LayoutScore score = MapReshapeScorer::score(*graph, positions, weights);
        const int64_t layerCost = score.zLayers + score.layerMismatch;
        if (layerCost != 0) {
            os << "Of the score, " << layerCost
               << " comes from layer usage. \"reshape\" collapses layers where it can; "
                  "\"reshape3d\" keeps them and honours up/down exits instead.\n";
        }
        job.report = os.str();
        return;
    }

    std::ignore = MapReshapeSolver::solvePositions(*graph,
                                                   ReshapeSolverOptions::forMode(job.mode),
                                                   job.result,
                                                   &pc);
    switch (job.result.status) {
    case ReshapeStatusEnum::Unchanged:
        os << "No better layout found; the area is left alone.\n";
        break;

    case ReshapeStatusEnum::InfeasibleWithCurrentBoundary:
        // Reporting the problem beats returning a malformed layout.
        os << "Cannot reshape this area while preserving the surrounding rooms.\n"
           << "Unresolved collisions: " << job.result.stats.conflictsAfter << " (was "
           << job.result.stats.conflictsBefore << ")\n"
           << "Try increasing the reshape margin, or including the neighbouring area.\n";
        break;

    case ReshapeStatusEnum::Improved:
        job.changes = map_reshape::buildChanges(job.map, job.result.moves);
        job.hasChanges = !job.changes.empty();
        os << "Rooms moved: " << job.result.stats.roomsMoved << "\n"
           << "Layout score: " << job.result.stats.scoreBefore << " -> "
           << job.result.stats.scoreAfter << "\n"
           << "Z levels: " << job.result.stats.zLevelsBefore << " -> "
           << job.result.stats.zLevelsAfter << "\n";
        break;
    }
    job.report = os.str();
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

    auto job = std::make_shared<ReshapeJob>();
    job->map = mapData.getCurrentMap();
    job->area = *area;
    job->mode = mode;
    job->apply = applyResult;

    // Reshaping a large area takes seconds, and doing it inline would freeze
    // the window with nothing to show that work is happening -- queued output
    // cannot reach the user while the main thread is busy producing it. The
    // search is pure, reading a snapshot and returning a list of movements,
    // so only applying the result has to return to the main thread.
    //
    // The parser is held weakly rather than captured outright: a session can
    // end while the task is still running, leaving nothing to apply to.
    QPointer<AbstractParser> self{this};
    const std::string taskName = applyResult ? "map area reshape" : "map area check";

    const async_tasks::AsyncTaskHandle handle = async_tasks::startAsyncTask(
        AsyncTaskTypeEnum::Task,
        AllowCancelEnum::Allow,
        taskName,
        [job](ProgressCounter &pc) { runInBackground(deref(job), pc); },
        [job, self]() {
            ABORT_IF_NOT_ON_MAIN_THREAD();
            ReshapeJob &finished = deref(job);
            std::string text = finished.report;
            if (finished.hasChanges) {
                if (self.isNull()) {
                    text += "The session ended before the reshape could be applied.\n";
                } else if (!self->m_mapData.applyChanges(finished.changes)) {
                    text += "Failed to apply the reshape; the map is unchanged.\n";
                } else {
                    text += "Applied as a single undo step.\n";
                }
            }
            global::sendToUser(QString::fromStdString(text));
        });

    os << "Started task #" << handle.getId() << " (" << taskName
       << "); the result will appear when it finishes.\n";
}
