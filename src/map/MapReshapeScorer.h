#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "ExitDirection.h"
#include "MapReshapeGraph.h"
#include "MapReshapeTypes.h"
#include "coordinate.h"
#include "roomid.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

/// Candidate coordinates, parallel to MapReshapeGraph::getRooms().
///
/// The graph holds the problem (topology, roles, obstacles) and never changes
/// during a solve; positions hold the candidate solution. Keeping them apart
/// lets a local search score a trial layout without touching the graph, and
/// lets the same graph be scored against several candidates.
using LayoutPositions = std::vector<Coordinate>;

/// What a specific piece of the layout gets wrong. Reported for diagnostics;
/// scoring aggregates the same conditions numerically.
enum class NODISCARD LayoutIssueEnum : uint8_t {
    /// An exit points to the wrong side, e.g. east to a room further west.
    WrongDirection,
    /// A NESW exit sits off its axis: north/south want equal x, east/west
    /// want equal y.
    Misaligned,
    /// A horizontal exit spans more than one cell. Not wrong -- spacing is
    /// elastic -- but preferably shorter.
    Overlong,
    /// A NESW exit whose endpoints sit on different z layers.
    LayerMismatch,
    /// Two rooms on the same cell.
    Collision,
    /// A pinned or external room is not where it started.
    ImmovableMoved
};

NODISCARD extern std::string_view to_string_view(LayoutIssueEnum kind);

struct NODISCARD LayoutIssue final
{
    LayoutIssueEnum kind = LayoutIssueEnum::WrongDirection;
    RoomId from = INVALID_ROOMID;
    /// INVALID_ROOMID for issues about a single room.
    RoomId to = INVALID_ROOMID;
    ExitDirEnum dir = ExitDirEnum::NONE;
    /// Signed offset from `from` to `to`, for edge issues.
    Coordinate delta;
    /// How far off, in cells: perpendicular offset, excess length, or layers.
    int amount = 0;
};

/// Scores a layout without modifying anything.
///
/// Scoring exists before any optimizer so the model can be checked against
/// real areas first: if the scorer disagrees with a human's sense of which
/// areas look bad, the weights or the model are wrong, and finding that out
/// before writing a solver is much cheaper than after.
class NODISCARD MapReshapeScorer final
{
public:
    /// The layout as it stands in the map today.
    NODISCARD static LayoutPositions currentPositions(const MapReshapeGraph &graph);

    NODISCARD static LayoutScore score(const MapReshapeGraph &graph,
                                       const LayoutPositions &positions,
                                       const ReshapeWeights &weights = {});

    /// Everything wrong with the layout, in graph order.
    NODISCARD static std::vector<LayoutIssue> findIssues(const MapReshapeGraph &graph,
                                                         const LayoutPositions &positions);

    /// One edge's contribution, split the same way the total is.
    ///
    /// Both the scorer's breakdown and the solver's incremental delta are
    /// built from this, because two copies of the arithmetic drift: an
    /// earlier version had the solver charging alignment by angle while the
    /// scorer still charged it by raw offset, so the search optimized one
    /// function and was judged against another.
    NODISCARD static LayoutScore edgeBreakdown(const LayoutEdge &edge,
                                               const Coordinate &from,
                                               const Coordinate &to,
                                               const ReshapeWeights &weights);

    /// Cost of one horizontal edge at the given endpoint positions.
    ///
    /// Exposed because the solver evaluates candidate moves incrementally and
    /// must charge each edge exactly what the scorer would. Two copies of this
    /// arithmetic would eventually disagree, and a solver optimizing a
    /// different function than the one being reported is a hard bug to see.
    NODISCARD static int64_t edgeCost(const LayoutEdge &edge,
                                      const Coordinate &from,
                                      const Coordinate &to,
                                      const ReshapeWeights &weights);

    /// Cost of one room sitting at `pos` instead of where it started.
    /// Horizontal displacement only; z is charged separately so diagnostics
    /// can tell "nudged sideways" apart from "moved to another layer".
    NODISCARD static int64_t movementCost(const LayoutRoom &room,
                                          const Coordinate &pos,
                                          const ReshapeWeights &weights);

    /// Cost of one room having left its original layer.
    NODISCARD static int64_t zMovementCost(const LayoutRoom &room,
                                           const Coordinate &pos,
                                           const ReshapeWeights &weights);

    /// Cost of one up/down exit at the given endpoint positions.
    ///
    /// Zero weight when flattening, where a staircase drawn as two adjacent
    /// rooms on one layer is the intended result rather than a defect.
    NODISCARD static int64_t verticalEdgeCost(const LayoutEdge &edge,
                                              const Coordinate &from,
                                              const Coordinate &to,
                                              const ReshapeWeights &weights);

    /// Human-readable breakdown for `_map area reshape`-style diagnostics.
    static void printReport(const MapReshapeGraph &graph,
                            const LayoutPositions &positions,
                            std::ostream &os,
                            const ReshapeWeights &weights = {});
};
