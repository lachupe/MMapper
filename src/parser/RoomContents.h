#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "XmlElement.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

class GmcpMessage;

/// What MMapper has seen of one container in the room this session.
///
/// MUME's room line for a chest never says whether it is open or locked. That is only learnt
/// from the replies to commands aimed at it -- "Ok.", "*click*", "It seems to be locked." --
/// which ContainerTracker pairs with the command that caused them. A field is empty until
/// such a reply has said something about it.
struct NODISCARD ContainerState final
{
    std::optional<bool> open;
    std::optional<bool> locked;
    std::optional<bool> pickproof;
    std::optional<bool> empty;
    /// When the latest evidence arrived, in seconds since the Unix epoch; 0 when there is none.
    int64_t known = 0;
};

/// One line of the room's object list.
struct NODISCARD RoomObject final
{
    /// The position in MUME's own list of the room's objects, from 0, characters left out. It
    /// is the order MUME counts in when a command says "2.chest".
    int index = 0;
    /// How many identical objects the line stands for, when MUME folds them into one line.
    int count = 1;
    /// The line as MUME showed it, colour removed.
    QString line;
    /// The text of an <object> element inside the line, when MUME marked one up.
    QString name;
    /// For a container, the noun a command can name it by ("chest", "corpse"); empty otherwise.
    QString keyword;
    /// For a container, what a command sent now would have to say to reach it: "chest" for the
    /// first in the list, "2.chest" for the second.
    QString target;
    bool container = false;
    ContainerState state;
};

/// The objects lying in the room where the body stands, as the last room display showed them.
struct NODISCARD RoomContentsSnapshot final
{
    /// Which room this is, for keeping container state per room: MUME's room id when Room.Info
    /// gave one, and otherwise the room's name and description.
    QString roomKey;
    std::vector<RoomObject> objects;
    /// False when the display could not show the room (darkness, dense fog, blindness), so
    /// that an empty list says nothing about what lies there.
    bool seen = true;
    /// True when a room display produced this, false when only a container's state changed.
    bool entered = true;
};

/// The container noun a room line names, lowercased, or an empty string when the line names
/// none. The first such noun in the line wins: "The corpse of a rooster is lying here." is a
/// corpse.
NODISCARD QString containerKeyword(const QString &line);

/// True when `word`, as a command would say it ("chest", "che", "stonechest"), can name a
/// container whose noun is `keyword`, or any container when `keyword` is empty.
NODISCARD bool namesContainer(const QString &word, const QString &keyword = QString{});

/// Follows MUME's room displays to keep the list of objects in the room.
///
/// MumeXmlParser already gathers the lines of a room display that are neither its name nor its
/// description -- what lies there and who is there -- into ParserCommonData::roomContents at
/// </room>. They are handed in with the room's element here. The people among them are the
/// lines equal to a Room.Chars `desc`, and the lines holding a <character>, <player>, <enemy>
/// or <familiar> element; they are left out, and what remains are the objects, in MUME's
/// order. The list is complete at the prompt that ends the display, which comes after MMapper
/// has moved the player, so it describes the room MMapper.Map.Position has just named.
class NODISCARD RoomContentsTracker final
{
private:
    /// Room.Chars, as id -> desc.
    std::map<int64_t, QString> m_chars;
    QStringList m_lines;
    QStringList m_objectNames;
    QStringList m_people;
    QStringList m_scenery;
    QString m_roomKey;
    bool m_seen = true;
    bool m_dirty = false;

public:
    /// Folds a Room.Chars.Set, .Add, .Update or .Remove into who is in the room. Any other
    /// message is ignored.
    void receiveChars(const GmcpMessage &msg);
    /// Feeds one completed element. For a <room>, `roomLines` are its dynamic lines and
    /// `roomKey` names the room; both are ignored for any other element. Returns the room's
    /// objects at the prompt that ends a room display, and nullopt otherwise.
    NODISCARD std::optional<RoomContentsSnapshot> receive(const XmlElement &element,
                                                          const QString &roomLines,
                                                          const QString &roomKey);
    /// For a new session, or when XML mode goes away.
    void reset();

private:
    NODISCARD RoomContentsSnapshot build() const;
    NODISCARD bool isCharacterLine(const QString &line) const;
};
