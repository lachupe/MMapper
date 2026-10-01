// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharAffects.h"

#include "CombatLines.h"

#include <algorithm>
#include <utility>

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

} // namespace

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

bool CharAffectsTracker::receiveEvent(const CombatEvent &event, const int64_t now)
{
    if (event.actor != QStringLiteral("you")) {
        return false;
    }
    CombatPhaseEnum phase = event.phase;
    if (event.kind == CombatKindEnum::CONDITION) {
        if (!isLastingCondition(event.detail)) {
            return false;
        }
        phase = CombatPhaseEnum::UP;
    } else if (event.kind != CombatKindEnum::AFFECT || event.detail == QStringLiteral("heal")) {
        return false;
    }
    const QString name = charAffectName(event.detail);
    if (name.isEmpty()) {
        return false;
    }

    CharAffect *const known = find(name);
    switch (phase) {
    case CombatPhaseEnum::UP:
        if (known != nullptr && event.kind == CombatKindEnum::CONDITION) {
            // "You suddenly feel a terrible headache!" again: still poisoned, nothing new.
            return false;
        }
        if (known != nullptr) {
            known->since = now;
            known->refreshed.reset();
            known->fromLine = true;
        } else {
            m_affects.push_back(CharAffect{name, now, std::nullopt, true});
        }
        break;
    case CombatPhaseEnum::REFRESH:
        if (known != nullptr) {
            known->refreshed = now;
            known->fromLine = true;
        } else {
            // On since before anything was seen: when it took hold is not known.
            m_affects.push_back(CharAffect{name, std::nullopt, now, true});
        }
        break;
    case CombatPhaseEnum::DOWN:
        if (known == nullptr) {
            return false;
        }
        m_affects.erase(m_affects.begin() + (known - m_affects.data()));
        break;
    default:
        return false;
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
    const auto gone = std::remove_if(m_affects.begin(), m_affects.end(), [&names](const CharAffect &a) {
        return !names.contains(a.name);
    });
    if (gone != m_affects.end()) {
        m_affects.erase(gone, m_affects.end());
        changed = true;
    }
    for (const QString &name : names) {
        if (find(name) == nullptr) {
            m_affects.push_back(CharAffect{name, std::nullopt, std::nullopt, false});
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
