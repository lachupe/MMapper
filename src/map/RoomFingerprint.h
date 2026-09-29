#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "mmapper2room.h"

#include <vector>

#include <QByteArray>
#include <QString>

class RoomHandle;

/// A short hash of what a room shows, the same in every export of the same map.
///
/// MUME gives most rooms an id of its own (serverId), but not every room, and MMapper's own ids
/// (externalId) belong to one map file: another export of the same world may number its rooms
/// differently. What a room shows -- its name, description, terrain and exits, and the names of
/// the rooms those exits lead to -- is the same in every export, so a hash of it is a key that a
/// frontend holding one export can match against another, or against the map MMapper has
/// loaded. It is derived: MMapper never reads it back, and it changes whenever any of those do.
///
/// The fingerprint is the first DIGITS hex digits, in lower case, of SHA-256 over the UTF-8 of
///
///     name "\n" description "\n" terrain "\n" letters "\n" targets
///
/// - `description` is the room's description exactly as the XML export writes it, final
///   newline included;
/// - `terrain` is the word the XML export writes for the room's terrain, such as "FOREST",
///   or "" when it writes none (UNDEFINED);
/// - `letters` are the one-letter directions (N, S, E, W, U, D) of the exits the room has,
///   sorted alphabetically and joined by ","; for instance "E,N,S,U";
/// - `targets` are, in the order of `letters`, the name of the room each exit leads to, or ""
///   for an exit that leads to no room on the map, joined by "\n".
///
/// An exit the room has is one with MMapper's EXIT flag, in one of those six directions: in the
/// XML export, an <exit> without <exitflag>NO_EXIT</exitflag>, with or without a <to>. An exit
/// leading to more than one room names the one with the lowest externalId, which is the first
/// <to> the export writes. MMapper keeps names and descriptions in ASCII, so in practice the
/// UTF-8 is ASCII.
namespace room_fingerprint {

/// How many hex digits of the SHA-256 are kept.
static constexpr int DIGITS = 12;

/// One exit the room has, as the fingerprint sees it.
struct NODISCARD Exit final
{
    /// 'N', 'S', 'E', 'W', 'U' or 'D'.
    char letter = '?';
    /// The name of the room it leads to; empty for an exit that leads to no room on the map.
    QString target;
};

/// Everything a fingerprint is computed from.
struct NODISCARD Fields final
{
    QString name;
    QString description;
    QString terrain;
    /// In any order: canonicalText() sorts them.
    std::vector<Exit> exits;
};

/// The word the XML export writes for `terrain`, or "" for UNDEFINED, which it leaves out.
NODISCARD QString terrainWord(RoomTerrainEnum terrain);

/// The fields of `room`, with the names of the rooms its exits lead to looked up in its map.
NODISCARD Fields fieldsOf(const RoomHandle &room);

/// The bytes that are hashed.
NODISCARD QByteArray canonicalText(const Fields &fields);

/// The fingerprint of `fields`.
NODISCARD QString compute(const Fields &fields);

/// The fingerprint of `room`: the one MMapper.Map.Position and the XML export give it.
NODISCARD QString compute(const RoomHandle &room);

} // namespace room_fingerprint
