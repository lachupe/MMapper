#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../proxy/GmcpMessage.h"
#include "../proxy/GmcpModule.h"

#include <QString>

/// Tracks which GMCP modules a single connected frontend has subscribed to.
///
/// This mirrors the subscription model UserTelnet already implements for downstream
/// telnet clients: a frontend announces interest with Core.Supports.Set/Add/Remove, and
/// only messages whose module prefix is in that set are delivered to it.
///
/// Deliberately free of Qt networking types so it can be unit tested on its own.
class NODISCARD FrontendSubscriptions final
{
private:
    GmcpModuleSet m_modules;

public:
    /// Applies Core.Supports.Set / .Add / .Remove. Returns false if the message was not
    /// a well formed supports message, in which case the subscription set is unchanged.
    NODISCARD bool applySupports(const GmcpMessage &msg);

    /// True if the frontend subscribed to the module that owns this message.
    ///
    /// The module is the message name minus its last dot-separated component, which is
    /// the same rule UserTelnet::onGmcpToUser uses. "MMapper.Map.Position" therefore
    /// belongs to "MMapper.Map", and "Char.Vitals" to "Char".
    NODISCARD bool wants(const GmcpMessage &msg) const;

    NODISCARD bool empty() const { return m_modules.empty(); }
    NODISCARD size_t size() const { return m_modules.size(); }
    void clear() { m_modules.clear(); }

private:
    void set(const GmcpModule &mod, bool enabled);
};
