#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "ContainerLines.h"

#include <optional>

#include <QString>
#include <QStringList>

/// A command of the player's that MUME refused, or answered "already" (MMapper.Char.Refused).
///
/// MUME says no in prose, one sentence per reason, and the sentence seldom names the command.
/// The moves and attacks it refuses are MMapper.Combat.Event `refused` (CombatLines.cpp:
/// g_refusals, g_rescueRefusals, g_assistRefused, g_backstabRefusals, g_castRefused,
/// g_castFraming), the answers to `order` are MMapper.Char.Followers `reply`, a refused practice
/// is MMapper.Guild.Practised, and a refusal about a chest is MMapper.Room.Container. This is
/// the rest: the sentences those readers leave alone, so that no line is two events. The one
/// exception is a ride refused ("Oops! You cannot go there riding!"), which is both a
/// Combat.Event `refused` `cannot-ride` and, here, `noride` with the side the move went.
///
/// The table is mume3d's docs/action-rules.md section 1 (the powwow logs of 2005-2006, cited
/// row by row in CharRefused.cpp); a line is matched whole. None was seen live.
struct NODISCARD CharRefused final
{
    /// The frontend's action the sentence belongs to: move, flee, rescue, assist, kill, bash,
    /// kick, backstab, cast, disengage, stand, rest, sleep, wake, ride, lead, dismount, order,
    /// camp, track, time, look, charge, butcher, whet, open, close, lock, unlock, pick. Empty
    /// when the sentence answers several commands and does not say which.
    QString action;
    /// Why, as a stable code; see refusedReasons().
    QString reason;
    /// MUME's line as received, colour removed.
    QString text;
    /// Who or what the line names: the mount, the door, the one in the way, the thing missing.
    /// Empty when it names none.
    QString target;
    /// For a move and for a door: the side, one letter (n e s w u d). Empty when not known.
    QString dir;
};

/// The refusal `line` is (colour removed, matched whole), or nullopt. `dir` is left empty: the
/// line never says it.
NODISCARD std::optional<CharRefused> parseRefusedLine(const QString &line);

/// The refusal a door command's answer is ("It seems to be locked." after `open exit e`), or
/// nullopt when the answer is no refusal ("Ok.", "*click*").
NODISCARD std::optional<CharRefused> refusedFromDoorReply(const DoorReply &reply);

/// Every reason code parseRefusedLine() and refusedFromDoorReply() can give, for the tests and
/// the documentation.
NODISCARD QStringList refusedReasons();
