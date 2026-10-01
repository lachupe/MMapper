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
/// "bleeding" and "entangled" are not kept: `stat` does not list them and MUME says nothing
/// when they end (bar the cobwebs burning), so an entry would never go away. They stay events.
///
/// Free of Qt networking types and of the clock, so that it can be tested on its own: the
/// caller passes the time.
class NODISCARD CharAffectsTracker final
{
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
    /// In the order they became known.
    NODISCARD const std::vector<CharAffect> &affects() const { return m_affects; }
    /// For a new session, or when the character leaves the game.
    void reset();

private:
    NODISCARD CharAffect *find(const QString &name);
};
