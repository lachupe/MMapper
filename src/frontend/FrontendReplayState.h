#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../proxy/GmcpMessage.h"
#include "FrontendReplayCache.h"
#include "FrontendReplayPolicy.h"

#include <functional>
#include <list>
#include <optional>
#include <utility>

#include <QString>

/// Everything a frontend that connects mid-session is told, with one owner: the cache of what
/// MUME said (FrontendReplayCache), the snapshots FrontendServer builds itself (the game time,
/// the ground, the room's contents, what the character carries) and the few containers last
/// listed. What each package keeps, and when it is forgotten, is its row in
/// FrontendReplayPolicy.cpp; clear() forgets by scope and replay() sends in a declared order.
///
/// Free of Qt networking types, so it can be unit tested on its own.
class NODISCARD FrontendReplayState final
{
public:
    /// How many containers' MMapper.Char.Container are kept: a backpack, a pouch or two, a keyring,
    /// and the corpse or chest last looked into.
    static constexpr const size_t CONTAINERS_KEPT = 8;
    using SendFn = std::function<void(const GmcpMessage &)>;

private:
    FrontendReplayCache m_cache;
    /// A list, since GmcpMessage cannot be assigned, which a deque needs to erase from the middle.
    std::list<std::pair<QString, GmcpMessage>> m_containers;
    /// Pulled into the void (FEAT.195): the character is out of its room, and what MUME says of a
    /// room meanwhile (Room.Chars, a room display's contents) is the void's, not the room's.
    bool m_inVoid = false;

public:
    /// Folds `msg` into the state if its package's policy says it is state the cache keeps; in
    /// the void, not the room's people or contents (isRoomState()).
    void remember(const GmcpMessage &msg);
    /// Keeps `msg`, whose package's policy is SNAPSHOT, replacing the last one; in the void, not
    /// the room's contents.
    void hold(const GmcpMessage &msg);
    /// Keeps `msg` as the listing of the container `key`, the last of CONTAINERS_KEPT.
    void holdContainer(const QString &key, const GmcpMessage &msg);
    /// Forgets the containers held; for a listing that cannot be told apart.
    void forgetContainers() { m_containers.clear(); }

    /// Forgets what the packages of `scope` and the smaller scopes kept (ReplayScopeEnum).
    void clear(ReplayScopeEnum scope);

    /// The character is in the void, or back from it.
    void setInVoid(bool inVoid) { m_inVoid = inVoid; }
    NODISCARD bool inVoid() const { return m_inVoid; }
    /// Whether `type` is the state of the room the character stands in that the void does not
    /// change: Room.Chars (its Set and the changes folded into it) and MMapper.Room.Contents.
    NODISCARD static bool isRoomState(GmcpMessageTypeEnum type);
    /// Sends the room's held people and contents (isRoomState()), for a character back from the
    /// void: MUME restates neither on the return, nor on a `look` in the same room (FIX.463).
    void replayRoom(const SendFn &send) const;

    NODISCARD bool has(GmcpMessageTypeEnum type) const { return m_cache.find(type) != nullptr; }
    NODISCARD const FrontendReplayCache &cache() const { return m_cache; }
    NODISCARD size_t containerCount() const { return m_containers.size(); }

    /// Sends what is known, in the declared order: the state MUME stated, in package order; then
    /// the game time and the ground; then `position`, which the room-scoped packages after it
    /// name; then the room's contents, what the character wears and carries, and the containers.
    /// `position` is withheld while MUME is not connected: the room it names is not where
    /// anyone stands.
    void replay(const SendFn &send,
                const std::optional<GmcpMessage> &position,
                bool upstreamConnected) const;
};
