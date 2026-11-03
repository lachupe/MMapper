// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharNeeds.h"

#include <QRegularExpression>

std::optional<CharNeedLine> parseNeedLine(const QString &line)
{
    static const QRegularExpression up(
        QStringLiteral("^You (?:begin to feel|are) (hungry|thirsty)\\.?$"));
    const QString text = line.trimmed();
    if (const auto m = up.match(text); m.hasMatch()) {
        return CharNeedLine{m.captured(1), true};
    }
    if (text.startsWith(QStringLiteral("You eat ")) || text == QStringLiteral("You are full.")) {
        return CharNeedLine{QStringLiteral("hungry"), false};
    }
    if (text.startsWith(QStringLiteral("You drink "))
        || text.startsWith(QStringLiteral("You do not feel thirsty"))) {
        return CharNeedLine{QStringLiteral("thirsty"), false};
    }
    return std::nullopt;
}

bool CharNeedsTracker::receive(const CharNeedLine &line)
{
    int &slot = line.need == QStringLiteral("hungry") ? hungry : thirsty;
    const int now = line.on ? 1 : 0;
    const bool changed = slot != now;
    slot = now;
    return changed;
}

void CharNeedsTracker::reset()
{
    hungry = -1;
    thirsty = -1;
}
