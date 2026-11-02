#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "CombatLines.h"
#include "ContainerLines.h"

#include <optional>

#include <QString>
#include <QStringList>

/// A command of the player's that MUME refused, or answered "already" (MMapper.Char.Refused).
///
/// MUME says no in prose, one sentence per reason, and the sentence seldom names the command.
/// This is every refusal MMapper reads, in one shape, so that a frontend learns refusals from one
/// package and never from the text: the table in CharRefused.cpp (mume3d's docs/action-rules.md
/// section 1, the powwow logs of 2005-2006, cited row by row; matched whole, none seen live), a
/// door's refusals (refusedFromDoorReply()), and the refusals the fight reader reads
/// (refusedFromCombat(): MMapper.Combat.Event `refused`, and `cast`, `rescue`, `assist`,
/// `backstab` with phase `refused`). A sentence that another package also tells -- those
/// Combat.Events, a follow, protect or order answer in MMapper.Char.Followers, a practice refused
/// in MMapper.Guild.Practised -- is sent there too; one line is never two Char.Refused.
struct NODISCARD CharRefused final
{
    /// The frontend's action the sentence belongs to: move, flee, rescue, assist, kill, bash,
    /// kick, backstab, cast, disengage, stand, rest, sleep, wake, ride, lead, dismount, order,
    /// camp, track, time, look, charge, butcher, whet, open, close, lock, unlock, pick, follow,
    /// protect, practise, shoot. Empty when the sentence answers several commands and does not
    /// say which.
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

/// The refusal a fight line is (MMapper.Combat.Event `refused`, or `cast`, `rescue`, `assist`,
/// `backstab` with phase `refused`), or nullopt. The ride refused (`cannot-ride`) is nullopt here:
/// parseRefusedLine() has it as `noride`.
NODISCARD std::optional<CharRefused> refusedFromCombat(const CombatEvent &event);

/// The one Char.Refused a line of MUME's is, given what the fight reader made of it (`combat`,
/// parseCombatLine()): parseRefusedLine()'s reading, else refusedFromCombat()'s, else nullopt.
/// What MumeXmlParser publishes for each line, the door replies aside.
NODISCARD std::optional<CharRefused> refusalOfLine(const QString &line,
                                                   const std::optional<CombatEvent> &combat);

/// The refusal a door command's answer is ("It seems to be locked." after `open exit e`), or
/// nullopt when the answer is no refusal ("Ok.", "*click*").
NODISCARD std::optional<CharRefused> refusedFromDoorReply(const DoorReply &reply);

/// Every reason code parseRefusedLine() and refusedFromDoorReply() can give, for the tests and
/// the documentation.
NODISCARD QStringList refusedReasons();
