#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <optional>

#include <QString>

/// One social or emote MUME showed (MMapper.Room.Social): who did which social, and at whom.
///
/// MUME's XML mode wraps these lines in <social>, but says nothing of which social it is; the
/// lines of the social table (SocialLines.inc, generated from mume3d's data/socials/socials.json)
/// say that. A line is matched whole, the most specific line first.
struct NODISCARD RoomSocial final
{
    /// As MUME wrote it: "Bob", "The cityguard"; "you" is never an actor here, as MUME's lines
    /// about the player's own social are in the second person and name no one.
    QString actor;
    /// The social's command name ("smile").
    QString social;
    /// As MUME wrote it; "you" for the player, the actor's own name for a social at themself.
    /// Empty for none.
    QString target;
    /// "alone", "other", "you" or "self".
    QString form;
    /// The line, colour removed.
    QString text;
};

/// What `line` (colour removed) says, or nullopt when it is no known social line.
NODISCARD std::optional<RoomSocial> parseSocialLine(const QString &line);
