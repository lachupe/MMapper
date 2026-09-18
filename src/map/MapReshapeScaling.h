#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "MapReshapeGraph.h"
#include "MapReshapeScorer.h"
#include "roomid.h"

#include <cstddef>
#include <ostream>
#include <vector>

/// A group of rooms that would be better drawn at a reduced scale.
///
/// The rigid global grid gives every room one cell, so a place that is
/// logically dense -- a city, a keep, the inside of a mountain -- can only be
/// drawn by pushing its rooms apart until they fit, which is what makes such
/// places sprawl across a map far beyond the ground they occupy. Every exit
/// inside them ends up several cells long.
///
/// The alternative is to draw the group smaller rather than spread it out:
/// give it a footprint in the outer map and render its own layout shrunk to
/// fit. Rendering that way needs local coordinate spaces, which mainline
/// MMapper does not have; detecting where it would help does not, and is
/// useful on its own, because a group flagged here is exactly a place the
/// reshaper is currently making worse by stretching.
struct NODISCARD ScalingCandidate final
{
    std::vector<RoomId> rooms;

    /// Cells the group spans after solving, and how many rooms are in it.
    int spanWidth = 0;
    int spanHeight = 0;

    /// Mean length of the exits inside the group. One means the group fits
    /// the grid naturally; larger means it is being held open.
    double averageEdgeLength = 1.0;

    /// Scale at which the group's own exits would read as one cell again.
    /// This is the figure a local space's portal would be sized from.
    NODISCARD double suggestedScale() const
    {
        return (averageEdgeLength > 0.0) ? (1.0 / averageEdgeLength) : 1.0;
    }

    /// Footprint the group would occupy at that scale, rounded up.
    NODISCARD int scaledWidth() const;
    NODISCARD int scaledHeight() const;
};

class NODISCARD MapReshapeScaling final
{
public:
    /// Groups whose internal exits had to stretch well past one cell.
    ///
    /// Measured on exits rather than on room density, because sparseness on
    /// its own is not evidence of anything: a road network is legitimately
    /// spread out, and its exits are still one cell long. A group only shows
    /// up here when its own connections were forced apart.
    NODISCARD static std::vector<ScalingCandidate> findCandidates(const MapReshapeGraph &graph,
                                                                  const LayoutPositions &positions,
                                                                  double stretchThreshold = 1.5,
                                                                  size_t minimumRooms = 8);

    static void printReport(const MapReshapeGraph &graph,
                            const LayoutPositions &positions,
                            std::ostream &os);
};
