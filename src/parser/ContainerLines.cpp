// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "ContainerLines.h"

#include "../global/parserutils.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>

#include <QRegularExpression>

namespace {

using A = ContainerActionEnum;
using R = ContainerResultEnum;

/// How long a command waits for its reply, in prompts. MUME answers at once, but a fight round
/// or someone arriving can bring a prompt of its own before the answer. Picking a lock takes a
/// while, and the twiddlers it draws are not prompts.
constexpr int PROMPTS_TO_WAIT = 3;
constexpr int PROMPTS_TO_PICK = 60;
/// Rooms whose containers are remembered, the most recently seen kept.
constexpr size_t ROOMS_REMEMBERED = 256;

struct NODISCARD Verb final
{
    const char *word;
    /// The shortest abbreviation taken for it.
    int shortest;
    ContainerActionEnum action;
};

constexpr std::array VERBS{Verb{"open", 2, A::OPEN},
                           Verb{"close", 2, A::CLOSE},
                           Verb{"unlock", 3, A::UNLOCK},
                           Verb{"lock", 3, A::LOCK},
                           Verb{"pick", 3, A::PICK},
                           Verb{"examine", 3, A::LOOK},
                           Verb{"get", 2, A::GET},
                           Verb{"take", 3, A::GET},
                           Verb{"put", 2, A::PUT}};

constexpr std::array<const char *, 12>
    DIRECTIONS{"n", "s", "e", "w", "u", "d", "north", "south", "east", "west", "up", "down"};

constexpr std::array<const char *, 19> NUMBERS{"two",
                                               "three",
                                               "four",
                                               "five",
                                               "six",
                                               "seven",
                                               "eight",
                                               "nine",
                                               "ten",
                                               "eleven",
                                               "twelve",
                                               "thirteen",
                                               "fourteen",
                                               "fifteen",
                                               "sixteen",
                                               "seventeen",
                                               "eighteen",
                                               "nineteen",
                                               "twenty"};

// The "twiddlers" prompt option draws \|/- while a delayed action such as picking a lock runs,
// and they end up on the front of the line that ends it: "/-\|/The lock finally yields to your
// skill." Backspaces come with them when the terminal is live.
const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};

// "chest (here) : " heads the listing of a container in the room, "backpack (used) :" one the
// player wears and "pouch (carried) :" one they carry. The word is the container's first
// keyword, not the one typed: "exami chest" is answered "stonechest (here) :".
const QRegularExpression g_header{QStringLiteral(R"(^(\S+) \((here|used|carried|worn)\) ?:$)")};
const QRegularExpression g_contains{QStringLiteral(R"(^.+ contains nothing\.$)")};
const QRegularExpression g_isClosed{QStringLiteral(R"(^(?:The|An?) (.+) is closed\.$)")};
const QRegularExpression g_nothingIn{QStringLiteral(R"(^You can't find anything in the .+\.$)")};
const QRegularExpression g_get{QStringLiteral(R"(^You get (.+) from (.+)\.$)")};
const QRegularExpression g_put{QStringLiteral(R"(^You put (.+) in (.+)\.$)")};
const QRegularExpression g_coins{QStringLiteral(R"(^There (?:was|were) (.+)\.$)")};
const QRegularExpression g_coinPart{QStringLiteral(R"(^(\d+) (.+)$)")};
const QRegularExpression g_keyBroke{QStringLiteral(
    R"(^As you unlock the .+?(?:there is a sharp crack, and you are left holding only the handle of the key|the key crumbles in your hands)\.$)")};
const QRegularExpression g_picking{QStringLiteral(
    R"(^(?:Using your lockpicks, you try|You begin to try) to (?:pick|enable) the lock\.\.\.$)")};
const QRegularExpression g_notFound{QStringLiteral(
    R"(^(?:You don't see (?:any|an?) \S+(?: (?:here|there|to get things from))?|You can't find an? \S+|You do not see that here)\.$)")};
const QRegularExpression g_condition{QStringLiteral(R"(\s*\([^()]*\)$)")};

NODISCARD QString cleaned(const QString &line)
{
    QString text = line;
    ParserUtils::removeAnsiMarksInPlace(text);
    text = text.simplified();
    text.remove(g_twiddlers);
    return text;
}

NODISCARD bool isDirection(const QString &word)
{
    return std::any_of(DIRECTIONS.begin(), DIRECTIONS.end(), [&word](const char *const d) {
        return word == QLatin1String(d);
    });
}

/// A set of actions, one bit each.
using Actions = uint16_t;

NODISCARD constexpr Actions bit(const ContainerActionEnum action)
{
    return static_cast<Actions>(1u << static_cast<unsigned>(action));
}

NODISCARD constexpr Actions actions(const std::initializer_list<ContainerActionEnum> list)
{
    Actions result = 0;
    for (const ContainerActionEnum action : list) {
        result = static_cast<Actions>(result | bit(action));
    }
    return result;
}

constexpr Actions ANY_ACTION = actions(
    {A::OPEN, A::CLOSE, A::UNLOCK, A::LOCK, A::PICK, A::LOOK, A::GET, A::PUT});

/// The first letter of an item in a listing is lowercase ("a gold ring", "two amethysts", "the
/// scalp of Horiam"); lines that break into a listing are sentences, and start with a capital.
NODISCARD bool isItemLine(const QString &line)
{
    if (line.isEmpty()) {
        return false;
    }
    const QChar first = line.front();
    return first.isLower() || first.isDigit() || line == QStringLiteral("Something.");
}

NODISCARD std::vector<ContainerItem> coinsOf(const QString &body, const QString &text)
{
    // "There was 3 gold coins." or "There was 5 gold coins, 12 silver pennies, and 3 copper
    // pennies." after "You get a pile of coins from ...".
    std::vector<ContainerItem> items;
    QString rest = body;
    rest.replace(QStringLiteral(", and "), QStringLiteral(", "));
    rest.replace(QStringLiteral(" and "), QStringLiteral(", "));
    for (const QString &part : rest.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
        const auto match = g_coinPart.match(part.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        items.push_back(
            ContainerItem{match.captured(2), std::max(1, match.captured(1).toInt()), text});
    }
    return items;
}

} // namespace

std::optional<ContainerCommand> parseContainerCommand(const QString &input)
{
    QStringList words = input.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 2) {
        return std::nullopt;
    }
    const QString verb = words.takeFirst();

    ContainerCommand command;
    if (QStringLiteral("look").startsWith(verb)
        && (words.front() == QStringLiteral("in") || words.front() == QStringLiteral("into"))) {
        words.removeFirst();
        command.action = A::LOOK;
    } else {
        const auto it = std::find_if(VERBS.begin(), VERBS.end(), [&verb](const Verb &v) {
            return verb.size() >= v.shortest && QLatin1String(v.word).startsWith(verb);
        });
        if (it == VERBS.end()) {
            return std::nullopt;
        }
        command.action = it->action;
    }
    if (words.isEmpty()) {
        return std::nullopt;
    }

    switch (command.action) {
    case A::GET:
    case A::PUT: {
        // "get all chest", "get coins from corpse", "put ring in pouch".
        words.removeAll(QStringLiteral("from"));
        words.removeAll(QStringLiteral("in"));
        words.removeAll(QStringLiteral("into"));
        if (words.size() < 2) {
            return std::nullopt; // from the ground, or onto it
        }
        command.item = words.front();
        command.target = words.back();
        break;
    }
    case A::OPEN:
    case A::CLOSE:
    case A::UNLOCK:
    case A::LOCK:
    case A::PICK:
        command.target = words.front();
        // "open door north" and "pick exit w" name a door by its side.
        if (words.size() >= 2 && isDirection(words.at(1))) {
            command.container = false;
        }
        break;
    case A::LOOK:
        command.target = words.front();
        break;
    }

    command.word = command.target;
    const qsizetype dot = command.target.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        bool ok = false;
        const int ordinal = command.target.left(dot).toInt(&ok);
        if (ok && ordinal > 0) {
            command.ordinal = ordinal;
            command.word = command.target.mid(dot + 1);
        }
    }
    if (!namesContainer(command.word)) {
        command.container = false;
    }
    // A door keeps its place in line, so that its reply is not taken for a chest's; a get or put
    // that names no container is about the ground or the player's own things.
    if (!command.container && (command.action == A::GET || command.action == A::PUT)) {
        return std::nullopt;
    }
    return command;
}

ContainerItem parseContainerItem(const QString &raw)
{
    ContainerItem item;
    item.text = raw.trimmed();
    QString name = item.text;
    // "an azure scroll; it glows blue", "the black sword (flawless); it glows blue".
    if (const qsizetype semicolon = name.indexOf(QStringLiteral("; ")); semicolon > 0) {
        name = name.left(semicolon);
    }
    while (true) {
        const auto match = g_condition.match(name);
        if (!match.hasMatch() || match.capturedStart() == 0) {
            break;
        }
        name = name.left(match.capturedStart());
    }
    if (name.endsWith(QLatin1Char('.'))) {
        name.chop(1); // "Something."
    }
    const qsizetype space = name.indexOf(QLatin1Char(' '));
    if (space > 0) {
        const QString first = name.left(space);
        for (size_t i = 0; i < NUMBERS.size(); ++i) {
            if (first == QLatin1String(NUMBERS.at(i))) {
                item.count = static_cast<int>(i) + 2;
                name = name.mid(space + 1);
                break;
            }
        }
        bool ok = false;
        const int number = first.toInt(&ok);
        if (ok && number > 0) {
            item.count = number;
            name = name.mid(space + 1);
        }
    }
    item.name = name;
    return item;
}

std::string_view to_string_view(const ContainerActionEnum action)
{
    switch (action) {
    case A::OPEN:
        return "open";
    case A::CLOSE:
        return "close";
    case A::UNLOCK:
        return "unlock";
    case A::LOCK:
        return "lock";
    case A::PICK:
        return "pick";
    case A::LOOK:
        return "look";
    case A::GET:
        return "get";
    case A::PUT:
        return "put";
    }
    return "open";
}

std::string_view to_string_view(const ContainerResultEnum result)
{
    switch (result) {
    case R::OPENED:
        return "opened";
    case R::CLOSED:
        return "closed";
    case R::ALREADY_OPEN:
        return "already-open";
    case R::ALREADY_CLOSED:
        return "already-closed";
    case R::LOCKED:
        return "locked";
    case R::UNLOCKED:
        return "unlocked";
    case R::NO_KEY:
        return "no-key";
    case R::KEY_BROKE:
        return "key-broke";
    case R::PICKING:
        return "picking";
    case R::PICKED:
        return "picked";
    case R::PICK_FAILED:
        return "pick-failed";
    case R::PICK_STOPPED:
        return "pick-stopped";
    case R::PICKPROOF:
        return "pickproof";
    case R::EMPTY:
        return "empty";
    case R::CONTENTS:
        return "contents";
    case R::NOT_FOUND:
        return "not-found";
    case R::CANNOT:
        return "cannot";
    }
    return "cannot";
}

void ContainerTracker::receiveCommand(const QString &input)
{
    if (auto command = parseContainerCommand(input)) {
        m_pending.push_back(Pending{std::move(*command), 0, false});
    }
}

template<typename Accepts>
std::optional<ContainerTracker::Pending> ContainerTracker::take(Accepts &&acceptsIt, const bool keep)
{
    const auto it = std::find_if(m_pending.begin(),
                                 m_pending.end(),
                                 [&acceptsIt](const Pending &p) { return acceptsIt(p); });
    if (it == m_pending.end()) {
        return std::nullopt;
    }
    if (keep) {
        it->picking = true;
    }
    const Pending found = *it;
    const auto position = std::distance(m_pending.begin(), it);
    if (!keep) {
        m_pending.erase(it);
    }
    // The commands before it had their replies go by unrecognised. A lock being picked is the
    // exception: it goes on while other commands are answered.
    const auto before = m_pending.begin() + position;
    const auto firstKept = std::remove_if(m_pending.begin(), before, [](const Pending &p) {
        return !p.picking;
    });
    m_pending.erase(firstKept, before);
    return found;
}

std::vector<ContainerEvent> ContainerTracker::receiveLine(const QString &line, const int64_t now)
{
    std::vector<ContainerEvent> events;
    const QString text = cleaned(line);
    const auto flushInto = [this, &events, now]() {
        for (ContainerEvent &event : flush(now)) {
            events.push_back(std::move(event));
        }
    };

    if (m_gathering.has_value() && m_gathering->listing) {
        if (text == QStringLiteral("Nothing.")) {
            m_gathering->nothing = true;
            m_gathering->lines << text;
            return events;
        }
        if (isItemLine(text)) {
            m_gathering->items.push_back(parseContainerItem(text));
            m_gathering->lines << text;
            return events;
        }
        // A blank line or a sentence ends the listing; the sentence is read on its own below.
        flushInto();
    }
    if (text.isEmpty()) {
        return events;
    }

    // Items taken out, or put in, one line each.
    if (const auto get = g_get.match(text); get.hasMatch()) {
        if (!(m_gathering.has_value() && m_gathering->command.action == A::GET)) {
            flushInto();
            const auto found = take([](const Pending &p) { return p.command.action == A::GET; });
            if (!found.has_value()) {
                return events;
            }
            m_gathering = Gathering{found->command, R::CONTENTS, {}, {}, false, false};
        }
        m_gathering->items.push_back(parseContainerItem(get.captured(1)));
        m_gathering->lines << text;
        return events;
    }
    if (const auto coins = g_coins.match(text); coins.hasMatch()) {
        if (m_gathering.has_value() && m_gathering->command.action == A::GET) {
            for (ContainerItem &item : coinsOf(coins.captured(1), text)) {
                m_gathering->items.push_back(std::move(item));
            }
            m_gathering->lines << text;
        }
        return events;
    }
    if (const auto put = g_put.match(text); put.hasMatch()) {
        if (!(m_gathering.has_value() && m_gathering->command.action == A::PUT)) {
            flushInto();
            const auto found = take([](const Pending &p) { return p.command.action == A::PUT; });
            if (!found.has_value()) {
                return events;
            }
            m_gathering = Gathering{found->command, R::CONTENTS, {}, {}, false, false};
        }
        m_gathering->items.push_back(parseContainerItem(put.captured(1)));
        m_gathering->lines << text;
        return events;
    }

    // A listing's header.
    const bool inside = text == QStringLiteral("When you look inside, you see:");
    if (const auto header = g_header.match(text); header.hasMatch() || inside) {
        flushInto();
        const bool here = inside || header.captured(2) == QStringLiteral("here");
        std::optional<Pending> found = take(
            [](const Pending &p) { return p.command.action == A::LOOK; });
        ContainerCommand command;
        if (found.has_value()) {
            command = found->command;
        } else {
            // Looked into without MMapper seeing the command: the header still names it.
            command.action = A::LOOK;
            command.target = command.word = header.captured(1);
        }
        if (!here) {
            command.container = false; // the player's own; not the room's
        }
        m_gathering = Gathering{command, R::CONTENTS, {}, {text}, true, false};
        return events;
    }

    // Replies of one line.
    struct NODISCARD Reply final
    {
        std::optional<ContainerResultEnum> result;
        Actions accepted = 0;
        bool keep = false;
    };
    const auto reply = [&text]() -> Reply {
        if (text == QStringLiteral("Ok.")) {
            return Reply{std::nullopt, actions({A::OPEN, A::CLOSE})};
        }
        if (text == QStringLiteral("*click*")) {
            return Reply{std::nullopt, actions({A::UNLOCK, A::LOCK})};
        }
        if (text == QStringLiteral("It's already open!")
            || text == QStringLiteral("But it's already open!")
            || text == QStringLiteral("It isn't even closed!")
            || text == QStringLiteral("But it's open!")
            || text == QStringLiteral("You have to close it first, I'm afraid.")) {
            return Reply{R::ALREADY_OPEN, actions({A::OPEN, A::UNLOCK, A::LOCK, A::PICK})};
        }
        if (text == QStringLiteral("It's already closed!")
            || text == QStringLiteral("But it's already closed!")) {
            return Reply{R::ALREADY_CLOSED, actions({A::CLOSE})};
        }
        if (text == QStringLiteral("It seems to be locked.")) {
            return Reply{R::LOCKED, actions({A::OPEN})};
        }
        if (text == QStringLiteral("It's already locked!")) {
            return Reply{R::LOCKED, actions({A::LOCK})};
        }
        if (text == QStringLiteral("It's already unlocked, it seems.")) {
            return Reply{R::UNLOCKED, actions({A::UNLOCK})};
        }
        if (text == QStringLiteral("You don't have the proper key.")
            || text == QStringLiteral("You do not have the proper key for that.")) {
            return Reply{R::NO_KEY, actions({A::UNLOCK, A::LOCK})};
        }
        if (text == QStringLiteral("The fragile key breaks in your hands.")
            || g_keyBroke.match(text).hasMatch()) {
            return Reply{R::KEY_BROKE, actions({A::UNLOCK})};
        }
        if (g_picking.match(text).hasMatch()) {
            return Reply{R::PICKING, actions({A::PICK}), true};
        }
        if (text == QStringLiteral("The lock finally yields to your skill.")) {
            return Reply{R::PICKED, actions({A::PICK})};
        }
        // Seen after "pick", with the same twiddlers as picking one open; taken to mean the lock
        // was picked shut. Not confirmed against MUME's help.
        if (text == QStringLiteral("You manage to enable the lock.")) {
            return Reply{R::LOCKED, actions({A::PICK})};
        }
        if (text == QStringLiteral("You failed to pick the lock.")) {
            return Reply{R::PICK_FAILED, actions({A::PICK})};
        }
        if (text == QStringLiteral("You seem to be unable to pick this lock.")) {
            return Reply{R::PICKPROOF, actions({A::PICK})};
        }
        if (text == QStringLiteral("You stop trying to pick the lock.")
            || text == QStringLiteral("Aye! You cannot concentrate any more...")) {
            return Reply{R::PICK_STOPPED, actions({A::PICK})};
        }
        if (text == QStringLiteral("That's not a container.")
            || text == QStringLiteral("That's impossible, I'm afraid.")
            || text == QStringLiteral("You cannot.") || text == QStringLiteral("That's absurd.")
            || text == QStringLiteral("You can't seem to spot any lock to pick.")
            || text == QStringLiteral("You can't seem to spot any keyholes.")) {
            return Reply{R::CANNOT, ANY_ACTION};
        }
        if (g_notFound.match(text).hasMatch()) {
            return Reply{R::NOT_FOUND, ANY_ACTION};
        }
        if (g_nothingIn.match(text).hasMatch()) {
            return Reply{R::EMPTY, actions({A::GET})};
        }
        if (g_contains.match(text).hasMatch()) {
            return Reply{R::EMPTY, actions({A::LOOK})};
        }
        if (text == QStringLiteral("It is closed.")) {
            return Reply{R::CLOSED, actions({A::LOOK, A::GET, A::PUT})};
        }
        if (const auto closed = g_isClosed.match(text);
            closed.hasMatch() && !containerKeyword(closed.captured(1)).isEmpty()) {
            // "A large cabinet is closed.", in answer to "get all cabinet".
            return Reply{R::CLOSED, actions({A::LOOK, A::GET, A::PUT})};
        }
        return Reply{};
    }();
    if (reply.accepted == 0) {
        return events;
    }

    flushInto();
    const bool picking = reply.result == R::PICK_STOPPED && text.startsWith(QStringLiteral("Aye!"));
    const auto found = take(
        [&reply, picking](const Pending &p) {
            // Losing concentration only stops a lock already being picked.
            return (reply.accepted & bit(p.command.action)) != 0 && (!picking || p.picking);
        },
        reply.keep);
    std::optional<Pending> answered = found;
    if (!answered.has_value() && reply.result == R::KEY_BROKE && m_lastUnlock.has_value()) {
        // "*click*" and then "As you unlock the stonechest there is a sharp crack, ...": both
        // answer the same unlock.
        answered = Pending{*m_lastUnlock, 0, false};
    }
    if (!answered.has_value()) {
        return events;
    }
    ContainerResultEnum result = R::CANNOT;
    if (reply.result.has_value()) {
        result = *reply.result;
    } else if (text == QStringLiteral("Ok.")) {
        result = answered->command.action == A::OPEN ? R::OPENED : R::CLOSED;
    } else {
        result = answered->command.action == A::UNLOCK ? R::UNLOCKED : R::LOCKED;
    }
    if (result == R::UNLOCKED && answered->command.action == A::UNLOCK) {
        m_lastUnlock = answered->command;
    } else if (result == R::KEY_BROKE) {
        m_lastUnlock.reset();
    }
    if (auto event = finish(answered->command, result, {}, text, now)) {
        events.push_back(std::move(*event));
    }
    return events;
}

std::vector<ContainerEvent> ContainerTracker::receivePrompt(const int64_t now)
{
    std::vector<ContainerEvent> events = flush(now);
    m_lastUnlock.reset();
    for (Pending &p : m_pending) {
        ++p.prompts;
    }
    const auto expired = std::remove_if(m_pending.begin(), m_pending.end(), [](const Pending &p) {
        return p.prompts > (p.picking ? PROMPTS_TO_PICK : PROMPTS_TO_WAIT);
    });
    m_pending.erase(expired, m_pending.end());
    return events;
}

std::vector<ContainerEvent> ContainerTracker::flush(const int64_t now)
{
    std::vector<ContainerEvent> events;
    if (!m_gathering.has_value()) {
        return events;
    }
    Gathering gathering = std::move(*m_gathering);
    m_gathering.reset();
    ContainerResultEnum result = gathering.result;
    if (gathering.listing && (gathering.nothing || gathering.items.empty())) {
        result = R::EMPTY;
    }
    if (auto event = finish(gathering.command,
                            result,
                            std::move(gathering.items),
                            gathering.lines.join(QLatin1Char('\n')),
                            now)) {
        events.push_back(std::move(*event));
    }
    return events;
}

std::optional<ContainerEvent> ContainerTracker::finish(const ContainerCommand &command,
                                                       const ContainerResultEnum result,
                                                       std::vector<ContainerItem> items,
                                                       const QString &text,
                                                       const int64_t now)
{
    if (!command.container) {
        return std::nullopt;
    }
    ContainerEvent event;
    event.command = command;
    event.result = result;
    event.items = std::move(items);
    event.text = text;
    event.index = resolve(command);
    // "get" and "put" reach the player's own bags first; without an object of that name in the
    // room, one of those is what they reached. The other verbs are about the room.
    const bool listedHere = command.action == A::LOOK;
    if (event.index < 0 && !listedHere && (command.action == A::GET || command.action == A::PUT)) {
        return std::nullopt;
    }
    apply(event.index, command.action, result, !event.items.empty(), now);
    if (event.index >= 0 && result == R::KEY_BROKE && text.startsWith(QStringLiteral("As you"))) {
        apply(event.index, command.action, R::UNLOCKED, false, now);
    }
    return event;
}

int ContainerTracker::resolve(const ContainerCommand &command) const
{
    int seen = 0;
    for (const RoomObject &object : m_current.objects) {
        if (!object.container || !namesContainer(command.word, object.keyword)) {
            continue;
        }
        seen += object.count;
        if (seen >= command.ordinal) {
            return object.index;
        }
    }
    return -1;
}

QString ContainerTracker::objectKey(const int index) const
{
    // The line, and which of the identical lines it is: two corpses of rabbits are told apart
    // by their order.
    const auto &objects = m_current.objects;
    if (index < 0 || static_cast<size_t>(index) >= objects.size()) {
        return QString{};
    }
    int before = 0;
    for (int i = 0; i < index; ++i) {
        if (objects.at(static_cast<size_t>(i)).line == objects.at(static_cast<size_t>(index)).line) {
            ++before;
        }
    }
    return QStringLiteral("%1#%2").arg(objects.at(static_cast<size_t>(index)).line).arg(before);
}

void ContainerTracker::apply(const int index,
                             const ContainerActionEnum action,
                             const ContainerResultEnum result,
                             const bool hadItems,
                             const int64_t now)
{
    if (index < 0) {
        return;
    }
    if (!m_rooms.contains(m_current.roomKey)) {
        m_roomOrder.push_back(m_current.roomKey);
        if (m_roomOrder.size() > ROOMS_REMEMBERED) {
            m_rooms.erase(m_roomOrder.front());
            m_roomOrder.pop_front();
        }
    }
    ContainerState &state = m_rooms[m_current.roomKey][objectKey(index)];
    switch (result) {
    case R::OPENED:
    case R::ALREADY_OPEN:
        state.open = true;
        state.locked = false;
        break;
    case R::CLOSED:
    case R::ALREADY_CLOSED:
        state.open = false;
        break;
    case R::LOCKED:
        state.open = false;
        state.locked = true;
        break;
    case R::UNLOCKED:
    case R::PICKED:
        state.locked = false;
        break;
    case R::NO_KEY:
        // MUME checks the lock before the key, so for "unlock" it is locked.
        if (action == A::UNLOCK) {
            state.open = false;
            state.locked = true;
        }
        break;
    case R::PICK_FAILED:
        state.locked = true;
        break;
    case R::PICKPROOF:
        state.pickproof = true;
        state.locked = true;
        break;
    case R::EMPTY:
        state.open = true;
        state.empty = true;
        break;
    case R::CONTENTS:
        state.open = true;
        if (action == A::LOOK) {
            state.empty = !hadItems;
        } else if (action == A::PUT) {
            state.empty = false;
        }
        break;
    case R::KEY_BROKE:
    case R::PICKING:
    case R::PICK_STOPPED:
    case R::NOT_FOUND:
    case R::CANNOT:
        return;
    }
    state.known = now;
    m_changed = true;
}

void ContainerTracker::decorate(RoomContentsSnapshot &contents)
{
    m_current = contents;
    for (RoomObject &object : m_current.objects) {
        object.state = ContainerState{};
    }
    contents = current();
}

RoomContentsSnapshot ContainerTracker::current() const
{
    RoomContentsSnapshot result = m_current;
    const auto room = m_rooms.find(result.roomKey);
    if (room == m_rooms.end()) {
        return result;
    }
    for (RoomObject &object : result.objects) {
        if (!object.container) {
            continue;
        }
        const auto state = room->second.find(objectKey(object.index));
        if (state != room->second.end()) {
            object.state = state->second;
        }
    }
    return result;
}

bool ContainerTracker::takeChanged()
{
    return std::exchange(m_changed, false);
}

void ContainerTracker::reset()
{
    m_pending.clear();
    m_gathering.reset();
    m_lastUnlock.reset();
    m_rooms.clear();
    m_roomOrder.clear();
    m_current = RoomContentsSnapshot{};
    m_changed = false;
}
