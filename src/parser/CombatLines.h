#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include <QString>

/// What happened in a fight, read off one line of MUME's output.
///
/// GMCP carries the state of a fight -- Char.Vitals says who your opponent is and how it is
/// doing, Room.Chars says who is fighting whom -- but not the events: which blow landed, which
/// was parried, who panicked and ran. MUME's XML brackets each blow in <hit> or <miss> and
/// names the people in it, and MumeXmlParser relays those elements as MMapper.Xml.Element for
/// a client's combat log and participants; but where a blow landed and how hard, and the
/// flights, bashes and deaths, are in the text. So the text is recognised here, once, and
/// handed on as MMapper.Combat.Event, the events a client animates from, rather than every
/// frontend writing its own patterns against MUME's prose.
///
/// The grammar is MUME's, not the player's: a blow reads "<attacker> [quality] <verb>
/// <target>'s <part> [severity] [and <effect> it]." whatever the settings. What settings do
/// change is what is shown at all -- a player can turn misses off -- so a missing event is
/// not evidence that nothing happened, and a leading run of the prompt's twiddler
/// characters is tolerated because the "twiddlers" prompt option glues one onto a line.
enum class NODISCARD CombatKindEnum : uint8_t {
    /// a blow and how it landed
    BLOW,
    /// somebody trying to leave the fight, and whether they managed it
    FLEE,
    /// a move MUME refused because the character is engaged
    REFUSED,
    /// knocked down by a bash, or bashing someone
    BASH,
    /// stabbed in the back by somebody who was not visible until then
    BACKSTAB,
    /// a condition line: incapacitated, mortally wounded, stunned
    CONDITION,
    /// "is dead! R.I.P."
    DEATH,
    /// a delayed action starting, going off, or being broken
    CAST,
    /// the player's own state changing in a way the prompt may not show: stunned, sleepy,
    /// standing up again
    SELF
};

enum class NODISCARD BlowOutcomeEnum : uint8_t { HIT, PARRY, DODGE, MISS };

/// For FLEE: an attempt seen from outside, a failure, or getting away.
/// For CAST: started, went off, broken.
/// For SELF: stunned, sleepy, stood.
enum class NODISCARD CombatPhaseEnum : uint8_t {
    NONE,
    ATTEMPT,
    FAILED,
    ESCAPED,
    STARTED,
    DONE,
    BROKEN,
    STUNNED,
    SLEEPY,
    STOOD
};

struct NODISCARD CombatEvent final
{
    CombatKindEnum kind = CombatKindEnum::BLOW;
    BlowOutcomeEnum outcome = BlowOutcomeEnum::HIT;
    CombatPhaseEnum phase = CombatPhaseEnum::NONE;
    /// "you", or the name as MUME wrote it -- "the one-eyed orc", "*a Man*", "Kazadoe" -- with
    /// any group label such as "(K)" taken off. Empty when the line does not say.
    QString actor;
    QString target;
    /// Base form: "slash", not "slashes".
    QString verb;
    /// "left arm", "body".
    QString part;
    /// Before the verb: "barely", "lightly", "strongly".
    QString quality;
    /// After the part: "hard", "very hard", "extremely hard".
    QString severity;
    /// Base form: "shatter", "tickle".
    QString effect;
    /// FLEE: the direction fled in. CONDITION: the condition. CAST: the words uttered.
    QString detail;
    /// The line as it was recognised, twiddlers removed.
    QString text;
};

/// The event this line describes, or nothing when it is not a fight line.
NODISCARD std::optional<CombatEvent> parseCombatLine(const QString &line);

NODISCARD std::string_view to_string_view(CombatKindEnum kind);
NODISCARD std::string_view to_string_view(BlowOutcomeEnum outcome);
NODISCARD std::string_view to_string_view(CombatPhaseEnum phase);
