// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "ItemLines.h"

#include "../global/parserutils.h"
#include "ContainerLines.h"

#include <algorithm>
#include <array>
#include <utility>

#include <QRegularExpression>

namespace {

using A = ItemActionEnum;
using K = ItemBlockKindEnum;

/// A listing longer than this is taken for one whose end went by unseen, and dropped. The
/// longest in the logs is a container of a few dozen lines; MUME caps what one can carry well
/// below this.
constexpr int MAX_BLOCK_LINES = 200;
/// How long a look into a container waits for "It is closed.", in prompts.
constexpr int PROMPTS_TO_WAIT = 2;
/// Containers whose place is remembered.
constexpr size_t PLACES_REMEMBERED = 32;

struct NODISCARD SlotName final
{
    const char *label;
    const char *slot;
};

// MUME's equipment labels and the slot ids the frontend protocol fixes for them. The first
// block is the table of the protocol spec; the second are the other wordings the logs show for
// the same places, and one place the table left out (across the back: a quiver, a longbow).
constexpr std::array SLOTS{SlotName{"used as light", "light"},
                           SlotName{"worn on finger", "finger"},
                           SlotName{"worn around neck", "neck"},
                           SlotName{"worn on body", "body"},
                           SlotName{"worn on head", "head"},
                           SlotName{"worn on legs", "legs"},
                           SlotName{"worn on feet", "feet"},
                           SlotName{"worn on hands", "hands"},
                           SlotName{"worn as belt", "belt"},
                           SlotName{"worn on belt", "belt"},
                           SlotName{"worn on arms", "arms"},
                           SlotName{"worn as shield", "shield"},
                           SlotName{"worn about body", "about"},
                           SlotName{"worn about waist", "waist"},
                           SlotName{"worn around wrist", "wrist"},
                           SlotName{"worn on wrist", "wrist"},
                           SlotName{"wielded", "wielded"},
                           SlotName{"held", "held"},
                           SlotName{"worn on back", "back"},
                           // Seen in the logs besides the table.
                           SlotName{"used as shield", "shield"},
                           SlotName{"worn on forearm", "shield"},
                           SlotName{"wielded two-handed", "wielded"},
                           SlotName{"as two-handed weapon", "wielded"},
                           SlotName{"as right weapon", "wielded"},
                           SlotName{"worn across back", "across_back"}};

// Where a wear, remove or refusal puts it, as "on your X" and friends say it, and the slot.
constexpr std::array
    PLACES{SlotName{"feet", "feet"},          SlotName{"hands", "hands"},
           SlotName{"body", "body"},          SlotName{"legs", "legs"},
           SlotName{"head", "head"},          SlotName{"arms", "arms"},
           SlotName{"back", "back"},          SlotName{"belt", "belt"},
           SlotName{"girdle", "belt"},        SlotName{"arm", "shield"},
           SlotName{"neck", "neck"},          SlotName{"around neck", "neck"},
           SlotName{"about body", "about"},   SlotName{"around body", "about"},
           SlotName{"about waist", "waist"},  SlotName{"around waist", "waist"},
           SlotName{"waist", "waist"},        SlotName{"wrist", "wrist"},
           SlotName{"right wrist", "wrist"},  SlotName{"left wrist", "wrist"},
           SlotName{"finger", "finger"},      SlotName{"right finger", "finger"},
           SlotName{"left finger", "finger"}, SlotName{"across back", "across_back"}};

// The "twiddlers" prompt option draws \|/- while a delayed action runs, and they end up on the
// front of the next line. Backspaces come with them when the terminal is live.
const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*<a-z]))")};
// A group label trails a name in parentheses: "Stolb (S)", "the sage (buh)".
const QRegularExpression g_label{QStringLiteral(R"(\s+\([^()]{1,8}\)$)")};

const QRegularExpression g_equipment{QStringLiteral(R"(^<([^<>]+)>\s*(\S.*)$)")};
const QRegularExpression g_condition{QStringLiteral(R"(\s*\(([^()]*)\)$)")};
const QRegularExpression g_conditionWords{QStringLiteral(R"(^[a-z][a-z ,.-]*$)")};

// "Miltar of the Golden Wood is using:", "*Herby the Dwarf* is using:".
const QRegularExpression g_using{QStringLiteral(R"(^([A-Z*].*) is using:$)")};
// "backpack (used) :", "pouch (carried) :", "corpse (here) :", "the corpse of *an Elf* (here):".
const QRegularExpression g_container{
    QStringLiteral(R"(^([a-z*][^()]*?) \((used|carried|here|worn)\) ?:$)")};
const QRegularExpression g_isClosed{QStringLiteral(R"(^(?:The|An?) (.+) is closed\.$)")};

// Replies of one line.
const QRegularExpression g_wear{
    QStringLiteral(R"(^You wear (.+?) (on|about|around|across|over) your (.+)\.$)")};
const QRegularExpression g_fasten{
    QStringLiteral(R"(^You fasten (.+?) on your (.+?)(?:, (?:becoming|looking) .+)?\.$)")};
const QRegularExpression g_putOnYour{QStringLiteral(R"(^You put (.+?) on your (.+)\.$)")};
const QRegularExpression g_putOn{QStringLiteral(R"(^You put (.+?) on (.+)\.$)")};
const QRegularExpression g_putIn{QStringLiteral(R"(^You put (.+) in (.+)\.$)")};
const QRegularExpression g_wield{QStringLiteral(
    R"(^You wield (.+?)(?: as (two-handed) weapon)?(?:, (?:looking|having|becoming) .+)?\.$)")};
const QRegularExpression g_hold{QStringLiteral(R"(^You hold (?!on )(.+)\.$)")};
const QRegularExpression g_grab{
    QStringLiteral(R"(^You grab (?!the handle |the ladder )([^,]+)\.$)")};
const QRegularExpression g_light{QStringLiteral(R"(^You light (.+)\.$)")};
const QRegularExpression g_stopUsing{QStringLiteral(R"(^You stop using (.+)\.$)")};
const QRegularExpression g_takeOff{QStringLiteral(R"(^You take (.+) off (?:the|your) (\S+)\.$)")};
const QRegularExpression g_removeFrom{QStringLiteral(
    R"(^You remove (.+?) from (your keyring|.+?)(?: and put into your inventory)?\.$)")};
const QRegularExpression g_getFrom{QStringLiteral(R"(^You get (?!a shock )(.+) from (.+)\.$)")};
const QRegularExpression g_get{QStringLiteral(R"(^You get (?!a shock )(.+)\.$)")};
const QRegularExpression g_drop{QStringLiteral(R"(^You drop (.+)\.$)")};
const QRegularExpression g_give{QStringLiteral(R"(^You give (.+?) to (.+)\.$)")};
const QRegularExpression g_receive{QStringLiteral(R"(^([A-Z*][^'"]*?) gives you (.+)\.$)")};

const QRegularExpression g_slotTaken{
    QStringLiteral(R"(^You are already wearing something (on|about|around) your (.+)\.$)")};
const QRegularExpression g_tooMany{
    QStringLiteral(R"(^You can't get (.+), you are carrying too many items already\.$)")};
const QRegularExpression g_tooHeavy{
    QStringLiteral(R"(^You can't get (.+), (?:it's|it is|they're|they are) too heavy\.$)")};
const QRegularExpression g_cursed{
    QStringLiteral(R"(^You can't remove (.+)\. It appears to be cursed\.$)")};
const QRegularExpression g_wontFit{QStringLiteral(R"(^(.+) won't fit in (.+)\.$)")};
const QRegularExpression g_cantPut{QStringLiteral(R"(^You can't put (.+) in (.+)\.$)")};
const QRegularExpression g_cant{QStringLiteral(R"(^You can't (?:wear|wield|hold|take) (.+)\.$)")};
const QRegularExpression g_dontHave{QStringLiteral(R"(^You don't have (?!the proper key)(.+)\.$)")};
const QRegularExpression g_notWearing{
    QStringLiteral(R"(^You (?:aren't|are not) wearing (?:any )?(.+)\.$)")};

NODISCARD QString cleaned(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    text = text.simplified();
    text.remove(g_twiddlers);
    return text;
}

NODISCARD QString withoutLabel(QString name)
{
    name.remove(g_label);
    return name;
}

NODISCARD QString placeSlot(const QString &place)
{
    if (place.isEmpty()) {
        return QString{};
    }
    const QString lower = place.toLower();
    for (const SlotName &entry : PLACES) {
        if (lower == QLatin1String(entry.label)) {
            return QString::fromLatin1(entry.slot);
        }
    }
    QString slot = lower;
    slot.replace(QLatin1Char(' '), QLatin1Char('_'));
    return slot;
}

/// "around neck" for "around your neck", "feet" for "on your feet".
NODISCARD QString placeOf(const QString &preposition, const QString &part)
{
    if (preposition.isEmpty() || preposition == QStringLiteral("on")) {
        return part;
    }
    return preposition + QLatin1Char(' ') + part;
}

NODISCARD ItemEvent event(const A action, const QString &item, const QString &text)
{
    ItemEvent result;
    result.action = action;
    result.item = item;
    result.text = text;
    return result;
}

NODISCARD ItemEvent placed(const A action,
                           const QString &item,
                           const QString &place,
                           const QString &text)
{
    ItemEvent result = event(action, item, text);
    result.place = place;
    result.slot = placeSlot(place);
    return result;
}

NODISCARD ItemEvent refused(const QString &reason, const QString &text)
{
    ItemEvent result = event(A::REFUSED, QString{}, text);
    result.reason = reason;
    return result;
}

/// Lines that can be an item in an inventory or container listing: the first letter of an item
/// is lowercase ("a gold ring", "two amethysts", "the scalp of Horiam"), while the lines that
/// could break into a listing are sentences and start with a capital.
NODISCARD bool isItemLine(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    const QChar first = text.front();
    return first.isLower() || first.isDigit() || text == QStringLiteral("Something.");
}

/// "It is closed.", or "The leather backpack is closed." for the container `word` names; not
/// "The door is closed.", which a look in a direction says.
NODISCARD bool isClosedReply(const QString &text, const QString &word)
{
    if (text == QStringLiteral("It is closed.")) {
        return true;
    }
    const auto match = g_isClosed.match(text);
    return match.hasMatch() && !word.isEmpty()
           && match.captured(1).split(QLatin1Char(' ')).contains(word, Qt::CaseInsensitive);
}

NODISCARD bool isNothing(const QString &text)
{
    return text == QStringLiteral("Nothing.") || text == QStringLiteral("You don't see anything.");
}

} // namespace

ListedItem parseListedItem(const QString &line)
{
    const QString text = line.simplified();
    // The name and count as the room's container listings have them, so that both packages
    // agree on "two azure scrolls".
    const ContainerItem base = parseContainerItem(text);

    ListedItem item;
    item.name = base.name;
    item.count = base.count;
    item.text = text;

    QString rest = text;
    if (const qsizetype semicolon = rest.indexOf(QLatin1Char(';')); semicolon > 0) {
        for (const QString &flag : rest.mid(semicolon + 1).split(QLatin1Char(';'))) {
            if (const QString trimmed = flag.trimmed(); !trimmed.isEmpty()) {
                item.flags << trimmed;
            }
        }
        rest = rest.left(semicolon);
    }
    // "(flawless)", "(worn out)", "(brand new)". "(invisible)" is not a condition but a flag,
    // as "; it is invisible" is.
    const auto match = g_condition.match(rest);
    if (match.hasMatch() && match.capturedStart() > 0) {
        const QString words = match.captured(1).trimmed();
        if (words == QStringLiteral("invisible")) {
            item.flags.prepend(words);
        } else if (g_conditionWords.match(words).hasMatch()) {
            item.condition = words;
        }
    }
    return item;
}

std::optional<ListedItem> parseEquipmentLine(const QString &line)
{
    const QString text = line.simplified();
    const auto match = g_equipment.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    ListedItem item = parseListedItem(match.captured(2));
    item.text = text;
    item.label = match.captured(1).trimmed();
    item.slot = equipmentSlot(item.label);
    const QString lower = item.label.toLower();
    item.twoHanded = lower == QStringLiteral("wielded two-handed")
                     || lower == QStringLiteral("as two-handed weapon");
    return item;
}

QString equipmentSlot(const QString &label)
{
    const QString lower = label.simplified().toLower();
    for (const SlotName &entry : SLOTS) {
        if (lower == QLatin1String(entry.label)) {
            return QString::fromLatin1(entry.slot);
        }
    }
    QString slot = lower;
    slot.replace(QLatin1Char(' '), QLatin1Char('_'));
    return slot;
}

std::optional<ItemEvent> parseItemEvent(const QString &line)
{
    const QString text = cleaned(line);
    if (text.size() < 8) {
        return std::nullopt;
    }

    if (text.startsWith(QStringLiteral("You "))) {
        if (const auto m = g_wear.match(text); m.hasMatch()) {
            return placed(A::WEAR, m.captured(1), placeOf(m.captured(2), m.captured(3)), text);
        }
        if (const auto m = g_fasten.match(text); m.hasMatch()) {
            // "on your belt", and a shield "on your arm, becoming very impressive".
            return placed(A::WEAR, m.captured(1), m.captured(2), text);
        }
        if (const auto m = g_putOnYour.match(text); m.hasMatch()) {
            // The keyring is a container; a finger, a head or a back is a place.
            if (m.captured(2) == QStringLiteral("keyring")) {
                ItemEvent result = event(A::PUT, m.captured(1), text);
                result.container = QStringLiteral("your keyring");
                return result;
            }
            return placed(A::WEAR, m.captured(1), m.captured(2), text);
        }
        if (const auto m = g_putIn.match(text); m.hasMatch()) {
            ItemEvent result = event(A::PUT, m.captured(1), text);
            result.container = m.captured(2);
            return result;
        }
        if (const auto m = g_putOn.match(text); m.hasMatch()) {
            // A saddle "on a trained horse (my)'s back".
            ItemEvent result = event(A::PUT, m.captured(1), text);
            result.container = m.captured(2);
            return result;
        }
        if (const auto m = g_wield.match(text); m.hasMatch()) {
            return placed(A::WIELD, m.captured(1), m.captured(2), text);
        }
        if (const auto m = g_hold.match(text); m.hasMatch()) {
            return event(A::HOLD, m.captured(1), text);
        }
        if (const auto m = g_grab.match(text); m.hasMatch()) {
            return event(A::HOLD, m.captured(1), text);
        }
        if (const auto m = g_light.match(text); m.hasMatch()) {
            return event(A::LIGHT, m.captured(1), text);
        }
        if (const auto m = g_stopUsing.match(text); m.hasMatch()) {
            return event(A::REMOVE, m.captured(1), text);
        }
        if (const auto m = g_takeOff.match(text); m.hasMatch()) {
            return placed(A::REMOVE, m.captured(1), m.captured(2), text);
        }
        if (const auto m = g_removeFrom.match(text); m.hasMatch()) {
            // Off the keyring, or out of a corpse: either way into the inventory.
            ItemEvent result = event(A::GET, m.captured(1), text);
            result.container = m.captured(2);
            return result;
        }
        if (const auto m = g_getFrom.match(text); m.hasMatch()) {
            ItemEvent result = event(A::GET, m.captured(1), text);
            result.container = m.captured(2);
            return result;
        }
        if (const auto m = g_get.match(text); m.hasMatch()) {
            return event(A::GET, m.captured(1), text);
        }
        if (const auto m = g_drop.match(text); m.hasMatch()) {
            return event(A::DROP, m.captured(1), text);
        }
        if (const auto m = g_give.match(text); m.hasMatch()) {
            ItemEvent result = event(A::GIVE, m.captured(1), text);
            result.other = withoutLabel(m.captured(2));
            return result;
        }

        // Refusals.
        if (const auto m = g_slotTaken.match(text); m.hasMatch()) {
            ItemEvent result = refused(QStringLiteral("slot-taken"), text);
            result.place = placeOf(m.captured(1), m.captured(2));
            result.slot = placeSlot(result.place);
            return result;
        }
        if (text == QStringLiteral("You are already holding too much.")) {
            return refused(QStringLiteral("hands-full"), text);
        }
        if (text == QStringLiteral("You need two hands free to wield that.")) {
            return refused(QStringLiteral("two-hands"), text);
        }
        if (text == QStringLiteral("You can't carry that many items.")) {
            return refused(QStringLiteral("too-many"), text);
        }
        if (text == QStringLiteral("You can't carry that.")) {
            return refused(QStringLiteral("too-heavy"), text);
        }
        if (text == QStringLiteral("You are not carrying that.")) {
            return refused(QStringLiteral("not-carried"), text);
        }
        if (text == QStringLiteral("You are not holding that item.")) {
            return refused(QStringLiteral("not-worn"), text);
        }
        const auto withItem = [&text](const QString &reason, const QString &item) {
            ItemEvent result = refused(reason, text);
            result.item = item;
            return result;
        };
        if (const auto m = g_tooMany.match(text); m.hasMatch()) {
            return withItem(QStringLiteral("too-many"), m.captured(1));
        }
        if (const auto m = g_tooHeavy.match(text); m.hasMatch()) {
            return withItem(QStringLiteral("too-heavy"), m.captured(1));
        }
        if (const auto m = g_cursed.match(text); m.hasMatch()) {
            return withItem(QStringLiteral("cursed"), m.captured(1));
        }
        if (const auto m = g_cantPut.match(text); m.hasMatch()) {
            ItemEvent result = withItem(QStringLiteral("cannot"), m.captured(1));
            result.container = m.captured(2);
            return result;
        }
        if (const auto m = g_cant.match(text); m.hasMatch()) {
            return withItem(QStringLiteral("cannot"), m.captured(1));
        }
        if (const auto m = g_dontHave.match(text); m.hasMatch()) {
            return withItem(QStringLiteral("not-carried"), m.captured(1));
        }
        if (const auto m = g_notWearing.match(text); m.hasMatch()) {
            return withItem(QStringLiteral("not-worn"), m.captured(1));
        }
        return std::nullopt;
    }

    if (const auto m = g_wontFit.match(text); m.hasMatch()) {
        // "A leather backpack won't fit in a large sack."
        ItemEvent result = refused(QStringLiteral("wont-fit"), text);
        result.item = m.captured(1);
        result.container = m.captured(2);
        return result;
    }
    if (const auto m = g_receive.match(text); m.hasMatch()) {
        ItemEvent result = event(A::RECEIVE, m.captured(2), text);
        result.other = withoutLabel(m.captured(1));
        return result;
    }
    return std::nullopt;
}

std::string_view to_string_view(const ItemBlockKindEnum kind)
{
    switch (kind) {
    case K::EQUIPMENT:
        return "equipment";
    case K::INVENTORY:
        return "inventory";
    case K::CONTAINER:
        return "container";
    }
    return "inventory";
}

std::string_view to_string_view(const ItemActionEnum action)
{
    switch (action) {
    case A::WEAR:
        return "wear";
    case A::REMOVE:
        return "remove";
    case A::WIELD:
        return "wield";
    case A::HOLD:
        return "hold";
    case A::LIGHT:
        return "light";
    case A::GET:
        return "get";
    case A::PUT:
        return "put";
    case A::DROP:
        return "drop";
    case A::GIVE:
        return "give";
    case A::RECEIVE:
        return "receive";
    case A::REFUSED:
        return "refused";
    }
    return "refused";
}

void ItemBlockTracker::receiveCommand(const QString &input)
{
    const auto command = parseContainerCommand(input);
    if (command.has_value() && command->action == ContainerActionEnum::LOOK) {
        m_look = Look{command->word, 0};
    }
}

std::vector<ItemBlock> ItemBlockTracker::receiveLine(const QString &line)
{
    std::vector<ItemBlock> blocks;
    const QString text = cleaned(line);

    if (m_open.has_value()) {
        if (text.isEmpty()) {
            return close();
        }
        if (m_open->overflow) {
            return blocks; // swallowed until the listing ends
        }
        if (accept(text)) {
            return blocks;
        }
        // A sentence after a listing ends it, and is read on its own below.
        blocks = close();
    }
    if (text.isEmpty()) {
        return blocks;
    }

    if (text == QStringLiteral("You are using:")) {
        ItemBlock block;
        block.kind = K::EQUIPMENT;
        open(std::move(block), text);
    } else if (text == QStringLiteral("You are carrying:")) {
        ItemBlock block;
        block.kind = K::INVENTORY;
        open(std::move(block), text);
    } else if (text == QStringLiteral("You attempt to peek at the inventory:")) {
        // After a look at someone: the peek follows their equipment in the same reply.
        ItemBlock block;
        block.kind = K::INVENTORY;
        block.peek = true;
        block.owner = m_lastOwner;
        open(std::move(block), text);
    } else if (const auto using_ = g_using.match(text); using_.hasMatch()) {
        ItemBlock block;
        block.kind = K::EQUIPMENT;
        block.owner = withoutLabel(using_.captured(1));
        m_lastOwner = block.owner;
        open(std::move(block), text);
    } else if (const auto header = g_container.match(text); header.hasMatch()) {
        ItemBlock block;
        block.kind = K::CONTAINER;
        block.keyword = header.captured(1);
        block.where = header.captured(2);
        if (!m_whereOf.contains(block.keyword) && m_whereOf.size() >= PLACES_REMEMBERED) {
            m_whereOf.erase(m_whereOf.begin());
        }
        m_whereOf[block.keyword] = block.where;
        m_look.reset();
        open(std::move(block), text);
    } else if (m_look.has_value() && isClosedReply(text, m_look->word)) {
        // A look into a closed container is answered without a header.
        ItemBlock block;
        block.kind = K::CONTAINER;
        block.keyword = m_look->word;
        if (const auto it = m_whereOf.find(block.keyword); it != m_whereOf.end()) {
            block.where = it->second;
        }
        block.closed = true;
        block.text = text;
        m_look.reset();
        blocks.push_back(std::move(block));
    }
    return blocks;
}

std::vector<ItemBlock> ItemBlockTracker::receivePrompt()
{
    std::vector<ItemBlock> blocks = close();
    m_lastOwner.clear();
    if (m_look.has_value() && ++m_look->prompts > PROMPTS_TO_WAIT) {
        m_look.reset();
    }
    return blocks;
}

void ItemBlockTracker::reset()
{
    m_open.reset();
    m_look.reset();
    m_lastOwner.clear();
    m_whereOf.clear();
}

void ItemBlockTracker::open(ItemBlock block, const QString &header)
{
    m_open = Open{std::move(block), QStringList{header}, false};
}

bool ItemBlockTracker::accept(const QString &text)
{
    Open &open = m_open.value();
    ItemBlock &block = open.block;
    if (isNothing(text)) {
        open.lines << text;
    } else if (block.kind == K::EQUIPMENT) {
        auto item = parseEquipmentLine(text);
        if (!item.has_value()) {
            return false;
        }
        block.items.push_back(std::move(*item));
        open.lines << text;
    } else if (isItemLine(text) && !g_container.match(text).hasMatch()) {
        // The header of a container is lowercase too, and opens a listing of its own.
        block.items.push_back(parseListedItem(text));
        open.lines << text;
    } else {
        return false;
    }
    if (open.lines.size() > MAX_BLOCK_LINES) {
        open.overflow = true;
    }
    return true;
}

std::vector<ItemBlock> ItemBlockTracker::close()
{
    std::vector<ItemBlock> blocks;
    if (!m_open.has_value()) {
        return blocks;
    }
    Open open = std::move(*m_open);
    m_open.reset();
    if (open.overflow) {
        return blocks;
    }
    open.block.text = open.lines.join(QLatin1Char('\n'));
    blocks.push_back(std::move(open.block));
    return blocks;
}
