// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CharRefused.h"

#include <QRegularExpression>
#include <QStringList>

namespace {

struct NODISCARD RefusedRow final
{
    QRegularExpression pattern;
    /// Empty when the line answers several commands.
    const char *action;
    const char *reason;
    /// What is missing, for a line that names it in a word of its own ("shield"); else the
    /// pattern's group "t" is the target.
    const char *target;
};

NODISCARD RefusedRow row(const char *const pattern,
                         const char *const action,
                         const char *const reason,
                         const char *const target = "")
{
    return RefusedRow{QRegularExpression{QString::fromUtf8(pattern)}, action, reason, target};
}

// Sources: the powwow logs under /home/aza/data/powwow/logs/archives, as file:line with the
// directory left off, and the count of whole lines in all 364 logs (2005-2006); "M row" is
// mume3d's docs/data/combat-help-matrix.tsv (2013-2026 logs). The command a line followed is
// from mume3d's docs/action-rules.md, sections 1.2-1.4.
const RefusedRow g_rows[] = {
    // -- already so ------------------------------------------------------------------------
    // 6,972; log-2005.08.31-14.00.43.txt:1399. M row 80.
    row(R"(^You are already standing\.$)", "stand", "already"),
    // 521; log-2005.09.05-19.23.16.txt:1436. M row 82.
    row(R"(^You are already resting\.$)", "rest", "already"),
    // 42; log-2005.09.05-19.23.16.txt:5773.
    row(R"(^You are already sound asleep\.$)", "sleep", "already"),
    // 1,496; log-2005.09.05-19.23.16.txt:2142. M row 83.
    row(R"(^You are already awake\.\.\.$)", "wake", "already"),
    // 76; log-2005.09.05-19.23.16.txt:38128. M row 244.
    row(R"(^You are already riding\.$)", "ride", "already"),
    // 1,700; log-2005.08.31-14.00.43.txt:1615 (the `dism` of his `lead;dism` after the lead
    // had dismounted him).
    row(R"(^You are not riding\.$)", "dismount", "already"),
    // 326; log-2005.08.31-14.00.43.txt:804. M row 45.
    row(R"(^You are already attempting to flee!$)", "flee", "already"),
    // 132; "A pony is already following you!" log-2005.09.05-19.23.16.txt:280 (after `lead`).
    row(R"(^(?<t>.+?) is already following you!$)", "lead", "already"),
    // 460; log-2005.09.15-21.01.17.txt:22647. M row 281.
    row(R"(^Bash someone already bashed\? Aren't we funny\?$)", "bash", "already"),

    // -- the position, the fight ---------------------------------------------------------------
    // 19; log-2005.09.15-21.01.17.txt:90756.
    row(R"(^Rest while fighting\? Are you MAD\?$)", "rest", "fighting"),
    // 2; log-2006.02.05-19.56.25.txt:67907.
    row(R"(^Sleep while fighting\? Are you MAD\?$)", "sleep", "fighting"),
    // 4; log-2005.11.08-15.42.24.txt:50038: a magical sleep (Char.Vitals `slept`).
    row(R"(^You can't wake up!$)", "wake", "slept"),
    // 42; log-2005.09.05-19.23.16.txt:1003 (after `cast`, sitting).
    row(R"(^You can't do this sitting!$)", "cast", "position"),
    // 15; log-2005.09.21-01.39.48.txt:28473: the player is the one being fought.
    row(R"(^Your opponent won't stop this fight just like that\.$)", "disengage", "fighting"),
    // 21; log-2005.09.05-19.23.16.txt:106747. Today's "You are not fighting." (M row 35) is
    // MMapper.Combat.Event refused not-fighting.
    row(R"(^Disengage what\? You must be fighting!$)", "disengage", "not-fighting"),
    // 44; log-2005.09.15-21.01.17.txt:45845. M row 285.
    row(R"(^You cannot bash someone you aren't fighting!$)", "bash", "not-fighting"),
    // M row 302 (n=2); in none of his logs, where nobody kicks.
    row(R"(^You cannot kick someone you aren't fighting!$)", "kick", "not-fighting"),
    // 4; log-2006.05.02-16.20.44.txt:6970 (after `as <name>`; the rescue's "But nobody is
    // fighting him?" with a question mark is MMapper.Combat.Event rescue refused).
    row(R"(^But nobody is fighting (?:him|her|it|them)!$)", "assist", "not-fighting"),
    // 107; log-2005.09.15-21.01.17.txt:28241 (a bare `rescue` by the one attacked).
    row(R"(^What about fleeing instead\?$)", "rescue", "self"),

    // -- MUME keeps the command ----------------------------------------------------------------
    // 227; log-2005.08.31-14.00.43.txt:2451: typed while bashed or busy; the flee follows.
    row(R"(^You will attempt to flee!$)", "flee", "queued"),
    // 43; log-2005.09.05-19.23.16.txt:81106 (after rem, wear, whe, pick in a fight).
    row(R"(^You are too busy right now!$)", "", "busy"),

    // -- nobody, nothing ---------------------------------------------------------------------
    // 510; log-2005.09.15-21.01.17.txt:3237. M row 297. And the other order of the words,
    // 18; log-2005.10.16-23.42.44.txt:45960.
    row(R"(^Bash (?:what or whom|whom or what)\?$)", "bash", "no-target"),
    // 1 each; log-2006.01.06-20.57.51.txt:51979, log-2006.08.28-17.47.08.txt:42817.
    row(R"(^Kick whom\?$)", "kick", "no-target"),
    row(R"(^Hit whom\?$)", "kill", "no-target"),
    // 49; log-2005.09.15-21.01.17.txt:42856 (a `lead` with no mount there).
    row(R"(^Lead what\?$)", "lead", "no-target"),
    // 15,686; log-2005.08.31-14.00.43.txt:4582 (after kill, cast, bash: any command at a
    // character not in the room; M row 56). Today's wordings, "Nobody here by that name." and
    // "You don't see any X here.", are MMapper.Combat.Event refused no-target.
    row(R"(^They aren't here\.$)", "", "no-target"),
    // 192; log-2005.09.05-19.23.16.txt:58262 (after `ride my` 15 times).
    row(R"(^No one here by that name\.$)", "", "no-target"),
    // 14; log-2005.09.21-01.39.48.txt:19820.
    row(R"(^No one responds to your commanding voice\.$)", "order", "no-target"),

    // -- no room ----------------------------------------------------------------------------
    // 23; log-2005.09.15-21.01.17.txt:54684.
    row(R"(^Alas! You have no fighting space left!$)", "rescue", "no-space"),
    // 36; "... to target her safely!" log-2005.10.06-18.57.04.txt:123771.
    row(R"(^Alas! The melee is too dense to target (?:him|her|it|them) safely!$)", "kill",
        "no-space"),

    // -- the character -----------------------------------------------------------------------
    // 207; log-2005.08.31-14.00.43.txt:1353 (after kill 100, cast 48, as 19, bash 5): what makes
    // the character afraid is not known.
    row(R"(^You are too afraid\.$)", "", "afraid"),
    // 5; log-2005.12.11-20.24.40.txt:41778.
    row(R"(^Alas, your lips are sealed!$)", "cast", "silenced"),
    // 46; log-2005.11.01-00.23.09.txt:11609 (45 after a `cast`; it answers any skill).
    row(R"(^Sorry, you can't do that, you don't have any idea about it!$)", "", "no-skill"),
    // 2; log-2006.05.20-01.28.11.txt:61399. M row 286.
    row(R"(^Maybe learning how to bash would help\?$)", "bash", "no-skill"),
    // 1; log-2006.02.14-00.57.13.txt:68916.
    row(R"(^Perhaps you should learn the art of backstabbing\.\.\.$)", "backstab", "no-skill"),
    // 17; log-2006.01.06-20.57.51.txt:71429 (16 after `bash`).
    row(R"(^You need a shield to do that successfully\.$)", "bash", "no-item", "shield"),
    // 6; log-2005.09.05-19.23.16.txt:51404 (butcher, scalp).
    row(R"(^You need a knife to do that\.$)", "butcher", "no-item", "knife"),
    // 1; log-2005.12.11-20.24.40.txt:101142.
    row(R"(^You need a lance and a mount to charge\.$)", "charge", "no-item", "lance"),
    // 2; log-2006.02.12-20.44.23.txt:137651.
    row(R"(^You need a whetstone in your equipment\.$)", "whet", "no-item", "whetstone"),

    // -- the mount ---------------------------------------------------------------------------
    // 62; log-2005.09.07-02.50.20.txt:15394 (after a move, riding). In no other reader. The
    // three other wordings ("Oops! You cannot go there riding!" 3,891,
    // log-2005.08.31-14.00.43.txt:1305; today's "You cannot ride there." M row 242, and "OOPS!
    // You cannot go there while riding!") are also MMapper.Combat.Event refused cannot-ride:
    // the one line that is two events, so that this package has the ride refused with its side.
    row(R"(^It's too difficult to ride here\.$)", "move", "noride"),
    row(R"(^(?:Oops! You cannot go there riding|OOPS! You cannot go there while riding)!$)", "move",
        "noride"),
    row(R"(^You cannot ride there\.$)", "move", "noride"),
    // 14; log-2005.12.19-20.51.29.txt:75928.
    row(R"(^You don't control your mount!$)", "move", "no-control"),
    // 13; "A pack horse doesn't want to follow you!" log-2005.11.07-05.28.32.txt:10776.
    row(R"(^(?<t>.+?) doesn't want to follow you!$)", "lead", "unwilling"),
    // 19; log-2006.02.03-00.39.44.txt:113597.
    row(R"(^You cannot camp while riding\.$)", "camp", "riding"),

    // -- the place ---------------------------------------------------------------------------
    // 29; "... past a huge stone giant." log-2005.08.31-14.00.43.txt:13462.
    row(R"(^You are unable to manoeuvre past (?<t>.+)\.$)", "move", "guarded"),
    // 174; "... while the crack is in motion." log-2005.09.05-19.23.16.txt:30353. M row 162.
    row(R"(^You can't proceed while the (?<t>[a-z' -]+?) (?:is|are) in motion\.$)", "move",
        "door-moving"),
    // 8; log-2005.09.05-19.23.16.txt:66914 (after `open exit w` at the Unqalome door).
    row(R"(^The ice layer is too thick and prevents you from reaching it\.$)", "open", "door-iced"),
    // 9; "... the trapdoor ..." log-2005.10.27-01.19.35.txt:7114 (a break door cast).
    row(R"(^The will blocking the (?<t>[a-z' -]+?) resisted your spell\.$)", "cast",
        "door-blocked"),
    // 122; log-2005.08.31-14.00.43.txt:8573: what a look, and a room entered, shows in the dark.
    row(R"(^It is pitch black\.\.\.$)", "look", "dark"),
    // 3; log-2005.11.07-15.57.10.txt:79367 (after cast 2, say 1).
    row(R"(^You can't do that under water!$)", "", "water"),
    // 45; log-2005.09.21-01.39.48.txt:13688.
    row(R"(^Tracking in water is not possible\.$)", "track", "water"),
    // 9; log-2005.09.21-01.39.48.txt:62112.
    row(R"(^You fail to find tracks on the stone floor\.$)", "track", "not-here"),
    // 4; log-2006.02.09-19.48.52.txt:134955.
    row(R"(^You cannot camp in the city\.$)", "camp", "not-here"),
    // 314; log-2005.08.31-14.00.43.txt:5990.
    row(R"(^You cannot guess the time indoors\.$)", "time", "not-here"),
    // 39; log-2005.09.21-01.39.48.txt:51268 (rent, buy, offer, list with no keeper there).
    row(R"(^Sorry, but you cannot do that here!$)", "", "not-here"),
    // M row 153 (n=4): a place that holds the character; in none of his logs.
    row(R"(^You are unable to leave this place\.$)", "", "not-here"),
};

// The twiddlers a delayed command draws end up on the front of the line that ends it.
const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};

NODISCARD QString actionWord(const ContainerActionEnum action)
{
    switch (action) {
    case ContainerActionEnum::OPEN:
        return QStringLiteral("open");
    case ContainerActionEnum::CLOSE:
        return QStringLiteral("close");
    case ContainerActionEnum::UNLOCK:
        return QStringLiteral("unlock");
    case ContainerActionEnum::LOCK:
        return QStringLiteral("lock");
    case ContainerActionEnum::PICK:
        return QStringLiteral("pick");
    case ContainerActionEnum::LOOK:
    case ContainerActionEnum::GET:
    case ContainerActionEnum::PUT:
        break;
    }
    return QString{};
}

} // namespace

std::optional<CharRefused> parseRefusedLine(const QString &raw)
{
    QString line = raw.trimmed();
    line.remove(g_twiddlers);
    for (const RefusedRow &r : g_rows) {
        const auto m = r.pattern.match(line);
        if (!m.hasMatch()) {
            continue;
        }
        CharRefused refused;
        refused.action = QString::fromLatin1(r.action);
        refused.reason = QString::fromLatin1(r.reason);
        refused.text = line;
        refused.target = m.hasCaptured(u"t") ? m.captured(u"t") : QString::fromLatin1(r.target);
        return refused;
    }
    return std::nullopt;
}

std::optional<CharRefused> refusedFromDoorReply(const DoorReply &reply)
{
    using R = ContainerResultEnum;
    using A = ContainerActionEnum;
    const char *reason = nullptr;
    const char *target = "";
    switch (reply.result) {
    case R::LOCKED:
        // "It seems to be locked." after `open exit e` (383 lines,
        // log-2006.08.03-19.24.37.txt:18504-18505) and "It's already locked!" after `lock`
        // (224, log-2006.02.14-00.57.13.txt:54284-54287). A "*click*" that locked is no refusal.
        if (reply.command.action == A::OPEN) {
            reason = "door-locked";
        } else if (reply.text.startsWith(QStringLiteral("It's already"))) {
            reason = "already";
        }
        break;
    case R::UNLOCKED:
        // "It's already unlocked, it seems." (123, log-2006.05.15-18.37.20.txt:82344-82346).
        if (reply.text.startsWith(QStringLiteral("It's already"))) {
            reason = "already";
        }
        break;
    case R::ALREADY_OPEN:
        // "It's already open!" (2,149, log-2005.11.02-01.27.58.txt:23486-23487).
        reason = "already";
        break;
    case R::ALREADY_CLOSED:
        // "It's already closed!" (5,643, log-2005.09.07-02.50.20.txt:67604-67606).
        reason = "already";
        break;
    case R::NO_KEY:
        // "You do not have the proper key for that." (61, log-2006.03.14-21.39.15.txt:27484-27485).
        reason = "no-item";
        target = "key";
        break;
    case R::NOT_FOUND:
        // "You don't see any exit there." (1,021, log-2005.08.31-14.00.43.txt:2985).
        reason = "not-here";
        break;
    case R::PICKPROOF:
        reason = "unknown";
        break;
    case R::CANNOT:
        // "That's impossible, I'm afraid." after `open exit e` (214,
        // log-2005.10.04-03.05.23.txt:46509-46510; after a block door,
        // log-2006.04.19-23.32.31.txt:88306-88310); "That's absurd." after `close` at a door
        // a spell broke (log-2006.02.09-00.15.52.txt:46226-46232).
        reason = reply.text == QStringLiteral("That's impossible, I'm afraid.")
                         && reply.command.action == A::OPEN
                     ? "door-blocked"
                     : "unknown";
        break;
    case R::OPENED:
    case R::CLOSED:
    case R::KEY_BROKE:
    case R::PICKING:
    case R::PICKED:
    case R::PICK_FAILED:
    case R::PICK_STOPPED:
    case R::EMPTY:
    case R::CONTENTS:
        break;
    }
    if (reason == nullptr) {
        return std::nullopt;
    }
    CharRefused refused;
    refused.action = actionWord(reply.command.action);
    refused.reason = QString::fromLatin1(reason);
    refused.text = reply.text;
    refused.target = QString::fromLatin1(target);
    refused.dir = reply.command.direction;
    return refused;
}

QStringList refusedReasons()
{
    QStringList reasons;
    for (const RefusedRow &r : g_rows) {
        if (const QString reason = QString::fromLatin1(r.reason); !reasons.contains(reason)) {
            reasons.append(reason);
        }
    }
    for (const char *const reason : {"door-locked", "unknown"}) {
        if (!reasons.contains(QLatin1String(reason))) {
            reasons.append(QString::fromLatin1(reason));
        }
    }
    reasons.sort();
    return reasons;
}
