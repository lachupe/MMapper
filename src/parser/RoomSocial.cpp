// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "RoomSocial.h"

#include <algorithm>
#include <vector>

#include <QRegularExpression>
#include <QStringList>

namespace {

struct SocialRow
{
    const char *social;
    const char *form;
    const char *text;
};

const SocialRow g_rows[] = {
#include "SocialLines.inc"
};

constexpr int MAX_LINE = 160;
constexpr int MAX_NAME_WORDS = 7;

struct Compiled
{
    QString social;
    QString form;
    /// The fixed text after the last token: rules out nearly every line before a pattern runs.
    QString tail;
    int literal = 0;
    int order = 0;
    bool hasTarget = false;
    QRegularExpression regex;
};

NODISCARD QString pronoun(const QString &token)
{
    if (token == QStringLiteral("his")) {
        return QStringLiteral("(?:his|her|its)");
    }
    if (token == QStringLiteral("him")) {
        return QStringLiteral("(?:him|her|it)");
    }
    if (token == QStringLiteral("he")) {
        return QStringLiteral("(?:he|she|it)");
    }
    if (token == QStringLiteral("self")) {
        return QStringLiteral("(?:himself|herself|itself)");
    }
    return {};
}

NODISCARD Compiled compile(const SocialRow &row, int order)
{
    Compiled c;
    c.social = QString::fromUtf8(row.social);
    c.form = QString::fromUtf8(row.form);
    c.order = order;
    const QString text = QString::fromUtf8(row.text);
    QString pattern = QStringLiteral("^");
    bool actorSeen = false;
    QString rest = text;
    int fixed = 0;
    while (!rest.isEmpty()) {
        const int open = rest.indexOf(QLatin1Char('{'));
        const int close = open >= 0 ? rest.indexOf(QLatin1Char('}'), open) : -1;
        if (open < 0 || close < 0) {
            pattern += QRegularExpression::escape(rest);
            fixed += rest.size();
            c.tail = rest;
            break;
        }
        pattern += QRegularExpression::escape(rest.left(open));
        fixed += open;
        const QString token = rest.mid(open + 1, close - open - 1);
        if (token == QStringLiteral("actor")) {
            pattern += actorSeen ? QStringLiteral(".+?") : QStringLiteral("(?<actor>.+?)");
            actorSeen = true;
        } else if (token == QStringLiteral("target")) {
            pattern += c.hasTarget ? QStringLiteral(".+?") : QStringLiteral("(?<target>.+?)");
            c.hasTarget = true;
        } else if (const QString alt = pronoun(token); !alt.isEmpty()) {
            pattern += alt;
        } else {
            pattern += QRegularExpression::escape(rest.mid(open, close - open + 1));
            fixed += close - open + 1;
        }
        rest = rest.mid(close + 1);
        c.tail = rest;
    }
    pattern += QStringLiteral("$");
    c.literal = fixed;
    c.regex = QRegularExpression(pattern);
    return c;
}

NODISCARD const std::vector<Compiled> &compiled()
{
    static const std::vector<Compiled> table = [] {
        std::vector<Compiled> all;
        int order = 0;
        for (const SocialRow &row : g_rows) {
            all.push_back(compile(row, order++));
        }
        // The most specific line first ("smiles at you." before "smiles at {target}."); ties
        // keep the table's order.
        std::stable_sort(all.begin(), all.end(), [](const Compiled &a, const Compiled &b) {
            return a.literal != b.literal ? a.literal > b.literal : a.order < b.order;
        });
        return all;
    }();
    return table;
}

NODISCARD bool plausibleName(const QString &name)
{
    if (name.isEmpty() || name.size() > 60) {
        return false;
    }
    if ((name.contains(QLatin1Char('\'')) && !name.endsWith(QStringLiteral("'s")))
        || name.contains(QLatin1Char(':')) || name.contains(QLatin1Char(','))) {
        return false;
    }
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty() || words.size() > MAX_NAME_WORDS) {
        return false;
    }
    static const QStringList
        notNames{"you", "your", "it", "he", "she", "they", "this", "that", "there"};
    return !notNames.contains(words.first().toLower());
}

NODISCARD bool plausibleTarget(const QString &name)
{
    if (!plausibleName(name)) {
        return false;
    }
    static const QStringList notTargets{"his", "her", "its", "their", "you"};
    return !notTargets.contains(name.split(QLatin1Char(' '), Qt::SkipEmptyParts).first().toLower());
}

} // namespace

std::optional<RoomSocial> parseSocialLine(const QString &line)
{
    const QString text = line.trimmed();
    if (text.isEmpty() || text.size() > MAX_LINE) {
        return std::nullopt;
    }
    for (const Compiled &c : compiled()) {
        if (!text.endsWith(c.tail)) {
            continue;
        }
        const auto m = c.regex.match(text);
        if (!m.hasMatch()) {
            continue;
        }
        const QString actor = m.captured(QStringLiteral("actor")).trimmed();
        QString target = c.hasTarget ? m.captured(QStringLiteral("target")).trimmed() : QString();
        if (!plausibleName(actor) || (c.hasTarget && !plausibleTarget(target))) {
            continue;
        }
        if (c.form == QStringLiteral("you")) {
            target = QStringLiteral("you");
        } else if (c.form == QStringLiteral("self")) {
            target = actor;
        }
        return RoomSocial{actor, c.social, target, c.form, text};
    }
    return std::nullopt;
}
