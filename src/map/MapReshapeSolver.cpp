// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "MapReshapeSolver.h"

#include "../global/progresscounter.h"
#include "ExitDirection.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

/// The unit moves available in the x/y-only milestone. Structural moves
/// (inserting or removing a whole row or column) come later; these four are
/// enough to straighten and space a layout locally.
// Not constexpr: Coordinate's constructor is not constexpr.
// The vertical pair is skipped when z is frozen.
const std::array<Coordinate, 6> UNIT_MOVES = {Coordinate{1, 0, 0},
                                              Coordinate{-1, 0, 0},
                                              Coordinate{0, 1, 0},
                                              Coordinate{0, -1, 0},
                                              Coordinate{0, 0, 1},
                                              Coordinate{0, 0, -1}};

/// Mutable search state over one graph.
///
/// Holds everything needed to price a single room's move without walking the
/// whole layout: which edges touch each room, how many rooms sit on each
/// cell, and ordered tallies of the x and y coordinates in use so the
/// bounding box is available in logarithmic time.
class NODISCARD SearchState final
{
private:
    const MapReshapeGraph &m_graph;
    const ReshapeWeights &m_weights;

    LayoutPositions m_positions;
    /// Edge indices touching each room, from either end.
    std::vector<std::vector<size_t>> m_incident;
    std::vector<std::vector<size_t>> m_verticalIncident;
    /// Rooms currently standing on each cell.
    std::unordered_map<Coordinate, int64_t> m_cellCounts;
    /// x and y tallies for movable rooms, so the bounding box that drives the
    /// compactness term can be read off the ends.
    std::map<int, size_t> m_xs;
    std::map<int, size_t> m_ys;
    /// Layers in use by movable rooms. Needed because the cost of an extra
    /// layer is a property of the whole scope rather than of one room, and
    /// it is the single largest term once z can change.
    std::map<int, size_t> m_zs;

public:
    SearchState(const MapReshapeGraph &graph, const ReshapeWeights &weights)
        : m_graph{graph}
        , m_weights{weights}
        , m_positions{MapReshapeScorer::currentPositions(graph)}
        , m_incident(graph.getRooms().size())
        , m_verticalIncident(graph.getRooms().size())
    {
        const auto &edges = graph.getHorizontalEdges();
        for (size_t e = 0; e < edges.size(); ++e) {
            m_incident[edges[e].fromIndex].push_back(e);
            if (edges[e].toIndex != edges[e].fromIndex) {
                m_incident[edges[e].toIndex].push_back(e);
            }
        }
        const auto &vertical = graph.getVerticalEdges();
        for (size_t e = 0; e < vertical.size(); ++e) {
            m_verticalIncident[vertical[e].fromIndex].push_back(e);
            if (vertical[e].toIndex != vertical[e].fromIndex) {
                m_verticalIncident[vertical[e].toIndex].push_back(e);
            }
        }
        for (size_t i = 0; i < m_positions.size(); ++i) {
            ++m_cellCounts[m_positions[i]];
            if (isMovable(graph.getRooms()[i].role)) {
                ++m_xs[m_positions[i].x];
                ++m_ys[m_positions[i].y];
                ++m_zs[m_positions[i].z];
            }
        }
    }

    NODISCARD const LayoutPositions &positions() const { return m_positions; }

    /// Replace the whole layout, e.g. after a structural shift moved many
    /// rooms at once, and rebuild the incremental bookkeeping from scratch.
    void reset(LayoutPositions positions)
    {
        m_positions = std::move(positions);
        m_cellCounts.clear();
        m_xs.clear();
        m_ys.clear();
        m_zs.clear();
        for (size_t i = 0; i < m_positions.size(); ++i) {
            ++m_cellCounts[m_positions[i]];
            if (canMove(i)) {
                ++m_xs[m_positions[i].x];
                ++m_ys[m_positions[i].y];
                ++m_zs[m_positions[i].z];
            }
        }
    }

    NODISCARD bool canMove(const size_t index) const
    {
        return isMovable(m_graph.getRooms()[index].role);
    }

    NODISCARD const std::vector<size_t> &incidentEdges(const size_t index) const
    {
        return m_incident[index];
    }

    /// Change in total score if room `index` moved to `to`. Negative is an
    /// improvement.
    NODISCARD int64_t deltaFor(const size_t index, const Coordinate &to) const
    {
        const LayoutRoom &room = m_graph.getRooms()[index];
        const Coordinate from = m_positions[index];
        if (from == to) {
            return 0;
        }

        int64_t delta = MapReshapeScorer::movementCost(room, to, m_weights)
                        - MapReshapeScorer::movementCost(room, from, m_weights);

        for (const size_t e : m_incident[index]) {
            const LayoutEdge &edge = m_graph.getHorizontalEdges()[e];
            const Coordinate oldFrom = m_positions[edge.fromIndex];
            const Coordinate oldTo = m_positions[edge.toIndex];
            const Coordinate newFrom = (edge.fromIndex == index) ? to : oldFrom;
            const Coordinate newTo = (edge.toIndex == index) ? to : oldTo;
            delta += MapReshapeScorer::edgeCost(edge, newFrom, newTo, m_weights)
                     - MapReshapeScorer::edgeCost(edge, oldFrom, oldTo, m_weights);
        }

        delta += MapReshapeScorer::zMovementCost(room, to, m_weights)
                 - MapReshapeScorer::zMovementCost(room, from, m_weights);

        for (const size_t e : m_verticalIncident[index]) {
            const LayoutEdge &edge = m_graph.getVerticalEdges()[e];
            const Coordinate oldFrom = m_positions[edge.fromIndex];
            const Coordinate oldTo = m_positions[edge.toIndex];
            const Coordinate newFrom = (edge.fromIndex == index) ? to : oldFrom;
            const Coordinate newTo = (edge.toIndex == index) ? to : oldTo;
            delta += MapReshapeScorer::verticalEdgeCost(edge, newFrom, newTo, m_weights)
                     - MapReshapeScorer::verticalEdgeCost(edge, oldFrom, oldTo, m_weights);
        }

        delta += collisionDelta(index, from, to);
        delta += compactnessDelta(index, from, to);
        delta += layerCountDelta(index, from, to);
        return delta;
    }

    void apply(const size_t index, const Coordinate &to)
    {
        const Coordinate from = m_positions[index];
        if (from == to) {
            return;
        }
        release(from, index);
        m_positions[index] = to;
        occupy(to, index);
    }

private:
    void release(const Coordinate &cell, const size_t index)
    {
        const auto it = m_cellCounts.find(cell);
        assert(it != m_cellCounts.end() && it->second > 0);
        if (--it->second == 0) {
            m_cellCounts.erase(it);
        }
        if (canMove(index)) {
            decrement(m_xs, cell.x);
            decrement(m_ys, cell.y);
            decrement(m_zs, cell.z);
        }
    }

    void occupy(const Coordinate &cell, const size_t index)
    {
        ++m_cellCounts[cell];
        if (canMove(index)) {
            ++m_xs[cell.x];
            ++m_ys[cell.y];
            ++m_zs[cell.z];
        }
    }

    static void decrement(std::map<int, size_t> &tally, const int key)
    {
        const auto it = tally.find(key);
        assert(it != tally.end() && it->second > 0);
        if (--it->second == 0) {
            tally.erase(it);
        }
    }

    NODISCARD int64_t countAt(const Coordinate &cell) const
    {
        const auto it = m_cellCounts.find(cell);
        return (it == m_cellCounts.end()) ? 0 : it->second;
    }

    NODISCARD bool blockedByExternal(const Coordinate &cell, const RoomId self) const
    {
        const auto occupant = m_graph.findOccupant(cell);
        return occupant && *occupant != self && !m_graph.findIndex(*occupant);
    }

    /// Collisions are counted per unordered pair, so vacating a cell shared
    /// with n others removes n pairs and entering one adds n.
    NODISCARD int64_t collisionDelta(const size_t index,
                                     const Coordinate &from,
                                     const Coordinate &to) const
    {
        const RoomId id = m_graph.getRooms()[index].id;
        int64_t pairs = countAt(to) - (countAt(from) - 1);
        int64_t delta = m_weights.collision * pairs;
        if (blockedByExternal(to, id)) {
            delta += m_weights.collision;
        }
        if (blockedByExternal(from, id)) {
            delta -= m_weights.collision;
        }
        return delta;
    }

    NODISCARD int64_t spanDelta(const std::map<int, size_t> &tally,
                                const int from,
                                const int to) const
    {
        if (tally.empty()) {
            return 0;
        }
        const int64_t oldSpan = tally.rbegin()->first - tally.begin()->first;

        // Simulate removing `from` and adding `to` without touching the tally.
        int lo = tally.begin()->first;
        int hi = tally.rbegin()->first;
        const auto it = tally.find(from);
        const bool wasOnlyOccupant = (it != tally.end() && it->second == 1);
        if (wasOnlyOccupant && (from == lo || from == hi)) {
            // The edge of the span may retract; find the next value inwards.
            if (tally.size() == 1) {
                lo = to;
                hi = to;
            } else if (from == lo) {
                lo = std::next(tally.begin())->first;
            } else {
                hi = std::prev(std::prev(tally.end()))->first;
            }
        }
        lo = std::min(lo, to);
        hi = std::max(hi, to);
        return (hi - lo) - oldSpan;
    }

    /// Change in the extra-layer charge. The scope pays for every layer past
    /// the first, so this only moves when a layer empties out or a new one is
    /// opened.
    NODISCARD int64_t layerCountDelta(const size_t index,
                                      const Coordinate &from,
                                      const Coordinate &to) const
    {
        if (!canMove(index) || from.z == to.z || m_weights.extraZLayer == 0) {
            return 0;
        }
        const int64_t oldCount = static_cast<int64_t>(m_zs.size());
        int64_t newCount = oldCount;
        const auto it = m_zs.find(from.z);
        if (it != m_zs.end() && it->second == 1) {
            --newCount;
        }
        if (m_zs.find(to.z) == m_zs.end()) {
            ++newCount;
        }
        const int64_t before = std::max<int64_t>(0, oldCount - 1);
        const int64_t after = std::max<int64_t>(0, newCount - 1);
        return m_weights.extraZLayer * (after - before);
    }

    NODISCARD int64_t compactnessDelta(const size_t index,
                                       const Coordinate &from,
                                       const Coordinate &to) const
    {
        if (!canMove(index)) {
            return 0;
        }
        const int64_t dx = spanDelta(m_xs, from.x, to.x);
        const int64_t dy = spanDelta(m_ys, from.y, to.y);
        return m_weights.compactness * (dx + dy);
    }
};

enum class NODISCARD ShiftAxisEnum : uint8_t { X, Y };

/// Shift every movable room at or beyond `threshold` along `axis` by `delta`.
///
/// Immovable rooms deliberately stay where they are. That is what lets an
/// area grow into free space while the world around it holds still: the exits
/// crossing the boundary simply get longer, which the scorer charges for
/// gently on purpose.
NODISCARD LayoutPositions shifted(const MapReshapeGraph &graph,
                                  const LayoutPositions &positions,
                                  const ShiftAxisEnum axis,
                                  const int threshold,
                                  const int delta)
{
    LayoutPositions result = positions;
    for (size_t i = 0; i < result.size(); ++i) {
        if (!isMovable(graph.getRooms()[i].role)) {
            continue;
        }
        const int value = (axis == ShiftAxisEnum::X) ? result[i].x : result[i].y;
        if (value < threshold) {
            continue;
        }
        if (axis == ShiftAxisEnum::X) {
            result[i].x += delta;
        } else {
            result[i].y += delta;
        }
    }
    return result;
}

/// Coordinates worth cutting at: every occupied line, plus one past the end,
/// so a column can be inserted after the last one too.
/// Coordinates worth cutting at: occupied lines, plus one past the end so a
/// column can be inserted after the last one too.
///
/// Sampled rather than exhaustive. Every candidate costs a full copy of the
/// layout and a full rescore, and that happens before any budget applies, so
/// a wide area could spend billions of operations just building the list of
/// things it might try. Spreading a fixed number of cuts across the span
/// keeps that bounded; a shift one column away from the ideal one is
/// normally repaired by the unit moves that follow anyway.
NODISCARD std::vector<int> cutPoints(const MapReshapeGraph &graph,
                                     const LayoutPositions &positions,
                                     const ShiftAxisEnum axis,
                                     const size_t maxCuts)
{
    std::set<int> values;
    for (size_t i = 0; i < positions.size(); ++i) {
        if (!isMovable(graph.getRooms()[i].role)) {
            continue;
        }
        values.insert((axis == ShiftAxisEnum::X) ? positions[i].x : positions[i].y);
    }
    if (values.empty()) {
        return {};
    }
    std::vector<int> all(values.begin(), values.end());
    all.push_back(*values.rbegin() + 1);

    if (maxCuts == 0 || all.size() <= maxCuts) {
        return all;
    }
    std::vector<int> sampled;
    sampled.reserve(maxCuts);
    for (size_t i = 0; i < maxCuts; ++i) {
        sampled.push_back(all[i * (all.size() - 1) / (maxCuts - 1)]);
    }
    sampled.erase(std::unique(sampled.begin(), sampled.end()), sampled.end());
    return sampled;
}

/// Groups of movable rooms joined to each other by horizontal exits.
///
/// These are effectively the storeys of a building: rooms that belong on one
/// layer because they are connected sideways. Moving one such group up or
/// down as a unit is the only way to restore or remove height, because
/// moving a single room out of its storey breaks every horizontal exit it
/// has -- which costs far more than the vertical exit it fixes. That is why
/// a hand-flattened building stays flat under unit moves however the weights
/// are set: the move it needs cannot be expressed one room at a time.
NODISCARD std::vector<std::vector<size_t>> horizontalGroups(const MapReshapeGraph &graph)
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
                // A group that touches an immovable room cannot travel, so
                // it is simply not grown through one.
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

/// Move a whole horizontal group one layer up or down.
NODISCARD LayoutPositions liftedGroup(const LayoutPositions &positions,
                                      const std::vector<size_t> &group,
                                      const int dz)
{
    LayoutPositions result = positions;
    for (const size_t index : group) {
        result[index].z += dz;
    }
    return result;
}

NODISCARD int64_t totalScore(const MapReshapeGraph &graph,
                             const LayoutPositions &positions,
                             const ReshapeWeights &weights)
{
    return MapReshapeScorer::score(graph, positions, weights).total();
}

/// Run unit-move hill climbing until nothing local helps.
///
/// Shared by the main search and by structural look-ahead, which has to know
/// what a shift is worth *after* the layout settles around it, not before.
void refineWithUnitMoves(const MapReshapeGraph &graph,
                         const ReshapeSolverOptions &options,
                         SearchState &state,
                         size_t &moves,
                         size_t &iterations,
                         const size_t iterationLimit,
                         ProgressCounter *const pc)
{
    const std::vector<LayoutRoom> &rooms = graph.getRooms();

    // Work queue rather than repeated full sweeps: only rooms whose
    // surroundings changed can have a new best move.
    std::deque<size_t> queue;
    std::vector<bool> queued(rooms.size(), false);
    for (size_t i = 0; i < rooms.size(); ++i) {
        if (state.canMove(i)) {
            queue.push_back(i);
            queued[i] = true;
        }
    }

    while (!queue.empty() && moves < options.maxMoves && iterations < iterationLimit) {
        ++iterations;
        if (pc != nullptr) {
            // Also how cancellation reaches the search: step() throws once
            // the user asks the task to stop.
            pc->step();
        }
        const size_t index = queue.front();
        queue.pop_front();
        queued[index] = false;

        int64_t bestDelta = 0;
        Coordinate bestTarget;
        bool found = false;
        for (const Coordinate &step : UNIT_MOVES) {
            if (options.freezeZ && step.z != 0) {
                continue;
            }
            const Coordinate target = state.positions()[index] + step;
            const int64_t delta = state.deltaFor(index, target);
            if (delta < bestDelta) {
                bestDelta = delta;
                bestTarget = target;
                found = true;
            }
        }
        if (!found) {
            continue;
        }

        state.apply(index, bestTarget);
        ++moves;

        // The moved room may now enable a better move for anything attached
        // to it, so put those back in the queue along with the room itself.
        const auto requeue = [&](const size_t other) {
            if (!queued[other] && state.canMove(other)) {
                queued[other] = true;
                queue.push_back(other);
            }
        };
        requeue(index);
        for (const size_t e : state.incidentEdges(index)) {
            const LayoutEdge &edge = graph.getHorizontalEdges()[e];
            requeue(edge.fromIndex == index ? edge.toIndex : edge.fromIndex);
        }
    }
}

/// Best row/column shift, judged by where the layout settles afterwards.
///
/// A shift is usually not an improvement on its own: opening a column costs a
/// little extra edge length immediately, and only pays off once a room moves
/// into the space that appeared. Judging shifts on their immediate score
/// therefore rejects exactly the ones worth making, so each candidate is
/// refined with unit moves before being compared.
///
/// Candidates that introduce collisions are dropped before that refinement.
/// Hill climbing will not walk a room back out through an occupied cell, so
/// refining from an overlapping start is wasted work.
NODISCARD std::optional<LayoutPositions> bestStructuralShift(
    const MapReshapeGraph &graph,
    const ReshapeSolverOptions &options,
    const LayoutPositions &current,
    size_t &moves,
    size_t &iterations,
    size_t &refinementBudget,
    const size_t iterationLimit,
    const std::vector<std::vector<size_t>> &groups,
    ProgressCounter *const pc)
{
    const ReshapeWeights &weights = options.weights;
    const int64_t base = MapReshapeScorer::score(graph, current, weights).total();

    // Shortlist first. Scoring a shift is cheap; refining one is not, so
    // rank every candidate and look ahead only on the most promising.
    //
    // Ranked ignoring collisions, though still judged on them afterwards.
    // Overlap is exactly what the refinement that follows is good at
    // clearing -- stepping a room off an occupied cell onto a free one is
    // the largest improvement available to it -- and a collision costs so
    // much that including it in the ranking buries every candidate that
    // needs it. Lowering a storey onto the floor below almost always lands
    // on something first, so dropping those outright meant a stacked
    // building could never be flattened at all.
    struct NODISCARD Candidate final
    {
        /// Immediate score with collisions left out, used only for ranking.
        int64_t rank = 0;
        LayoutPositions positions;
    };
    std::vector<Candidate> shortlist;

    for (const std::vector<size_t> &group : groups) {
        for (const int dz : {1, -1}) {
            LayoutPositions candidate = liftedGroup(current, group, dz);
            const LayoutScore score = MapReshapeScorer::score(graph, candidate, weights);
            shortlist.push_back(Candidate{score.total() - score.collision, std::move(candidate)});
        }
    }

    for (const ShiftAxisEnum axis : {ShiftAxisEnum::X, ShiftAxisEnum::Y}) {
        for (const int threshold : cutPoints(graph, current, axis, options.maxCutPoints)) {
            for (const int delta : {1, -1}) {
                LayoutPositions candidate = shifted(graph, current, axis, threshold, delta);
                if (candidate == current) {
                    continue;
                }
                const LayoutScore score = MapReshapeScorer::score(graph, candidate, weights);
                shortlist.push_back(
                    Candidate{score.total() - score.collision, std::move(candidate)});
            }
        }
    }

    const size_t budget = std::min(shortlist.size(), options.maxStructuralCandidates);
    std::partial_sort(shortlist.begin(),
                      shortlist.begin() + static_cast<std::ptrdiff_t>(budget),
                      shortlist.end(),
                      [](const Candidate &a, const Candidate &b) { return a.rank < b.rank; });

    std::optional<LayoutPositions> best;
    int64_t bestScore = base;
    for (size_t i = 0; i < budget; ++i) {
        if (iterations >= iterationLimit || moves >= options.maxMoves || refinementBudget == 0) {
            break;
        }
        --refinementBudget;
        // Counted before the nested search rather than after it, so the
        // check at the top of this loop keeps the total within the budget
        // instead of overshooting by one per candidate.
        ++iterations;
        SearchState trial{graph, weights};
        trial.reset(std::move(shortlist[i].positions));
        // Counted against the same budget as everything else. Giving each
        // trial its own fresh allowance meant the real bound was the budget
        // times the number of trials, which only stayed invisible while
        // frozen z let every refinement converge almost immediately.
        refineWithUnitMoves(graph, options, trial, moves, iterations, iterationLimit, pc);

        const int64_t score = MapReshapeScorer::score(graph, trial.positions(), weights).total();
        if (score < bestScore) {
            bestScore = score;
            best = trial.positions();
        }
    }
    if (best) {
        ++moves;
    }
    return best;
}

NODISCARD size_t countCollisions(const MapReshapeGraph &graph, const LayoutPositions &positions)
{
    size_t count = 0;
    for (const LayoutIssue &issue : MapReshapeScorer::findIssues(graph, positions)) {
        if (issue.kind == LayoutIssueEnum::Collision) {
            ++count;
        }
    }
    return count;
}

} // namespace

LayoutPositions MapReshapeSolver::solvePositions(const MapReshapeGraph &graph,
                                                 const ReshapeSolverOptions &options,
                                                 ReshapeResult &resultOut,
                                                 ProgressCounter *const pc)
{
    const std::vector<LayoutRoom> &rooms = graph.getRooms();
    const LayoutPositions before = MapReshapeScorer::currentPositions(graph);

    resultOut = ReshapeResult{};
    resultOut.stats = graph.computeStatistics();
    resultOut.stats.scoreBefore = MapReshapeScorer::score(graph, before, options.weights).total();
    // Counted the same way as conflictsAfter, so the two are comparable.
    // computeStatistics() only sees in-scope overlaps, while findIssues also
    // reports rooms standing on external fixed rooms.
    resultOut.stats.conflictsBefore = countCollisions(graph, before);

    if (graph.empty()) {
        resultOut.status = ReshapeStatusEnum::Unchanged;
        resultOut.stats.scoreAfter = resultOut.stats.scoreBefore;
        return before;
    }

    SearchState state{graph, options.weights};
    size_t moves = 0;
    size_t iterations = 0;
    const size_t iterationLimit = options.maxSweeps * std::max<size_t>(1, rooms.size());
    ReshapeSolverOptions effective = options;
    if (effective.maxMoves == 0) {
        // Scaled to the scope so that every room can actually travel, rather
        // than the first rooms in the queue spending the whole allowance.
        effective.maxMoves = iterationLimit;
    }

    // Two nested searches. The inner one nudges single rooms until nothing
    // local helps; the outer one then tries shifting a whole row or column,
    // which is the only way to make space that is not already there. A
    // successful shift opens up new local improvements, so the pair repeats.
    if (pc != nullptr) {
        pc->setCurrentTask(ProgressMsg{"reshaping layout"});
        pc->increaseTotalStepsBy(iterationLimit);
    }
    const std::vector<std::vector<size_t>> groups = horizontalGroups(graph);
    refineWithUnitMoves(graph, effective, state, moves, iterations, iterationLimit, pc);
    size_t refinementBudget = effective.maxStructuralRefinements;
    if (refinementBudget == 0) {
        // Each look-ahead costs about one small solve, so keep budget times
        // room count roughly constant.
        constexpr size_t TOTAL_WORK = 60'000;
        constexpr size_t MIN_BUDGET = 32;
        constexpr size_t MAX_BUDGET = 512;
        refinementBudget = std::clamp(TOTAL_WORK / std::max<size_t>(1, rooms.size()),
                                      MIN_BUDGET,
                                      MAX_BUDGET);
    }
    while (effective.allowStructuralMoves && moves < effective.maxMoves
           && iterations < iterationLimit && refinementBudget > 0) {
        auto shift = bestStructuralShift(graph,
                                         effective,
                                         state.positions(),
                                         moves,
                                         iterations,
                                         refinementBudget,
                                         iterationLimit,
                                         groups,
                                         pc);
        if (!shift) {
            break;
        }
        state.reset(std::move(*shift));
    }

    const LayoutPositions &after = state.positions();
    resultOut.stats.iterations = iterations;
    resultOut.stats.hitLimits = (iterations >= iterationLimit) || (moves >= effective.maxMoves);
    resultOut.stats.scoreAfter = MapReshapeScorer::score(graph, after, effective.weights).total();

    for (size_t i = 0; i < rooms.size(); ++i) {
        if (after[i] != before[i]) {
            resultOut.moves.push_back(RoomMove{rooms[i].id, before[i], after[i]});
        }
    }
    resultOut.stats.roomsMoved = resultOut.moves.size();
    {
        // Was equal to zLevelsBefore while rooms could not change layer.
        std::set<int> layers;
        for (size_t i = 0; i < rooms.size(); ++i) {
            if (rooms[i].role != LayoutRoomRoleEnum::FixedExternal) {
                layers.insert(after[i].z);
            }
        }
        resultOut.stats.zLevelsAfter = layers.size();
    }

    for (const LayoutIssue &issue : MapReshapeScorer::findIssues(graph, after)) {
        if (issue.kind == LayoutIssueEnum::Collision) {
            resultOut.conflicts.push_back(
                ReshapeConflict{issue.from, issue.to, "rooms share the same coordinate"});
        }
    }
    resultOut.stats.conflictsAfter = resultOut.conflicts.size();

    if (resultOut.stats.conflictsAfter > resultOut.stats.conflictsBefore) {
        // Refusing to return a layout that is worse than what it replaced is
        // the point of reporting infeasibility: a malformed result is worse
        // than no result.
        resultOut.status = ReshapeStatusEnum::InfeasibleWithCurrentBoundary;
        return before;
    }
    if (resultOut.moves.empty() || resultOut.stats.scoreAfter >= resultOut.stats.scoreBefore) {
        resultOut.status = ReshapeStatusEnum::Unchanged;
        resultOut.moves.clear();
        resultOut.stats.roomsMoved = 0;
        return before;
    }

    resultOut.status = ReshapeStatusEnum::Improved;
    return after;
}

ReshapeResult MapReshapeSolver::solve(const MapReshapeGraph &graph,
                                      const ReshapeSolverOptions &options,
                                      ProgressCounter *const pc)
{
    ReshapeResult result;
    (void) solvePositions(graph, options, result, pc);
    return result;
}
