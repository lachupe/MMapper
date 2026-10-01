// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharFollowers.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include <QRegularExpression>

namespace {

// A command whose answer has not come after this many prompts (as in ContainerTracker) and
// this many seconds is given up on. Both, since the prompts of commands sent before it come
// first: five moves and an order sent at once have five prompts before the order's answer.
constexpr int PROMPTS_TO_WAIT = 3;
constexpr int64_t SECONDS_TO_WAIT = 10;
// The followers kept. One left behind for good is never said to be gone, so the oldest of
// those makes room.
constexpr size_t FOLLOWERS_TO_KEEP = 16;
// The mounts seen ridden that are remembered; the oldest goes first.
constexpr int RIDDEN_TO_KEEP = 16;
// The players that follow who are kept: one who left unseen is never said to be gone.
constexpr int PLAYERS_TO_KEEP = 32;

// The "twiddlers" prompt option draws \|/- while a delayed action runs, and they end up on the
// front of whatever line comes next: "/-\|/-Ok." (logs/archives/log-2006.02.05-19.56.25.txt:
// 169078, the charm going off). As in CombatLines.
const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};

// The label that trails a name: "a mother eagle (one)", "Kazadoe (K)", and one MUME port's
// "*Stolb the Orc* [stolb]". As g_label in CombatLines, but kept.
const QRegularExpression g_label{
    QStringLiteral(R"(^(?<n>.*?)\s+(?:\((?<l>[^()]{1,12})\)|\[(?<s>[^\[\]]{1,12})\])$)")};

// A capital article at the start of a sentence: "A mother eagle starts following you." names
// the "a mother eagle" of "You failed to control a mother eagle (one).".
const QRegularExpression g_capitalArticle{QStringLiteral(R"(^(?:A|An|The) )")};
const QRegularExpression g_article{QStringLiteral(R"(^(?:a|an|the) (?=\S))")};

// All places below are under /home/aza/data/powwow; "logs/archives/" is left off the logs'
// names. The counts are of logs/archives/*.txt.
//
// The bond made, the follower's line (pow/tmp/follow.pow:19 "^&1 $2s following you."):
//   A mother eagle starts following you.             log-2005.11.07-15.57.10.txt:37021 (charm)
//   An orc prisoner starts following you.            log-2006.02.03-00.39.44.txt:47606
//   A starving warg starts following you.            log-2006.02.05-19.56.25.txt:169079
//   A raging bear starts following you.              log-2006.01.04-22.01.05.txt:114790
//   A trained horse (my) starts following you.       log-2005.11.07-15.57.10.txt:3219 (led,
//                                                    after "You stop riding ..." :3218)
//   Stolb (S) starts following you.                  a player; 6290 such lines in all
//   Something starts following you.                  log-2005.11.07-15.57.10.txt:3449 (dark)
//   Grayelf now follows you.                         log-2005.09.03-02.28.34.txt:1903 (7 lines,
//                                                    in the two oldest logs only)
// and somebody else's (pow/tmp/ride.pow:12,14):
//   An enslaved shadow now follows Farseer (F).      log-2006.02.09-19.48.52.txt:131913
//   A hungry warg now follows Stolb.                 log-2005.08.31-14.00.43.txt:12898
const QRegularExpression g_follows{
    QStringLiteral(R"(^(?<w>.+?) (?:starts following|now follows) (?<l>.+?)\.$)")};
// The bond ended (pow/tmp/ride.pow:13 for somebody else's):
//   A wild dog stops following you.                  log-2005.11.07-15.57.10.txt:102024
//   A trained horse (my) stops following you.        log-2005.11.07-15.57.10.txt:3259 (before
//                                                    "You pick up ... reins" :3260)
//   An enslaved shadow (one) stops following Farseer (F).  log-2006.02.06-18.58.18.txt:90615
const QRegularExpression g_stops{QStringLiteral(R"(^(?<w>.+?) stops following (?<l>.+?)\.$)")};
// The player hit its own follower; "X stops following you." comes next:
//   A wild dog hates your guts!                      log-2005.11.07-15.57.10.txt:102023
//   A large hawk hates your guts!                    log-2006.02.03-00.39.44.txt:20266
const QRegularExpression g_hates{QStringLiteral(R"(^(?<w>.+?) hates your guts!$)")};
// Left behind (pow/elestir.pow:55, pow/tmp/follow.pow:20). The label is never shown:
//   ACK! A pack horse didn't follow you, you lost him.   log-2005.08.31-14.00.43.txt:3223
//   ACK! A mother eagle didn't follow you, you lost her. log-2005.11.08-15.42.24.txt:129227
//   ACK! A raging bear didn't follow you, you lost it.   log-2006.01.04-22.01.05.txt:117999
//   ACK! Someone didn't follow you, you lost him.        (15 lines; a player unseen)
const QRegularExpression g_lost{
    QStringLiteral(R"(^ACK! (?<w>.+?) didn't follow you, you lost (?:him|her|it|them)\.$)")};
// An order refused, before the "Ok." for `order followers` and after it for `order one`:
//   You failed to control a mother eagle (one).      log-2005.11.07-15.57.10.txt:38029
//   You failed to control an orc prisoner (one).     log-2006.02.05-19.56.25.txt:170000
//   You failed to control Harle the Hobbit.          log-2006.01.04-22.01.05.txt:11825
const QRegularExpression g_failed{QStringLiteral(R"(^You failed to control (?<w>.+?)\.$)")};
// The other answers to `order`:
//   You have no loyal subjects here.                 log-2005.11.07-05.28.32.txt:16600
//   Order who to do what?                            log-2005.11.03-21.45.23.txt:2566 (after a
//                                                    bare `order followers`)
//   In your dreams, or what?                         log-2006.01.04-22.01.05.txt:99813 (after
//                                                    `order followers hit burly`; 501 lines in
//                                                    all, MUME's answer to most commands while
//                                                    asleep: pow/tmp/room.pow:17 has it for a
//                                                    scout)
//   Ok.                                              log-2005.11.07-15.57.10.txt:38030 (order),
//                                                    :37033 (after `label eagle one`)
//
//   A mother eagle (one) is now a group member.      log-2005.11.07-15.57.10.txt:37082
const QRegularExpression g_grouped{QStringLiteral(R"(^(?<w>.+?) is now a group member\.$)")};
// Deaths; the first two wordings are CombatLines' g_death:
//   A mother eagle (one) is dead! R.I.P.             log-2005.11.07-15.57.10.txt:43808
//   A pack horse (my) is dead! R.I.P.                log-2005.09.07-02.50.20.txt:2503
//   An enslaved shadow (one) disappears into nothing.  log-2006.02.09-19.48.52.txt:132543
const QRegularExpression g_died{QStringLiteral(
    R"(^(?<w>.+?) (?:is dead! R\.I\.P\.|has drawn (?:his|her|its) last breath! R\.I\.P\.|disappears into nothing\.)$)")};
// Coming along, which is all a follower that was left behind says when it follows again
// (log-2006.01.04-22.01.05.txt:117999 lost, :118004 seen, :118012 arrived):
//   A mother eagle (one) has arrived from the north. log-2005.11.07-15.57.10.txt:37049
//   A mother eagle has arrived from above.           log-2005.11.07-15.57.10.txt:37029
//   A pony (my) has suddenly arrived.                log-2005.08.31-14.00.43.txt:1980
const QRegularExpression g_arrived{QStringLiteral(
    R"(^(?<w>.+?) has (?:arrived from (?:the )?(?:north|east|south|west|above|below)|suddenly arrived)\.$)")};
//   An enslaved shadow (one) leaves north.           log-2006.02.06-18.58.18.txt:88997
const QRegularExpression g_went{
    QStringLiteral(R"(^(?<w>.+?) leaves (?:north|east|south|west|up|down)\.$)")};
// In the room's list:
//   A mother eagle (one) is standing here.           log-2005.11.07-15.57.10.txt:37167
//   An old man (one) is resting here.                log-2005.11.08-15.42.24.txt:180315
//   A mother eagle (one) is sleeping here.           log-2005.11.08-15.42.24.txt:129390
//   A raging bear (one) is here, fighting Nagash the Dark.  log-2006.01.04-22.01.05.txt:115891
//   An enslaved shadow (one), wielding a two-handed axe, is standing here.
//                                                    log-2006.02.06-18.58.18.txt:105450
const QRegularExpression g_seen{QStringLiteral(
    R"(^(?<w>.+?)(?:, [^,]+,)? is (?:(?:standing|resting|sitting|sleeping) here|here, fighting .+?)\.$)")};
// What is ridden is a mount:
//   You pick up a trained horse (my)'s reins, and start riding him.  log-2005.11.07-15.57.10.txt:3260
//   You pick up a hungry warg (my)'s reins, and start riding it.     (480 lines)
//   You stop riding a trained horse (my).            log-2005.11.07-15.57.10.txt:3218
//   You stop riding Gwaihir the Windlord.            log-2005.09.15-21.01.17.txt:100896
const QRegularExpression g_riding{
    QStringLiteral(R"(^You pick up (?<w>.+?)'s reins, and start riding (?:him|her|it)\.$)")};
const QRegularExpression g_rode{QStringLiteral(R"(^You stop riding (?<w>.+?)\.$)")};
// The player's own death, as CombatLines' g_youDead has it. What becomes of the followers is
// not in the logs; they are not where the character wakes.
const QRegularExpression g_youDead{QStringLiteral(R"(^You are dead!\s+Sorry\.\.\.$)")};
// Whom the player's character follows. MUME answers `follow` in sentences of its own: of the
// 279 `fol X` and 85 `fol me` typed in the logs none is answered "Ok.".
//   You now follow Grayelf.                          log-2005.09.02-23.20.17.txt:22833 (after
//                                                    `fol grayelf` :22832; 243 lines)
//   You now follow a black sorcerer.                 log-2005.11.03-21.45.23.txt:5134
//   You now follow Kazadoe (K).                      log-2005.12.12-16.09.18.txt:2077
const QRegularExpression g_youFollow{QStringLiteral(R"(^You now follow (?<w>.+?)\.$)")};
//   You stop following Zmej.                         log-2005.09.21-01.39.48.txt:29976 (after
//                                                    `fol me`; 186 lines: 59 after a typed
//                                                    follow, 74 after "X doesn't want you to
//                                                    follow him.", 44 before "You now follow Y.")
const QRegularExpression g_youStop{QStringLiteral(R"(^You stop following (?<w>.+?)\.$)")};
//   You will not follow anyone else now.             log-2005.11.03-21.45.23.txt:5425 (`fol me`
//                                                    alone), log-2005.09.21-01.39.48.txt:29977
//                                                    (after "You stop following Zmej."); 87 lines
// The move after a leader, 37091 lines of a player's bare name:
//   You follow Orhzul.                               log-2005.10.08-15.55.31.txt:32976
// Only a bare capitalised name is taken, never an article's: a room's description says "You
// follow the trail up to a hill." (log-2005.09.05-19.23.16.txt:72197) and "You follow a gentle
// slope downward to some shallow water." (log-2005.12.21-19.59.01.txt:26485). No label is
// ever shown here.
const QRegularExpression g_youWentAfter{
    QStringLiteral(R"(^You follow (?<w>\p{Lu}[\p{L}'\-]+)\.$)")};
// Sent away by the leader, "You stop following X." next, or refused at `follow` (the "!"):
//   Zmej doesn't want you to follow him.             log-2005.09.05-19.23.16.txt:42094 (71 lines)
//   Pimba doesn't want you to follow her.            log-2005.09.05-19.23.16.txt:67617 (3)
//   An old man doesn't want you to follow him!       log-2006.02.03-00.39.44.txt:109205 (2)
//   Sorry, but following in 'loops' is not allowed.  log-2005.09.05-19.23.16.txt:66214 (16)
const QRegularExpression g_followDenied{
    QStringLiteral(R"(^(?<w>.+?) doesn't want you to follow (?:him|her|it|them)[.!]$)")};
// Whom the character protects. `protect X` turns it on and off (84 typed in the logs, none
// answered "Ok."), a bare `protect` lists it, `protect self` ends it:
//   You will now try to protect Budach (B).          log-2005.09.15-21.01.17.txt:156 (after
//                                                    `protect b` :155; 67 lines)
//   You will no longer try to protect Kazadoe (k).   log-2005.11.17-17.17.33.txt:83438 (11)
//   You will try to protect:                         log-2005.12.19-20.51.29.txt:88011 (18)
//      Kazadoe (K)                                   :88012 (one name in each of the 18)
//   You aren't trying to protect anyone.             log-2006.01.08-19.15.21.txt:29266 (2)
//   Very well, you concentrate on your own health.   log-2005.11.17-17.17.33.txt:44529 (5; after
//                                                    it "You will now try to protect Kazadoe
//                                                    (k)." :44532 for one protected since :5534)
//   You can only protect those in your group.        log-2005.12.24-02.05.16.txt:130436 (3)
// Several can be protected at once: Kazadoe :5534 and Sisalik :13895 of log-2005.11.17-
// 17.17.33.txt, "i barelly will be able to protect both of you" :14930. No line says that
// somebody protects the player.
const QRegularExpression g_protects{QStringLiteral(R"(^You will now try to protect (?<w>.+?)\.$)")};
const QRegularExpression g_unprotects{
    QStringLiteral(R"(^You will no longer try to protect (?<w>.+?)\.$)")};
// Not read, being somebody else's orders: "Zmej issues the order 'bash'."
// (log-2005.10.06-18.57.04.txt:88248), "Farseer (F) gives an enslaved shadow (one) an order."
// (log-2006.02.06-18.58.18.txt:91239).

// The player's commands: `order followers assist` (pow/bn:25, pow/broga:8; 593 in the logs),
// `order one sleep` (log-2006.02.05-19.56.25.txt:169998), `order harle rest`
// (log-2006.01.04-22.01.05.txt:11823), a bare `order followers`
// (log-2005.11.03-21.45.23.txt:2565).
const QRegularExpression g_orderCommand{
    QStringLiteral(R"(^ord(?:e|er)?(?:\s+(?<who>\S+)(?:\s+(?<cmd>.+))?)?$)"),
    QRegularExpression::CaseInsensitiveOption};
// `label eagle one` (log-2005.11.07-15.57.10.txt:37032), `label 2.trained my`, `lab` in
// pow/tmp/cmd.pow:16. The one-word `label buh` (pow/binds.pow:3, 5068 in the logs) names
// nobody a follower could be told by, and is left alone.
const QRegularExpression g_labelCommand{
    QStringLiteral(R"(^lab(?:e|el)?\s+(?:(?<n>\d+)\.)?(?<who>\S+)\s+(?<label>\S+)$)"),
    QRegularExpression::CaseInsensitiveOption};

/// The mounts MUME's stables sell and the logs show led, by name without the article:
///   a trained horse (my)   2759 "starts following you"  log-2005.09.05-19.23.16.txt:46811
///   a pack horse (my)       951                         log-2005.08.31-14.00.43.txt:2421
///   a mountain mule (my)    906                         log-2005.11.07-05.28.32.txt:31969
///   a hungry warg (my)      407                         log-2005.11.01-00.23.09.txt:4194
///   a pony (my)              49                         log-2005.08.31-14.00.43.txt:1310
///   a horse (my)            "You stop riding"           log-2006.02.15-22.16.33.txt:23332
///   a warhorse, a horse of the Rohirrim: others'        log-2005.09.15-21.01.17.txt:6896, :11774
/// Anything else the player rides is learned from the riding lines.
const char *const g_mounts[] = {
    "trained horse",
    "pack horse",
    "mountain mule",
    "hungry warg",
    "pony",
    "horse",
    "warhorse",
    "horse of the Rohirrim",
};

struct NODISCARD Named final
{
    QString name;
    QString label;
};

/// "A mother eagle (one)" to "a mother eagle" and "one".
NODISCARD Named named(const QString &raw)
{
    Named result;
    result.name = raw.trimmed();
    if (const QRegularExpressionMatch m = g_label.match(result.name); m.hasMatch()) {
        result.label = m.captured(u"l") + m.captured(u"s");
        result.name = m.captured(u"n").trimmed();
    }
    if (g_capitalArticle.match(result.name).hasMatch()) {
        result.name[0] = result.name.at(0).toLower();
    }
    return result;
}

NODISCARD bool sameName(const QString &a, const QString &b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

/// "you" in the dark, or for one who is invisible: no name to keep a follower by.
NODISCARD bool isUnseen(const QString &name)
{
    return sameName(name, QStringLiteral("something")) || sameName(name, QStringLiteral("someone"));
}

NODISCARD FollowerLine lineOf(const FollowerLineEnum kind, const QString &who)
{
    const Named n = named(who);
    FollowerLine line;
    line.kind = kind;
    line.name = n.name;
    line.label = n.label;
    return line;
}

NODISCARD bool ended(const CharFollower &follower)
{
    return follower.state == FollowerStateEnum::LEFT || follower.state == FollowerStateEnum::DEAD;
}

} // namespace

std::string_view to_string_view(const FollowerKindEnum kind)
{
    switch (kind) {
    case FollowerKindEnum::CHARMIE:
        return "charmie";
    case FollowerKindEnum::MOUNT:
        return "mount";
    case FollowerKindEnum::SUMMONED:
        return "summoned";
    case FollowerKindEnum::UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

std::string_view to_string_view(const FollowerStateEnum state)
{
    switch (state) {
    case FollowerStateEnum::FOLLOWING:
        return "following";
    case FollowerStateEnum::REFUSING:
        return "refusing";
    case FollowerStateEnum::LOST:
        return "lost";
    case FollowerStateEnum::LEFT:
        return "left";
    case FollowerStateEnum::DEAD:
        return "dead";
    }
    return "following";
}

std::string_view to_string_view(const FollowerReplyEnum result)
{
    switch (result) {
    case FollowerReplyEnum::OK:
        return "ok";
    case FollowerReplyEnum::FAILED:
        return "failed";
    case FollowerReplyEnum::NONE_HERE:
        return "none-here";
    case FollowerReplyEnum::SYNTAX:
        return "syntax";
    case FollowerReplyEnum::ASLEEP:
        return "asleep";
    }
    return "ok";
}

bool isNpcName(const QString &name)
{
    return g_article.match(name).hasMatch();
}

FollowerKindEnum followerKindOf(const QString &name)
{
    if (!isNpcName(name)) {
        return FollowerKindEnum::UNKNOWN;
    }
    QString bare = name;
    bare.remove(g_article);
    for (const char *const mount : g_mounts) {
        if (bare == QLatin1String(mount)) {
            return FollowerKindEnum::MOUNT;
        }
    }
    // "As the corpse of a demon wolf crumbles, a shadow slowly rises from the dust." / "An
    // enslaved shadow now follows Farseer (F)." (log-2006.02.09-19.48.52.txt:133380-133381).
    if (bare.startsWith(QStringLiteral("enslaved "))) {
        return FollowerKindEnum::SUMMONED;
    }
    return FollowerKindEnum::CHARMIE;
}

std::optional<FollowerLine> parseFollowerLine(const QString &raw)
{
    QString line = raw.trimmed();
    line.remove(g_twiddlers);
    if (line.isEmpty()) {
        return std::nullopt;
    }

    using L = FollowerLineEnum;
    const auto bare = [](const L kind) {
        FollowerLine result;
        result.kind = kind;
        return result;
    };
    if (line == QStringLiteral("Ok.")) {
        return bare(L::OK);
    }
    if (line == QStringLiteral("You have no loyal subjects here.")) {
        return bare(L::NONE_HERE);
    }
    if (line == QStringLiteral("Order who to do what?")) {
        return bare(L::SYNTAX);
    }
    if (line == QStringLiteral("In your dreams, or what?")) {
        return bare(L::ASLEEP);
    }
    if (line == QStringLiteral("You will not follow anyone else now.")) {
        return bare(L::YOU_FOLLOW_NOBODY);
    }
    if (line == QStringLiteral("Sorry, but following in 'loops' is not allowed.")) {
        return bare(L::FOLLOW_DENIED);
    }
    if (line == QStringLiteral("You will try to protect:")) {
        return bare(L::PROTECT_LIST);
    }
    if (line == QStringLiteral("You aren't trying to protect anyone.")
        || line == QStringLiteral("Very well, you concentrate on your own health.")) {
        return bare(L::PROTECT_NONE);
    }
    if (line == QStringLiteral("You can only protect those in your group.")) {
        return bare(L::PROTECT_DENIED);
    }

    QRegularExpressionMatch m;
    if ((m = g_lost.match(line)).hasMatch()) {
        return lineOf(L::LOST, m.captured(u"w"));
    }
    if ((m = g_failed.match(line)).hasMatch()) {
        return lineOf(L::FAILED, m.captured(u"w"));
    }
    if ((m = g_riding.match(line)).hasMatch()) {
        return lineOf(L::RIDING, m.captured(u"w"));
    }
    if ((m = g_rode.match(line)).hasMatch()) {
        return lineOf(L::RODE, m.captured(u"w"));
    }
    if (g_youDead.match(line).hasMatch()) {
        return bare(L::YOU_DIED);
    }
    if ((m = g_youFollow.match(line)).hasMatch()) {
        return lineOf(L::YOU_FOLLOW, m.captured(u"w"));
    }
    if ((m = g_youStop.match(line)).hasMatch()) {
        return lineOf(L::YOU_STOP, m.captured(u"w"));
    }
    if ((m = g_youWentAfter.match(line)).hasMatch()) {
        return lineOf(L::YOU_WENT_AFTER, m.captured(u"w"));
    }
    if ((m = g_followDenied.match(line)).hasMatch()) {
        return lineOf(L::FOLLOW_DENIED, m.captured(u"w"));
    }
    if ((m = g_protects.match(line)).hasMatch()) {
        return lineOf(L::PROTECTS, m.captured(u"w"));
    }
    if ((m = g_unprotects.match(line)).hasMatch()) {
        return lineOf(L::UNPROTECTS, m.captured(u"w"));
    }
    const bool follows = (m = g_follows.match(line)).hasMatch();
    if (follows || (m = g_stops.match(line)).hasMatch()) {
        FollowerLine result = lineOf(follows ? L::FOLLOWS : L::STOPS, m.captured(u"w"));
        const Named leader = named(m.captured(u"l"));
        result.leader = sameName(leader.name, QStringLiteral("you")) ? QStringLiteral("you")
                                                                     : leader.name;
        return result;
    }
    if ((m = g_hates.match(line)).hasMatch()) {
        return lineOf(L::HATES, m.captured(u"w"));
    }
    if ((m = g_grouped.match(line)).hasMatch()) {
        return lineOf(L::GROUPED, m.captured(u"w"));
    }
    if ((m = g_died.match(line)).hasMatch()) {
        return lineOf(L::DIED, m.captured(u"w"));
    }
    if ((m = g_arrived.match(line)).hasMatch()) {
        return lineOf(L::ARRIVED, m.captured(u"w"));
    }
    if ((m = g_went.match(line)).hasMatch()) {
        return lineOf(L::WENT, m.captured(u"w"));
    }
    if ((m = g_seen.match(line)).hasMatch()) {
        return lineOf(L::SEEN, m.captured(u"w"));
    }
    return std::nullopt;
}

std::optional<FollowerCommand> parseFollowerCommand(const QString &input)
{
    const QString text = input.trimmed();
    QRegularExpressionMatch m;
    if ((m = g_orderCommand.match(text)).hasMatch()) {
        FollowerCommand command;
        command.type = FollowerCommand::TypeEnum::ORDER;
        command.who = m.captured(u"who").toLower();
        if (command.who.size() >= 3 && QStringLiteral("followers").startsWith(command.who)) {
            command.who = QStringLiteral("followers");
        }
        command.order = m.captured(u"cmd").trimmed();
        return command;
    }
    if ((m = g_labelCommand.match(text)).hasMatch()) {
        FollowerCommand command;
        command.type = FollowerCommand::TypeEnum::LABEL;
        command.who = m.captured(u"who").toLower();
        command.ordinal = m.captured(u"n").isEmpty() ? 1 : std::max(1, m.captured(u"n").toInt());
        command.label = m.captured(u"label");
        return command;
    }
    return std::nullopt;
}

CharFollowers lastingFollowers(const CharFollowers &change)
{
    CharFollowers result;
    for (const CharFollower &follower : change.followers) {
        if (!ended(follower)) {
            result.followers.push_back(follower);
        }
    }
    result.following = change.following;
    result.players = change.players;
    result.protecting = change.protecting;
    return result;
}

std::optional<FollowLeader> leaderOf(const CharFollowers &state)
{
    if (!state.following.isEmpty()) {
        return FollowLeader{state.following, false};
    }
    const bool led = !state.players.isEmpty()
                     || std::any_of(state.followers.begin(),
                                    state.followers.end(),
                                    [](const CharFollower &f) { return !ended(f); });
    if (led) {
        return FollowLeader{QString{}, true};
    }
    return std::nullopt;
}

void CharFollowersTracker::receiveCommand(const QString &input, const int64_t now)
{
    if (auto command = parseFollowerCommand(input)) {
        m_pending.push_back(Pending{std::move(*command), now, 0, false, false, std::nullopt, {}});
    }
}

CharFollower *CharFollowersTracker::find(const QString &name,
                                         const QString &label,
                                         const MatchEnum match)
{
    CharFollower *exact = nullptr;
    CharFollower *unlabelled = nullptr;
    CharFollower *first = nullptr;
    CharFollower *firstHere = nullptr;
    int ofThatName = 0;
    for (CharFollower &follower : m_followers) {
        if (!sameName(follower.name, name)) {
            continue;
        }
        ++ofThatName;
        if (follower.label == label && exact == nullptr) {
            exact = &follower;
        }
        if (follower.label.isEmpty() && unlabelled == nullptr) {
            unlabelled = &follower;
        }
        if (first == nullptr) {
            first = &follower;
        }
        if (follower.here && firstHere == nullptr) {
            firstHere = &follower;
        }
    }
    if (match == MatchEnum::ANY_LABEL) {
        // The one of that name that was with the player is the one that stayed behind.
        return firstHere != nullptr ? firstHere : first;
    }
    if (exact != nullptr || match == MatchEnum::EXACT) {
        return exact;
    }
    if (!label.isEmpty() && unlabelled != nullptr) {
        return unlabelled;
    }
    return (match == MatchEnum::CERTAIN && ofThatName == 1) ? first : nullptr;
}

CharFollower &CharFollowersTracker::add(CharFollower follower)
{
    if (m_followers.size() >= FOLLOWERS_TO_KEEP) {
        const auto oldest = std::find_if(m_followers.begin(),
                                         m_followers.end(),
                                         [](const CharFollower &f) {
                                             return f.state == FollowerStateEnum::LOST;
                                         });
        m_followers.erase(oldest == m_followers.end() ? m_followers.begin() : oldest);
    }
    m_followers.push_back(std::move(follower));
    return m_followers.back();
}

CharFollower *CharFollowersTracker::findByWord(const QString &word, const int ordinal)
{
    if (word.isEmpty()) {
        return nullptr;
    }
    int seen = 0;
    for (CharFollower &follower : m_followers) {
        if (!follower.here) {
            continue;
        }
        bool matches = follower.label.compare(word, Qt::CaseInsensitive) == 0;
        if (!matches) {
            // MUME takes the start of any word of the name: "eagle" and "moth" for "a mother
            // eagle", "harle" for "Harle the Hobbit".
            const QStringList words = follower.name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            matches = std::any_of(words.begin(), words.end(), [&word](const QString &w) {
                return w.startsWith(word, Qt::CaseInsensitive);
            });
        }
        if (matches && ++seen == ordinal) {
            return &follower;
        }
    }
    return nullptr;
}

FollowerKindEnum CharFollowersTracker::kindOf(const QString &name) const
{
    for (const QString &ridden : m_ridden) {
        if (sameName(ridden, name)) {
            return FollowerKindEnum::MOUNT;
        }
    }
    return followerKindOf(name);
}

CharFollowersTracker::Pending *CharFollowersTracker::firstOrder()
{
    // What was sent before it had its answer go by unread.
    while (!m_pending.empty() && m_pending.front().command.type != FollowerCommand::TypeEnum::ORDER) {
        m_pending.pop_front();
    }
    return m_pending.empty() ? nullptr : &m_pending.front();
}

CharFollowers CharFollowersTracker::take(std::optional<FollowerReply> reply)
{
    CharFollowers result;
    result.followers = m_followers;
    result.reply = std::move(reply);
    result.following = m_following;
    result.players = m_players;
    result.protecting = m_protecting;
    m_followers.erase(std::remove_if(m_followers.begin(), m_followers.end(), ended),
                      m_followers.end());
    return result;
}

bool CharFollowersTracker::endProtectList()
{
    if (!m_protectListing) {
        return false;
    }
    m_protectListing = false;
    // MUME says "You aren't trying to protect anyone." for nobody, so a list without a name
    // is one that was not read, and tells nothing.
    if (m_protectListed.isEmpty()
        || (m_protecting.has_value() && *m_protecting == m_protectListed)) {
        return false;
    }
    m_protecting = m_protectListed;
    return true;
}

std::optional<CharFollowers> CharFollowersTracker::receiveLine(const QString &text,
                                                               const int64_t now)
{
    bool listed = false;
    if (m_protectListing) {
        // "   Kazadoe (K)": the names of the list are indented; anything else ends it.
        if (!text.isEmpty() && text.at(0).isSpace() && !text.trimmed().isEmpty()) {
            const Named n = named(text);
            if (!m_protectListed.contains(n.name, Qt::CaseInsensitive)) {
                m_protectListed.append(n.name);
            }
            return std::nullopt;
        }
        listed = endProtectList();
    }
    std::optional<CharFollowers> result = readLine(text, now);
    if (!result.has_value() && listed) {
        result = take(std::nullopt);
    }
    return result;
}

std::optional<CharFollowers> CharFollowersTracker::readLine(const QString &text, const int64_t now)
{
    const std::optional<FollowerLine> parsed = parseFollowerLine(text);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    const FollowerLine &line = *parsed;
    using L = FollowerLineEnum;
    using S = FollowerStateEnum;

    // Takes the label a line showed, and says whether that changed anything.
    const auto relabel = [&line](CharFollower &follower) {
        if (line.label.isEmpty() || follower.label == line.label) {
            return false;
        }
        follower.label = line.label;
        return true;
    };

    switch (line.kind) {
    case L::OK: {
        if (m_pending.empty()) {
            return std::nullopt;
        }
        Pending &front = m_pending.front();
        if (front.command.type == FollowerCommand::TypeEnum::ORDER) {
            // The prompt ends the answer: a refusal may still come ("Ok." / "You failed to
            // control an orc prisoner (one).").
            front.answered = true;
            front.ok = true;
            return std::nullopt;
        }
        const FollowerCommand command = front.command;
        m_pending.pop_front();
        CharFollower *const follower = findByWord(command.who, command.ordinal);
        if (follower == nullptr || follower->label == command.label) {
            return std::nullopt;
        }
        follower->label = command.label;
        return take(std::nullopt);
    }
    case L::NONE_HERE:
    case L::SYNTAX:
        // Only an order is answered so.
        if (Pending *const order = firstOrder()) {
            order->answered = true;
            order->result = line.kind == L::SYNTAX ? FollowerReplyEnum::SYNTAX
                                                   : FollowerReplyEnum::NONE_HERE;
        }
        return std::nullopt;
    case L::ASLEEP:
        // Any command is answered so by one asleep: the oldest waiting had this answer.
        if (!m_pending.empty()) {
            Pending &front = m_pending.front();
            if (front.command.type == FollowerCommand::TypeEnum::ORDER) {
                front.answered = true;
                front.result = FollowerReplyEnum::ASLEEP;
            } else {
                m_pending.pop_front();
            }
        }
        return std::nullopt;
    case L::RIDING:
    case L::RODE: {
        if (!isUnseen(line.name)) {
            m_ridden.removeAll(line.name);
            m_ridden.append(line.name);
            while (m_ridden.size() > RIDDEN_TO_KEEP) {
                m_ridden.removeFirst();
            }
            return std::nullopt;
        }
        if (line.kind != L::RIDING) {
            return std::nullopt;
        }
        // "Something stops following you." / "You pick up Something's reins, and start riding
        // him." (logs/archives/log-2005.11.07-15.57.10.txt:3608-3609): mounted in the dark.
        // With one mount led, that is the one, and it is led no longer.
        CharFollower *led = nullptr;
        for (CharFollower &follower : m_followers) {
            if (follower.kind == FollowerKindEnum::MOUNT && follower.here
                && follower.state == S::FOLLOWING) {
                if (led != nullptr) {
                    return std::nullopt;
                }
                led = &follower;
            }
        }
        if (led == nullptr) {
            return std::nullopt;
        }
        led->state = S::LEFT;
        return take(std::nullopt);
    }
    case L::YOU_DIED: {
        // The character wakes elsewhere, without them. Whether a bond outlasts that is not
        // known, so they are left behind rather than gone. No answer is coming either.
        m_pending.clear();
        bool any = false;
        for (CharFollower &follower : m_followers) {
            if (follower.here || follower.state != S::LOST) {
                follower.here = false;
                follower.state = S::LOST;
                any = true;
            }
        }
        return any ? std::optional<CharFollowers>{take(std::nullopt)} : std::nullopt;
    }
    case L::YOU_STOP:
    case L::YOU_FOLLOW_NOBODY:
        // Whoever the line names: the character follows nobody now.
        if (m_following.isEmpty()) {
            return std::nullopt;
        }
        m_following.clear();
        return take(std::nullopt);
    case L::YOU_FOLLOW:
    case L::YOU_WENT_AFTER:
        // "You now follow someone." names nobody; "You stop following X." came before it, so
        // nothing stale is kept.
        if (isUnseen(line.name) || sameName(m_following, line.name)) {
            return std::nullopt;
        }
        m_following = line.name;
        return take(std::nullopt);
    case L::FOLLOW_DENIED:
    case L::PROTECT_DENIED:
        return std::nullopt;
    case L::PROTECT_LIST:
        m_protectListing = true;
        m_protectListed.clear();
        return std::nullopt;
    case L::PROTECT_NONE:
        if (m_protecting.has_value() && m_protecting->isEmpty()) {
            return std::nullopt;
        }
        m_protecting = QStringList{};
        return take(std::nullopt);
    case L::PROTECTS:
    case L::UNPROTECTS: {
        const bool known = m_protecting.has_value();
        if (!known) {
            m_protecting = QStringList{};
        }
        const bool has = m_protecting->contains(line.name, Qt::CaseInsensitive);
        if (line.kind == L::PROTECTS && !has) {
            m_protecting->append(line.name);
        } else if (line.kind == L::UNPROTECTS && has) {
            m_protecting->removeIf(
                [&line](const QString &name) { return sameName(name, line.name); });
        } else if (known) {
            return std::nullopt;
        }
        return take(std::nullopt);
    }
    default:
        break;
    }

    // The rest name somebody. In the dark that is "Something": no name to go by.
    if (isUnseen(line.name)) {
        return std::nullopt;
    }
    const auto dropPlayer = [this, &line]() {
        return m_players.removeIf(
                   [&line](const QString &name) { return sameName(name, line.name); })
               > 0;
    };
    if ((line.kind == L::FOLLOWS || line.kind == L::STOPS) && line.leader != QStringLiteral("you")) {
        // "Zmej now follows Rhuka." (log-2005.09.21-01.39.48.txt:29980): one who follows
        // somebody else follows the player no longer.
        if (line.kind == L::FOLLOWS && dropPlayer()) {
            return take(std::nullopt);
        }
        return std::nullopt;
    }

    MatchEnum match = MatchEnum::EXACT;
    if (line.kind == L::LOST) {
        match = MatchEnum::ANY_LABEL;
    } else if (line.kind == L::FOLLOWS) {
        match = MatchEnum::ADOPT;
    } else if (line.kind == L::STOPS || line.kind == L::FAILED) {
        match = MatchEnum::CERTAIN;
    }
    CharFollower *known = find(line.name, line.label, match);
    bool changed = false;
    switch (line.kind) {
    case L::FOLLOWS:
        if (known == nullptr) {
            // A player who starts following is no follower of this kind, and a name with no
            // article is a player's as far as the line tells; a mount seen ridden is one
            // whatever its name ("Gwaihir the Windlord").
            const FollowerKindEnum kind = kindOf(line.name);
            if (kind == FollowerKindEnum::UNKNOWN) {
                // Kept by name among the players that follow.
                if (m_players.contains(line.name, Qt::CaseInsensitive)) {
                    return std::nullopt;
                }
                m_players.append(line.name);
                while (m_players.size() > PLAYERS_TO_KEEP) {
                    m_players.removeFirst();
                }
                return take(std::nullopt);
            }
            // Taken for a player by its name before it was seen ridden.
            std::ignore = dropPlayer();
            CharFollower follower;
            follower.name = line.name;
            follower.label = line.label;
            follower.kind = kind;
            follower.since = now;
            std::ignore = add(std::move(follower));
        } else {
            // The bond made anew.
            std::ignore = relabel(*known);
            known->here = true;
            known->state = S::FOLLOWING;
            known->since = now;
        }
        changed = true;
        break;
    case L::STOPS:
    case L::HATES:
    case L::DIED:
        if (known == nullptr) {
            // "Budach (B) stops following you.": a player who followed.
            if (line.kind == L::STOPS && dropPlayer()) {
                return take(std::nullopt);
            }
            return std::nullopt;
        }
        std::ignore = relabel(*known);
        known->state = line.kind == L::DIED ? S::DEAD : S::LEFT;
        changed = true;
        break;
    case L::LOST:
        if (known == nullptr || (known->state == S::LOST && !known->here)) {
            return std::nullopt;
        }
        known->state = S::LOST;
        known->here = false;
        changed = true;
        break;
    case L::FAILED: {
        if (known == nullptr) {
            // Its bond was not seen made, but only a follower refuses an order. One taken for
            // a player by its name ("Harle the Hobbit") was none.
            std::ignore = dropPlayer();
            CharFollower follower;
            follower.name = line.name;
            follower.label = line.label;
            follower.kind = kindOf(line.name);
            known = &add(std::move(follower));
        }
        std::ignore = relabel(*known);
        known->here = true;
        known->state = S::REFUSING;
        if (Pending *const order = firstOrder()) {
            // Told with the answer, at the prompt.
            order->answered = true;
            order->failed.append(known->name);
            known->lastRefused = order->command.order;
            return std::nullopt;
        }
        // No order of the player's is waiting: one given before MMapper was watching, or by
        // a way it does not see.
        changed = true;
        break;
    }
    case L::GROUPED:
    case L::ARRIVED:
    case L::SEEN:
        if (known == nullptr) {
            return std::nullopt;
        }
        changed = relabel(*known);
        if (!known->here) {
            known->here = true;
            changed = true;
        }
        if (known->state == S::LOST) {
            // Back with the player: it follows again, and says no more than that it arrived.
            known->state = S::FOLLOWING;
            changed = true;
        }
        break;
    case L::WENT:
        if (known == nullptr) {
            return std::nullopt;
        }
        changed = relabel(*known);
        if (known->here) {
            known->here = false;
            changed = true;
        }
        break;
    default:
        break;
    }
    if (!changed) {
        return std::nullopt;
    }
    return take(std::nullopt);
}

std::optional<CharFollowers> CharFollowersTracker::receivePrompt(const int64_t now)
{
    std::optional<CharFollowers> result;
    const bool listed = endProtectList();
    if (!m_pending.empty() && m_pending.front().answered) {
        const Pending done = m_pending.front();
        m_pending.pop_front();

        FollowerReply reply;
        reply.order = done.command.order;
        reply.who = done.command.who;
        reply.failed = done.failed;
        if (done.result.has_value()) {
            reply.result = *done.result;
        } else if (!done.failed.isEmpty()) {
            reply.result = FollowerReplyEnum::FAILED;
        } else {
            reply.result = FollowerReplyEnum::OK;
        }

        if (done.ok) {
            // "Ok.": the order was given, and those that said nothing against it took it.
            const auto obeys = [&done](CharFollower &follower) {
                if (!follower.here || ended(follower) || done.failed.contains(follower.name)) {
                    return;
                }
                follower.lastOrder = done.command.order;
                if (follower.state == FollowerStateEnum::REFUSING) {
                    follower.state = FollowerStateEnum::FOLLOWING;
                }
            };
            if (done.command.who == QStringLiteral("followers")) {
                for (CharFollower &follower : m_followers) {
                    obeys(follower);
                }
            } else if (CharFollower *const one = findByWord(done.command.who, 1)) {
                obeys(*one);
            }
        }
        result = take(std::move(reply));
    }

    for (Pending &p : m_pending) {
        ++p.prompts;
    }
    m_pending.erase(std::remove_if(m_pending.begin(),
                                   m_pending.end(),
                                   [now](const Pending &p) {
                                       return !p.answered && p.prompts > PROMPTS_TO_WAIT
                                              && now - p.sent > SECONDS_TO_WAIT;
                                   }),
                    m_pending.end());
    if (!result.has_value() && listed) {
        result = take(std::nullopt);
    }
    return result;
}

void CharFollowersTracker::reset()
{
    m_followers.clear();
    m_following.clear();
    m_players.clear();
    m_protecting.reset();
    m_protectListing = false;
    m_protectListed.clear();
    m_pending.clear();
    m_ridden.clear();
}
