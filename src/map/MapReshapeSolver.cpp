// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "MapReshapeSolver.h"

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
const std::array<Coordinate, 4> UNIT_MOVES = {Coordinate{1, 0, 0},
                                              Coordinate{-1, 0, 0},
                                              Coordinate{0, 1, 0},
                                              Coordinate{0, -1, 0}};

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
    /// Rooms currently standing on each cell.
    std::unordered_map<Coordinate, int64_t> m_cellCounts;
    /// x and y tallies for movable rooms, so the bounding box that drives the
    /// compactness term can be read off the ends.
    std::map<int, size_t> m_xs;
    std::map<int, size_t> m_ys;

public:
    SearchState(const MapReshapeGraph &graph, const ReshapeWeights &weights)
        : m_graph{graph}
        , m_weights{weights}
        , m_positions{MapReshapeScorer::currentPositions(graph)}
        , m_incident(graph.getRooms().size())
    {
        const auto &edges = graph.getHorizontalEdges();
        for (size_t e = 0; e < edges.size(); ++e) {
            m_incident[edges[e].fromIndex].push_back(e);
            if (edges[e].toIndex != edges[e].fromIndex) {
                m_incident[edges[e].toIndex].push_back(e);
            }
        }
        for (size_t i = 0; i < m_positions.size(); ++i) {
            ++m_cellCounts[m_positions[i]];
            if (isMovable(graph.getRooms()[i].role)) {
                ++m_xs[m_positions[i].x];
                ++m_ys[m_positions[i].y];
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
        for (size_t i = 0; i < m_positions.size(); ++i) {
            ++m_cellCounts[m_positions[i]];
            if (canMove(i)) {
                ++m_xs[m_positions[i].x];
                ++m_ys[m_positions[i].y];
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

        delta += collisionDelta(index, from, to);
        delta += compactnessDelta(index, from, to);
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
        }
    }

    void occupy(const Coordinate &cell, const size_t index)
    {
        ++m_cellCounts[cell];
        if (canMove(index)) {
            ++m_xs[cell.x];
            ++m_ys[cell.y];
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
NODISCARD std::vector<int> cutPoints(const MapReshapeGraph &graph,
                                     const LayoutPositions &positions,
                                     const ShiftAxisEnum axis)
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
    std::vector<int> result(values.begin(), values.end());
    result.push_back(*values.rbegin() + 1);
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
                         const size_t iterationLimit)
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
NODISCARD std::optional<LayoutPositions> bestStructuralShift(const MapReshapeGraph &graph,
                                                             const ReshapeSolverOptions &options,
                                                             const LayoutPositions &current,
                                                             size_t &moves,
                                                             size_t &iterations,
                                                             size_t &refinementBudget,
                                                             const size_t iterationLimit)
{
    const ReshapeWeights &weights = options.weights;
    const LayoutScore baseScore = MapReshapeScorer::score(graph, current, weights);
    const int64_t base = baseScore.total();

    // Shortlist first. Scoring a shift is cheap; refining one is not, so
    // rank every candidate by its immediate score and look ahead only on the
    // most promising. Candidates that introduce collisions are dropped here:
    // hill climbing will not walk a room back out through an occupied cell,
    // so refining from an overlapping start is wasted work.
    struct NODISCARD Candidate final
    {
        int64_t immediateScore = 0;
        LayoutPositions positions;
    };
    std::vector<Candidate> shortlist;

    for (const ShiftAxisEnum axis : {ShiftAxisEnum::X, ShiftAxisEnum::Y}) {
        for (const int threshold : cutPoints(graph, current, axis)) {
            for (const int delta : {1, -1}) {
                LayoutPositions candidate = shifted(graph, current, axis, threshold, delta);
                if (candidate == current) {
                    continue;
                }
                const LayoutScore score = MapReshapeScorer::score(graph, candidate, weights);
                if (score.collision > baseScore.collision) {
                    continue;
                }
                shortlist.push_back(Candidate{score.total(), std::move(candidate)});
            }
        }
    }

    const size_t budget = std::min(shortlist.size(), options.maxStructuralCandidates);
    std::partial_sort(shortlist.begin(),
                      shortlist.begin() + static_cast<std::ptrdiff_t>(budget),
                      shortlist.end(),
                      [](const Candidate &a, const Candidate &b) {
                          return a.immediateScore < b.immediateScore;
                      });

    std::optional<LayoutPositions> best;
    int64_t bestScore = base;
    for (size_t i = 0; i < budget; ++i) {
        if (iterations >= iterationLimit || moves >= options.maxMoves || refinementBudget == 0) {
            break;
        }
        --refinementBudget;
        SearchState trial{graph, weights};
        trial.reset(std::move(shortlist[i].positions));
        size_t trialMoves = 0;
        size_t trialIterations = 0;
        refineWithUnitMoves(graph, options, trial, trialMoves, trialIterations, iterationLimit);
        iterations += trialIterations + 1;

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
                                                 ReshapeResult &resultOut)
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

    // Two nested searches. The inner one nudges single rooms until nothing
    // local helps; the outer one then tries shifting a whole row or column,
    // which is the only way to make space that is not already there. A
    // successful shift opens up new local improvements, so the pair repeats.
    refineWithUnitMoves(graph, options, state, moves, iterations, iterationLimit);
    size_t refinementBudget = options.maxStructuralRefinements;
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
    while (options.allowStructuralMoves && moves < options.maxMoves && iterations < iterationLimit
           && refinementBudget > 0) {
        auto shift = bestStructuralShift(graph,
                                         options,
                                         state.positions(),
                                         moves,
                                         iterations,
                                         refinementBudget,
                                         iterationLimit);
        if (!shift) {
            break;
        }
        state.reset(std::move(*shift));
    }

    const LayoutPositions &after = state.positions();
    resultOut.stats.iterations = iterations;
    resultOut.stats.scoreAfter = MapReshapeScorer::score(graph, after, options.weights).total();

    for (size_t i = 0; i < rooms.size(); ++i) {
        if (after[i] != before[i]) {
            resultOut.moves.push_back(RoomMove{rooms[i].id, before[i], after[i]});
        }
    }
    resultOut.stats.roomsMoved = resultOut.moves.size();
    resultOut.stats.zLevelsAfter = resultOut.stats.zLevelsBefore;

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
                                      const ReshapeSolverOptions &options)
{
    ReshapeResult result;
    (void) solvePositions(graph, options, result);
    return result;
}
