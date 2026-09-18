#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "MapReshapeGraph.h"
#include "MapReshapeScorer.h"
#include "MapReshapeTypes.h"

#include <cstddef>

struct NODISCARD ReshapeSolverOptions final
{
    /// Prefer forMode() over setting this and `weights` separately: changing
    /// one without the other silently solves for the wrong objective.
    ReshapeModeEnum mode = ReshapeModeEnum::Flatten;
    ReshapeWeights weights = makeWeights(ReshapeModeEnum::Flatten);

    /// Cap on accepted moves, so a pathological case terminates rather than
    /// running until someone kills MMapper.
    size_t maxMoves = 100'000;
    /// Cap on passes over the room set. Each pass is one chance for every
    /// movable room to improve.
    size_t maxSweeps = 500;

    /// Keep every room on its current layer. Both modes need this off --
    /// flattening means moving rooms down, and volumetric layout means
    /// moving them up -- so it exists only to isolate the horizontal search
    /// in tests.
    bool freezeZ = false;

    /// Allow whole rows and columns to be inserted or removed, not just
    /// single rooms nudged. Off only for testing the unit-move search in
    /// isolation.
    bool allowStructuralMoves = true;

    /// How many candidate shifts to look ahead on per round.
    ///
    /// Judging a shift means letting unit moves settle around it first, which
    /// costs roughly as much as a small solve. An area offers one candidate
    /// per occupied row and column in each direction, so refining all of them
    /// dominates the runtime: measured on a 900-room lattice, unbounded
    /// look-ahead took 26 seconds against 87 milliseconds for unit moves
    /// alone. Candidates are ranked by their immediate score and only the
    /// most promising are explored. That ranking is a weak predictor -- the
    /// whole reason look-ahead exists is that the best shift usually looks
    /// slightly bad at first -- so this trades some quality for bounded time,
    /// and the bound is deliberately generous.
    size_t maxStructuralCandidates = 32;

    /// Total look-aheads allowed across the whole solve, or 0 to scale it to
    /// the size of the area.
    ///
    /// Capping candidates per round is not enough on its own: exploring the
    /// most promising ones first means more rounds succeed, so the solve runs
    /// longer rather than shorter. This bounds the expensive operation
    /// directly, whichever round it happens in.
    ///
    /// A fixed budget suits one size of area and not others, because each
    /// look-ahead costs roughly in proportion to the room count while small
    /// areas need many more of them to grind a layout down. Measured on
    /// lattices, a flat budget of 120 left a 900-room area's result
    /// unchanged but made a 100-room area markedly worse. Scaling the budget
    /// inversely with room count keeps total work roughly constant instead.
    size_t maxStructuralRefinements = 0;

    NODISCARD static ReshapeSolverOptions forMode(const ReshapeModeEnum mode)
    {
        ReshapeSolverOptions options;
        options.mode = mode;
        options.weights = makeWeights(mode);
        return options;
    }
};

/// Local search over room positions.
///
/// Deliberately a small domain-specific optimizer rather than a general
/// graph-layout library: the constraints here (elastic spacing, semantic exit
/// directions, immovable surroundings, expensive layers) are not what
/// force-directed layout optimizes for, and the result has to land on integer
/// grid cells without collisions.
///
/// The search is first-improvement hill climbing over unit moves, driven by a
/// work queue: when a room moves, its graph neighbours are re-queued because
/// their best move may have changed. Candidate moves are evaluated with an
/// incremental score delta rather than by rescoring the whole layout, which
/// is what makes an area of a few thousand rooms tractable.
///
/// Unit moves alone cannot open up space: shifting one room into a crowded
/// neighbourhood just trades one problem for another. So the search also
/// offers structural moves -- shift every movable room on one side of a given
/// column or row by one cell. That inserts an empty column where the grid is
/// too tight, or closes one where the spacing serves no purpose, without
/// inventing rooms to fill it. Those candidates are priced by rescoring the
/// whole layout rather than incrementally: they move many rooms at once, and
/// they are tried far less often than unit moves.
///
/// Known limitation of unit moves: two rooms that need to swap sides cannot
/// do so if they share a row or column. The one that must pass would have to
/// step through the other's cell, and that intermediate state costs a
/// collision, which hill climbing will not accept. Escaping that needs the
/// structural moves -- row/column insertion and connected-group translation --
/// rather than a smarter search over the same move set.
///
/// Nothing here touches the map. The result is a list of proposed moves for
/// the caller to apply, or not.
class NODISCARD MapReshapeSolver final
{
public:
    NODISCARD static ReshapeResult solve(const MapReshapeGraph &graph,
                                         const ReshapeSolverOptions &options = {});

    /// The solved layout itself, for callers that want to inspect or rescore
    /// it rather than apply it. Parallel to MapReshapeGraph::getRooms().
    NODISCARD static LayoutPositions solvePositions(const MapReshapeGraph &graph,
                                                    const ReshapeSolverOptions &options,
                                                    ReshapeResult &resultOut);
};
