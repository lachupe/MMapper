#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../proxy/GmcpMessage.h"

#include <map>

/// What MUME has said about the current state of the game, kept so that a frontend which
/// connects mid-session can be brought up to date without waiting for the next change.
///
/// MUME rarely restates a whole state. Char.Vitals and Char.StatusVars carry only the fields
/// that changed, and Room.Chars and Group send a complete Set only now and then, with Add,
/// Update and Remove changing one member at a time in between. Keeping the last message of
/// each would therefore replay a fragment: an hp tick without the maximums, or the room as it
/// was before anyone came or went. Instead every change is folded into the state it changes,
/// and what is replayed is that accumulated state, in the shape of MUME's own full message: one
/// Char.Vitals with every field seen so far, one Room.Chars.Set with the room as it is now.
///
/// The changes are applied the way a frontend watching from the start would apply them, so
/// that a late one ends up knowing the same: an Add or Update for someone already present
/// merges into them, for someone not yet known adds them at the end, and a Remove takes them
/// out. A change that arrives before any Set of its kind is dropped, since there is nothing
/// it could be a change to.
///
/// Room.UpdateExits is not folded into Room.Info yet: MUME's help does not say whether an exit
/// it names is restated whole, and no frontend reads it, so a replayed room has its exits as
/// they were on arrival.
///
/// Deliberately free of Qt networking types so it can be unit tested on its own.
class NODISCARD FrontendReplayCache final
{
private:
    std::map<GmcpMessageTypeEnum, GmcpMessage> m_messages;

public:
    /// Folds `msg` into the cached state if it describes state, and ignores it otherwise.
    void remember(const GmcpMessage &msg);

    /// The accumulated state, one message per package, in the order they are replayed.
    NODISCARD const std::map<GmcpMessageTypeEnum, GmcpMessage> &messages() const
    {
        return m_messages;
    }

    /// For a new game session, which invalidates everything learned in the previous one.
    void clear() { m_messages.clear(); }

private:
    void store(const GmcpMessage &msg);
    void mergeFields(const GmcpMessage &msg);
    void applyToSet(GmcpMessageTypeEnum setType, const GmcpMessage &msg);
};
