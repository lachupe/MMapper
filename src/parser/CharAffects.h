#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

struct CombatEvent;

/// One wound on the player's character, as `stat` (and `info`) list it: "a deep wound at the
/// left foot (poorly bound)". See parseCharWound().
struct NODISCARD CharWound final
{
    /// MUME's word, lowercase: light, deep, serious, critical, grievous (in that order of
    /// harm; see charWoundSeverityRank()).
    QString severity;
    /// Where, without "the": "body", "head", "left foot", "left foreleg".
    QString location;
    /// MUME's word in brackets, lowercase: "clean", "dirty", "poorly bound", "bound up",
    /// "securely bound"; "bound" when a line said it was bound but not how well; empty when
    /// MUME gave none ("a deep wound at the left hand.").
    QString state;

    /// The state is "dirty".
    NODISCARD bool dirty() const;
    /// The state is one of the bound ones (poorly bound, bound up, securely bound, bound).
    NODISCARD bool bandaged() const;

    NODISCARD bool operator==(const CharWound &other) const
    {
        return severity == other.severity && location == other.location && state == other.state;
    }
};

/// One wound of `stat`'s or `info`'s list, the leading "- " and a closing full stop already
/// taken off (CharStat::wounds, CharScore::wounds): "a light wound at the head (clean)", "a
/// deep wound at the left hand". Nothing when the text is not a wound.
NODISCARD std::optional<CharWound> parseCharWound(const QString &text);

/// light 1, deep 2, serious 3, critical 4, grievous 5; 0 for a word not known.
NODISCARD int charWoundSeverityRank(const QString &severity);

/// One lasting effect on the player's character.
struct NODISCARD CharAffect final
{
    /// MUME's name for it as `stat` lists it, lowercase and without what `stat` puts in
    /// brackets after it: "armour", "detect magic", "poison". See charAffectName().
    QString name;
    /// Unix seconds at which MUME's line said it took hold; unset when no such line was seen,
    /// as for an effect only `stat` told of.
    std::optional<int64_t> since;
    /// Unix seconds at which MUME's line last said it was renewed; unset when none did.
    std::optional<int64_t> refreshed;
    /// True when one of MUME's lines told of it (taking hold or renewed); false when only
    /// `stat`'s list did.
    bool fromLine = false;
    /// Set for a wound (name "wound"), one entry per wound.
    std::optional<CharWound> wound;
};

/// The one place where the names of an effect are brought together.
///
/// MUME names an effect in `stat`'s list ("strength", "poison (type: psylonia)", "blindness",
/// "watch room (xanscasoebb)", "Orkish draught"), and CombatLines names the same effect in an
/// event's detail, mostly by the same word, but "poisoned" and "blind" for the two that are
/// also conditions. This gives the one name both go by: lowercase, the bracketed part taken off
/// ("poison", "watch room"), and the two event words turned into `stat`'s ("poison",
/// "blindness").
///
/// Empty for what `stat` lists among the affects but is none: "stored spell fireball".
NODISCARD QString charAffectName(const QString &name);

/// The effects on the player's character, kept from MUME's lines and set right by `stat`.
///
/// MUME gives no durations and no list of what is on, except when asked with `stat`. So an
/// effect is known from the line that says it took hold, was renewed or wore off (an AFFECT
/// event with actor "you"; a heal is not lasting and is left out), or that it landed (a
/// CONDITION event with actor "you" and detail "blind" or "poisoned"), and each `stat` sets the
/// list right: what it lists and was not known is added, without a time; what was known and it
/// does not list is dropped.
///
/// "entangled" is not kept: `stat` does not list it and MUME says nothing when it ends (bar
/// the cobwebs burning), so an entry would never go away. It stays an event.
///
/// "bleeding" is kept although MUME never says it stopped: it is switched on by the player's
/// own bleed lines (a CONDITION event with actor "you" and detail "bleeding", "You bleed from
/// open wounds.", "You wish that your wounds would stop BLEEDING so much!"), each renewing it,
/// and switched off when no such line came for BLEED_QUIET_SECONDS (see tick()), when a line
/// says the player's wound is bound (bound up, securely bound, or bound without saying how
/// well -- "poorly bound" leaves it on until it goes quiet), or when a `stat` or `info` lists
/// no wound or only bound ones. `stat` never lists it, so its list does not drop it.
///
/// Wounds are kept one entry per wound (name "wound", CharAffect::wound). Each `stat` (and each
/// `info` with its effects list) replaces them (receiveWounds()). The lines that tend the
/// player's own wound (an AFFECT event with actor "you", detail "wound" and the new state in
/// `effect`: see parseCombatLine()) update the one wound they can tell: for a bind, the only
/// wound bound less well than the line says, else the most severe of them, unbound ones first;
/// for a clean, the only dirty one, else the most severe. A wound is never made up from a line:
/// with no wound known from `stat`, the lines change nothing.
///
/// Free of Qt networking types and of the clock, so that it can be tested on its own: the
/// caller passes the time.
class NODISCARD CharAffectsTracker final
{
public:
    /// How long "bleeding" stays on after the last bleed line. MUME's interval between the
    /// ticks of "You bleed from open wounds." was not measured; a minute and a half without one
    /// is taken to mean the bleeding stopped.
    static constexpr int64_t BLEED_QUIET_SECONDS = 90;

private:
    std::vector<CharAffect> m_affects;
    /// Something has been told since the last reset, so that an empty list is a statement.
    bool m_told = false;

public:
    /// One of parseCombatLine()'s events; `now` is unix seconds. True when the list changed.
    NODISCARD bool receiveEvent(const CombatEvent &event, int64_t now);
    /// The affects a `stat` listed (CharStat::affects). True when the list changed, or when
    /// this is the first thing told since a reset (the list is then known, even if empty).
    NODISCARD bool receiveStat(const QStringList &listed);
    /// The wounds a `stat` or an `info` with its effects list listed (CharStat::wounds,
    /// CharScore::wounds), which replace the known ones; no wound listed means none. Ends the
    /// bleeding when none is listed, or only bound ones. True when the list changed.
    NODISCARD bool receiveWounds(const QStringList &listed);
    /// The clock moved on to `now` (unix seconds): ends the bleeding once it has been quiet for
    /// BLEED_QUIET_SECONDS. True when the list changed.
    NODISCARD bool tick(int64_t now);
    /// When tick() would end the bleeding, unix seconds; nothing when no bleeding is kept.
    NODISCARD std::optional<int64_t> bleedingEndsAt() const;
    /// In the order they became known.
    NODISCARD const std::vector<CharAffect> &affects() const { return m_affects; }
    /// For a new session, or when the character leaves the game.
    void reset();

private:
    NODISCARD CharAffect *find(const QString &name);
    NODISCARD bool receiveBleed(int64_t now);
    NODISCARD bool receiveWoundLine(const QString &said, int64_t now);
    NODISCARD bool stopBleeding();
};
