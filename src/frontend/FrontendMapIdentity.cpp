// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendMapIdentity.h"

#include <algorithm>

#include <QFileInfo>

void FrontendMapIdentity::loaded(const Map &map, const QString &fileName)
{
    m_map = map;
    m_fileName = fileName;
    m_generation = 0;
}

bool FrontendMapIdentity::changed(const Map &map)
{
    // Cheap when nothing changed: Map compares the shared world first, and only compares room
    // by room when a change produced a new world, as MapFrontend itself does for every change.
    const bool same = map == m_map;
    // Kept even when equal, so that the next comparison is of the same world again.
    m_map = map;
    if (same) {
        return false;
    }
    ++m_generation;
    return true;
}

bool FrontendMapIdentity::renamed(const QString &fileName)
{
    if (fileName == m_fileName) {
        return false;
    }
    m_fileName = fileName;
    return true;
}

frontend_messages::MapIdentity FrontendMapIdentity::get() const
{
    frontend_messages::MapIdentity identity;
    // Only the name: the directory says nothing about the map, and something about the user.
    identity.name = m_fileName.isEmpty() ? QString{} : QFileInfo{m_fileName}.fileName();
    identity.rooms = static_cast<qint64>(m_map.getRoomsCount());
    identity.generation = m_generation;
    return identity;
}

int64_t FrontendMapIdentity::announceDelayMs(const int64_t nowMs) const
{
    if (!m_announcedAtMs.has_value()) {
        return 0;
    }
    return std::max<int64_t>(0, *m_announcedAtMs + ANNOUNCE_INTERVAL_MS - nowMs);
}

void FrontendMapIdentity::announced(const int64_t nowMs)
{
    m_announcedAtMs = nowMs;
}
