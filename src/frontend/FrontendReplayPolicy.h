#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../proxy/GmcpMessage.h"

#include <cstdint>
#include <optional>
#include <span>

/// What a late frontend is told of a package.
enum class NODISCARD ReplayKindEnum : uint8_t {
    /// Something that happened: never replayed.
    EVENT,
    /// The latest message describes the state on its own, and is replayed as sent.
    WHOLE,
    /// Only the fields that changed: the state is every field seen so far.
    MERGE_FIELDS,
    /// A complete Set that Add, Update and Remove (SET_CHANGE rows) then change member by member.
    KEYED_SET,
    /// One member change of the KEYED_SET row named in `changes`.
    SET_CHANGE,
    /// State built by FrontendServer from the map and the item readers, held beside the cache
    /// and replayed in a declared place (FrontendReplayState).
    SNAPSHOT,
    /// A few of these are held, the most recent last, each under a key (FrontendReplayState).
    CONTAINER_LRU,
};

/// When what was learned of a package is forgotten. Each scope forgets its own and the smaller
/// ones: the refuter's two copied lists were ITEMS (MUME disconnected, or an item changed)
/// and GAME (the character left the game); a new connection forgets ACCOUNT too.
enum class NODISCARD ReplayScopeEnum : uint8_t {
    /// Events: nothing is kept.
    NONE,
    /// What the character carries and what lies in the room: stale as soon as an item moves, and
    /// when the connection to MUME goes.
    ITEMS,
    /// The character, the room and the world as the character sees them: forgotten when the
    /// character leaves the game (a rent, a quit, the menu).
    GAME,
    /// MUME's account menu and the account's characters: kept while no character plays,
    /// forgotten when a new connection begins.
    ACCOUNT,
    /// Never forgotten: the game clock.
    PROCESS,
};

struct NODISCARD ReplayPolicy final
{
    GmcpMessageTypeEnum type;
    ReplayKindEnum kind;
    ReplayScopeEnum scope;
    /// For SET_CHANGE: the KEYED_SET package it changes.
    std::optional<GmcpMessageTypeEnum> changes;
};

/// The policy of `type`: its row, or an EVENT with no scope when it has none.
NODISCARD ReplayPolicy replayPolicy(GmcpMessageTypeEnum type);
NODISCARD bool hasReplayPolicyRow(GmcpMessageTypeEnum type);
/// Every row, for tests and tools.
NODISCARD std::span<const ReplayPolicy> replayPolicies();
/// Whether clear(cleared) forgets what `policy` keeps.
NODISCARD bool isClearedBy(const ReplayPolicy &policy, ReplayScopeEnum cleared);
