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
    /// one's feet, a mount that will not go, a closed door, no exit; or an attack (kill, hit)
    /// MUME refused: nobody by that name, no line of sight, no room to reach, already fighting,
    /// the victim gone; a command a fight rules out; disengage with no fight to leave; a loaded
    /// crossbow loaded again
    REFUSED,
    /// knocked down by a bash, or bashing someone; a bash dodged, which puts the one who
    /// tried it on the ground instead; and the player getting over being bashed
    BASH,
    /// stabbed in the back by somebody who was not visible until then, or stabbing somebody so;
    /// and the backstab's start, its failure and its refusal
    BACKSTAB,
    /// a condition line: incapacitated, mortally wounded, stunned; and a harmful state landing
    /// on somebody: blind, poisoned, bleeding, entangled (its end, where MUME tells it, is an
    /// AFFECT down with the same detail)
    CONDITION,
    /// "is dead! R.I.P.", and the player's own "You are dead! Sorry..."
    DEATH,
    /// a delayed action starting, going off, being broken, or refused before it began
    CAST,
    /// the player's own state changing in a way the prompt may not show: stunned, sleepy,
    /// standing up again, falling
    SELF,
    /// a spell or other lasting effect on somebody taking hold, wearing off or being renewed
    /// (armour, shield, sanctuary, bless, strength, ...), or a heal landing; the one it is on
    /// is the actor, and the effect is the detail
    AFFECT,
    /// somebody stepping in to take a fight off somebody else: the actor rescued the target
    RESCUE,
    /// somebody joining a fight on somebody's side: the actor joins the target's fight
    ASSIST
};

/// A blow stopped by a block or by somebody stepping in to take it is a PARRY, with detail
/// "block" or "intercept"; for an intercepted blow the target is the one who stepped in. A blow
/// a shimmering tunic turned aside is a DODGE with detail "shimmer".
enum class NODISCARD BlowOutcomeEnum : uint8_t { HIT, PARRY, DODGE, MISS };

/// For BLOW: none for a blow that was struck, attempt for the swing that opens a fight ("You
/// approach X, trying to pound him.", "X quickly approaches, trying to hit you.") or a called
/// shot ("X strikes for a weakness in your armour!"), or a missile weapon made ready (aimed,
/// nocked, loaded), whose outcome is not known yet and is left at HIT; where it lands is a line
/// of its own. An opening that was held off or turned aside
/// keeps attempt with its outcome, PARRY or DODGE.
/// For FLEE: an attempt, a failure, or getting away, the player's or anybody's; disengaged for
/// somebody leaving the fight without leaving the room ("X seems to avoid the fight.").
/// For CAST: started, went off, broken (a backfire too), or refused before it started; step for
/// each turn of the spinner while the player's own spell is being cast (see OwnCastTracker).
/// For BASH: none for a bash that landed, dodged for one that missed and floored the one who
/// tried it, recovered for somebody getting over one.
/// For SELF: stunned, sleepy, stood, fell.
/// For AFFECT: up when it takes hold (a heal too), down when it wears off, refresh when it is
/// renewed while still on.
/// For BACKSTAB: none for a stab that landed, attempt for one being set up (sneaking behind),
/// failed for one the victim noticed, refused before it began.
/// For RESCUE: none for a rescue made, failed, or refused before it began. For ASSIST: none, or
/// refused.
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
    RECOVERED,
    FELL,
    UP,
    DOWN,
    REFRESH,
    DISENGAGED,
    STEP
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
    /// Base form: "shatter", "tickle", "fragment". For a blow a tunic turned aside, the tunic
    /// as MUME named it: "ebony tunic", "tunic". For a missile weapon made ready, the weapon
    /// without its article: "vicious bow", "crossbow".
    QString effect;
    /// FLEE: the direction fled in. CONDITION: the condition. CAST: the spell, named by the words
    /// uttered (understood, or the incantation MUME garbles them to for an observer who does not
    /// know the caster's language, looked up in a table), the words as uttered where the table
    /// does not know them, the spell named, why it was refused, "stored" for a stored spell
    /// recalled, "backfire". BACKSTAB, RESCUE, ASSIST: why it failed or was refused. SELF: what
    /// threw the player down ("earthquake").
    /// REFUSED: why the move was refused, one of the words listed at parseCombatLine(). BLOW:
    /// "block" or "intercept" for how a PARRY was made, "shimmer" for a tunic's DODGE,
    /// "keep-at-bay", "armour-gap" or "fumble", "aim", "nock" or "load" for a missile weapon made
    /// ready, or the spell that struck ("magic missile",
    /// "lightning bolt", ...); empty otherwise. AFFECT: the effect ("armour", "heal", ...).
    QString detail;
    /// The line as it was recognised, twiddlers and a trailing "[Damage:N]" removed.
    QString text;
};

/// The event this line describes, or nothing when it is not a fight line.
///
/// A refused move's detail is one of: fighting, exhausted, mount-exhausted, mount-refuses,
/// thrown, resting, sitting, sleeping, door-closed, no-exit, climb, climb-failed, swim,
/// swim-failed, deep-water, cannot-ride, ice, boat. They are the refusals MMapper's path
/// machine already recognises (MumeXmlParserBase::initActionMap), so that what drops a move
/// from the path is what a client is told about. A refused attack's is one of: no-target,
/// no-line-of-sight, no-space, already-fighting, melee, victim-gone; none of these is a move.
/// Nor are these: while-fighting (a command a fight rules out), already-standing ("stand" typed
/// while fighting), not-fighting and nobody-fighting-you (disengage with nothing to leave),
/// already-loaded (a loaded crossbow, the weapon as target). A refused cast's detail is resting,
/// mana, no-target ("What should the spell be cast upon?"), no-spells, or empty.
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
///
/// The spinner itself is one character of `\|/-` and a backspace, a chunk of its own about
/// every quarter second ("prompt \^H", "prompt |^H", ... in the powwow movies). While the
/// player's own spell is being cast each one is a CAST STEP event, actor "you", with the
/// character as text, so that a client can advance a ring rather than guess. None come when
/// the player has the spinner turned off.
class NODISCARD OwnCastTracker final
{
private:
    bool m_casting = false;

public:
    /// Every event parseCombatLine() found, in order.
    void receiveEvent(const CombatEvent &event);
    /// A prompt arrived (MUME's <prompt> element, not a twiddler).
    NODISCARD std::optional<CombatEvent> receivePrompt();
    /// A chunk MUME ended with a backspace (TelnetDataEnum::Backspace), as received.
    NODISCARD std::optional<CombatEvent> receiveTwiddler(const QByteArray &chunk) const;
    NODISCARD bool casting() const { return m_casting; }
    void reset() { m_casting = false; }
};

NODISCARD std::string_view to_string_view(CombatKindEnum kind);
NODISCARD std::string_view to_string_view(BlowOutcomeEnum outcome);
NODISCARD std::string_view to_string_view(CombatPhaseEnum phase);
