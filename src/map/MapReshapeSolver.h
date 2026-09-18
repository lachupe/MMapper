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
    ReshapeWeights weights;

    /// Cap on accepted moves, so a pathological case terminates rather than
    /// running until someone kills MMapper.
    size_t maxMoves = 100'000;
    /// Cap on passes over the room set. Each pass is one chance for every
    /// movable room to improve.
    size_t maxSweeps = 500;

    /// Keep z where it is. The first milestone optimizes x/y only; the
    /// machinery below is written so that lifting this does not require
    /// restructuring the search.
    bool freezeZ = true;
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
