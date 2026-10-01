#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstddef>

/// Decides whether MMapper's own map is drawn while frontends are attached.
///
/// A frontend that shows the world itself makes the map canvas redundant, and on an
/// integrated GPU the two compete for the same GPU and memory bandwidth. So rendering is
/// paused from the first frontend to connect until the last one disconnects, unless the user
/// turned that off (Configuration::canvas.pauseWhileFrontendAttached).
///
/// Only the decision lives here, so that it can be tested without a window: the setters
/// return true when isPaused() changed, which is when the caller has something to do.
class NODISCARD FrontendRenderPause final
{
private:
    size_t m_clientCount = 0;
    bool m_enabled = true;

public:
    NODISCARD bool isPaused() const { return m_enabled && m_clientCount > 0; }
    NODISCARD size_t clientCount() const { return m_clientCount; }
    NODISCARD bool isEnabled() const { return m_enabled; }

    /// The number of frontends now attached (FrontendServer::sig_clientCountChanged).
    NODISCARD bool setClientCount(const size_t count)
    {
        const bool was = isPaused();
        m_clientCount = count;
        return isPaused() != was;
    }

    /// Whether the user wants rendering paused while a frontend is attached.
    NODISCARD bool setEnabled(const bool enabled)
    {
        const bool was = isPaused();
        m_enabled = enabled;
        return isPaused() != was;
    }
};
