#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "ChangeList.h"
#include "Map.h"
#include "MapReshapeTypes.h"

#include <vector>

namespace map_reshape {

/// Turn solved room movements into changes that can be applied safely.
///
/// A reshape cannot be applied as a plain sequence of per-room moves. Changes
/// are applied one at a time, so a room frequently lands on a cell that
/// another room has not vacated yet -- shifting a row east moves the first
/// room onto the second's cell before the second has moved. The final layout
/// is collision free, but the sequence getting there is not, and MMapper's
/// spatial index cannot represent two rooms on one cell: it keeps only the
/// last room assigned to a coordinate, so the earlier one silently drops out
/// of the index. The resulting map has correct room positions but a corrupt
/// coordinate lookup, and fails a consistency check.
///
/// So the movement is staged. Every moving room is first translated as one
/// rigid group into empty space beyond the map's bounds, which MMapper can do
/// atomically, and only then moved individually to its final cell. By that
/// point every destination is genuinely empty, so no intermediate state ever
/// doubles up.
///
/// Returns an empty list when there is nothing to move.
NODISCARD ChangeList buildChanges(const Map &map, const std::vector<RoomMove> &moves);

} // namespace map_reshape
