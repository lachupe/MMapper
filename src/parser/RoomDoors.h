#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "ContainerLines.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include <QJsonObject>
#include <QString>

/// The doors of the room the player's character stands in, as MUME told the player this session
/// (MMapper.Room.Door).
///
/// GMCP Room.Info and Room.UpdateExits say which exits have a door and whether it is closed or
/// broken, and the exits line marks the same ("[north]" closed, "(north)" open, "#north#" broken:
/// help exits). What they do not say is that a door is locked, covered with ice, held by a block
/// door spell, or what happened to it a moment ago; MUME says that in prose, most of it in
/// answer to a command and without naming the door. RoomDoorTracker puts the two together.
///
/// HIDDEN DOORS. MUME's rules forbid actions "that automatically inform your client about the
/// names of hidden doors" (news post "Action and Punishment", 25 July 2006,
/// logs/archives/log-2006.07.25-23.49.46.txt:74-112). Nothing here reads MMapper's map: this
/// file does not include it and the tracker is given no room. A door's name is in the package
/// only when MUME itself sent it to the player in this room on this visit -- as the `name` of
/// an exit in Room.Info or Room.UpdateExits, or in a line ("The stonedoor seems to be closed.")
/// -- or when the player typed it in a door command and MUME's answer was about that door
/// ("Ok.", "It seems to be locked."; not "You don't see any icewall there."). The word "exit",
/// which names any door, is never a name. Everything is forgotten when the room changes.
///
/// The lines are from the powwow logs of 2005-2006 (cited in RoomDoors.cpp); none was seen live.
enum class NODISCARD DoorStateEnum : uint8_t {
    OPEN,
    /// Shut, and not known to be locked.
    CLOSED,
    LOCKED,
    /// Covered by a layer of ice that keeps a hand from it (the Unqalome doors).
    ICED,
    /// Will not open: "That's impossible, I'm afraid.", or a block door spell seen cast on it.
    BLOCKED,
    /// Bashed in, or broken by a spell: an exit with no door to shut.
    BROKEN,
    /// The ice has melted; the door itself is as it was under the ice, most likely shut.
    MOLTEN,
    /// Something was done to it and the lines do not say what it is now.
    UNKNOWN
};
NODISCARD std::string_view to_string_view(DoorStateEnum state);

struct NODISCARD RoomDoor final
{
    /// One letter, n e s w u d; empty when no line or command gave the side.
    QString dir;
    /// The door's keyword, lowercase, under the rule above; empty when MUME has not shown it.
    QString name;
    DoorStateEnum state = DoorStateEnum::UNKNOWN;
    /// Unix seconds at which the state last changed, by the caller's clock.
    int64_t since = 0;

    NODISCARD bool operator==(const RoomDoor &other) const = default;
};

/// One MMapper.Room.Door: every door known in the room, whole.
struct NODISCARD RoomDoors final
{
    /// MUME's id of the room, from Room.Info; unset when it gave none.
    std::optional<int64_t> room;
    /// Those with a side first, in the order n e s w u d, then the others as they became known.
    std::vector<RoomDoor> doors;

    NODISCARD bool operator==(const RoomDoors &other) const = default;
};

/// What Room.Info, Room.UpdateExits or the exits line says of one exit.
struct NODISCARD DoorExit final
{
    QString dir;
    /// False when Room.UpdateExits took the exit away (its value is `false`).
    bool exists = true;
    /// True when the exit has a door: a `name`, or the flag closed or broken; in the exits
    /// line, one of the three door marks.
    bool door = false;
    QString name;
    bool closed = false;
    bool broken = false;

    NODISCARD bool operator==(const DoorExit &other) const = default;
};

struct NODISCARD RoomExitsInfo final
{
    std::optional<int64_t> room;
    std::vector<DoorExit> exits;
};

/// The `exits` object of Room.Info, which is also the whole payload of Room.UpdateExits:
/// {"n":{"id":123,"name":"door","flags":["closed"]},"e":false}. Only n, e, s, w, u and d are
/// read, in that order.
NODISCARD std::vector<DoorExit> parseExitsObject(const QJsonObject &exits);
/// Room.Info's payload: its `id` and its `exits`.
NODISCARD RoomExitsInfo parseRoomInfoDoors(const QJsonObject &roomInfo);
/// The exits line of a room display, "Exits: [north], (east), =#south#=, west.": the exits that
/// carry a door mark. `line` may be the whole text of an <exits> element: the first line that
/// begins "Exits:" is read. Empty for anything else (the `exits` command's list among it).
NODISCARD std::vector<DoorExit> parseExitsLine(const QString &line);

enum class NODISCARD DoorLineEnum : uint8_t {
    /// "The door seems to be closed.": a move refused.
    SEEMS_CLOSED,
    /// "The hangingbranch is closed." / "The deadbark is open.": a look at that side.
    LOOKED_CLOSED,
    LOOKED_OPEN,
    /// "The door is opened from the other side.", "A brown-skinned orc opens the irondoor."
    OPENED,
    /// "The sturdydoor is closed from the other side.", "The irondoor closes quietly.",
    /// "Lungorthin closes the fence."
    CLOSED,
    /// "A brown-skinned orc unlocks the irondoor." / "Gumak the Uruk-hai locks the stonedoor."
    UNLOCKED,
    LOCKED,
    /// "The irondoor gave away under the pressure.": bashed in.
    GAVE_WAY,
    /// "The towerdoor is filled with a bright light.": a break door spell, or a rock.
    LIT,
    /// "The exit west seems to blur for a while." (`dir` set) and "The gate seems to blur for a
    /// while." (`name` set): a block door spell, or a rock.
    BLURRED,
    /// "As you throw a twisted rock fragment at the door, there is an explosion."
    EXPLOSION,
    /// "The will blocking the trapdoor resisted your spell."
    RESISTED,
    /// "The door slams shut, and a thick layer of ice covers it." / "A thick layer of ice covers
    /// the door."
    ICED,
    /// "The ice layer is too thick and prevents you from reaching it."
    ICE_THICK,
    /// "You aim your spell at the ice layer."
    ICE_AIMED,
    /// "Some of the ice melts down."
    ICE_MELTS,
    /// "The ice layer is completely molten!"
    ICE_MOLTEN
};

struct NODISCARD DoorLine final
{
    DoorLineEnum kind = DoorLineEnum::SEEMS_CLOSED;
    /// The door as the line named it, lowercase; empty when it named none.
    QString name;
    /// One letter, for "The exit west seems to blur for a while." only.
    QString dir;
    /// Who did it, for "X opens the door." and its kin; empty otherwise.
    QString actor;
};

/// The door line `line` is (colour removed, matched whole), or nullopt.
NODISCARD std::optional<DoorLine> parseDoorLine(const QString &line);

/// A command that aims at a door or may: open, close, unlock, lock, pick, knock, bash and break
/// with their target, `use <item> <door> [side]`, `cast '<spell>' <target> [side]`, and `look
/// <side>`.
struct NODISCARD DoorAim final
{
    /// "open", "close", "unlock", "lock", "pick", "knock", "bash", "break", "use", "cast",
    /// "look".
    QString verb;
    /// The target word, lowercase; "exit" when the player named the door by its side alone.
    /// Empty for look.
    QString word;
    /// One letter, or empty.
    QString dir;
};
NODISCARD std::optional<DoorAim> parseDoorAim(const QString &input);

/// Keeps the doors of the current room. See DoorStateEnum and the note on hidden doors above.
///
/// Free of the map, of Qt's networking types and of the clock: the caller passes the time.
/// Every method that can change the doors returns them whole when they did change, and nullopt
/// otherwise.
class NODISCARD RoomDoorTracker final
{
public:
    /// True for a word that names a container ("chest"): "X opens the chest." is then no door.
    using NotDoor = std::function<bool(const QString &)>;

private:
    RoomDoors m_state;
    /// What was last returned, to tell a change from a restatement.
    std::optional<RoomDoors> m_sent;
    /// The door the player last aimed a command at, for the lines that name none.
    std::optional<DoorAim> m_aim;
    int m_aimPrompts = 0;
    /// The side of the last `look <side>`, until its answer or the second prompt.
    QString m_lookDir;
    int m_lookPrompts = 0;
    /// A rock went off at the door of this name: the blur that follows is the rock's, not a
    /// block door spell's. Until the next prompt.
    std::optional<QString> m_explosion;
    NotDoor m_notDoor;

public:
    void setNotDoor(NotDoor notDoor) { m_notDoor = std::move(notDoor); }

    /// Room.Info: another room forgets everything; the same room again (a `look`) keeps what
    /// the lines said and takes open, closed and broken from MUME.
    NODISCARD std::optional<RoomDoors> receiveRoomInfo(const RoomExitsInfo &info, int64_t now);
    /// Room.UpdateExits: the exits that changed.
    NODISCARD std::optional<RoomDoors> receiveUpdateExits(const std::vector<DoorExit> &exits,
                                                          int64_t now);
    /// The exits line of a room display.
    NODISCARD std::optional<RoomDoors> receiveExitsLine(const QString &line, int64_t now);
    /// A command on its way to MUME.
    void receiveCommand(const QString &input);
    /// MUME's answer to a door command, paired by ContainerTracker.
    NODISCARD std::optional<RoomDoors> receiveDoorReply(const DoorReply &reply, int64_t now);
    /// One line of MUME's output, colour removed. `moveDir` is the side of the oldest move still
    /// unanswered (one letter), or empty: the side "The door seems to be closed." is about.
    NODISCARD std::optional<RoomDoors> receiveLine(const QString &line,
                                                   const QString &moveDir,
                                                   int64_t now);
    void receivePrompt();
    /// The doors as last returned; empty before the first.
    NODISCARD RoomDoors current() const { return m_state; }
    /// For a new session, or when the character leaves the game.
    void reset();

private:
    NODISCARD int indexByDir(const QString &dir) const;
    NODISCARD int indexByName(const QString &name) const;
    /// The entry for a door of this side and name, made when there is none. Either may be
    /// empty. `typed`: the name is the player's own word for it, which MUME took but which may
    /// be an abbreviation, so it never replaces a name MUME gave.
    NODISCARD size_t entry(const QString &dir, const QString &name, bool typed = false);
    /// The door a line without a name is about: the only one iced, the one aimed at, the only
    /// one there is, or a new entry.
    NODISCARD size_t unnamedTarget();
    /// The side of the player's last door command when it is one of `verbs` and no more than
    /// one prompt old; empty otherwise.
    NODISCARD QString freshAimDir(std::initializer_list<const char *> verbs) const;
    /// `soft`: a plain "closed" that must not overwrite locked, iced, blocked or molten.
    void set(size_t index, DoorStateEnum state, int64_t now, bool soft = false);
    void applyExit(const DoorExit &exit, int64_t now);
    NODISCARD std::optional<RoomDoors> changed(int64_t now);
    void sort();
};
