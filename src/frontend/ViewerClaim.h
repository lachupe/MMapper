#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <functional>

#include <QString>

class FrontendSubscriptions;

/// True if a frontend with these subscriptions claims MUME's viewer: only the driving frontend
/// does, and only while it subscribes to MMapper.View.
NODISCARD bool claimsViewer(const FrontendSubscriptions &subscriptions, bool driving);

/// MUME's viewer setting as MMapper knows it (MMapper.Session.State's `viewer`): "unknown"
/// until a connection has set it, "external" once MMapper did, and whatever the player typed
/// with `change viewer` after that, which is respected for the rest of the connection.
///
/// MMapper sets it once per login while the driving frontend claims the viewer, so that MUME
/// hands the viewed texts over (MMapper.View.Text) instead of showing them in a window. This
/// keeps only that setting; the CommandRunner decides when MUME is free to be told, and sends.
class NODISCARD ViewerClaim final
{
public:
    using ChangedFn = std::function<void()>;

private:
    QString m_viewer = QStringLiteral("unknown");
    bool m_sent = false;
    bool m_setByPlayer = false;
    ChangedFn m_changed;

public:
    /// Called whenever the setting as known changes.
    void setChanged(ChangedFn fn) { m_changed = std::move(fn); }

    NODISCARD QString state() const { return m_viewer; }
    /// Whether MMapper still owes MUME `change viewer external` for this login.
    NODISCARD bool due() const { return !m_sent && !m_setByPlayer; }

    /// `change viewer external` has gone to MUME.
    void sentExternal();
    /// A line the player (or an alias) sent: a `change viewer` of theirs is theirs to keep.
    void notePlayerLine(const QString &line);
    /// A new login sets the viewer again, unless the player set it in this connection.
    void newLogin() { m_sent = false; }
    /// A connection began or ended: nothing is known, and nothing was sent.
    void reset();

private:
    void set(const QString &viewer);
};
