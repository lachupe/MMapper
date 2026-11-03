// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CommGroup.h"

namespace {

constexpr int MAX_SPEAKER = 60;

} // namespace

std::optional<CommGroup> parseGroupTell(const QString &line)
{
    const QString text = line.trimmed();
    const QString own = QStringLiteral("You tell the group '");
    const QString other = QStringLiteral(" tells the group '");
    QString speaker;
    qsizetype from = 0;
    if (text.startsWith(own)) {
        speaker = QStringLiteral("you");
        from = own.size();
    } else {
        const qsizetype at = text.indexOf(other);
        if (at <= 0 || at > MAX_SPEAKER) {
            return std::nullopt;
        }
        speaker = text.left(at).trimmed();
        from = at + other.size();
    }
    const qsizetype close = text.lastIndexOf(QLatin1Char('\''));
    if (speaker.isEmpty() || close < from) {
        return std::nullopt;
    }
    return CommGroup{speaker, text.mid(from, close - from), text};
}
