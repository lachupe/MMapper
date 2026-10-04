// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "RoomDoors.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

// Sources: the powwow logs under /home/aza/data/powwow/logs/archives (2005-2006), cited as
// file:line with that directory left off; counts are whole lines in all 364 logs. MUME's help
// pages `exits` and `gmcp_room` as read at https://mume.org/help/ on 2026-10-01.

namespace {

using S = DoorStateEnum;
using K = DoorLineEnum;
using A = ContainerActionEnum;
using R = ContainerResultEnum;

constexpr std::array<const char *, 6> SIDES{"n", "e", "s", "w", "u", "d"};
constexpr std::array<const char *, 6> SIDE_WORDS{"north", "east", "south", "west", "up", "down"};

/// "n" for "n" and "north"; empty for anything else.
NODISCARD QString sideOf(const QString &word)
{
    for (size_t i = 0; i < SIDES.size(); ++i) {
        if (word == QLatin1String(SIDES.at(i)) || word == QLatin1String(SIDE_WORDS.at(i))) {
            return QString::fromLatin1(SIDES.at(i));
        }
    }
    return QString{};
}

NODISCARD int sideOrder(const QString &dir)
{
    for (size_t i = 0; i < SIDES.size(); ++i) {
        if (dir == QLatin1String(SIDES.at(i))) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(SIDES.size());
}

/// A door's keyword as a line gives it: one word, lowercase ("irondoor", "secrethoard"). The
/// word "exit" names any door and is no name.
NODISCARD QString doorName(const QString &word)
{
    const QString name = word.trimmed().toLower();
    return name == QStringLiteral("exit") ? QString{} : name;
}

// A door's keyword in a line: one lowercase word.
#define DOOR R"((?<d>[a-z][a-z'-]*))"

struct NODISCARD LinePattern final
{
    QRegularExpression pattern;
    DoorLineEnum kind;
};

// One row per sentence, matched whole. The order matters only for the two blurs.
const LinePattern g_doorLines[] = {
    // A move refused by a shut door. 2,205 lines; "The woodendoor seems to be closed."
    // log-2005.08.31-14.00.43.txt:2344, "The gate seems to be closed." log-2006.01.06-01.59.16.txt:54137.
    // "seem" for a plural keyword is MMapper's own (AbstractParser-Actions.cpp), in no log.
    {QRegularExpression{QStringLiteral("^The " DOOR R"( seems? to be closed\.$)")},
     K::SEEMS_CLOSED},
    // A look at a side with a door. "The hangingbranch is closed." after `l s`
    // log-2006.01.04-22.01.05.txt:20040-20043, "The deadbark is open." after `l w`
    // log-2006.02.01-22.13.02.txt:130136-130138 (94 and 107 lines of the two shapes).
    {QRegularExpression{QStringLiteral("^The " DOOR R"( (?:is|are) closed\.$)")}, K::LOOKED_CLOSED},
    {QRegularExpression{QStringLiteral("^The " DOOR R"( (?:is|are) open\.$)")}, K::LOOKED_OPEN},
    // Somebody on the far side. "The door is opened from the other side."
    // log-2005.08.31-14.00.43.txt:1607 (3,874); "The sturdydoor is closed from the other side."
    // log-2005.12.08-02.16.02.txt:33830 (10).
    {QRegularExpression{QStringLiteral("^The " DOOR R"( (?:is|are) opened from the other side\.$)")},
     K::OPENED},
    {QRegularExpression{QStringLiteral("^The " DOOR R"( (?:is|are) closed from the other side\.$)")},
     K::CLOSED},
    // A door that shuts by itself. "The irondoor closes quietly."
    // log-2005.08.31-14.00.43.txt:1761 (2,069).
    {QRegularExpression{QStringLiteral("^The " DOOR R"( closes quietly\.$)")}, K::CLOSED},
    // Somebody in the room. "A brown-skinned orc unlocks the irondoor." / "... opens the
    // irondoor." log-2005.08.31-14.00.43.txt:1746-1747, "Lungorthin closes the fence." :8864,
    // "Gumak the Uruk-hai locks the stonedoor." log-2005.09.05-19.23.16.txt:11151 (6,199 opens,
    // 6,196 closes, 734 unlocks, 554 locks). The same words are used of a chest: see NotDoor.
    {QRegularExpression{QStringLiteral(R"(^(?<a>.+?) opens the )" DOOR R"(\.$)")}, K::OPENED},
    {QRegularExpression{QStringLiteral(R"(^(?<a>.+?) closes the )" DOOR R"(\.$)")}, K::CLOSED},
    {QRegularExpression{QStringLiteral(R"(^(?<a>.+?) unlocks the )" DOOR R"(\.$)")}, K::UNLOCKED},
    {QRegularExpression{QStringLiteral(R"(^(?<a>.+?) locks the )" DOOR R"(\.$)")}, K::LOCKED},
    // Bashed in. "Zmej tries to bash the irondoor." / "The irondoor gave away under the
    // pressure." log-2005.09.05-19.23.16.txt:17299-17300; the player's own `bash exit n`
    // log-2005.11.17-17.17.33.txt:117926-117927 (246). The exits line then lists the side
    // without a door mark (10 of the 30 read).
    {QRegularExpression{QStringLiteral("^The " DOOR R"( gave away under the pressure\.$)")},
     K::GAVE_WAY},
    // The break door spell: "Kazadoe (K) utters the words 'bfzat eaaf'" / "The gate is filled
    // with a bright light." and the player walks through, log-2005.12.19-20.51.29.txt:165354-165359;
    // "The towerdoor is filled with a bright light." log-2005.10.08-15.55.31.txt:41403 (78).
    // After it `close` answers "That's absurd." (log-2006.02.09-00.15.52.txt:46226-46232).
    {QRegularExpression{QStringLiteral("^The " DOOR R"( (?:is|are) filled with a bright light\.$)")},
     K::LIT},
    // The block door spell, which names the side when cast at "exit <side>": "*Sator the Rohir
    // Man* utters the words 'block door'" / "The exit east seems to blur for a while." and then
    // `open exit e` / "That's impossible, I'm afraid." log-2006.04.19-23.32.31.txt:88305-88310;
    // "The exit west seems to blur for a while." log-2005.09.05-19.23.16.txt:23805 (392).
    {QRegularExpression{QStringLiteral(
         R"(^The exit (?<side>north|east|south|west|up|down) seems to blur for a while\.$)")},
     K::BLURRED},
    // The same with the door's name, which is also what a rock does:
    // "The door seems to blur for a while." log-2005.10.04-03.05.23.txt:46527 (96).
    {QRegularExpression{QStringLiteral("^The " DOOR R"( seems? to blur for a while\.$)")},
     K::BLURRED},
    // His alias `get rock backpack;use rock ${door};get rock` (/home/aza/data/powwow/m:84-85):
    // "As you throw a twisted rock fragment at the door, there is an explosion."
    // log-2005.10.04-03.05.23.txt:46526; somebody else's, "As Kazadoe (K) throws a twisted rock
    // fragment at the gate, there is an explosion." log-2006.01.04-03.42.35.txt:11765.
    {QRegularExpression{QStringLiteral(
         R"(^As (?:you throw|(?<a>.+?) throws) .+? at the )" DOOR R"(, there is an explosion\.$)")},
     K::EXPLOSION},
    // A break door that failed on a blocked door. "The will blocking the trapdoor resisted your
    // spell." log-2005.10.27-01.19.35.txt:7114 (9).
    {QRegularExpression{QStringLiteral("^The will blocking the " DOOR R"( resisted your spell\.$)")},
     K::RESISTED},
    // The Unqalome doors. "The door slams shut, and a thick layer of ice covers it."
    // log-2005.11.02-01.27.58.txt:23464 (92); "A thick layer of ice covers the door."
    // log-2005.09.05-19.23.16.txt:67137 (27).
    {QRegularExpression{QStringLiteral(
         "^The " DOOR R"( slams shut, and a thick layer of ice covers it\.$)")},
     K::ICED},
    {QRegularExpression{QStringLiteral("^A thick layer of ice covers the " DOOR R"(\.$)")}, K::ICED},
    // `open exit w` at such a door: log-2005.09.05-19.23.16.txt:66914 (8).
    {QRegularExpression{
         QStringLiteral(R"(^The ice layer is too thick and prevents you from reaching it\.$)")},
     K::ICE_THICK},
    // `cast normal 'burning hands' stonedoor`: "You aim your spell at the ice layer." / "Some
    // of the ice melts down." log-2005.09.21-01.39.48.txt:26019-26020 (175 and 270), and the
    // last cast, "The ice layer is completely molten!" :26044 (155).
    {QRegularExpression{QStringLiteral(R"(^You aim your spell at the ice layer\.$)")}, K::ICE_AIMED},
    {QRegularExpression{QStringLiteral(R"(^Some of the ice melts down\.$)")}, K::ICE_MELTS},
    {QRegularExpression{QStringLiteral(R"(^The ice layer is completely molten!$)")}, K::ICE_MOLTEN},
};

#undef DOOR

// The twiddlers a delayed command draws end up on the front of the line that ends it:
// "\|You aim your spell at the ice layer." (log-2005.09.21-01.39.48.txt:26019).
const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};

/// Whether a state says more than "closed" does, so that a plain closed must not replace it.
NODISCARD bool keepsOverClosed(const DoorStateEnum state)
{
    switch (state) {
    case S::LOCKED:
    case S::ICED:
    case S::BLOCKED:
    case S::MOLTEN:
        return true;
    case S::OPEN:
    case S::CLOSED:
    case S::BROKEN:
    case S::UNKNOWN:
        return false;
    }
    return false;
}

} // namespace

std::string_view to_string_view(const DoorStateEnum state)
{
    switch (state) {
    case S::OPEN:
        return "open";
    case S::CLOSED:
        return "closed";
    case S::LOCKED:
        return "locked";
    case S::ICED:
        return "iced";
    case S::BLOCKED:
        return "blocked";
    case S::BROKEN:
        return "broken";
    case S::MOLTEN:
        return "molten";
    case S::UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

std::vector<DoorExit> parseExitsObject(const QJsonObject &exits)
{
    std::vector<DoorExit> result;
    for (const char *const side : SIDES) {
        const QString dir = QString::fromLatin1(side);
        const auto it = exits.constFind(dir);
        if (it == exits.constEnd()) {
            continue;
        }
        DoorExit exit;
        exit.dir = dir;
        if (!it->isObject()) {
            // help gmcp_room: "If an exit is removed, its value becomes false".
            exit.exists = false;
            result.push_back(std::move(exit));
            continue;
        }
        const QJsonObject obj = it->toObject();
        exit.name = doorName(obj.value(QStringLiteral("name")).toString());
        exit.door = obj.contains(QStringLiteral("name"));
        for (const QJsonValue &flag : obj.value(QStringLiteral("flags")).toArray()) {
            const QString word = flag.toString();
            if (word == QStringLiteral("closed")) {
                exit.closed = exit.door = true;
            } else if (word == QStringLiteral("broken")) {
                exit.broken = exit.door = true;
            }
        }
        result.push_back(std::move(exit));
    }
    return result;
}

RoomExitsInfo parseRoomInfoDoors(const QJsonObject &roomInfo)
{
    RoomExitsInfo info;
    if (const QJsonValue id = roomInfo.value(QStringLiteral("id")); id.isDouble()) {
        info.room = id.toInteger();
    }
    info.exits = parseExitsObject(roomInfo.value(QStringLiteral("exits")).toObject());
    return info;
}

std::vector<DoorExit> parseExitsLine(const QString &line)
{
    // "Exits: (east), south." log-2005.11.02-01.27.58.txt:23494, "Exits: [east], west."
    // log-2006.01.06-01.59.16.txt:54134, "Exits: east, south, =west=, =(up)=."
    // log-2005.12.08-02.16.02.txt:26545, and without commas, "Exits:  north east south #west#"
    // log-2006.08.05-21.17.51.txt:2169. help exits: [] closed door, () open door, ## broken
    // door, {} portal, == road, -- trail, ~~ swim, ^^ outdoors, ** sunlit; "Flags from the two
    // categories can be combined; e.g., =#up#= is a road leading through a broken door."
    std::vector<DoorExit> result;
    // The <exits> element may hold more than the line: the first line that is one.
    QString text;
    for (const QString &candidate : line.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (candidate.trimmed().startsWith(QStringLiteral("Exits:"))) {
            text = candidate.trimmed().mid(6);
            break;
        }
    }
    if (text.isEmpty()) {
        return result;
    }
    static const QRegularExpression separators{QStringLiteral(R"([,\s]+)")};
    for (QString token : text.split(separators, Qt::SkipEmptyParts)) {
        if (token.endsWith(QLatin1Char('.'))) {
            token.chop(1);
        }
        static const QRegularExpression word{QStringLiteral(R"([a-z]+)")};
        const auto m = word.match(token);
        if (!m.hasMatch()) {
            continue;
        }
        const QString dir = m.captured(0).size() > 1 ? sideOf(m.captured(0)) : QString{};
        if (dir.isEmpty()) {
            continue;
        }
        const QString before = token.left(m.capturedStart(0));
        const QString after = token.mid(m.capturedEnd(0));
        DoorExit exit;
        exit.dir = dir;
        if (before.contains(QLatin1Char('[')) && after.contains(QLatin1Char(']'))) {
            exit.door = exit.closed = true;
        } else if (before.contains(QLatin1Char('#')) && after.contains(QLatin1Char('#'))) {
            exit.door = exit.broken = true;
        } else if (before.contains(QLatin1Char('(')) && after.contains(QLatin1Char(')'))) {
            exit.door = true;
        } else {
            continue;
        }
        result.push_back(std::move(exit));
    }
    return result;
}

std::optional<DoorLine> parseDoorLine(const QString &raw)
{
    QString line = raw.trimmed();
    line.remove(g_twiddlers);
    for (const LinePattern &row : g_doorLines) {
        const auto m = row.pattern.match(line);
        if (!m.hasMatch()) {
            continue;
        }
        DoorLine result;
        result.kind = row.kind;
        if (m.hasCaptured(u"d")) {
            result.name = doorName(m.captured(u"d"));
        }
        if (m.hasCaptured(u"side")) {
            result.dir = sideOf(m.captured(u"side"));
        }
        if (m.hasCaptured(u"a")) {
            result.actor = m.captured(u"a");
        }
        if (line.endsWith(QStringLiteral(" from the other side."))) {
            result.side = QStringLiteral("other side");
        } else if (line.endsWith(QStringLiteral(" closes quietly."))) {
            result.side = QStringLiteral("itself");
        }
        return result;
    }
    return std::nullopt;
}

std::optional<DoorAim> parseDoorAim(const QString &input)
{
    struct NODISCARD Verb final
    {
        const char *word;
        int shortest;
    };
    // The shortest forms are the ones the container commands take (ContainerLines.cpp), and
    // MUME's own for the rest as far as his files use them (`l s`, `bash exit s`).
    static constexpr std::array VERBS{Verb{"open", 2},
                                      Verb{"close", 2},
                                      Verb{"unlock", 3},
                                      Verb{"lock", 3},
                                      Verb{"pick", 3},
                                      Verb{"knock", 3},
                                      Verb{"bash", 2},
                                      Verb{"break", 3},
                                      Verb{"use", 3},
                                      Verb{"cast", 1},
                                      Verb{"look", 1}};
    const QString text = input.simplified().toLower();
    const qsizetype space = text.indexOf(QLatin1Char(' '));
    if (space <= 0) {
        return std::nullopt;
    }
    const QString first = text.left(space);
    const auto it = std::find_if(VERBS.begin(), VERBS.end(), [&first](const Verb &v) {
        return first.size() >= v.shortest && QLatin1String(v.word).startsWith(first);
    });
    if (it == VERBS.end()) {
        return std::nullopt;
    }
    DoorAim aim;
    aim.verb = QString::fromLatin1(it->word);
    QString rest = text.mid(space + 1);
    if (aim.verb == QStringLiteral("cast")) {
        // cast [speed] 'burning hands' stonedoor: what follows the spell's closing quote.
        const qsizetype quote = rest.lastIndexOf(QLatin1Char('\''));
        if (quote < 0) {
            return std::nullopt;
        }
        rest = rest.mid(quote + 1).trimmed();
    }
    QStringList words = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (aim.verb == QStringLiteral("use") && !words.isEmpty()) {
        words.removeFirst(); // the item: `use rock exit e`
    }
    if (words.isEmpty()) {
        return std::nullopt;
    }
    if (aim.verb == QStringLiteral("look")) {
        // `l s`: a look at a side. `look in chest` and `look orc` are not about a door.
        aim.dir = words.size() == 1 ? sideOf(words.front()) : QString{};
        if (aim.dir.isEmpty()) {
            return std::nullopt;
        }
        return aim;
    }
    aim.word = words.front();
    if (words.size() >= 2) {
        aim.dir = sideOf(words.at(1));
    }
    return aim;
}

int RoomDoorTracker::indexByDir(const QString &dir) const
{
    if (dir.isEmpty()) {
        return -1;
    }
    for (size_t i = 0; i < m_state.doors.size(); ++i) {
        if (m_state.doors[i].dir == dir) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int RoomDoorTracker::indexByName(const QString &name) const
{
    if (name.isEmpty()) {
        return -1;
    }
    // Two doors of one name cannot be told apart by it.
    int found = -1;
    for (size_t i = 0; i < m_state.doors.size(); ++i) {
        if (m_state.doors[i].name == name) {
            if (found >= 0) {
                return -1;
            }
            found = static_cast<int>(i);
        }
    }
    return found;
}

size_t RoomDoorTracker::entry(const QString &dir, const QString &name, const bool typed)
{
    auto &doors = m_state.doors;
    if (const int byDir = indexByDir(dir); byDir >= 0) {
        RoomDoor &door = doors[static_cast<size_t>(byDir)];
        if (!name.isEmpty() && door.name != name && (!typed || door.name.isEmpty())) {
            door.name = name;
            // The same door known by its name alone until now.
            for (size_t i = 0; i < doors.size(); ++i) {
                if (static_cast<int>(i) != byDir && doors[i].dir.isEmpty()
                    && doors[i].name == name) {
                    doors.erase(doors.begin() + static_cast<std::ptrdiff_t>(i));
                    return static_cast<size_t>(indexByDir(dir));
                }
            }
        }
        return static_cast<size_t>(byDir);
    }
    if (const int byName = indexByName(name); byName >= 0) {
        RoomDoor &door = doors[static_cast<size_t>(byName)];
        if (!dir.isEmpty() && door.dir.isEmpty()) {
            door.dir = dir;
        }
        if (dir.isEmpty() || door.dir == dir) {
            return static_cast<size_t>(byName);
        }
        // The same name on another side: a second door.
    }
    if (dir.isEmpty() && name.isEmpty()) {
        for (size_t i = 0; i < doors.size(); ++i) {
            if (doors[i].dir.isEmpty() && doors[i].name.isEmpty()) {
                return i;
            }
        }
    }
    RoomDoor door;
    door.dir = dir;
    door.name = name;
    doors.push_back(std::move(door));
    return doors.size() - 1;
}

size_t RoomDoorTracker::unnamedTarget()
{
    auto &doors = m_state.doors;
    int iced = -1;
    int count = 0;
    for (size_t i = 0; i < doors.size(); ++i) {
        if (doors[i].state == S::ICED) {
            iced = static_cast<int>(i);
            ++count;
        }
    }
    if (count == 1) {
        return static_cast<size_t>(iced);
    }
    if (m_aim.has_value()) {
        if (const int byDir = indexByDir(m_aim->dir); byDir >= 0) {
            return static_cast<size_t>(byDir);
        }
        if (const int byName = indexByName(doorName(m_aim->word)); byName >= 0) {
            return static_cast<size_t>(byName);
        }
    }
    if (doors.size() == 1) {
        return 0;
    }
    return entry(m_aim.has_value() ? m_aim->dir : QString{}, QString{});
}

QString RoomDoorTracker::freshAimDir(const std::initializer_list<const char *> verbs) const
{
    if (!m_aim.has_value() || m_aimPrompts > 1) {
        return QString{};
    }
    for (const char *const verb : verbs) {
        if (m_aim->verb == QLatin1String(verb)) {
            return m_aim->dir;
        }
    }
    return QString{};
}

void RoomDoorTracker::set(const size_t index,
                          const DoorStateEnum state,
                          const int64_t now,
                          const bool soft,
                          const QString &by)
{
    RoomDoor &door = m_state.doors[index];
    if (door.state == state && door.since != 0) {
        return;
    }
    if (soft && keepsOverClosed(door.state)) {
        return;
    }
    door.state = state;
    door.since = now;
    door.by = by;
}

void RoomDoorTracker::sort()
{
    std::stable_sort(m_state.doors.begin(),
                     m_state.doors.end(),
                     [](const RoomDoor &a, const RoomDoor &b) {
                         return sideOrder(a.dir) < sideOrder(b.dir);
                     });
}

std::optional<RoomDoors> RoomDoorTracker::changed(const int64_t now)
{
    for (RoomDoor &door : m_state.doors) {
        if (door.since == 0) {
            door.since = now; // just become known, with no state said
        }
    }
    sort();
    if (m_sent.has_value()) {
        if (*m_sent == m_state) {
            return std::nullopt;
        }
        // A room without a door after a room without a door: nothing to tell.
        if (m_sent->doors.empty() && m_state.doors.empty()) {
            m_sent = m_state;
            return std::nullopt;
        }
    } else if (m_state.doors.empty()) {
        m_sent = m_state;
        return std::nullopt;
    }
    m_sent = m_state;
    return m_state;
}

void RoomDoorTracker::applyExit(const DoorExit &exit, const int64_t now)
{
    if (exit.dir.isEmpty()) {
        return;
    }
    if (!exit.exists) {
        if (const int index = indexByDir(exit.dir); index >= 0) {
            m_state.doors.erase(m_state.doors.begin() + index);
        }
        return;
    }
    if (!exit.door) {
        // MUME names no door there now. What the lines said of that side stays.
        return;
    }
    const size_t index = entry(exit.dir, exit.name);
    if (exit.broken) {
        set(index, S::BROKEN, now);
    } else if (exit.closed) {
        set(index, S::CLOSED, now, true);
    } else {
        set(index, S::OPEN, now);
    }
}

std::optional<RoomDoors> RoomDoorTracker::receiveRoomInfo(const RoomExitsInfo &info,
                                                          const int64_t now)
{
    // Without an id from MUME there is no telling a `look` from a move: taken for a move.
    if (!info.room.has_value() || info.room != m_state.room) {
        m_state = RoomDoors{};
        m_state.room = info.room;
        m_aim.reset();
        m_lookDir.clear();
        m_explosion.reset();
    }
    for (const DoorExit &exit : info.exits) {
        applyExit(exit, now);
    }
    return changed(now);
}

std::optional<RoomDoors> RoomDoorTracker::receiveUpdateExits(const std::vector<DoorExit> &exits,
                                                             const int64_t now)
{
    for (const DoorExit &exit : exits) {
        applyExit(exit, now);
    }
    return changed(now);
}

std::optional<RoomDoors> RoomDoorTracker::receiveExitsLine(const QString &line, const int64_t now)
{
    for (const DoorExit &exit : parseExitsLine(line)) {
        applyExit(exit, now);
    }
    return changed(now);
}

void RoomDoorTracker::receiveCommand(const QString &input)
{
    const auto aim = parseDoorAim(input);
    if (!aim.has_value()) {
        return;
    }
    if (aim->verb == QStringLiteral("look")) {
        m_lookDir = aim->dir;
        m_lookPrompts = 0;
        return;
    }
    m_aim = aim;
    m_aimPrompts = 0;
}

std::optional<RoomDoors> RoomDoorTracker::receiveDoorReply(const DoorReply &reply, const int64_t now)
{
    const ContainerCommand &command = reply.command;
    const bool impossible = reply.text == QStringLiteral("That's impossible, I'm afraid.");
    switch (reply.result) {
    case R::NOT_FOUND:
        // "You don't see any icewall there.": no door of that name that the character can see.
        return std::nullopt;
    case R::CANNOT:
        if (!impossible) {
            return std::nullopt; // "That's absurd.", "You cannot.": nothing about a door's state
        }
        break;
    default:
        break;
    }
    // MUME answered about the door the player named: the name is confirmed. Without a side and
    // without a name ("open exit") only a room with one door says which door it was.
    const QString name = doorName(command.word);
    size_t index = 0;
    if (command.direction.isEmpty() && name.isEmpty()) {
        if (m_state.doors.size() != 1) {
            return std::nullopt;
        }
    } else {
        index = entry(command.direction, name, true);
    }
    const DoorStateEnum before = m_state.doors[index].state;
    switch (reply.result) {
    case R::OPENED:
    case R::ALREADY_OPEN:
        // "Ok." after `open secrethoard w` log-2006.01.06-01.59.16.txt:47595-47596; "It's already
        // open!" log-2005.11.02-01.27.58.txt:23486-23487.
        set(index, S::OPEN, now);
        break;
    case R::CLOSED:
        set(index, S::CLOSED, now);
        break;
    case R::ALREADY_CLOSED:
        // "It's already closed!" log-2005.09.07-02.50.20.txt:67604-67606 (after `close exit w`).
        set(index, S::CLOSED, now, true);
        break;
    case R::LOCKED:
    case R::NO_KEY:
        // "It seems to be locked." after `open exit e` log-2006.08.03-19.24.37.txt:18504-18505;
        // "It's already locked!" log-2006.02.14-00.57.13.txt:54284-54287; "*click*" after
        // `lock`; "You do not have the proper key for that." after `unlock exit e`
        // log-2006.03.14-21.39.15.txt:27484-27485.
        set(index, S::LOCKED, now);
        break;
    case R::UNLOCKED:
    case R::PICKED:
        // "*click*" after `unlock exit d` log-2005.09.21-01.39.48.txt:26048-26050; "It's
        // already unlocked, it seems." log-2006.05.15-18.37.20.txt:82344-82346. Unlocked says
        // nothing of an open door, nor of the ice or a block on a shut one.
        if (before == S::LOCKED || before == S::UNKNOWN) {
            set(index, S::CLOSED, now);
        }
        break;
    case R::CANNOT:
        // `open exit e` / "That's impossible, I'm afraid." log-2005.10.04-03.05.23.txt:46509-46510.
        if (command.action == A::OPEN) {
            set(index, S::BLOCKED, now);
        }
        break;
    case R::KEY_BROKE:
    case R::PICKING:
    case R::PICK_FAILED:
    case R::PICK_STOPPED:
    case R::PICKPROOF:
    case R::EMPTY:
    case R::CONTENTS:
    case R::NOT_FOUND:
        break;
    }
    return changed(now);
}

std::optional<RoomDoors> RoomDoorTracker::receiveLine(const QString &line,
                                                      const QString &moveDir,
                                                      const int64_t now)
{
    const auto parsed = parseDoorLine(line);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    const DoorLine &door = *parsed;
    const bool known = indexByName(door.name) >= 0;
    switch (door.kind) {
    case K::SEEMS_CLOSED:
        set(entry(known ? QString{} : moveDir, door.name), S::CLOSED, now, true);
        break;
    case K::LOOKED_CLOSED:
    case K::LOOKED_OPEN: {
        // "The chest is closed." reads the same: only for a door already known by that name,
        // or right after a look at a side.
        if (!known && m_lookDir.isEmpty()) {
            return std::nullopt;
        }
        const size_t index = entry(known ? QString{} : m_lookDir, door.name);
        m_lookDir.clear();
        if (door.kind == K::LOOKED_OPEN) {
            set(index, S::OPEN, now);
        } else {
            set(index, S::CLOSED, now, true);
        }
        break;
    }
    case K::OPENED:
    case K::CLOSED:
    case K::UNLOCKED:
    case K::LOCKED: {
        if (!door.actor.isEmpty() && !known && m_notDoor && m_notDoor(door.name)) {
            return std::nullopt; // "Stolb opens the chest."
        }
        const size_t index = entry(QString{}, door.name);
        const DoorStateEnum before = m_state.doors[index].state;
        const QString by = !door.side.isEmpty() ? door.side : door.actor;
        if (door.kind == K::OPENED) {
            set(index, S::OPEN, now, false, by);
        } else if (door.kind == K::CLOSED) {
            set(index, S::CLOSED, now, false, by);
        } else if (door.kind == K::LOCKED) {
            set(index, S::LOCKED, now, false, by);
        } else if (before == S::LOCKED || before == S::UNKNOWN) {
            set(index, S::CLOSED, now, false, by);
        }
        break;
    }
    case K::GAVE_WAY:
        // The player's own `bash exit n` says which side (log-2005.11.17-17.17.33.txt:117926).
        set(entry(known ? QString{} : freshAimDir({"bash"}), door.name), S::BROKEN, now);
        break;
    case K::LIT:
        set(entry(QString{}, door.name), S::BROKEN, now);
        m_explosion.reset();
        break;
    case K::BLURRED: {
        const size_t index = entry(door.dir, door.name);
        if (m_explosion.has_value()) {
            // A rock's blur: in the logs the door opened afterwards whatever it was before
            // (log-2005.10.04-03.05.23.txt:46526-46533, log-2006.01.06-01.59.16.txt:54145-54149,
            // :47586-47596), so it is not called blocked; what it is, the lines do not say.
            set(index, S::UNKNOWN, now);
            m_explosion.reset();
        } else {
            set(index, S::BLOCKED, now);
        }
        break;
    }
    case K::EXPLOSION:
        m_explosion = door.name;
        // The name is MUME's; the door's state is in the line that follows. The player's own
        // `use rock exit e` says which side.
        std::ignore = entry(known || !door.actor.isEmpty() ? QString{} : freshAimDir({"use"}),
                            door.name);
        break;
    case K::RESISTED:
        set(entry(QString{}, door.name), S::BLOCKED, now);
        break;
    case K::ICED: {
        // "the door" may be the sentence's word, not the keyword (the Ice Mound's is
        // `stonedoor`): a room with one door has that one iced, and keeps its name.
        if (!known && m_state.doors.size() == 1) {
            if (m_state.doors[0].name.isEmpty()) {
                m_state.doors[0].name = door.name;
            }
            set(0, S::ICED, now);
        } else {
            set(entry(QString{}, door.name), S::ICED, now);
        }
        break;
    }
    case K::ICE_THICK:
    case K::ICE_AIMED:
    case K::ICE_MELTS:
    case K::ICE_MOLTEN: {
        const size_t index = unnamedTarget();
        // The player's own spell went at the ice, or the player's own hand: the word typed
        // names the door. The two other lines are also seen when somebody else casts.
        const bool own = m_aim.has_value()
                         && ((door.kind == K::ICE_AIMED && m_aim->verb == QStringLiteral("cast"))
                             || (door.kind == K::ICE_THICK
                                 && m_aim->verb != QStringLiteral("cast")));
        if (own && m_state.doors[index].name.isEmpty()) {
            m_state.doors[index].name = doorName(m_aim->word);
        }
        set(index, door.kind == K::ICE_MOLTEN ? S::MOLTEN : S::ICED, now);
        break;
    }
    }
    return changed(now);
}

void RoomDoorTracker::receivePrompt()
{
    // A command's answer comes at once; a fight round can bring a prompt before it, and a
    // spell's own lines come some prompts after the cast.
    constexpr int PROMPTS_TO_LOOK = 2;
    constexpr int PROMPTS_TO_AIM = 6;
    if (!m_lookDir.isEmpty() && ++m_lookPrompts >= PROMPTS_TO_LOOK) {
        m_lookDir.clear();
    }
    if (m_aim.has_value() && ++m_aimPrompts >= PROMPTS_TO_AIM) {
        m_aim.reset();
    }
    m_explosion.reset();
}

void RoomDoorTracker::reset()
{
    m_state = RoomDoors{};
    m_sent.reset();
    m_aim.reset();
    m_aimPrompts = 0;
    m_lookDir.clear();
    m_lookPrompts = 0;
    m_explosion.reset();
}
