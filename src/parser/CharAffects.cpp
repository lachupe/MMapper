// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharAffects.h"

#include "CombatLines.h"

#include <algorithm>
#include <utility>

#include <QRegularExpression>

namespace {

/// An event's word for an effect, and `stat`'s for the same one. Every other effect goes by
/// the same word in both (CombatLines takes its details from `stat`'s list).
struct NODISCARD AffectAlias final
{
    const char *event;
    const char *stat;
};

const AffectAlias g_aliases[] = {
    // "poison (type: psylonia)", "poison (type: arachnia)", "poison (type: venom)" in `stat`.
    {"poisoned", "poison"},
    {"blind", "blindness"},
};

/// The conditions that are effects `stat` lists, by the event's word.
NODISCARD bool isLastingCondition(const QString &detail)
{
    return detail == QStringLiteral("blind") || detail == QStringLiteral("poisoned");
}

const QString g_bleeding = QStringLiteral("bleeding");
const QString g_woundName = QStringLiteral("wound");

/// "a deep wound at the left foot (poorly bound)", "an extra-deep wound in the body"; the
/// severity is any one word, so that a word not known still makes a wound.
const QRegularExpression
    g_woundText{QStringLiteral(
                    R"(^an? ([\w-]+) wound (?:at|on|in) (?:the )?(.+?)(?: \(([^)]*)\))?$)"),
                QRegularExpression::CaseInsensitiveOption};

/// How well a wound is bound: 0 unbound (clean, dirty, or not said), 1 bound without saying
/// how well, 2 poorly bound, 3 bound up, 4 securely bound.
NODISCARD int bindRank(const QString &state)
{
    if (state == QStringLiteral("bound")) {
        return 1;
    }
    if (state == QStringLiteral("poorly bound")) {
        return 2;
    }
    if (state == QStringLiteral("bound up")) {
        return 3;
    }
    if (state == QStringLiteral("securely bound")) {
        return 4;
    }
    return 0;
}

/// The bind states that stop the bleeding: all but "poorly bound".
NODISCARD bool stopsBleeding(const QString &state)
{
    return state == QStringLiteral("bound") || state == QStringLiteral("bound up")
           || state == QStringLiteral("securely bound");
}

NODISCARD bool isWound(const CharAffect &a)
{
    return a.wound.has_value();
}

} // namespace

bool CharWound::dirty() const
{
    return state == QStringLiteral("dirty");
}

bool CharWound::bandaged() const
{
    return bindRank(state) > 0;
}

std::optional<CharWound> parseCharWound(const QString &text)
{
    const QRegularExpressionMatch m = g_woundText.match(text.trimmed());
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    return CharWound{m.captured(1).toLower(),
                     m.captured(2).trimmed().toLower(),
                     m.captured(3).trimmed().toLower()};
}

int charWoundSeverityRank(const QString &severity)
{
    static const char *const order[] = {"light", "deep", "serious", "critical", "grievous"};
    for (int i = 0; i < 5; ++i) {
        if (severity == QLatin1String(order[i])) {
            return i + 1;
        }
    }
    return 0;
}

QString charAffectName(const QString &name)
{
    QString result = name.trimmed().toLower();
    // "poison (type: psylonia)", "watch room (xanscasoebb)", "disease (type: flu)".
    if (const auto bracket = result.indexOf(QLatin1Char('(')); bracket > 0) {
        result = result.left(bracket).trimmed();
    }
    // Listed among the affects, but a spell held ready, and several of one name at that.
    if (result.startsWith(QStringLiteral("stored spell"))) {
        return QString{};
    }
    for (const AffectAlias &alias : g_aliases) {
        if (result == QLatin1String(alias.event)) {
            return QString::fromLatin1(alias.stat);
        }
    }
    return result;
}

CharAffect *CharAffectsTracker::find(const QString &name)
{
    const auto it = std::find_if(m_affects.begin(), m_affects.end(), [&name](const CharAffect &a) {
        return a.name == name;
    });
    return it == m_affects.end() ? nullptr : &*it;
}

bool CharAffectsTracker::receiveBleed(const int64_t now)
{
    if (CharAffect *const known = find(g_bleeding)) {
        known->refreshed = now;
    } else {
        m_affects.push_back(CharAffect{g_bleeding, now, now, true, std::nullopt});
    }
    m_told = true;
    return true;
}

bool CharAffectsTracker::stopBleeding()
{
    const auto it = std::find_if(m_affects.begin(), m_affects.end(), [](const CharAffect &a) {
        return a.name == g_bleeding;
    });
    if (it == m_affects.end()) {
        return false;
    }
    m_affects.erase(it);
    return true;
}

bool CharAffectsTracker::receiveWoundLine(const QString &said, const int64_t now)
{
    const QString state = said.trimmed().toLower();
    if (state.isEmpty()) {
        return false;
    }
    const bool clean = state == QStringLiteral("clean");
    const int rank = bindRank(state);
    // The wound the line is about: for a clean, a dirty one; for a bind, one bound less well
    // than the line says, unbound ones first; among those the most severe, the first listed
    // of equals.
    CharAffect *chosen = nullptr;
    const auto better = [&clean](const CharWound &w, const CharWound &than) {
        if (!clean) {
            const bool unbound = !w.bandaged();
            const bool thanUnbound = !than.bandaged();
            if (unbound != thanUnbound) {
                return unbound;
            }
        }
        return charWoundSeverityRank(w.severity) > charWoundSeverityRank(than.severity);
    };
    for (CharAffect &a : m_affects) {
        if (!a.wound.has_value()) {
            continue;
        }
        const CharWound &w = *a.wound;
        const bool fits = clean ? w.dirty() : bindRank(w.state) < rank;
        if (fits && (chosen == nullptr || better(w, *chosen->wound))) {
            chosen = &a;
        }
    }
    bool changed = false;
    if (chosen != nullptr) {
        chosen->wound->state = state;
        chosen->refreshed = now;
        chosen->fromLine = true;
        changed = true;
    }
    if (stopsBleeding(state)) {
        changed = stopBleeding() || changed;
    }
    if (changed) {
        m_told = true;
    }
    return changed;
}

bool CharAffectsTracker::receiveWounds(const QStringList &listed)
{
    std::vector<CharWound> wounds;
    for (const QString &text : listed) {
        if (auto wound = parseCharWound(text)) {
            wounds.push_back(std::move(*wound));
        }
    }

    bool changed = !std::exchange(m_told, true);
    // What is listed as it was known keeps its entry, with the line's times; the rest goes,
    // and the new ones come after the others.
    std::vector<CharAffect> kept;
    std::vector<bool> placed(wounds.size(), false);
    for (CharAffect &a : m_affects) {
        if (!a.wound.has_value()) {
            kept.push_back(std::move(a));
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < wounds.size(); ++i) {
            if (!placed[i] && wounds[i] == *a.wound) {
                placed[i] = true;
                found = true;
                break;
            }
        }
        if (found) {
            kept.push_back(std::move(a));
        } else {
            changed = true;
        }
    }
    for (size_t i = 0; i < wounds.size(); ++i) {
        if (!placed[i]) {
            kept.push_back(CharAffect{g_woundName, std::nullopt, std::nullopt, false, wounds[i]});
            changed = true;
        }
    }
    m_affects = std::move(kept);

    const bool allBound = std::all_of(wounds.begin(), wounds.end(), [](const CharWound &w) {
        return w.bandaged();
    });
    if (allBound) {
        changed = stopBleeding() || changed;
    }
    return changed;
}

bool CharAffectsTracker::tick(const int64_t now)
{
    const auto endsAt = bleedingEndsAt();
    if (!endsAt.has_value() || now < *endsAt) {
        return false;
    }
    return stopBleeding();
}

std::optional<int64_t> CharAffectsTracker::bleedingEndsAt() const
{
    for (const CharAffect &a : m_affects) {
        if (a.name == g_bleeding) {
            const auto last = a.refreshed.has_value() ? a.refreshed : a.since;
            return last.value_or(0) + BLEED_QUIET_SECONDS;
        }
    }
    return std::nullopt;
}

bool CharAffectsTracker::receiveEvent(const CombatEvent &event, const int64_t now)
{
    if (event.actor != QStringLiteral("you")) {
        return false;
    }
    // Quiet long enough, whatever this event is.
    const bool ended = tick(now);
    if (event.kind == CombatKindEnum::CONDITION && event.detail == g_bleeding) {
        return receiveBleed(now) || ended;
    }
    if (event.kind == CombatKindEnum::AFFECT && event.detail == g_woundName) {
        return receiveWoundLine(event.effect, now) || ended;
    }
    CombatPhaseEnum phase = event.phase;
    if (event.kind == CombatKindEnum::CONDITION) {
        if (!isLastingCondition(event.detail)) {
            return ended;
        }
        phase = CombatPhaseEnum::UP;
    } else if (event.kind != CombatKindEnum::AFFECT || event.detail == QStringLiteral("heal")) {
        return ended;
    }
    const QString name = charAffectName(event.detail);
    if (name.isEmpty()) {
        return ended;
    }

    CharAffect *const known = find(name);
    switch (phase) {
    case CombatPhaseEnum::UP:
        if (known != nullptr && event.kind == CombatKindEnum::CONDITION) {
            // "You suddenly feel a terrible headache!" again: still poisoned, nothing new.
            return ended;
        }
        if (known != nullptr) {
            known->since = now;
            known->refreshed.reset();
            known->fromLine = true;
        } else {
            m_affects.push_back(CharAffect{name, now, std::nullopt, true, std::nullopt});
        }
        break;
    case CombatPhaseEnum::REFRESH:
        if (known != nullptr) {
            known->refreshed = now;
            known->fromLine = true;
        } else {
            // On since before anything was seen: when it took hold is not known.
            m_affects.push_back(CharAffect{name, std::nullopt, now, true, std::nullopt});
        }
        break;
    case CombatPhaseEnum::DOWN:
        if (known == nullptr) {
            return ended;
        }
        m_affects.erase(m_affects.begin() + (known - m_affects.data()));
        break;
    default:
        return ended;
    }
    m_told = true;
    return true;
}

bool CharAffectsTracker::receiveStat(const QStringList &listed)
{
    QStringList names;
    for (const QString &raw : listed) {
        const QString name = charAffectName(raw);
        if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
        }
    }

    bool changed = !std::exchange(m_told, true);
    // `stat` lists neither the bleeding nor its wounds among the names: those have rules of
    // their own (receiveWounds()).
    const auto gone = std::remove_if(m_affects.begin(),
                                     m_affects.end(),
                                     [&names](const CharAffect &a) {
                                         return !isWound(a) && a.name != g_bleeding
                                                && !names.contains(a.name);
                                     });
    if (gone != m_affects.end()) {
        m_affects.erase(gone, m_affects.end());
        changed = true;
    }
    for (const QString &name : names) {
        if (find(name) == nullptr) {
            m_affects.push_back(CharAffect{name, std::nullopt, std::nullopt, false, std::nullopt});
            changed = true;
        }
    }
    return changed;
}

void CharAffectsTracker::reset()
{
    m_affects.clear();
    m_told = false;
}
