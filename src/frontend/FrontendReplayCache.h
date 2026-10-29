#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../proxy/GmcpMessage.h"
#include "FrontendReplayPolicy.h"

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
/// Room.UpdateExits is not folded into Room.Info yet, although frontends read it (each exit it
/// names is restated whole, and false for one that has gone, as a secret door shut again): a
/// replayed room has its exits, and so its doors, as they were on arrival, and a frontend that
/// attaches late shows them so until the next change.
///
/// The kind of replay each package gets (whole, merged fields, keyed set, none) and when it is
/// forgotten is one row per package in FrontendReplayPolicy.cpp; protocol_vocab.json is written
/// from that table by tools/protocol_vocab.py.
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

    /// The message kept for `type`, or null.
    NODISCARD const GmcpMessage *find(GmcpMessageTypeEnum type) const;

    /// Keeps `msg` whole, whatever its package's kind: for SNAPSHOT packages, which FrontendServer
    /// builds itself and which no change of the cache could fold into.
    void hold(const GmcpMessage &msg) { store(msg); }

    /// Forgets what the packages of this scope and the smaller ones kept (see ReplayScopeEnum).
    void clear(ReplayScopeEnum scope);

    /// Forgets everything.
    void clear() { m_messages.clear(); }

    /// For a character leaving the game with the connection open (a rent, a quit, the menu):
    /// forgets everything but the account's menu and characters, which describe the account,
    /// not the character.
    void clearGame() { clear(ReplayScopeEnum::GAME); }

private:
    void store(const GmcpMessage &msg);
    void mergeFields(const GmcpMessage &msg);
    void applyToSet(GmcpMessageTypeEnum setType, const GmcpMessage &msg);
};
