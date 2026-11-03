#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <optional>

#include <QString>

/// One message on the group's channel (MMapper.Comm.Group): MUME's `<tell>` line
/// "Kili tells the group 'follow me'" or the player's own "You tell the group 'ok'".
struct NODISCARD CommGroup final
{
    /// As MUME wrote it ("Kili", "The cityguard"); "you" for the player's own message.
    QString speaker;
    /// What was said, between the quotes.
    QString text;
    /// The line, colour removed.
    QString line;
};

/// What `line` (colour removed) says, or nullopt when it is no group tell.
NODISCARD std::optional<CommGroup> parseGroupTell(const QString &line);
