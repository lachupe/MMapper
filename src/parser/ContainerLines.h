#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "RoomContents.h"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include <QString>

/// What happened to a container in the room, read off MUME's replies to the player's commands.
///
/// MUME's replies to "open chest" or "unlock chest" do not name what they are about: "Ok.",
/// "*click*" and "It seems to be locked." are the same words it uses for a door. They only mean
/// something paired with the command that caused them, and MMapper sees every command on its
/// way to MUME -- from its own client and from a frontend's MMapper.Input.Command alike -- so
/// the pairing is done here, once, and the result is published as MMapper.Room.Container. What
/// a reply says about the container (open, locked, pickproof, empty) is kept per room and rides
/// along in the next MMapper.Room.Contents.
///
/// The sentences come from the powwow logs, which are one player's configuration with a client's
/// additions mixed in. MUME answers commands in the order they were sent, so replies are matched
/// to the oldest command still waiting for one that could have caused them. Door commands are
/// queued too, so that their "Ok." is not taken for a chest's, but nothing is published for
/// them.
enum class NODISCARD ContainerActionEnum : uint8_t {
    OPEN,
    CLOSE,
    UNLOCK,
    LOCK,
    PICK,
    LOOK,
    GET,
    PUT
};

enum class NODISCARD ContainerResultEnum : uint8_t {
    OPENED,
    CLOSED,
    ALREADY_OPEN,
    ALREADY_CLOSED,
    LOCKED,
    UNLOCKED,
    NO_KEY,
    KEY_BROKE,
    PICKING,
    PICKED,
    PICK_FAILED,
    /// The player stopped picking, or something broke it off.
    PICK_STOPPED,
    PICKPROOF,
    EMPTY,
    /// A listing of what is inside, or what was taken out of it or put in.
    CONTENTS,
    NOT_FOUND,
    CANNOT
};

/// A command the player sent that is aimed at a container, or at a door by the same verbs.
struct NODISCARD ContainerCommand final
{
    ContainerActionEnum action = ContainerActionEnum::OPEN;
    /// As the player wrote it: "chest", "2.chest", "che".
    QString target;
    /// The target without its count: "chest" for "2.chest".
    QString word;
    /// Which of the matching objects: 2 for "2.chest".
    int ordinal = 1;
    /// For GET and PUT, the item named: "all", "coins", "all.arrow".
    QString item;
    /// False for a door, or anything else the verbs can name that is not a container.
    bool container = true;
};

/// One item in a container's listing, or taken from or put into one.
struct NODISCARD ContainerItem final
{
    /// The item as MUME named it, without a leading count, condition or glow: "amethysts" for
    /// "two amethysts", "a black sword" for "a black sword (flawless); it glows blue".
    QString name;
    int count = 1;
    /// The line as MUME wrote it.
    QString text;
};

struct NODISCARD ContainerEvent final
{
    ContainerCommand command;
    ContainerResultEnum result = ContainerResultEnum::CANNOT;
    std::vector<ContainerItem> items;
    /// The reply as MUME wrote it, one line per line, twiddlers removed.
    QString text;
    /// The object in the room's list the command reached, or -1 when it could not be told.
    int index = -1;
};

/// The container command `input` is, or nullopt when it is not one of open, close, unlock,
/// lock, pick, look in, examine, get from or put in. Door commands are returned with
/// `container` false.
NODISCARD std::optional<ContainerCommand> parseContainerCommand(const QString &input);

/// An item line of a listing ("two amethysts", "a black sword (flawless); it glows blue").
NODISCARD ContainerItem parseContainerItem(const QString &line);

NODISCARD std::string_view to_string_view(ContainerActionEnum action);
NODISCARD std::string_view to_string_view(ContainerResultEnum result);

/// Pairs container commands with MUME's replies, and keeps what they said about each container.
class NODISCARD ContainerTracker final
{
private:
    struct NODISCARD Pending final
    {
        ContainerCommand command;
        /// Prompts seen since it was sent. A command whose reply never came is given up on.
        int prompts = 0;
        /// True once MUME started the lock picking, which lasts over several prompts.
        bool picking = false;
    };

    /// A reply of several lines being gathered: a listing, or items taken out one by one.
    struct NODISCARD Gathering final
    {
        ContainerCommand command;
        ContainerResultEnum result = ContainerResultEnum::CONTENTS;
        std::vector<ContainerItem> items;
        QStringList lines;
        /// For a listing: true while its item lines are still coming.
        bool listing = false;
        bool nothing = false;
    };

    using RoomStates = std::map<QString, ContainerState>;

    std::deque<Pending> m_pending;
    std::optional<Gathering> m_gathering;
    std::map<QString, RoomStates> m_rooms;
    std::deque<QString> m_roomOrder;
    RoomContentsSnapshot m_current;
    /// The unlock the last "*click*" answered, until the next prompt: a one-use key breaks
    /// with a line of its own after the click.
    std::optional<ContainerCommand> m_lastUnlock;
    bool m_changed = false;

public:
    /// Notes a command on its way to MUME. Anything that is not a container command is ignored.
    void receiveCommand(const QString &input);
    /// Reads one line of MUME's output, colour removed, and returns the events it completes.
    NODISCARD std::vector<ContainerEvent> receiveLine(const QString &line, int64_t now);
    /// A prompt ends every reply still being gathered.
    NODISCARD std::vector<ContainerEvent> receivePrompt(int64_t now);
    /// Fills in the state known for each container, and remembers `contents` as the room the
    /// body is in, for working out which object a later command reached.
    void decorate(RoomContentsSnapshot &contents);
    /// The room as decorate() last saw it, with what has been learnt since.
    NODISCARD RoomContentsSnapshot current() const;
    /// True once after a reply changed what is known about a container in the current room.
    NODISCARD bool takeChanged();
    /// For a new session, or when XML mode goes away.
    void reset();

private:
    NODISCARD std::vector<ContainerEvent> flush(int64_t now);
    NODISCARD std::optional<ContainerEvent> finish(const ContainerCommand &command,
                                                   ContainerResultEnum result,
                                                   std::vector<ContainerItem> items,
                                                   const QString &text,
                                                   int64_t now);
    NODISCARD int resolve(const ContainerCommand &command) const;
    NODISCARD QString objectKey(int index) const;
    void apply(int index,
               ContainerActionEnum action,
               ContainerResultEnum result,
               bool hadItems,
               int64_t now);
    /// Takes the oldest waiting command that `accepts` allows, dropping the ones before it,
    /// whose replies were missed.
    template<typename Accepts>
    NODISCARD std::optional<Pending> take(Accepts &&accepts, bool keep = false);
};
