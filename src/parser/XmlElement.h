#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QString>

/// The vocabulary MUME's XML mode wraps around its output.
///
/// MMapper's room parser only ever needed the handful of tags that describe a room, and the
/// character stream drops every tag before it reaches a client: MumeXmlParser::parse() puts
/// everything between < and > into a scratch buffer, hands it to element() for room-mode
/// tracking, and never appends it to the line the user sees. MUME marks up considerably
/// more than rooms -- who struck whom, what is lying on the ground, who spoke -- and a
/// renderer wants all of it, so the full documented vocabulary is listed here.
///
/// X(UPPER_CASE, "tag as MUME spells it", CATEGORY)
#define XFOREACH_XML_TAG(X) \
    /* The room. MMapper consumes these itself, and they are repeated here so that a */ \
    /* client reading this feed sees the same document MUME sent. */ \
    X(ROOM, "room", ROOM) \
    X(NAME, "name", ROOM) \
    X(DESCRIPTION, "description", ROOM) \
    X(TERRAIN, "terrain", ROOM) \
    X(EXITS, "exits", ROOM) \
    X(EXIT, "exit", ROOM) \
    X(HEADER, "header", ROOM) \
    X(PROMPT, "prompt", STATUS) \
    X(STATUS, "status", STATUS) \
    X(WEATHER, "weather", STATUS) \
    /* One blow, and how it landed. */ \
    X(HIT, "hit", COMBAT) \
    X(DAMAGE, "damage", COMBAT) \
    X(AVOID_DAMAGE, "avoid_damage", COMBAT) \
    X(MISS, "miss", COMBAT) \
    X(MAGIC, "magic", MAGIC) \
    /* Who and what: the participants in a blow, and objects lying in the room. MUME has */ \
    /* no GMCP package for room contents, so this is the only structured sight of them. */ \
    X(CHARACTER, "character", ENTITY) \
    X(PLAYER, "player", ENTITY) \
    X(ENEMY, "enemy", ENTITY) \
    X(FAMILIAR, "familiar", ENTITY) \
    X(OBJECT, "object", ENTITY) \
    X(MOVEMENT, "movement", MOVEMENT) \
    X(MOVE_IN, "move_in", MOVEMENT) \
    X(MOVE_OUT, "move_out", MOVEMENT) \
    X(SAY, "say", COMMUNICATION) \
    X(TELL, "tell", COMMUNICATION) \
    X(NARRATE, "narrate", COMMUNICATION) \
    X(SHOUT, "shout", COMMUNICATION) \
    X(YELL, "yell", COMMUNICATION) \
    X(SONG, "song", COMMUNICATION) \
    X(PRAY, "pray", COMMUNICATION) \
    X(EMOTE, "emote", COMMUNICATION) \
    X(SOCIAL, "social", COMMUNICATION) \
    X(ACHIEVEMENT, "achievement", PROGRESS) \
    X(SNOOP, "snoop", FORMATTING) \
    X(CODE, "code", FORMATTING) \
    X(EM, "em", FORMATTING) \
    X(HIGHLIGHT, "highlight", FORMATTING) \
    X(XML, "xml", FORMATTING) \
    X(GRATUITOUS, "gratuitous", FORMATTING) \
    /* define xml tags above */

enum class NODISCARD XmlTagEnum : uint8_t {
#define X_DECL_XML_TAG(UPPER_CASE, lower_case, category) UPPER_CASE,
    XFOREACH_XML_TAG(X_DECL_XML_TAG)
#undef X_DECL_XML_TAG
    /// A tag MUME sent that is not in the list above. The documentation says outright that
    /// "there will be more", so an unrecognised tag is reported rather than dropped.
    UNKNOWN
};

enum class NODISCARD XmlCategoryEnum : uint8_t {
    ROOM,
    STATUS,
    COMBAT,
    MAGIC,
    ENTITY,
    MOVEMENT,
    COMMUNICATION,
    PROGRESS,
    FORMATTING,
    UNKNOWN
};

using XmlAttributes = std::vector<std::pair<std::string, std::string>>;

/// One complete element: everything MUME wrapped in a matching pair of tags.
///
/// `text` is what MUME would have shown, markup removed and entities decoded, and it
/// includes the text of every child. Children are nested rather than reported separately so
/// that a consumer receives a blow and its participants as one thing:
///
///     <hit><character>A dirty uruk</character> barely slashes your body.</hit>
///
/// arrives as a single HIT element whose text is the whole sentence and whose children hold
/// the CHARACTER inside it.
struct NODISCARD XmlElement final
{
    XmlTagEnum tag = XmlTagEnum::UNKNOWN;
    /// As MUME spelled it. This is the only identity an UNKNOWN tag has.
    std::string name;
    XmlAttributes attributes;
    QString text;
    std::vector<XmlElement> children;
    /// True when something here was cut short: text past the size limit was discarded, the
    /// element was closed implicitly because MUME never sent its closing tag, or a nested
    /// element was refused for being too deep. It propagates outwards, so an element that
    /// does not carry it is complete all the way down.
    bool truncated = false;
    /// For the movement tags only: which way someone went, as a lowercase direction word
    /// ("north" ... "down"), or empty when that cannot be told. For MOVE_IN it is where they
    /// came from and for MOVE_OUT where they went, read out of the line; for MOVEMENT it is
    /// MUME's own `dir`. Filled in when the element completes; see deriveMovementDirection().
    std::string direction;
};

NODISCARD XmlTagEnum toXmlTag(std::string_view name);
NODISCARD std::string_view to_string_view(XmlTagEnum tag);
NODISCARD XmlCategoryEnum toXmlCategory(XmlTagEnum tag);
NODISCARD std::string_view to_string_view(XmlCategoryEnum category);

/// Which way a movement element says someone went: MUME's own `dir` attribute where it gives
/// one, and otherwise, for MOVE_IN and MOVE_OUT, the direction word in the line itself --
/// "A scholar has arrived from the south." is south, "... leaves west." west, "from above" and
/// "from below" up and down. MUME documents `dir` only for the player's own MOVEMENT, so for
/// other characters the line is the only statement of it.
///
/// The names of whoever is moving are left out of the search, so that "The North Wind leaves
/// south." is south; when several direction words remain the last one wins, because the
/// direction closes the sentence ("... from the north, riding a pony" still ends on the
/// direction's clause). Empty for every other tag, and for a line that names no direction,
/// such as "An old man leaves with a sigh."
NODISCARD std::string deriveMovementDirection(const XmlElement &element);

/// Parses the attributes out of a tag body, i.e. what stood between < and > with the tag
/// name still on the front. MUME's XML is, in its own words, "not very strict": values may
/// be single quoted, double quoted or bare.
NODISCARD XmlAttributes parseXmlAttributes(const QString &tagBody);
