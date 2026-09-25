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
    /// a move MUME refused, with the reason in detail: engaged in a fight, too tired, not on
    /// one's feet, a mount that will not go, a closed door, no exit
    REFUSED,
    /// knocked down by a bash, or bashing someone; a bash dodged, which puts the one who
    /// tried it on the ground instead; and the player getting over being bashed
    BASH,
    /// stabbed in the back by somebody who was not visible until then
    BACKSTAB,
    /// a condition line: incapacitated, mortally wounded, stunned
    CONDITION,
    /// "is dead! R.I.P.", and the player's own "You are dead! Sorry..."
    DEATH,
    /// a delayed action starting, going off, being broken, or refused before it began
    CAST,
    /// the player's own state changing in a way the prompt may not show: stunned, sleepy,
    /// standing up again
    SELF
};

enum class NODISCARD BlowOutcomeEnum : uint8_t { HIT, PARRY, DODGE, MISS };

/// For FLEE: an attempt seen from outside, a failure, or getting away.
/// For CAST: started, went off, broken, or refused before it started.
/// For BASH: none for a bash that landed, dodged for one that missed and floored the one who
/// tried it, recovered for the player getting over one.
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
    STOOD,
    REFUSED,
    DODGED,
    RECOVERED
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
    /// FLEE: the direction fled in. CONDITION: the condition. CAST: the words uttered, the
    /// spell named, or why it was refused. REFUSED: why the move was refused, one of the
    /// words listed at parseCombatLine().
    QString detail;
    /// The line as it was recognised, twiddlers removed.
    QString text;
};

/// The event this line describes, or nothing when it is not a fight line.
///
/// A refused move's detail is one of: fighting, exhausted, mount-exhausted, mount-refuses,
/// thrown, resting, sitting, sleeping, door-closed, no-exit, climb, climb-failed, swim,
/// swim-failed, deep-water, cannot-ride, ice, boat. They are the refusals MMapper's path
/// machine already recognises (MumeXmlParserBase::initActionMap), so that what drops a move
/// from the path is what a client is told about.
NODISCARD std::optional<CombatEvent> parseCombatLine(const QString &line);

/// When the player's own spell goes off.
///
/// MUME says when the player starts to concentrate and when the concentration breaks, but
/// not when the spell goes off: there is no "You utter the words" for the player, only
/// whatever the spell does, which is a different line for every spell ("Ok.", "You feel
/// better.", nothing at all). What does mark the end is the prompt. While a delayed action
/// runs MUME sends no prompt, only the spinner the twiddlers option draws, and the prompt
/// comes back once the action is over. So the first prompt after the player started to
/// concentrate, with no broken or refused line in between, is the spell going off, and this
/// turns it into a CAST DONE event with actor "you" and empty text.
class NODISCARD OwnCastTracker final
{
private:
    bool m_casting = false;

public:
    /// Every event parseCombatLine() found, in order.
    void receiveEvent(const CombatEvent &event);
    /// A prompt arrived (MUME's <prompt> element, not a twiddler).
    NODISCARD std::optional<CombatEvent> receivePrompt();
    NODISCARD bool casting() const { return m_casting; }
    void reset() { m_casting = false; }
};

NODISCARD std::string_view to_string_view(CombatKindEnum kind);
NODISCARD std::string_view to_string_view(BlowOutcomeEnum outcome);
NODISCARD std::string_view to_string_view(CombatPhaseEnum phase);
