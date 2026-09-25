#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include <QString>
#include <QStringList>

/// What the player wears and carries, read off MUME's listings and the replies that change them.
///
/// MUME sends no inventory or equipment over GMCP. What it prints instead names itself in its
/// first line -- "You are using:", "You are carrying:", "backpack (used) :", "Miltar of the
/// Golden Wood is using:" -- and ends with a blank line and the prompt, so a listing can be
/// read without knowing which command, alias or client asked for it. The replies to wear,
/// remove, get, put, drop and give are one line each and name what they moved.
///
/// The sentences come from the powwow logs (1999-2012), which are one player's configuration with
/// a client's additions mixed in. What is matched is the stable core of each message, and an
/// equipment label nobody listed is kept rather than refused, since MUME may word slots
/// differently today.

/// One object in a listing, split into its parts.
struct NODISCARD ListedItem final
{
    /// Without a leading count, the condition or the flags: "azure scrolls" for "two azure
    /// scrolls", "an engraved broadsword" for "an engraved broadsword (flawless); it glows blue".
    QString name;
    int count = 1;
    /// What the parentheses after the name say ("flawless", "worn out"), or empty.
    QString condition;
    /// What follows the name after semicolons, in MUME's words: "it glows blue", "it is lit".
    QStringList flags;
    /// The line as MUME wrote it, runs of spaces made one.
    QString text;
    /// For equipment: MUME's label without its brackets ("worn on belt"), and the slot id the
    /// label maps to ("belt"). Empty for anything else.
    QString label;
    QString slot;
    /// For equipment: a weapon wielded in both hands ("<wielded two-handed>").
    bool twoHanded = false;
};

enum class NODISCARD ItemBlockKindEnum : uint8_t {
    /// "You are using:" or "<someone> is using:".
    EQUIPMENT,
    /// "You are carrying:", or another's seen with "You attempt to peek at the inventory:".
    INVENTORY,
    /// "<keyword> (used|carried|here) :", or "It is closed." after a look into one.
    CONTAINER
};

/// One listing, complete.
struct NODISCARD ItemBlock final
{
    ItemBlockKindEnum kind = ItemBlockKindEnum::INVENTORY;
    /// EQUIPMENT and INVENTORY: "you" for the player, otherwise the person as MUME named them
    /// with any group label taken off. Empty for a peek whose owner the reply did not name.
    QString owner = QStringLiteral("you");
    /// INVENTORY: seen with a thief's peek, so someone else's.
    bool peek = false;
    /// CONTAINER: MUME's first keyword for it ("backpack"), and where it is: "used" (worn),
    /// "carried" or "here" (in the room); empty when the reply did not say.
    QString keyword;
    QString where;
    /// CONTAINER: the reply was "It is closed." rather than a listing.
    bool closed = false;
    std::vector<ListedItem> items;
    /// The listing as MUME wrote it, header included, one line per line.
    QString text;
};

enum class NODISCARD ItemActionEnum : uint8_t {
    WEAR,
    REMOVE,
    WIELD,
    HOLD,
    LIGHT,
    GET,
    PUT,
    DROP,
    GIVE,
    RECEIVE,
    REFUSED
};

/// One line that changed, or refused to change, what the player wears or carries.
struct NODISCARD ItemEvent final
{
    ItemActionEnum action = ItemActionEnum::REFUSED;
    /// The object as MUME named it: "a sable pouch", "the key" (drop names it by keyword).
    QString item;
    /// For GET and PUT, and a refused PUT: what it came out of or went into, as MUME named it
    /// ("a leather backpack", "your keyring").
    QString container;
    /// For WEAR, REMOVE and a refusal: where on the body, in MUME's words less "your": "belt",
    /// "right finger", "about body", "around neck"; "on" is left out. For WIELD, "two-handed"
    /// when both hands hold it.
    QString place;
    /// `place` as the slot id an equipment listing would give it ("about body" gives "about",
    /// "right finger" "finger", "arm" "shield"), or empty when there is no place.
    QString slot;
    /// For GIVE and RECEIVE: the other person, group label taken off.
    QString other;
    /// For REFUSED: slot-taken, hands-full, two-hands, too-many, too-heavy, cursed, wont-fit,
    /// not-carried, not-worn or cannot.
    QString reason;
    /// The line itself.
    QString text;
};

/// Splits an item line of a listing ("two azure scrolls", "a black sword (flawless); it glows
/// blue") into its parts.
NODISCARD ListedItem parseListedItem(const QString &line);

/// Splits an equipment line ("<worn on belt> a sable pouch"), or nullopt when it is not one.
NODISCARD std::optional<ListedItem> parseEquipmentLine(const QString &line);

/// The slot id for one of MUME's equipment labels ("worn around neck" gives "neck"). A label the
/// table does not know gives the label lowercased with spaces as underscores.
NODISCARD QString equipmentSlot(const QString &label);

/// The item reply `line` is, or nullopt when it is none of the ones MMapper reads.
NODISCARD std::optional<ItemEvent> parseItemEvent(const QString &line);

NODISCARD std::string_view to_string_view(ItemBlockKindEnum kind);
NODISCARD std::string_view to_string_view(ItemActionEnum action);

/// Gathers the listings of equipment, inventory and containers, line by line.
///
/// A header line opens a listing, the lines after it are its items, and a blank line, a line
/// that cannot be an item, or the prompt closes it. A listing that runs past a few hundred lines
/// without closing is dropped, not published.
class NODISCARD ItemBlockTracker final
{
private:
    struct NODISCARD Open final
    {
        ItemBlock block;
        QStringList lines;
        bool overflow = false;
    };

    /// The last container the player looked into, for the "It is closed." that answers it
    /// without naming it.
    struct NODISCARD Look final
    {
        QString word;
        int prompts = 0;
    };

    std::optional<Open> m_open;
    std::optional<Look> m_look;
    /// Whose equipment the reply being read showed, for the peek at their inventory after it.
    QString m_lastOwner;
    /// Where each container was last listed, so that "It is closed." can say it again.
    std::map<QString, QString> m_whereOf;

public:
    /// Notes a command on its way to MUME: only a look into a container matters here.
    void receiveCommand(const QString &input);
    /// Reads one line of MUME's output, colour removed, and returns the listings it completes.
    NODISCARD std::vector<ItemBlock> receiveLine(const QString &line);
    /// A prompt closes the listing being read.
    NODISCARD std::vector<ItemBlock> receivePrompt();
    /// For a new session, or when XML mode goes away.
    void reset();

private:
    NODISCARD std::vector<ItemBlock> close();
    NODISCARD bool accept(const QString &text);
    void open(ItemBlock block, const QString &header);
};
