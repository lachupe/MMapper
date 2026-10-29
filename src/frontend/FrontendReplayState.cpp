// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendReplayState.h"

#include <algorithm>
#include <array>

namespace {

/// The snapshots, after everything MUME stated: the clock and the ground first, then the
/// position (a stand-in, MAP_POSITION, is where it goes), then what belongs to the room it
/// names. The containers follow the last of these.
constexpr const std::array<GmcpMessageTypeEnum, 6> TAIL{GmcpMessageTypeEnum::MMAPPER_TIME_STATE,
                                                        GmcpMessageTypeEnum::MMAPPER_WEATHER_GROUND,
                                                        GmcpMessageTypeEnum::MMAPPER_MAP_POSITION,
                                                        GmcpMessageTypeEnum::MMAPPER_ROOM_CONTENTS,
                                                        GmcpMessageTypeEnum::MMAPPER_CHAR_EQUIPMENT,
                                                        GmcpMessageTypeEnum::MMAPPER_CHAR_INVENTORY};

} // namespace

void FrontendReplayState::holdContainer(const QString &key, const GmcpMessage &msg)
{
    const auto it = std::find_if(m_containers.begin(),
                                 m_containers.end(),
                                 [&key](const auto &entry) { return entry.first == key; });
    if (it != m_containers.end()) {
        m_containers.erase(it);
    }
    m_containers.emplace_back(key, msg);
    while (m_containers.size() > CONTAINERS_KEPT) {
        m_containers.pop_front();
    }
}

void FrontendReplayState::clear(const ReplayScopeEnum scope)
{
    m_cache.clear(scope);
    // The containers are ITEMS, the smallest scope: every clearing forgets them.
    m_containers.clear();
}

void FrontendReplayState::replay(const SendFn &send,
                                 const std::optional<GmcpMessage> &position,
                                 const bool upstreamConnected) const
{
    for (const auto &[type, msg] : m_cache.messages()) {
        if (replayPolicy(type).kind != ReplayKindEnum::SNAPSHOT) {
            send(msg);
        }
    }
    for (const GmcpMessageTypeEnum type : TAIL) {
        if (type == GmcpMessageTypeEnum::MMAPPER_MAP_POSITION) {
            if (upstreamConnected && position.has_value()) {
                send(*position);
            }
        } else if (const GmcpMessage *const msg = m_cache.find(type)) {
            send(*msg);
        }
    }
    for (const auto &[key, msg] : m_containers) {
        send(msg);
    }
}
