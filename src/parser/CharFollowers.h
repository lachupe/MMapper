#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

#include <QString>
#include <QStringList>

/// The creatures that follow the player's character and take its orders: charmed mobs, led
/// mounts, summoned ones. MUME has no list of them and GMCP says nothing of the bond, so it is
/// read off the lines that make it, strain it and end it, and off the answers to the player's
/// `order`, paired with the command that caused them. Published whole at each change as
/// MMapper.Char.Followers.
///
/// With them goes the other side of following: whom the player's character follows, which
/// players follow it, who therefore leads, and whom it has said it will protect.
///
/// The sentences are MUME's own, from the powwow logs and files under /home/aza/data/powwow
/// (2005-2006, one player's configuration); the places are cited line by line in
/// CharFollowers.cpp. None was seen live.

enum class NODISCARD FollowerKindEnum : uint8_t {
    /// A mob under a charm: its name begins with an article and it is no known mount.
    CHARMIE,
    /// A mount led by its reins: one of the mounts MUME sells, or anything the player was seen
    /// to ride.
    MOUNT,
    /// Raised by a spell: "an enslaved shadow".
    SUMMONED,
    /// It takes the player's orders, but its name does not say what it is ("Harle the Hobbit").
    UNKNOWN
};

enum class NODISCARD FollowerStateEnum : uint8_t {
    FOLLOWING,
    /// It refused the last order: "You failed to control a mother eagle (one)."
    REFUSING,
    /// It stayed behind when the player moved: "ACK! A raging bear didn't follow you, you lost
    /// it." The bond holds: back in its room, it follows again.
    LOST,
    /// "A wild dog stops following you." The bond is over.
    LEFT,
    DEAD
};

NODISCARD std::string_view to_string_view(FollowerKindEnum kind);
NODISCARD std::string_view to_string_view(FollowerStateEnum state);

/// What one line of MUME's output says of a follower, or of an order.
enum class NODISCARD FollowerLineEnum : uint8_t {
    /// "A mother eagle starts following you.", "Grayelf now follows you.", and somebody else's
    /// "An enslaved shadow now follows Farseer (F)." -- `leader` says whose.
    FOLLOWS,
    /// "A trained horse (my) stops following you."; `leader` as for FOLLOWS.
    STOPS,
    /// "A wild dog hates your guts!": the player attacked its own follower.
    HATES,
    /// "ACK! A raging bear didn't follow you, you lost it."
    LOST,
    /// "You failed to control a mother eagle (one)."
    FAILED,
    /// "You have no loyal subjects here."
    NONE_HERE,
    /// "Order who to do what?"
    SYNTAX,
    /// "In your dreams, or what?": MUME's answer to most commands while asleep.
    ASLEEP,
    /// "Ok.": MUME's answer to an order given, to `label`, and to a good many other commands.
    OK,
    /// "A mother eagle (one) is now a group member."
    GROUPED,
    /// "A mother eagle (one) is dead! R.I.P.", "An enslaved shadow (one) disappears into
    /// nothing."
    DIED,
    /// "A mother eagle (one) has arrived from the north."
    ARRIVED,
    /// "An enslaved shadow (one) leaves north."
    WENT,
    /// "A mother eagle (one) is standing here.", "A raging bear (one) is here, fighting Nagash
    /// the Dark."
    SEEN,
    /// "You pick up a trained horse (my)'s reins, and start riding him.": what is ridden is a
    /// mount, and no longer led.
    RIDING,
    /// "You stop riding a trained horse (my)."
    RODE,
    /// "You are dead! Sorry...": the player's character died, and is where no follower is.
    YOU_DIED,
    /// "You now follow Grayelf.": the player's character follows `name`.
    YOU_FOLLOW,
    /// "You stop following Zmej."
    YOU_STOP,
    /// "You will not follow anyone else now.": MUME's answer to `follow self`.
    YOU_FOLLOW_NOBODY,
    /// "You follow Orhzul.": the leader moved, and the character went after.
    YOU_WENT_AFTER,
    /// "Zmej doesn't want you to follow him.", "An old man doesn't want you to follow him!",
    /// "Sorry, but following in 'loops' is not allowed.": refused or sent away. `name` is who
    /// refused, empty for the loop. The bond's end is "You stop following X.", which follows.
    FOLLOW_DENIED,
    /// "You will now try to protect Budach (B)."
    PROTECTS,
    /// "You will no longer try to protect Kazadoe (k)."
    UNPROTECTS,
    /// "You will try to protect:": the names follow, one to a line, indented.
    PROTECT_LIST,
    /// "You aren't trying to protect anyone.", and `protect self`'s "Very well, you concentrate
    /// on your own health."
    PROTECT_NONE,
    /// "You can only protect those in your group.": nothing changed.
    PROTECT_DENIED
};

struct NODISCARD FollowerLine final
{
    FollowerLineEnum kind = FollowerLineEnum::OK;
    /// The name as MUME wrote it, its label taken off and a capital article at the start of
    /// the sentence made small: "a mother eagle", "Harle the Hobbit". Empty for the lines that
    /// name nobody.
    QString name;
    /// The label the line showed after the name, without its brackets: "one". Empty when none.
    QString label;
    /// FOLLOWS and STOPS: "you", or whoever else is followed, without a label.
    QString leader;
};

/// What `line` says of a follower or an order, or nothing. `line` is the line as the user sees
/// it, colour removed; twiddlers in front of it are taken off here.
NODISCARD std::optional<FollowerLine> parseFollowerLine(const QString &line);

/// A command the player sent that the tracker has to know of.
struct NODISCARD FollowerCommand final
{
    enum class NODISCARD TypeEnum : uint8_t {
        /// `order followers assist`, `order one sleep`, and a bare `order followers`.
        ORDER,
        /// `label eagle one`.
        LABEL
    };
    TypeEnum type = TypeEnum::ORDER;
    /// ORDER: "followers", or the word the player named one follower by, lowercase ("one",
    /// "harle"); empty when the command named nobody. LABEL: the word naming whom to label,
    /// lowercase, with its count taken off ("trained" for "2.trained").
    QString who;
    /// LABEL: which of those the word matches, 2 for "2.trained".
    int ordinal = 1;
    /// ORDER: what was ordered, as written: "assist", "hit orc". Empty when nothing was.
    QString order;
    /// LABEL: the label given: "one".
    QString label;
};

/// The `order` or two-word `label` command `input` is, or nothing. `order` is taken down to
/// "ord"; "followers" down to "fol".
NODISCARD std::optional<FollowerCommand> parseFollowerCommand(const QString &input);

/// Whether a name is an NPC's by the look of it: it begins with an article, "a mother eagle",
/// "an orc prisoner", "the sage". A player is "Stolb", or "*an Orc*" to an enemy.
NODISCARD bool isNpcName(const QString &name);

/// What a follower of this name is, by the name alone; see FollowerKindEnum.
NODISCARD FollowerKindEnum followerKindOf(const QString &name);

struct NODISCARD CharFollower final
{
    /// See FollowerLine::name.
    QString name;
    /// The player's label for it, from the name's suffix in MUME's lines or from the player's
    /// `label` command. Empty when none is known.
    QString label;
    FollowerKindEnum kind = FollowerKindEnum::UNKNOWN;
    /// In the player's room, as far as the lines told.
    bool here = true;
    FollowerStateEnum state = FollowerStateEnum::FOLLOWING;
    /// Unix seconds at which the line that made the bond was read; unset for a follower known
    /// only from a later line.
    std::optional<int64_t> since;
    /// The last order given that it did not refuse; empty when none.
    QString lastOrder;
    /// The last order it refused; empty when none.
    QString lastRefused;
};

enum class NODISCARD FollowerReplyEnum : uint8_t { OK, FAILED, NONE_HERE, SYNTAX, ASLEEP };
NODISCARD std::string_view to_string_view(FollowerReplyEnum result);

/// MUME's answer to one `order`.
struct NODISCARD FollowerReply final
{
    /// What was ordered: "assist". Empty for a bare `order followers`.
    QString order;
    /// "followers", or the word the player named one follower by.
    QString who;
    FollowerReplyEnum result = FollowerReplyEnum::OK;
    /// The followers that refused, by name (FollowerLine::name), in the order MUME named them.
    QStringList failed;
};

/// Who leads: the one the player's character follows, or the character itself.
struct NODISCARD FollowLeader final
{
    /// The one followed, as CharFollowers::following. Empty when `you`: the tracker does not
    /// know the character's own name (Char.Name has it).
    QString name;
    bool you = false;
};

/// One MMapper.Char.Followers: the followers after a change, and the answer to an order when
/// that is what changed them.
struct NODISCARD CharFollowers final
{
    /// In the order they became known. One that just left or died is in this once, with that
    /// state, and in none after.
    std::vector<CharFollower> followers;
    std::optional<FollowerReply> reply;
    /// Whom the player's character follows, label taken off: "Grayelf", "a black sorcerer".
    /// Empty when nobody, as far as the lines told.
    QString following;
    /// The players that follow the character, in the order they began: "Budach". They take no
    /// orders, and are in `followers` never.
    QStringList players;
    /// Whom the character has said it will try to protect; unset until a line stated it.
    std::optional<QStringList> protecting;
};

/// Who leads, by what `state` holds: the one followed; else the player's character when a
/// player or a bound follower follows it; else nothing, for nobody is known to lead.
NODISCARD std::optional<FollowLeader> leaderOf(const CharFollowers &state);

/// What of `change` is state to replay to a frontend that connects later: the followers still
/// bound, without the answer to an order that is over.
NODISCARD CharFollowers lastingFollowers(const CharFollowers &change);

/// Keeps the followers of the player's character.
///
/// A bond is made by "X starts following you." (accepted for an NPC's name only: a player who
/// follows is no follower of this kind) and ended by "X stops following you.", "X hates your
/// guts!" or its death. In between, "ACK! X didn't follow you, you lost it." says it stayed
/// behind, its arriving or being seen in the room that it is back, and "You failed to control
/// X." that it refused an order. One whose bond was never seen made is taken in when it
/// refuses an order.
///
/// The other way round: "You now follow X." and the "You follow X." of each move after a
/// leader say whom the character follows, "You stop following X." and `follow self`'s "You
/// will not follow anyone else now." that it follows nobody. A player who starts following is
/// kept by name in `players` until "X stops following you." (or "X now follows Y."). MUME
/// answers `follow` and `protect` in sentences of their own, never with "Ok.", so those
/// commands need no pairing. `protect X` turns the protection of X on or off, a bare
/// `protect` lists it and `protect self` ends it all.
///
/// Orders: MUME answers `order` with "Ok." and, before or after it, one "You failed to
/// control X." for each follower that refused; or with "You have no loyal subjects here.",
/// "Order who to do what?" or "In your dreams, or what?". None of these names the order, and
/// "Ok." answers much else, so the commands the player sent are queued and MUME's answers go
/// to the oldest that could have caused them, as in ContainerTracker. `label` is queued too:
/// its "Ok." sets the label, and must not be taken for an order's. An answer is complete at
/// the prompt after it.
///
/// Free of Qt networking types and of the clock, so that it can be tested on its own: the
/// caller passes the time.
class NODISCARD CharFollowersTracker final
{
private:
    struct NODISCARD Pending final
    {
        FollowerCommand command;
        /// Unix seconds at which it was sent, and the prompts seen since. A command whose
        /// answer never came is given up on.
        int64_t sent = 0;
        int prompts = 0;
        /// ORDER: an answering line came; the prompt ends the answer.
        bool answered = false;
        /// ORDER: MUME said "Ok.".
        bool ok = false;
        std::optional<FollowerReplyEnum> result;
        QStringList failed;
    };

    std::vector<CharFollower> m_followers;
    /// See CharFollowers::following, ::players and ::protecting.
    QString m_following;
    QStringList m_players;
    std::optional<QStringList> m_protecting;
    /// "You will try to protect:" was read, and the names after it so far.
    bool m_protectListing = false;
    QStringList m_protectListed;
    std::deque<Pending> m_pending;
    /// What the player was seen to ride or to stop riding: mounts, whatever their names.
    QStringList m_ridden;

public:
    /// Notes a command on its way to MUME; `now` is unix seconds. Anything but `order` and
    /// `label` is ignored.
    void receiveCommand(const QString &input, int64_t now);
    /// Reads one line of MUME's output, colour removed; `now` is unix seconds. Returns the
    /// followers when the line changed them, to be published.
    NODISCARD std::optional<CharFollowers> receiveLine(const QString &line, int64_t now);
    /// A real prompt: ends the answer to an order. Returns the followers with that answer.
    NODISCARD std::optional<CharFollowers> receivePrompt(int64_t now);
    /// The followers still bound, in the order they became known.
    NODISCARD const std::vector<CharFollower> &followers() const { return m_followers; }
    NODISCARD const QString &following() const { return m_following; }
    NODISCARD const QStringList &players() const { return m_players; }
    NODISCARD const std::optional<QStringList> &protecting() const { return m_protecting; }
    /// For a new session, or when the character leaves the game.
    void reset();

private:
    /// How sure a line is to mean a follower of the player's, which decides how far its name
    /// may be from an entry's.
    enum class NODISCARD MatchEnum : uint8_t {
        /// Any creature of that name says this ("has arrived", "is dead!"): the entry of that
        /// name and that very label, or of that name and none when the line shows none.
        EXACT,
        /// EXACT, or else the entry of that name without a label when the line shows one: the
        /// player labelled it since.
        ADOPT,
        /// Only a follower of the player's says this ("stops following you", "failed to
        /// control"): ADOPT, or else the only entry of that name, whatever its label.
        CERTAIN,
        /// The line never shows a label ("ACK! ..."): the name decides, one in the room first.
        ANY_LABEL
    };
    NODISCARD CharFollower *find(const QString &name, const QString &label, MatchEnum match);
    /// Adds a follower, giving up the oldest one left behind when there are too many.
    CharFollower &add(CharFollower follower);
    /// The entry in the room a command's word names: by its label, or by a word of its name.
    NODISCARD CharFollower *findByWord(const QString &word, int ordinal);
    NODISCARD FollowerKindEnum kindOf(const QString &name) const;
    NODISCARD Pending *firstOrder();
    NODISCARD std::optional<CharFollowers> readLine(const QString &line, int64_t now);
    /// Ends the list after "You will try to protect:", and says whether it changed anything.
    NODISCARD bool endProtectList();
    /// The followers as they are, with `reply`; those that left or died are dropped after.
    NODISCARD CharFollowers take(std::optional<FollowerReply> reply);
};
