// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/AnsiOstream.h"
#include "../global/AsyncTasks.h"
#include "../global/ConfigConsts.h"
#include "../global/SendToUser.h"
#include "../global/progresscounter.h"
#include "../global/thread_utils.h"
#include "../map/ChangeList.h"
#include "../map/Map.h"
#include "../map/MapReshapeApply.h"
#include "../map/MapReshapeGraph.h"
#include "../map/MapReshapeScaling.h"
#include "../map/MapReshapeScorer.h"
#include "../map/MapReshapeSolver.h"
#include "../map/MapReshapeTypes.h"
#include "../map/RoomHandle.h"
#include "../map/World.h"
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

/// Beyond this many rooms a reshape stops being reviewable, whatever it
/// costs to compute.
///
/// Speed is not the binding constraint: a release build flattens ten
/// thousand rooms in under a second. The limit is that nobody can check a
/// change that large, and the spec is explicit that reshaping the whole
/// world should not be an ordinary action. Some maps put every room in one
/// area, or in none, where reshaping "the area" means exactly that -- hence
/// the radius-scoped commands, which give those maps a way in piece by
/// piece. A debug build is roughly forty times slower than the figure
/// above, so this is generous there rather than tight.
constexpr size_t MAX_SCOPE_ROOMS = 20000;

struct NODISCARD ResolvedScope final
{
    RoomIdSet core;
    std::string label;
    /// Set only by the explicit whole-world command, which exists to do the
    /// one thing the size limit is there to prevent.
    bool unlimited = false;
};

/// What to reshape: the named area, everything within a few steps of the
/// player, or the area the player is standing in.
NODISCARD std::optional<ResolvedScope> resolveScope(MapData &mapData,
                                                    const Map &map,
                                                    const std::optional<std::string> &requested,
                                                    const std::optional<int> radius,
                                                    const bool wholeWorld,
                                                    AnsiOstream &os)
{
    const auto fromArea = [&map](const RoomArea &area) -> ResolvedScope {
        RoomIdSet core;
        if (const auto *const rooms = map.getWorld().findAreaRoomSet(area)) {
            rooms->for_each([&core](const RoomId id) { core.insert(id); });
        }
        const std::string name = std::string{area.getStdStringViewUtf8()};
        return ResolvedScope{std::move(core),
                             name.empty() ? std::string{"the unnamed area"}
                                          : ("area \"" + name + "\"")};
    };

    if (wholeWorld) {
        RoomIdSet everything;
        for (const RoomId id : map.getRooms()) {
            everything.insert(id);
        }
        return ResolvedScope{std::move(everything), "the entire map", true};
    }

    if (requested) {
        return fromArea(makeRoomArea(*requested));
    }

    const RoomHandle here = mapData.getCurrentRoom();
    if (!here) {
        os << "You are not in a known room, so there is nothing to reshape.\n";
        return std::nullopt;
    }

    if (radius) {
        return ResolvedScope{MapReshapeGraph::collectWithinRadius(map, here.getId(), *radius),
                             "rooms within " + std::to_string(*radius) + " steps of here"};
    }
    return fromArea(here.getArea());
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
    /// Pre-resolved, because working out "the area I am standing in" or
    /// "everything within N steps" needs the current room, which belongs to
    /// the main thread.
    RoomIdSet core;
    std::string scopeLabel;
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
    const MapReshapeGraph graph = MapReshapeGraph::build(job.map, job.core, options);
    if (graph.empty()) {
        os << "No rooms found in " << job.scopeLabel << ".\n";
        job.report = os.str();
        return;
    }

    os << "Scope: " << job.scopeLabel << " (" << to_string_view(job.mode) << ")\n";
    if constexpr (IS_DEBUG_BUILD) {
        // Worth saying out loud: the same reshape takes well under a second
        // in a release build and the better part of a minute here, which is
        // easily mistaken for the reshaper having gone wrong.
        os << "NOTE: this is a debug build with sanitizers, roughly forty "
              "times slower than a release build.\n";
    }
    describeScope(os, graph);

    const ReshapeWeights weights = makeWeights(job.mode);

    if (!job.apply) {
        const LayoutPositions positions = MapReshapeScorer::currentPositions(graph);
        MapReshapeScorer::printReport(graph, positions, os, weights);

        const auto &ignored = graph.getUnsupportedExits();
        if (!ignored.empty()) {
            os << "ignored exits: " << ignored.size()
               << " (random, special or unmapped exits say nothing reliable about geometry)\n";
        }
        os << "z levels in use: " << graph.countZLevels() << "\n";

        MapReshapeScaling::printReport(graph, positions, os);

        const LayoutScore score = MapReshapeScorer::score(graph, positions, weights);
        const int64_t layerCost = score.zLayers + score.layerMismatch;
        if (layerCost != 0) {
            os << "Of the score, " << layerCost
               << " comes from layer usage. \"reshape\" collapses layers where it can; "
                  "\"reshape3d\" keeps them and honours up/down exits instead.\n";
        }
        job.report = os.str();
        return;
    }

    std::ignore = MapReshapeSolver::solvePositions(graph,
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
        if (job.result.stats.hitLimits) {
            // Never leave this implicit: a solve that ran out of budget
            // leaves part of the scope reshaped and the rest untouched,
            // which reads as a broken result rather than an unfinished one.
            // Deliberately not called "incomplete": the search stopping on
            // budget usually still leaves a much better layout, and saying
            // otherwise sends people to undo a result worth keeping.
            os << "NOTE: the search stopped on its budget rather than because "
                  "it had run out of improvements. Running this again will "
                  "carry on from here, or use a smaller scope for a more "
                  "thorough pass.\n";
        }
        break;
    }
    job.report = os.str();
}

/// Reshape the whole map a piece at a time.
///
/// Each piece is solved against the map as it stands after the previous
/// ones, so a piece sees where its neighbours actually ended up rather than
/// where they started. The work is done on a copy: nothing touches the real
/// map until every piece is done, and then the whole batch travels as one
/// change list so a single undo puts everything back.
void runBatchInBackground(ReshapeJob &job, ProgressCounter &pc)
{
    std::ostringstream os;

    constexpr size_t MAX_CHUNK_ROOMS = 1500;
    const std::vector<RoomIdSet> chunks = MapReshapeGraph::partitionForBatch(job.map,
                                                                             MAX_CHUNK_ROOMS);
    if (chunks.empty()) {
        os << "There is nothing on the map to reshape.\n";
        job.report = os.str();
        return;
    }

    pc.setCurrentTask(ProgressMsg{"reshaping the map piece by piece"});
    pc.increaseTotalStepsBy(chunks.size());

    Map working = job.map;
    size_t improved = 0;
    size_t truncated = 0;
    int64_t before = 0;
    int64_t after = 0;

    for (const RoomIdSet &chunk : chunks) {
        ReshapeOptions options;
        const MapReshapeGraph graph = MapReshapeGraph::build(working, chunk, options);
        if (!graph.empty()) {
            ReshapeResult piece;
            std::ignore = MapReshapeSolver::solvePositions(graph,
                                                           ReshapeSolverOptions::forMode(job.mode),
                                                           piece);
            before += piece.stats.scoreBefore;
            after += piece.stats.scoreAfter;
            if (piece.stats.hitLimits) {
                ++truncated;
            }
            if (piece.status == ReshapeStatusEnum::Improved) {
                ++improved;
                ProgressCounter quiet;
                working = working.apply(quiet, map_reshape::buildChanges(working, piece.moves)).map;
            }
        }
        pc.step();
    }

    // One change list for the lot, built by comparing where every room
    // started against where it ended up.
    std::vector<RoomMove> moves;
    for (const RoomId id : job.map.getRooms()) {
        const RoomHandle from = job.map.findRoomHandle(id);
        const RoomHandle to = working.findRoomHandle(id);
        if (from && to && from.getPosition() != to.getPosition()) {
            moves.push_back(RoomMove{id, from.getPosition(), to.getPosition()});
        }
    }

    os << "Pieces: " << chunks.size() << ", improved: " << improved << "\n"
       << "Rooms moved: " << moves.size() << "\n"
       << "Layout score: " << before << " -> " << after << "\n";
    if (truncated != 0) {
        os << truncated << " piece(s) stopped on their budget rather than on running out of "
           << "improvements; running this again will carry on from here.\n";
    }
    if (!moves.empty()) {
        job.changes = map_reshape::buildChanges(job.map, moves);
        job.hasChanges = !job.changes.empty();
        job.result.status = ReshapeStatusEnum::Improved;
        job.result.stats.roomsMoved = moves.size();
    }
    job.report = os.str();
}

} // namespace

void AbstractParser::doMapBatchReshape(AnsiOstream &os, const ReshapeModeEnum mode)
{
    auto job = std::make_shared<ReshapeJob>();
    job->map = m_mapData.getCurrentMap();
    job->mode = mode;
    job->apply = true;

    QPointer<AbstractParser> self{this};
    const async_tasks::AsyncTaskHandle handle = async_tasks::startAsyncTask(
        AsyncTaskTypeEnum::Task,
        AllowCancelEnum::Allow,
        "map reshape all",
        [job](ProgressCounter &pc) { runBatchInBackground(deref(job), pc); },
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

    os << "Started task #" << handle.getId()
       << " (reshaping the whole map piece by piece); this takes a while, and the progress bar "
          "counts pieces.\n";
}

void AbstractParser::doMapAreaReshape(AnsiOstream &os,
                                      const std::optional<std::string> &requestedArea,
                                      const std::optional<int> radius,
                                      const bool wholeWorld,
                                      const bool applyResult,
                                      const ReshapeModeEnum mode)
{
    MapData &mapData = m_mapData;
    const Map map = mapData.getCurrentMap();

    std::optional<ResolvedScope> scope
        = resolveScope(mapData, map, requestedArea, radius, wholeWorld, os);
    if (!scope) {
        return;
    }
    if (scope->core.empty()) {
        os << "No rooms found in " << scope->label << ".\n";
        return;
    }
    if (scope->core.size() > MAX_SCOPE_ROOMS && !scope->unlimited) {
        os << scope->label << " holds " << scope->core.size() << " rooms, which is too many to "
           << "reshape in one go (the limit is " << MAX_SCOPE_ROOMS << ").\n"
           << "Reshaping that much at once cannot finish quickly enough to stay responsive, and "
           << "a partial result would leave some of the map moved and the rest untouched.\n"
           << "Use \"_map near reshape <radius>\" to work outwards from where you are "
           << "instead.\n";
        return;
    }

    auto job = std::make_shared<ReshapeJob>();
    job->map = map;
    job->core = std::move(scope->core);
    job->scopeLabel = std::move(scope->label);
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
    const std::string taskName = applyResult ? "map reshape" : "map check";

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
