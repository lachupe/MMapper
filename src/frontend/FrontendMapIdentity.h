#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../map/Map.h"
#include "FrontendMessages.h"

#include <cstdint>
#include <optional>

#include <QString>

/// The loaded map as MMapper.Session.State names it -- the file it came from, how many rooms it
/// has, and how many times it has changed since it was loaded -- and when a change of those may
/// next be announced.
///
/// A frontend that reads the map from an export of its own, rather than from MMapper, uses
/// these to notice that the export is of another map or has fallen behind this one.
class NODISCARD FrontendMapIdentity final
{
public:
    /// MMapper.Session.State is sent again for the map at most this often, so that a burst of
    /// changes -- mapping at speed, a change to a whole selection, a run of undos -- is announced
    /// once, when it is over, rather than once for each.
    static constexpr int64_t ANNOUNCE_INTERVAL_MS = 1000;

private:
    /// The map as last seen, to tell a change apart from a notification that changed nothing.
    Map m_map;
    QString m_fileName;
    int64_t m_generation = 0;
    std::optional<int64_t> m_announcedAtMs;

public:
    /// A map was loaded, or a new empty one started: the generation starts again at 0.
    void loaded(const Map &map, const QString &fileName);

    /// MapData reported a change. Counts it, and returns true, only if `map` differs from the
    /// map last seen: MapData also reports changes that left the map as it was.
    NODISCARD bool changed(const Map &map);

    /// The map was saved; returns true if that gave it a new file name.
    NODISCARD bool renamed(const QString &fileName);

    NODISCARD frontend_messages::MapIdentity get() const;

public:
    /// How long, in milliseconds from `nowMs`, to hold back the next announcement: 0 if it may
    /// go now.
    NODISCARD int64_t announceDelayMs(int64_t nowMs) const;

    /// Records that MMapper.Session.State went out at `nowMs`, for whatever reason.
    void announced(int64_t nowMs);
};
