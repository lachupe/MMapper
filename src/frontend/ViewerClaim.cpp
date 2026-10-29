// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "ViewerClaim.h"

#include "FrontendSubscriptions.h"

#include <QRegularExpression>

bool claimsViewer(const FrontendSubscriptions &subscriptions, const bool driving)
{
    return driving && subscriptions.subscribes("MMapper.View");
}

void ViewerClaim::set(const QString &viewer)
{
    if (m_viewer != viewer) {
        m_viewer = viewer;
        if (m_changed) {
            m_changed();
        }
    }
}

void ViewerClaim::sentExternal()
{
    m_sent = true;
    set(QStringLiteral("external"));
}

void ViewerClaim::notePlayerLine(const QString &line)
{
    static const QRegularExpression re{QStringLiteral(R"(^\s*cha\w*\s+vie\w*\s+(\w+))"),
                                       QRegularExpression::CaseInsensitiveOption};
    const auto match = re.match(line);
    if (!match.hasMatch()) {
        return;
    }
    const QString value = match.captured(1).toLower();
    QString viewer;
    for (const char *const option : {"external", "simple", "off"}) {
        if (QString::fromLatin1(option).startsWith(value)) {
            viewer = QString::fromLatin1(option);
            break;
        }
    }
    if (viewer.isEmpty()) {
        return; // Not a setting MUME knows; it answers with an error and changes nothing.
    }
    // The player's own choice, respected for the rest of this connection.
    m_setByPlayer = true;
    set(viewer);
}

void ViewerClaim::reset()
{
    m_sent = false;
    m_setByPlayer = false;
    set(QStringLiteral("unknown"));
}
