#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "CombatLines.h"
#include "XmlElement.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <QString>
#include <QStringList>

/// What MMapper's readers made of one chunk of MUME's output, for the prioritised Log
/// (MMapper.Log, spec section 37). MumeXmlParser::parse() fills one LineFacts per chunk as its
/// readers claim the line and emits it once, after every reader (GameObserver::sig2_lineFacts);
/// the rules of the Log (LogRules) match on these tags.
///
/// X(UPPER_CASE, "tag as the rules spell it")
#define XFOREACH_LINE_TAG(X) \
    X(PROMPT, "prompt") \
    X(PAGER, "pager") \
    X(QUIET, "quiet") \
    X(ROOM, "room") \
    X(ROOM_DESC, "room.desc") \
    X(COMBAT, "combat") \
    X(COMBAT_BLOW, "combat.blow") \
    X(COMBAT_FLEE, "combat.flee") \
    X(COMBAT_REFUSED, "combat.refused") \
    X(COMBAT_BASH, "combat.bash") \
    X(COMBAT_BACKSTAB, "combat.backstab") \
    X(COMBAT_CONDITION, "combat.condition") \
    X(COMBAT_DEATH, "combat.death") \
    X(COMBAT_CAST, "combat.cast") \
    X(COMBAT_SELF, "combat.self") \
    X(COMBAT_AFFECT, "combat.affect") \
    X(COMBAT_RESCUE, "combat.rescue") \
    X(COMBAT_ASSIST, "combat.assist") \
    X(COMBAT_HIT, "combat.hit") \
    X(COMBAT_PARRY, "combat.parry") \
    X(COMBAT_DODGE, "combat.dodge") \
    X(COMBAT_MISS, "combat.miss") \
    X(LOGIN, "login") \
    X(ACCOUNT, "account") \
    X(CONTAINER, "container") \
    X(DOOR, "door") \
    X(REFUSED, "refused") \
    X(ITEMS, "items") \
    X(ITEM, "item") \
    X(STAT, "stat") \
    X(SCORE, "score") \
    X(INFO, "info") \
    X(FOLLOWERS, "followers") \
    X(SHOP, "shop") \
    X(GUILD, "guild") \
    X(INN, "inn") \
    X(TROPHIES, "trophies") \
    X(COMM, "comm") \
    X(COMM_TELL, "comm.tell") \
    X(COMM_GTELL, "comm.gtell") \
    X(COMM_NARRATE, "comm.narrate") \
    X(COMM_YELL, "comm.yell") \
    X(COMM_SHOUT, "comm.shout") \
    X(COMM_SAY, "comm.say") \
    X(COMM_SONG, "comm.song") \
    X(COMM_PRAY, "comm.pray") \
    X(COMM_EMOTE, "comm.emote") \
    X(COMM_SOCIAL, "comm.social") \
    X(MOVE, "move") \
    X(MAGIC, "magic") \
    X(ACHIEVEMENT, "achievement") \
    X(WEATHER, "weather") \
    X(TEXT, "text") \
    /* define line tags above */

enum class NODISCARD LineTagEnum : uint8_t {
#define X_DECL_LINE_TAG(UPPER_CASE, name) UPPER_CASE,
    XFOREACH_LINE_TAG(X_DECL_LINE_TAG)
#undef X_DECL_LINE_TAG
};

#define X_COUNT(...) +1
static constexpr const size_t NUM_LINE_TAGS = XFOREACH_LINE_TAG(X_COUNT);
#undef X_COUNT
static_assert(NUM_LINE_TAGS <= 64, "LineTagSet is one 64-bit word");

NODISCARD std::string_view lineTagName(LineTagEnum tag);
/// The tag a rule names, or nullopt for a name no reader gives.
NODISCARD std::optional<LineTagEnum> lineTagFromName(const QString &name);

/// A set of tags, one bit each.
struct NODISCARD LineTagSet final
{
    uint64_t bits = 0;

    void insert(const LineTagEnum tag) { bits |= bit(tag); }
    NODISCARD bool contains(const LineTagEnum tag) const { return (bits & bit(tag)) != 0; }
    NODISCARD bool empty() const { return bits == 0; }
    NODISCARD static uint64_t bit(const LineTagEnum tag)
    {
        return uint64_t{1} << static_cast<unsigned>(tag);
    }
    /// The names, in the order of XFOREACH_LINE_TAG.
    NODISCARD QStringList names() const;
};

/// Who a line is about, as far as the readers said: the player (a blow by or at "you", a tell
/// to the player), the group (a group tell), or not known here (the Log decides from the names).
enum class NODISCARD LineAboutEnum : uint8_t { UNKNOWN, YOU, GROUP };

/// One chunk of MUME's output as the Log sees it. A message of communication over several
/// lines is one LineFacts (LineTagger joins it).
struct NODISCARD LineFacts final
{
    /// As the terminal was sent it, ANSI escapes kept; the lines of a joined message are joined
    /// by a newline.
    QString text;
    /// Colour removed, trimmed; the lines of a joined message are joined by a space.
    QString plain;
    LineTagSet tags;
    /// The names the readers found in it: a blow's actor and target, a speaker. "you" is never
    /// one of them.
    QStringList names;
    LineAboutEnum about = LineAboutEnum::UNKNOWN;
};

/// Communication kinds, from MUME's elements (`<tell>`, `<narrate>`, ...). A `<tell>` to the
/// group ("Kili tells the group '...'", "You tell the group '...'") is GTELL.
NODISCARD std::optional<LineTagEnum> commTagOf(const XmlElement &element);

/// The tags of a fight's event: `combat`, `combat.<kind>` and, for a blow, `combat.<outcome>`;
/// its actor and target go into the names, "you" into `about`.
void tagCombat(LineFacts &facts, const CombatEvent &event);

/// The tags of the elements that closed on a chunk: communication (with the speaker in the
/// names), movement, magic, achievements, and a COMBAT element the fight reader did not read.
/// Room and status elements say nothing here: the parser knows a room display by itself.
void tagElements(LineFacts &facts, const std::vector<XmlElement> &elements);

/// Joins a message of communication MUME wraps over several lines into one LineFacts, and
/// gives `text` to a line nothing else claimed.
///
/// finish() is called once per chunk that gets a record, after every reader, with the tags
/// collected and the communication elements still open after the chunk: while one is open the
/// line is held, and the record of the whole message comes with the line that closes it.
class NODISCARD LineTagger final
{
public:
    /// A message that never closes is let go after this many lines.
    static constexpr const int MAX_JOINED_LINES = 20;

private:
    std::optional<LineFacts> m_pending;
    int m_pendingLines = 0;

public:
    /// The records to emit for this chunk: none while a message is open, the joined message
    /// when it closes, and a held message first when something else interrupts it (a prompt).
    NODISCARD std::vector<LineFacts> finish(LineFacts facts, bool commOpen);
    /// For a new session.
    void reset();
};
