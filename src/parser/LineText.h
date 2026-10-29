#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <optional>

#include <QString>

struct XmlElement;

/// How the line readers clean a line of MUME's output before they match it. The helpers were
/// copied into each reader; they live here so that a change to the twiddlers or the colour
/// codes is made once.
namespace line_text {

/// The "twiddlers" prompt option draws \|/- (and backspaces) while a delayed action runs, and
/// they end up on the front of whatever line comes next: "/-\|/-Ok.". Takes them off the front
/// of `line` when a capital letter or a star follows.
void removeTwiddlers(QString &line);

/// As removeTwiddlers(), and also before a lowercase letter or a "<": an item listing's line
/// starts in lowercase. A deliberate difference of ItemLines, kept as this explicit variant.
void removeTwiddlersBeforeLowercase(QString &line);

/// The line with colour removed.
NODISCARD QString plainOf(const QString &line);

/// The line with colour removed and the spacing simplified.
NODISCARD QString cleaned(const QString &line);

/// The line with colour removed, the spacing simplified and the twiddlers taken off.
NODISCARD QString cleanedWithoutTwiddlers(const QString &line);

/// As cleanedWithoutTwiddlers(), for the item listings (removeTwiddlersBeforeLowercase()).
NODISCARD QString cleanedForItems(const QString &line);

/// The line trimmed, with the twiddlers taken off; colour is left in.
NODISCARD QString trimmedWithoutTwiddlers(const QString &line);

/// The line with colour and the line ending removed, the spacing inside kept: for the tables
/// that are laid out in columns.
NODISCARD QString rawLine(const QString &line);

/// "1,136,776" or "-45" as a number; nullopt when it is not one.
NODISCARD std::optional<int64_t> toNumber(QString digits);

/// Whether a cleaned line can be an item in an inventory or container listing: the first letter
/// of an item is lowercase ("a gold ring", "two amethysts", "the scalp of Horiam"), while the
/// lines that could break into a listing are sentences and start with a capital.
NODISCARD bool isItemLine(const QString &text);

/// Whether a room display shows the room: in dense fog or darkness MUME puts a line saying so
/// where the name would be, and what the rest of the display says is then not the room's.
NODISCARD bool seesTheRoom(const XmlElement &room);

} // namespace line_text
