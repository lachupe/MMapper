// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestRefusedDoors.h"

#include "../src/parser/CharRefused.h"
#include "../src/parser/CombatLines.h"
#include "../src/parser/RoomDoors.h"

#include <optional>

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

// The lines are MUME's own, from the powwow logs under /home/aza/data/powwow/logs/archives; each
// row names where it was found, with that directory left off. "M row" is a row of mume3d's
// docs/data/combat-help-matrix.tsv, for a wording of today that his logs do not have.

namespace {

using S = DoorStateEnum;
using K = DoorLineEnum;
using A = ContainerActionEnum;
using R = ContainerResultEnum;

struct NODISCARD RefusedCase final
{
    const char *line;
    const char *action;
    const char *reason;
    const char *target;
    /// True for the one kind of line that is also an MMapper.Combat.Event: a ride refused.
    bool alsoCombat;
    const char *source;
};

const RefusedCase g_refused[] = {
    // already so
    {"You are already standing.", "stand", "already", "", false,
     "log-2005.08.31-14.00.43.txt:1399"},
    {"You are already resting.", "rest", "already", "", false, "log-2005.09.05-19.23.16.txt:1436"},
    {"You are already sound asleep.", "sleep", "already", "", false,
     "log-2005.09.05-19.23.16.txt:5773"},
    {"You are already awake...", "wake", "already", "", false, "log-2005.09.05-19.23.16.txt:2142"},
    {"You are already riding.", "ride", "already", "", false, "log-2005.09.05-19.23.16.txt:38128"},
    {"You are not riding.", "dismount", "already", "", false, "log-2005.08.31-14.00.43.txt:1615"},
    {"You are already attempting to flee!", "flee", "already", "", false,
     "log-2005.08.31-14.00.43.txt:804"},
    {"A pony is already following you!", "lead", "already", "A pony", false,
     "log-2005.09.05-19.23.16.txt:280"},
    {"A pack horse (my) is already following you!", "lead", "already", "A pack horse (my)",
     false, "log-2005.10.08-15.55.31.txt:4133"},
    {"Bash someone already bashed? Aren't we funny?", "bash", "already", "", false,
     "log-2005.09.15-21.01.17.txt:22647"},
    // the position, the fight
    {"Rest while fighting? Are you MAD?", "rest", "fighting", "", false,
     "log-2005.09.15-21.01.17.txt:90756"},
    {"Sleep while fighting? Are you MAD?", "sleep", "fighting", "", false,
     "log-2006.02.05-19.56.25.txt:67907"},
    {"You can't wake up!", "wake", "slept", "", false, "log-2005.11.08-15.42.24.txt:50038"},
    {"You can't do this sitting!", "cast", "position", "", false,
     "log-2005.09.05-19.23.16.txt:1003"},
    {"Your opponent won't stop this fight just like that.", "disengage", "fighting", "", false,
     "log-2005.09.21-01.39.48.txt:28473"},
    {"Disengage what? You must be fighting!", "disengage", "not-fighting", "", false,
     "log-2005.09.05-19.23.16.txt:106747"},
    {"You cannot bash someone you aren't fighting!", "bash", "not-fighting", "", false,
     "log-2005.09.15-21.01.17.txt:45845"},
    {"You cannot kick someone you aren't fighting!", "kick", "not-fighting", "", false,
     "M row 302"},
    {"But nobody is fighting him!", "assist", "not-fighting", "", false,
     "log-2006.05.02-16.20.44.txt:6970"},
    {"What about fleeing instead?", "rescue", "self", "", false,
     "log-2005.09.15-21.01.17.txt:28241"},
    // MUME keeps the command
    {"You will attempt to flee!", "flee", "queued", "", false, "log-2005.08.31-14.00.43.txt:2451"},
    {"You are too busy right now!", "", "busy", "", false, "log-2005.09.05-19.23.16.txt:81106"},
    // nobody, nothing
    {"Bash what or whom?", "bash", "no-target", "", false, "log-2005.09.15-21.01.17.txt:3237"},
    {"Bash whom or what?", "bash", "no-target", "", false, "log-2005.10.16-23.42.44.txt:45960"},
    {"Kick whom?", "kick", "no-target", "", false, "log-2006.01.06-20.57.51.txt:51979"},
    {"Hit whom?", "kill", "no-target", "", false, "log-2006.08.28-17.47.08.txt:42817"},
    {"Lead what?", "lead", "no-target", "", false, "log-2005.09.15-21.01.17.txt:42856"},
    {"They aren't here.", "", "no-target", "", false, "log-2005.08.31-14.00.43.txt:4582"},
    {"No one here by that name.", "", "no-target", "", false,
     "log-2005.09.05-19.23.16.txt:58262"},
    {"No one responds to your commanding voice.", "order", "no-target", "", false,
     "log-2005.09.21-01.39.48.txt:19820"},
    // no room
    {"Alas! You have no fighting space left!", "rescue", "no-space", "", false,
     "log-2005.09.15-21.01.17.txt:54684"},
    {"Alas! The melee is too dense to target her safely!", "kill", "no-space", "", false,
     "log-2005.10.06-18.57.04.txt:123771"},
    // the character
    {"You are too afraid.", "", "afraid", "", false, "log-2005.08.31-14.00.43.txt:1353"},
    {"Alas, your lips are sealed!", "cast", "silenced", "", false,
     "log-2005.12.11-20.24.40.txt:41778"},
    {"Sorry, you can't do that, you don't have any idea about it!", "", "no-skill", "", false,
     "log-2005.11.01-00.23.09.txt:11609"},
    {"Maybe learning how to bash would help?", "bash", "no-skill", "", false,
     "log-2006.05.20-01.28.11.txt:61399"},
    {"Perhaps you should learn the art of backstabbing...", "backstab", "no-skill", "", false,
     "log-2006.02.14-00.57.13.txt:68916"},
    {"You need a shield to do that successfully.", "bash", "no-item", "shield", false,
     "log-2006.01.06-20.57.51.txt:71429"},
    {"You need a knife to do that.", "butcher", "no-item", "knife", false,
     "log-2005.09.05-19.23.16.txt:51404"},
    {"You need a lance and a mount to charge.", "charge", "no-item", "lance", false,
     "log-2005.12.11-20.24.40.txt:101142"},
    {"You need a whetstone in your equipment.", "whet", "no-item", "whetstone", false,
     "log-2006.02.12-20.44.23.txt:137651"},
    // the mount
    {"It's too difficult to ride here.", "move", "noride", "", false,
     "log-2005.09.07-02.50.20.txt:15394"},
    {"Oops! You cannot go there riding!", "move", "noride", "", true,
     "log-2005.08.31-14.00.43.txt:1305"},
    {"You cannot ride there.", "move", "noride", "", true, "M row 242"},
    {"OOPS! You cannot go there while riding!", "move", "noride", "", true,
     "CombatLines.cpp g_refusals (today's wording)"},
    {"You don't control your mount!", "move", "no-control", "", false,
     "log-2005.12.19-20.51.29.txt:75928"},
    {"A pack horse doesn't want to follow you!", "lead", "unwilling", "A pack horse", false,
     "log-2005.11.07-05.28.32.txt:10776"},
    {"You cannot camp while riding.", "camp", "riding", "", false,
     "log-2006.02.03-00.39.44.txt:113597"},
    // the place
    {"You are unable to manoeuvre past a huge stone giant.", "move", "guarded",
     "a huge stone giant", false, "log-2005.08.31-14.00.43.txt:13462"},
    {"You can't proceed while the crack is in motion.", "move", "door-moving", "crack", false,
     "log-2005.09.05-19.23.16.txt:30353"},
    {"The ice layer is too thick and prevents you from reaching it.", "open", "door-iced", "",
     false, "log-2005.09.05-19.23.16.txt:66914"},
    {"The will blocking the trapdoor resisted your spell.", "cast", "door-blocked", "trapdoor",
     false, "log-2005.10.27-01.19.35.txt:7114"},
    {"It is pitch black...", "look", "dark", "", false, "log-2005.08.31-14.00.43.txt:8573"},
    {"You can't do that under water!", "", "water", "", false,
     "log-2005.11.07-15.57.10.txt:79367"},
    {"Tracking in water is not possible.", "track", "water", "", false,
     "log-2005.09.21-01.39.48.txt:13688"},
    {"You fail to find tracks on the stone floor.", "track", "not-here", "", false,
     "log-2005.09.21-01.39.48.txt:62112"},
    {"You cannot camp in the city.", "camp", "not-here", "", false,
     "log-2006.02.09-19.48.52.txt:134955"},
    {"You cannot guess the time indoors.", "time", "not-here", "", false,
     "log-2005.08.31-14.00.43.txt:5990"},
    {"Sorry, but you cannot do that here!", "", "not-here", "", false,
     "log-2005.09.21-01.39.48.txt:51268"},
    {"You are unable to leave this place.", "", "not-here", "", false, "M row 153"},
};

struct NODISCARD NotRefusedCase final
{
    const char *line;
    /// Which package has it instead; "" for a line that is no refusal at all.
    const char *instead;
    const char *source;
};

// The refusals another reader already publishes: kept as they are, and not sent twice.
const NotRefusedCase g_notRefused[] = {
    // MMapper.Combat.Event kind refused (CombatLines.cpp g_refusals)
    {"No way! You are fighting for your life!", "Combat.Event refused fighting",
     "log-2005.09.06-14.57.57.txt:2702"},
    {"You are too exhausted.", "Combat.Event refused exhausted",
     "log-2005.10.08-15.55.31.txt:58239"},
    {"You are too exhausted to ride.", "Combat.Event refused exhausted",
     "log-2005.12.19-20.51.29.txt:136070"},
    {"A pack horse is too exhausted.", "Combat.Event refused mount-exhausted",
     "log-2005.09.07-02.50.20.txt:82513"},
    {"Your mount refuses to follow your orders!", "Combat.Event refused mount-refuses",
     "log-2005.08.31-14.00.43.txt:2257"},
    {"ZBLAM! A pack horse (my) doesn't want you riding him anymore.",
     "Combat.Event refused thrown", "log-2005.09.05-19.23.16.txt:2915"},
    {"Nah... You feel too relaxed to do that..", "Combat.Event refused resting",
     "log-2005.09.21-01.39.48.txt:1330"},
    {"Maybe you should get on your feet first?", "Combat.Event refused sitting",
     "log-2005.09.15-21.01.17.txt:9519"},
    {"In your dreams, or what?", "Combat.Event refused sleeping",
     "log-2005.09.21-01.39.48.txt:9838"},
    {"The door seems to be closed.", "Combat.Event refused door-closed",
     "log-2005.09.05-19.23.16.txt:27966"},
    {"Alas, you cannot go that way...", "Combat.Event refused no-exit",
     "log-2005.09.21-01.39.48.txt:1265"},
    {"The ascent is too steep, you need to climb to go there.", "Combat.Event refused climb",
     "log-2005.09.21-01.39.48.txt:65863"},
    {"You failed to climb there and fall down, hurting yourself.",
     "Combat.Event refused climb-failed", "action-rules.md 1.3 (L 152)"},
    {"You need to swim to go there.", "Combat.Event refused swim",
     "log-2005.12.12-16.09.18.txt:3286"},
    {"You failed swimming there.", "Combat.Event refused swim-failed",
     "log-2005.09.21-01.39.48.txt:23006"},
    {"You can't go into deep water!", "Combat.Event refused deep-water",
     "log-2005.10.08-15.55.31.txt:1893"},
    {"Nobody here by that name.", "Combat.Event refused no-target", "M row 56"},
    {"Alas! There was no clear line of sight to him!", "Combat.Event refused no-line-of-sight",
     "M row 63"},
    {"Alas! There is no fighting space left to reach him!", "Combat.Event refused no-space",
     "log-2005.10.16-23.42.44.txt:23666"},
    {"You're already fighting!", "Combat.Event refused already-fighting",
     "log-2005.09.15-18.43.38.txt:2408"},
    {"Alas! You failed to reach him through the melee.", "Combat.Event refused melee", "M row 62"},
    {"Your victim has disappeared!", "Combat.Event refused victim-gone",
     "log-2005.11.02-01.27.58.txt:23465"},
    {"You can't do that while fighting.", "Combat.Event refused while-fighting",
     "log-2006.03.06-01.08.16.txt:25417"},
    {"Do you not consider fighting as standing?", "Combat.Event refused already-standing",
     "log-2005.09.15-18.43.38.txt:12179"},
    {"You are not fighting.", "Combat.Event refused not-fighting", "M row 35"},
    {"But your crossbow is already loaded!", "Combat.Event refused already-loaded", "M row 79"},
    // MMapper.Combat.Event kinds rescue, assist, backstab, cast with phase refused
    {"Who do you want to rescue?", "Combat.Event rescue refused no-target",
     "log-2005.10.16-23.42.44.txt:6794"},
    {"But nobody is fighting him?", "Combat.Event rescue refused not-fighting",
     "log-2005.09.15-21.01.17.txt:10475"},
    {"Who do you want to assist?", "Combat.Event assist refused",
     "log-2005.09.06-14.57.57.txt:2620"},
    {"You can't backstab a fighting person, too alert!", "Combat.Event backstab refused fighting",
     "M row 250"},
    {"For a successful backstab you need to be wielding a suitable weapon.",
     "Combat.Event backstab refused weapon", "M row 252"},
    {"You can't concentrate enough while resting.", "Combat.Event cast refused resting",
     "log-2005.08.31-14.00.43.txt:11789"},
    {"Impossible! You can't concentrate enough.", "Combat.Event cast refused",
     "log-2005.12.19-20.51.29.txt:122009"},
    {"Alas, not enough mana flows through you...", "Combat.Event cast refused mana",
     "log-2005.09.07-02.50.20.txt:8605"},
    {"What should the spell be cast upon?", "Combat.Event cast refused no-target", "M row 21"},
    {"Try learning some spells first!", "Combat.Event cast refused no-spells", "M row 23"},
    // MMapper.Combat.Event flee failed: an outcome, not a refusal
    {"PANIC! You can't quit the fight!", "Combat.Event flee failed", "M row 41"},
    {"You try to flee, but cannot!", "Combat.Event flee failed",
     "log-2005.10.08-15.55.31.txt:17004"},
};

// The same for the readers TestRefusedDoors does not link, and lines that refuse nothing.
const NotRefusedCase g_notRefusedElsewhere[] = {
    {"You have no loyal subjects here.", "Char.Followers reply none-here",
     "log-2005.11.07-05.28.32.txt:16600"},
    {"You failed to control a mother eagle (one).", "Char.Followers reply failed",
     "log-2005.11.07-15.57.10.txt:38029"},
    {"Order who to do what?", "Char.Followers reply syntax", "log-2005.11.03-21.45.23.txt:2566"},
    {"You have to stand in order to practice anything.", "Guild.Practised refused",
     "log-2005.10.08-15.55.31.txt:9929"},
    // Door answers that mean nothing without the command: refusedFromDoorReply() has them, and
    // a chest's are MMapper.Room.Container.
    {"It seems to be locked.", "paired", "log-2005.09.05-19.23.16.txt:32897"},
    {"It's already open!", "paired", "log-2005.08.31-14.00.43.txt:2428"},
    {"It's already closed!", "paired", "log-2005.09.05-19.23.16.txt:23779"},
    {"That's impossible, I'm afraid.", "paired", "log-2005.09.05-19.23.16.txt:38055"},
    {"You don't see any exit there.", "paired", "log-2005.08.31-14.00.43.txt:2985"},
    {"Ok.", "", "log-2006.01.06-01.59.16.txt:47596"},
    {"You flee head over heels.", "", "log-2005.09.21-01.39.48.txt:1462"},
    {"You stop riding a mountain mule (my).", "", "log-2006.03.06-01.08.16.txt:7484"},
    {"Stolb is already following you!!", "", "(one character more than the line)"},
};

NODISCARD DoorReply reply(const A action,
                          const char *const word,
                          const char *const dir,
                          const R result,
                          const char *const text)
{
    DoorReply r;
    r.command.action = action;
    r.command.target = r.command.word = QString::fromLatin1(word);
    r.command.direction = QString::fromLatin1(dir);
    r.command.container = false;
    r.result = result;
    r.text = QString::fromLatin1(text);
    return r;
}

NODISCARD RoomExitsInfo roomInfo(const char *const json)
{
    return parseRoomInfoDoors(QJsonDocument::fromJson(QByteArray{json}).object());
}

/// "e:gate:closed n::open" -- side, name and state of each door, in order.
NODISCARD QString show(const RoomDoors &doors)
{
    QStringList parts;
    for (const RoomDoor &door : doors.doors) {
        parts.append(QStringLiteral("%1:%2:%3")
                         .arg(door.dir, door.name, QString::fromUtf8(to_string_view(door.state))));
    }
    return parts.join(QLatin1Char(' '));
}

NODISCARD QString show(const std::optional<RoomDoors> &doors)
{
    return doors.has_value() ? show(*doors) : QStringLiteral("(no change)");
}

constexpr int64_t T0 = 1790000000;

} // namespace

void TestRefusedDoors::refusedLinesTest()
{
    for (const RefusedCase &c : g_refused) {
        const QString line = QString::fromUtf8(c.line);
        const auto refused = parseRefusedLine(line);
        QVERIFY2(refused.has_value(), c.line);
        QVERIFY2(refused->action == QString::fromLatin1(c.action), c.line);
        QVERIFY2(refused->reason == QString::fromLatin1(c.reason), c.line);
        QVERIFY2(refused->target == QString::fromUtf8(c.target), c.line);
        QVERIFY2(refused->text == line, c.line);
        QVERIFY2(refused->dir.isEmpty(), c.line);
        QVERIFY2(QByteArray{c.source}.size() > 0, c.line);

        // No line is two events, a ride refused apart: what MMapper.Combat.Event has stays there.
        const auto combat = parseCombatLine(line);
        QVERIFY2(combat.has_value() == c.alsoCombat, c.line);
        if (c.alsoCombat) {
            QVERIFY2(combat->kind == CombatKindEnum::REFUSED, c.line);
            QVERIFY2(combat->detail == QStringLiteral("cannot-ride"), c.line);
        }
    }
    // The twiddlers of a delayed command in front of the line, and space around it.
    const auto spun = parseRefusedLine(QStringLiteral("\\|/You can't do this sitting!  "));
    QVERIFY(spun.has_value());
    QCOMPARE(spun->text, QStringLiteral("You can't do this sitting!"));
    // A whole line, not a part of one.
    QVERIFY(!parseRefusedLine(QStringLiteral("Stolb says 'You are already standing.'")));
    QVERIFY(!parseRefusedLine(QStringLiteral("You are already standing. Really.")));
    QVERIFY(!parseRefusedLine(QString{}));
}

void TestRefusedDoors::notRefusedTest()
{
    for (const NotRefusedCase &c : g_notRefused) {
        const QString line = QString::fromUtf8(c.line);
        QVERIFY2(!parseRefusedLine(line).has_value(), c.line);
        // ... and it is the event the table says it is.
        const auto combat = parseCombatLine(line);
        QVERIFY2(combat.has_value(), c.line);
        const QString instead = QString::fromLatin1(c.instead);
        if (instead.startsWith(QStringLiteral("Combat.Event refused"))) {
            QVERIFY2(combat->kind == CombatKindEnum::REFUSED, c.line);
            QVERIFY2(instead.endsWith(combat->detail), c.line);
        } else if (instead.contains(QStringLiteral(" refused"))) {
            QVERIFY2(combat->phase == CombatPhaseEnum::REFUSED, c.line);
        }
    }
    for (const NotRefusedCase &c : g_notRefusedElsewhere) {
        QVERIFY2(!parseRefusedLine(QString::fromUtf8(c.line)).has_value(), c.line);
    }
}

void TestRefusedDoors::refusedDoorReplyTest()
{
    struct NODISCARD Case final
    {
        DoorReply reply;
        const char *action;
        const char *reason;
        const char *target;
        const char *dir;
        const char *source;
    };
    const Case cases[] = {
        {reply(A::OPEN, "exit", "e", R::LOCKED, "It seems to be locked."), "open", "door-locked",
         "", "e", "log-2006.08.03-19.24.37.txt:18504-18505"},
        {reply(A::LOCK, "exit", "", R::LOCKED, "It's already locked!"), "lock", "already", "", "",
         "log-2006.02.14-00.57.13.txt:54284-54287"},
        {reply(A::UNLOCK, "exit", "s", R::UNLOCKED, "It's already unlocked, it seems."), "unlock",
         "already", "", "s", "log-2006.05.15-18.37.20.txt:82344-82346"},
        {reply(A::OPEN, "exit", "", R::ALREADY_OPEN, "It's already open!"), "open", "already", "",
         "", "log-2005.11.02-01.27.58.txt:23486-23487"},
        {reply(A::CLOSE, "exit", "w", R::ALREADY_CLOSED, "It's already closed!"), "close",
         "already", "", "w", "log-2005.09.07-02.50.20.txt:67604-67606"},
        {reply(A::UNLOCK, "exit", "e", R::NO_KEY, "You do not have the proper key for that."),
         "unlock", "no-item", "key", "e", "log-2006.03.14-21.39.15.txt:27484-27485"},
        {reply(A::OPEN, "exit", "e", R::CANNOT, "That's impossible, I'm afraid."), "open",
         "door-blocked", "", "e", "log-2005.10.04-03.05.23.txt:46509-46510"},
        {reply(A::CLOSE, "corner", "n", R::CANNOT, "That's absurd."), "close", "unknown", "", "n",
         "log-2006.02.09-00.15.52.txt:46231-46232"},
        {reply(A::CLOSE, "exit", "d", R::NOT_FOUND, "You don't see any exit there."), "close",
         "not-here", "", "d", "log-2006.05.02-22.38.10.txt:17804-17805"},
    };
    for (const Case &c : cases) {
        const auto refused = refusedFromDoorReply(c.reply);
        QVERIFY2(refused.has_value(), c.source);
        QVERIFY2(refused->action == QString::fromLatin1(c.action), c.source);
        QVERIFY2(refused->reason == QString::fromLatin1(c.reason), c.source);
        QVERIFY2(refused->target == QString::fromLatin1(c.target), c.source);
        QVERIFY2(refused->dir == QString::fromLatin1(c.dir), c.source);
        QVERIFY2(refused->text == c.reply.text, c.source);
    }
    // What worked is no refusal: "Ok." (log-2006.01.06-01.59.16.txt:47595-47596), "*click*"
    // (log-2005.09.21-01.39.48.txt:26048-26050), a lock picked.
    QVERIFY(!refusedFromDoorReply(reply(A::OPEN, "exit", "e", R::OPENED, "Ok.")));
    QVERIFY(!refusedFromDoorReply(reply(A::CLOSE, "exit", "e", R::CLOSED, "Ok.")));
    QVERIFY(!refusedFromDoorReply(reply(A::UNLOCK, "exit", "d", R::UNLOCKED, "*click*")));
    QVERIFY(!refusedFromDoorReply(reply(A::LOCK, "exit", "d", R::LOCKED, "*click*")));
    QVERIFY(!refusedFromDoorReply(
        reply(A::PICK, "exit", "d", R::PICKED, "The lock finally yields to your skill.")));
}

void TestRefusedDoors::reasonsTest()
{
    // The codes a frontend switches on: a change here is a change of the protocol.
    QCOMPARE(refusedReasons().join(QLatin1Char(' ')),
             QStringLiteral("afraid already busy dark door-blocked door-iced door-locked "
                            "door-moving fighting guarded no-control no-item no-skill no-space "
                            "no-target noride not-fighting not-here position queued riding self "
                            "silenced slept unknown unwilling water"));
}

void TestRefusedDoors::doorLinesTest()
{
    struct NODISCARD Case final
    {
        const char *line;
        K kind;
        const char *name;
        const char *dir;
        const char *actor;
        const char *source;
    };
    const Case cases[] = {
        {"The woodendoor seems to be closed.", K::SEEMS_CLOSED, "woodendoor", "", "",
         "log-2005.08.31-14.00.43.txt:2344"},
        {"The gate seems to be closed.", K::SEEMS_CLOSED, "gate", "", "",
         "log-2006.01.06-01.59.16.txt:54137"},
        {"The hangingbranch is closed.", K::LOOKED_CLOSED, "hangingbranch", "", "",
         "log-2006.01.04-22.01.05.txt:20043"},
        {"The deadbark is open.", K::LOOKED_OPEN, "deadbark", "", "",
         "log-2006.02.01-22.13.02.txt:130138"},
        {"The door is opened from the other side.", K::OPENED, "door", "", "",
         "log-2005.08.31-14.00.43.txt:1607"},
        {"The sturdydoor is closed from the other side.", K::CLOSED, "sturdydoor", "", "",
         "log-2005.12.08-02.16.02.txt:33830"},
        {"The irondoor closes quietly.", K::CLOSED, "irondoor", "", "",
         "log-2005.08.31-14.00.43.txt:1761"},
        {"A brown-skinned orc opens the irondoor.", K::OPENED, "irondoor", "",
         "A brown-skinned orc", "log-2005.08.31-14.00.43.txt:1747"},
        {"A brown-skinned orc unlocks the irondoor.", K::UNLOCKED, "irondoor", "",
         "A brown-skinned orc", "log-2005.08.31-14.00.43.txt:1746"},
        {"Lungorthin closes the fence.", K::CLOSED, "fence", "", "Lungorthin",
         "log-2005.08.31-14.00.43.txt:8864"},
        {"Gumak the Uruk-hai locks the stonedoor.", K::LOCKED, "stonedoor", "",
         "Gumak the Uruk-hai", "log-2005.09.05-19.23.16.txt:11151"},
        {"Vardamir opens the door.", K::OPENED, "door", "", "Vardamir",
         "log-2005.11.02-01.27.58.txt:23480"},
        {"The irondoor gave away under the pressure.", K::GAVE_WAY, "irondoor", "", "",
         "log-2005.09.05-19.23.16.txt:17300"},
        {"The towerdoor is filled with a bright light.", K::LIT, "towerdoor", "", "",
         "log-2005.10.08-15.55.31.txt:41403"},
        {"The exit west seems to blur for a while.", K::BLURRED, "", "w", "",
         "log-2005.09.05-19.23.16.txt:23805"},
        {"The exit east seems to blur for a while.", K::BLURRED, "", "e", "",
         "log-2006.04.19-23.32.31.txt:88306"},
        {"The door seems to blur for a while.", K::BLURRED, "door", "", "",
         "log-2005.10.04-03.05.23.txt:46527"},
        {"The bars seems to blur for a while.", K::BLURRED, "bars", "", "",
         "log-2006.02.14-00.57.13.txt:53889"},
        {"As you throw a twisted rock fragment at the door, there is an explosion.", K::EXPLOSION,
         "door", "", "", "log-2005.10.04-03.05.23.txt:46526"},
        {"As Kazadoe (K) throws a twisted rock fragment at the gate, there is an explosion.",
         K::EXPLOSION, "gate", "", "Kazadoe (K)", "log-2006.01.04-03.42.35.txt:11765"},
        {"The will blocking the trapdoor resisted your spell.", K::RESISTED, "trapdoor", "", "",
         "log-2005.10.27-01.19.35.txt:7114"},
        {"The door slams shut, and a thick layer of ice covers it.", K::ICED, "door", "", "",
         "log-2005.11.02-01.27.58.txt:23464"},
        {"A thick layer of ice covers the door.", K::ICED, "door", "", "",
         "log-2005.09.05-19.23.16.txt:67137"},
        {"The ice layer is too thick and prevents you from reaching it.", K::ICE_THICK, "", "", "",
         "log-2005.09.05-19.23.16.txt:66914"},
        {"You aim your spell at the ice layer.", K::ICE_AIMED, "", "", "",
         "log-2005.09.21-01.39.48.txt:26019"},
        {"\\|You aim your spell at the ice layer.", K::ICE_AIMED, "", "", "",
         "log-2005.09.21-01.39.48.txt:26019 (with its twiddlers)"},
        {"Some of the ice melts down.", K::ICE_MELTS, "", "", "",
         "log-2005.09.21-01.39.48.txt:26020"},
        {"The ice layer is completely molten!", K::ICE_MOLTEN, "", "", "",
         "log-2005.09.21-01.39.48.txt:26044"},
    };
    for (const Case &c : cases) {
        const auto line = parseDoorLine(QString::fromUtf8(c.line));
        QVERIFY2(line.has_value(), c.line);
        QVERIFY2(line->kind == c.kind, c.line);
        QVERIFY2(line->name == QString::fromLatin1(c.name), c.line);
        QVERIFY2(line->dir == QString::fromLatin1(c.dir), c.line);
        QVERIFY2(line->actor == QString::fromUtf8(c.actor), c.line);
        QVERIFY2(QByteArray{c.source}.size() > 0, c.line);
    }
}

void TestRefusedDoors::notDoorLinesTest()
{
    const char *const lines[] = {
        // Somebody else's cast at the ice says nothing new of the door
        // (log-2005.11.08-15.42.24.txt:11314).
        "Vardamir aims a jet of flame at the ice layer.",
        // Answers that mean something only with their command: ContainerTracker pairs them.
        "It seems to be locked.",
        "Ok.",
        "*click*",
        "It's already open!",
        "That's impossible, I'm afraid.",
        // Sentences of the same shape that are no door's.
        "The sun is already high in the sky.", // log-2005 passim: no "is open/closed"
        "The fragile key breaks in your hands.", // log-2005.09.21-01.39.48.txt:26051
        "The trolls are extremely well tempered.",
        "Stolb says 'The door is open.'",
        "You hear a *click* in a lock.", // log-2006.01.06-01.59.16.txt:24625
        "",
    };
    for (const char *const line : lines) {
        QVERIFY2(!parseDoorLine(QString::fromUtf8(line)).has_value(), line);
    }
}

void TestRefusedDoors::exitsLineTest()
{
    const auto marks = [](const char *const line) {
        QStringList parts;
        for (const DoorExit &exit : parseExitsLine(QString::fromUtf8(line))) {
            parts.append(exit.dir
                         + QLatin1Char(exit.broken ? '#' : exit.closed ? '[' : '(')
                         + (exit.door ? QString{} : QStringLiteral("?")));
        }
        return parts.join(QLatin1Char(' '));
    };
    // log-2005.11.02-01.27.58.txt:23494
    QCOMPARE(marks("Exits: (east), south."), QStringLiteral("e("));
    // log-2006.01.06-01.59.16.txt:54134
    QCOMPARE(marks("Exits: [east], west."), QStringLiteral("e["));
    // log-2005.12.08-02.16.02.txt:26545: a road through an open door
    QCOMPARE(marks("Exits: east, south, =west=, =(up)=."), QStringLiteral("u("));
    // log-2006.08.05-21.17.51.txt:2169 and log-2006.08.09-19.10.22.txt:296: no commas
    QCOMPARE(marks("Exits:  north east south #west#"), QStringLiteral("w#"));
    QCOMPARE(marks("Exits:  (north) #west# #down#"), QStringLiteral("n( w# d#"));
    // help exits: "=#up#= is a road leading through a broken door"; a portal is no door.
    QCOMPARE(marks("Exits: =#up#=, {down}, *[north]*, ~south~."), QStringLiteral("u# n["));
    // The element's text as the parser hands it over: a line break before and after.
    QCOMPARE(marks("\r\nExits: [north], (down).\r\n"), QStringLiteral("n[ d("));
    QCOMPARE(marks("Exits: none."), QString{});
    QCOMPARE(marks("You see no exits."), QString{});
    QCOMPARE(marks("North  - A dark room"), QString{});
}

void TestRefusedDoors::gmcpExitsTest()
{
    // help gmcp_room: exits is an object keyed n e s w u d; each has optional name, flags, id;
    // flags are broken, climb-down, climb-up, closed, hidden, road, sundeath, sunny, trail,
    // water; in Room.UpdateExits "If an exit is removed, its value becomes false".
    const RoomExitsInfo info = roomInfo(
        R"({"id":5988992,"name":"Backroom","exits":{)"
        R"("w":{"id":12925987,"name":"Door","flags":["closed"]},)"
        R"("n":{"id":7,"flags":["road"]},)"
        R"("u":{"id":8,"name":"hatch","flags":["broken","hidden"]},)"
        R"("d":{"id":9,"name":"exit"},)"
        R"("x":{"name":"nothing"}}})");
    QCOMPARE(info.room, std::optional<int64_t>{5988992});
    QCOMPARE(info.exits.size(), size_t{4});
    QCOMPARE(info.exits[0], (DoorExit{QStringLiteral("n"), true, false, QString{}, false, false}));
    QCOMPARE(info.exits[1],
             (DoorExit{QStringLiteral("w"), true, true, QStringLiteral("door"), true, false}));
    QCOMPARE(info.exits[2],
             (DoorExit{QStringLiteral("u"), true, true, QStringLiteral("hatch"), false, true}));
    // A door MUME names "exit" has a door and no name to give.
    QCOMPARE(info.exits[3], (DoorExit{QStringLiteral("d"), true, true, QString{}, false, false}));

    const auto update = parseExitsObject(
        QJsonDocument::fromJson(R"({"w":{"id":12925987,"name":"door"},"e":false})").object());
    QCOMPARE(update.size(), size_t{2});
    QCOMPARE(update[0], (DoorExit{QStringLiteral("e"), false, false, QString{}, false, false}));
    QCOMPARE(update[1],
             (DoorExit{QStringLiteral("w"), true, true, QStringLiteral("door"), false, false}));

    QVERIFY(!roomInfo("{}").room.has_value());
    QVERIFY(roomInfo("{}").exits.empty());
}

void TestRefusedDoors::aimTest()
{
    struct NODISCARD Case final
    {
        const char *input;
        const char *verb;
        const char *word;
        const char *dir;
        const char *source;
    };
    const Case cases[] = {
        {"open exit e", "open", "exit", "e", "log-2005.10.04-03.05.23.txt:46509"},
        {"open icewall n", "open", "icewall", "n", "log-2006.02.25-19.02.57.txt:31205"},
        {"close secrethoard w", "close", "secrethoard", "w", "log-2006.01.06-01.59.16.txt:47584"},
        {"unlock exit d", "unlock", "exit", "d", "log-2005.09.21-01.39.48.txt:26048"},
        {"lock exit ", "lock", "exit", "", "log-2006.02.14-00.57.13.txt:54285"},
        {"pick exit ", "pick", "exit", "", "log-2006.08.03-19.24.37.txt:18507"},
        {"bash exit n", "bash", "exit", "n", "log-2005.11.17-17.17.33.txt:117926"},
        {"use rock exit e", "use", "exit", "e", "log-2005.10.04-03.05.23.txt:46520"},
        {"use rock cliff u", "use", "cliff", "u", "action-rules.md 1.6 (3 times)"},
        {"cast normal 'burning hands' stonedoor", "cast", "stonedoor", "",
         "log-2005.09.21-01.39.48.txt:26014"},
        {"cast normal 'burning hands' door", "cast", "door", "",
         "log-2005.11.02-01.27.58.txt:23473"},
        {"cast normal 'block door' exit e", "cast", "exit", "e", "action-rules.md 1.6"},
        {"l s", "look", "", "s", "log-2006.01.04-22.01.05.txt:20040"},
        {"look west", "look", "", "w", "the long form of `l w`"},
    };
    for (const Case &c : cases) {
        const auto aim = parseDoorAim(QString::fromLatin1(c.input));
        QVERIFY2(aim.has_value(), c.input);
        QVERIFY2(aim->verb == QString::fromLatin1(c.verb), c.input);
        QVERIFY2(aim->word == QString::fromLatin1(c.word), c.input);
        QVERIFY2(aim->dir == QString::fromLatin1(c.dir), c.input);
    }
    for (const char *const input : {"look", "l", "look in chest", "l orc", "north", "kill orc",
                                    "cast 'armour'", "open", "use rock", "say open exit e"}) {
        QVERIFY2(!parseDoorAim(QString::fromLatin1(input)).has_value(), input);
    }
}

void TestRefusedDoors::roomInfoTest()
{
    RoomDoorTracker tracker;
    // A room without a door says nothing.
    QCOMPARE(show(tracker.receiveRoomInfo(roomInfo(R"({"id":1,"exits":{"n":{"id":2}}})"), T0)),
             QStringLiteral("(no change)"));
    // A room with doors: open unless flagged, in the order n e s w u d.
    const auto doors = tracker.receiveRoomInfo(
        roomInfo(R"({"id":2,"exits":{"w":{"name":"gate","flags":["closed"]},)"
                 R"("e":{"name":"door"},"s":{"id":1},"u":{"name":"hatch","flags":["broken"]}}})"),
        T0 + 1);
    QVERIFY(doors.has_value());
    QCOMPARE(doors->room, std::optional<int64_t>{2});
    QCOMPARE(show(doors), QStringLiteral("e:door:open w:gate:closed u:hatch:broken"));
    QCOMPARE(doors->doors[0].since, T0 + 1);
    // The same again (a `look`) is no change.
    QCOMPARE(show(tracker.receiveRoomInfo(
                 roomInfo(R"({"id":2,"exits":{"w":{"name":"gate","flags":["closed"]},)"
                          R"("e":{"name":"door"},"u":{"name":"hatch","flags":["broken"]}}})"),
                 T0 + 2)),
             QStringLiteral("(no change)"));
    // Room.UpdateExits: only what changed; `since` moves with the state.
    const auto opened
        = tracker.receiveUpdateExits(parseExitsObject(QJsonDocument::fromJson(
                                                          R"({"w":{"name":"gate"}})")
                                                          .object()),
                                     T0 + 9);
    QCOMPARE(show(opened), QStringLiteral("e:door:open w:gate:open u:hatch:broken"));
    QCOMPARE(opened->doors[1].since, T0 + 9);
    QCOMPARE(opened->doors[0].since, T0 + 1);
    // An exit taken away takes its door along.
    QCOMPARE(show(tracker.receiveUpdateExits(parseExitsObject(
                                                 QJsonDocument::fromJson(R"({"u":false})").object()),
                                             T0 + 10)),
             QStringLiteral("e:door:open w:gate:open"));
    // The exits line agrees: nothing new. One that differs is taken.
    QCOMPARE(show(tracker.receiveExitsLine(QStringLiteral("Exits: (east), south, (west)."), T0 + 11)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.receiveExitsLine(QStringLiteral("Exits: [east], south, (west)."), T0 + 12)),
             QStringLiteral("e:door:closed w:gate:open"));
    // The next room has none: one empty message, and then silence.
    const auto left = tracker.receiveRoomInfo(roomInfo(R"({"id":3,"exits":{"n":{"id":2}}})"),
                                              T0 + 20);
    QVERIFY(left.has_value());
    QVERIFY(left->doors.empty());
    QCOMPARE(left->room, std::optional<int64_t>{3});
    QCOMPARE(show(tracker.receiveRoomInfo(roomInfo(R"({"id":4,"exits":{}})"), T0 + 21)),
             QStringLiteral("(no change)"));
}

void TestRefusedDoors::repliesTest()
{
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":2,"exits":{"e":{"name":"gate","flags":["closed"]}}})"), T0);
    // log-2006.08.03-19.24.37.txt:18504-18505: `open exit e` / "It seems to be locked."
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "exit", "e", R::LOCKED, "It seems to be locked."), T0 + 1)),
             QStringLiteral("e:gate:locked"));
    // MUME restating the exit as closed does not undo what the line said.
    QCOMPARE(show(tracker.receiveRoomInfo(
                 roomInfo(R"({"id":2,"exits":{"e":{"name":"gate","flags":["closed"]}}})"), T0 + 2)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.receiveExitsLine(QStringLiteral("Exits: [east]."), T0 + 2)),
             QStringLiteral("(no change)"));
    // log-2006.03.14-21.39.15.txt:27484-27485: no key for it.
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::UNLOCK,
                                                 "exit",
                                                 "e",
                                                 R::NO_KEY,
                                                 "You do not have the proper key for that."),
                                           T0 + 3)),
             QStringLiteral("(no change)"));
    // log-2005.09.21-01.39.48.txt:26048-26050: `unlock exit d` / "*click*", then `open` / "Ok."
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::UNLOCK, "exit", "e", R::UNLOCKED, "*click*"),
                                           T0 + 4)),
             QStringLiteral("e:gate:closed"));
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::OPEN, "exit", "e", R::OPENED, "Ok."), T0 + 5)),
             QStringLiteral("e:gate:open"));
    // log-2005.11.02-01.27.58.txt:23486-23487: "It's already open!" changes nothing then.
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "exit", "", R::ALREADY_OPEN, "It's already open!"), T0 + 6)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::CLOSE, "exit", "e", R::CLOSED, "Ok."), T0 + 7)),
             QStringLiteral("e:gate:closed"));
    // log-2006.02.14-00.57.13.txt:54284-54287: `lock exit` / "It's already locked!", the one
    // door of the room.
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::LOCK, "exit", "", R::LOCKED, "It's already locked!"), T0 + 8)),
             QStringLiteral("e:gate:locked"));
    // log-2006.05.15-18.37.20.txt:82344-82346
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::UNLOCK, "exit", "e", R::UNLOCKED, "It's already unlocked, it seems."),
                 T0 + 9)),
             QStringLiteral("e:gate:closed"));
    // "That's absurd." and a door not found say nothing of a door's state.
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::CLOSE, "gate", "e", R::CANNOT, "That's absurd."),
                                           T0 + 10)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::CLOSE, "exit", "d", R::NOT_FOUND, "You don't see any exit there."),
                 T0 + 10)),
             QStringLiteral("(no change)"));
    // In a room with two doors a bare `open exit` does not say which.
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":5,"exits":{"e":{"name":"gate","flags":["closed"]},)"
                 R"("w":{"name":"door","flags":["closed"]}}})"),
        T0 + 20);
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "exit", "", R::LOCKED, "It seems to be locked."), T0 + 21)),
             QStringLiteral("(no change)"));
}

void TestRefusedDoors::backroomIceTest()
{
    // The Backroom of the Unqalome crypt, log-2005.11.02-01.27.58.txt:23462-23494.
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":5988992,"exits":{"w":{"id":12925987,"name":"door"}}})"), T0);
    const auto line = [&tracker](const char *const text, const int64_t now) {
        return show(tracker.receiveLine(QString::fromUtf8(text), QString{}, now));
    };
    QCOMPARE(line("Unqalome the Great Dark seeps through a small crack and is gone!", T0 + 1),
             QStringLiteral("(no change)"));
    // :23464
    QCOMPARE(line("The door slams shut, and a thick layer of ice covers it.", T0 + 1),
             QStringLiteral("w:door:iced"));
    // MUME says the exit shut: it stays iced.
    QCOMPARE(show(tracker.receiveUpdateExits(
                 parseExitsObject(QJsonDocument::fromJson(
                                      R"({"w":{"id":12925987,"name":"door","flags":["closed"]}})")
                                      .object()),
                 T0 + 1)),
             QStringLiteral("(no change)"));
    // log-2005.09.05-19.23.16.txt:66913-66914: `open exit w` there.
    tracker.receiveCommand(QStringLiteral("open exit w"));
    QCOMPARE(line("The ice layer is too thick and prevents you from reaching it.", T0 + 2),
             QStringLiteral("(no change)"));
    // :23473-23477
    tracker.receiveCommand(QStringLiteral("cast normal 'burning hands' door"));
    QCOMPARE(line("You start to concentrate...", T0 + 3), QStringLiteral("(no change)"));
    QCOMPARE(line("\\|You aim your spell at the ice layer.", T0 + 4), QStringLiteral("(no change)"));
    QCOMPARE(line("Some of the ice melts down.", T0 + 4), QStringLiteral("(no change)"));
    QCOMPARE(line("You aim your spell at the ice layer.", T0 + 8), QStringLiteral("(no change)"));
    const auto molten = tracker.receiveLine(QStringLiteral("The ice layer is completely molten!"),
                                            QString{},
                                            T0 + 8);
    QCOMPARE(show(molten), QStringLiteral("w:door:molten"));
    QCOMPARE(molten->doors[0].since, T0 + 8);
    // :23480 "Vardamir opens the door."
    QCOMPARE(line("Vardamir opens the door.", T0 + 12), QStringLiteral("w:door:open"));
}

void TestRefusedDoors::iceMoundTest()
{
    // The Ice Mound, log-2005.09.21-01.39.48.txt:26013-26051: the door down is `stonedoor`. Here
    // MUME gives no name for it (as the map has none), and the ice is already there.
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":9049025,"exits":{"d":{"id":1450601,"flags":["closed"]}}})"), T0);
    QCOMPARE(show(tracker.current()), QStringLiteral("d::closed"));
    const auto line = [&tracker](const char *const text, const int64_t now) {
        return show(tracker.receiveLine(QString::fromUtf8(text), QString{}, now));
    };
    tracker.receiveCommand(QStringLiteral("cast normal 'burning hands' stonedoor"));
    tracker.receivePrompt();
    // :26019-26020: the player's own spell went at the ice, so `stonedoor` is the door.
    QCOMPARE(line("\\|You aim your spell at the ice layer.", T0 + 1),
             QStringLiteral("d:stonedoor:iced"));
    QCOMPARE(line("Some of the ice melts down.", T0 + 1), QStringLiteral("(no change)"));
    for (int i = 0; i < 3; ++i) {
        tracker.receiveCommand(QStringLiteral("cast normal 'burning hands' stonedoor"));
        tracker.receivePrompt();
        QCOMPARE(line("You aim your spell at the ice layer.", T0 + 2 + i),
                 QStringLiteral("(no change)"));
        QCOMPARE(line("Some of the ice melts down.", T0 + 2 + i), QStringLiteral("(no change)"));
    }
    // :26044
    QCOMPARE(line("The ice layer is completely molten!", T0 + 9),
             QStringLiteral("d:stonedoor:molten"));
    // :26048-26051: `unlock exit d`, `open exit d` / "*click*" / "The fragile key breaks in your
    // hands." / "Ok." The click leaves the molten door as it is; the "Ok." opens it.
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::UNLOCK, "exit", "d", R::UNLOCKED, "*click*"),
                                           T0 + 10)),
             QStringLiteral("(no change)"));
    QCOMPARE(line("The fragile key breaks in your hands.", T0 + 10), QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::OPEN, "exit", "d", R::OPENED, "Ok."), T0 + 11)),
             QStringLiteral("d:stonedoor:open"));

    // Somebody else melting it: the lines say iced, and nobody's typed word names the door.
    RoomDoorTracker other;
    std::ignore = other.receiveRoomInfo(
        roomInfo(R"({"id":9049025,"exits":{"d":{"id":1450601,"flags":["closed"]}}})"), T0);
    other.receiveCommand(QStringLiteral("cast normal 'magic missile' orc"));
    // log-2005.11.08-15.42.24.txt:11314
    QCOMPARE(show(other.receiveLine(QStringLiteral("Vardamir aims a jet of flame at the ice layer."),
                                    QString{},
                                    T0 + 1)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(other.receiveLine(QStringLiteral("Some of the ice melts down."), QString{}, T0 + 1)),
             QStringLiteral("d::iced"));
    QCOMPARE(show(other.receiveLine(QStringLiteral("The ice layer is completely molten!"),
                                    QString{},
                                    T0 + 2)),
             QStringLiteral("d::molten"));
}

void TestRefusedDoors::rockTest()
{
    // log-2005.10.04-03.05.23.txt:46509-46533: a door that will not open, his rock alias, and
    // the door opens.
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":77,"exits":{"e":{"id":78,"name":"door","flags":["closed"]},)"
                 R"("w":{"id":76},"n":{"id":75}}})"),
        T0);
    const auto line = [&tracker](const char *const text, const int64_t now) {
        return show(tracker.receiveLine(QString::fromUtf8(text), QString{}, now));
    };
    // :46509-46510
    tracker.receiveCommand(QStringLiteral("open exit e"));
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "exit", "e", R::CANNOT, "That's impossible, I'm afraid."), T0 + 1)),
             QStringLiteral("e:door:blocked"));
    tracker.receivePrompt();
    // :46519-46527
    tracker.receiveCommand(QStringLiteral("get rock backpack"));
    tracker.receiveCommand(QStringLiteral("use rock exit e"));
    tracker.receiveCommand(QStringLiteral("get rock"));
    QCOMPARE(line("You get a twisted rock fragment from a leather backpack.", T0 + 2),
             QStringLiteral("(no change)"));
    tracker.receivePrompt();
    QCOMPARE(line("As you throw a twisted rock fragment at the door, there is an explosion.", T0 + 3),
             QStringLiteral("(no change)"));
    // After a rock the blur does not mean blocked: the door opened (:46530-46533).
    QCOMPARE(line("The door seems to blur for a while.", T0 + 3), QStringLiteral("e:door:unknown"));
    tracker.receivePrompt();
    QCOMPARE(line("You can't find a rock.", T0 + 4), QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.receiveDoorReply(reply(A::OPEN, "exit", "e", R::OPENED, "Ok."), T0 + 5)),
             QStringLiteral("e:door:open"));

    // log-2006.01.06-01.59.16.txt:54134-54149: the gate was only shut; MUME had named no door,
    // and the player's `use rock exit e` says which side the explosion was at.
    RoomDoorTracker second;
    std::ignore = second.receiveRoomInfo(roomInfo(R"({"id":80,"exits":{"w":{"id":79}}})"), T0);
    QCOMPARE(show(second.receiveExitsLine(QStringLiteral("Exits: [east], west."), T0)),
             QStringLiteral("e::closed"));
    QCOMPARE(show(second.receiveLine(QStringLiteral("The gate seems to be closed."),
                                     QStringLiteral("e"),
                                     T0 + 1)),
             QStringLiteral("e:gate:closed"));
    second.receiveCommand(QStringLiteral("use rock exit e"));
    QCOMPARE(show(second.receiveLine(
                 QStringLiteral(
                     "As you throw a twisted rock fragment at the gate, there is an explosion."),
                 QString{},
                 T0 + 2)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(second.receiveLine(QStringLiteral("The gate seems to blur for a while."),
                                     QString{},
                                     T0 + 2)),
             QStringLiteral("e:gate:unknown"));

    // log-2006.01.06-01.59.16.txt:24661-24663: the rock's other outcome.
    RoomDoorTracker third;
    std::ignore = third.receiveRoomInfo(
        roomInfo(R"({"id":81,"exits":{"n":{"name":"metaldoor","flags":["closed"]}}})"), T0);
    QCOMPARE(show(third.receiveLine(QStringLiteral("*Tahkr the Zaugurz Orc* locks the metaldoor."),
                                    QString{},
                                    T0 + 1)),
             QStringLiteral("n:metaldoor:locked"));
    std::ignore = third.receiveLine(
        QStringLiteral(
            "As you throw a twisted rock fragment at the metaldoor, there is an explosion."),
        QString{},
        T0 + 2);
    QCOMPARE(show(third.receiveLine(QStringLiteral("The metaldoor is filled with a bright light."),
                                    QString{},
                                    T0 + 2)),
             QStringLiteral("n:metaldoor:broken"));
}

void TestRefusedDoors::blockTest()
{
    // log-2006.04.19-23.32.31.txt:88304-88310: "The gate seems to be closed." / somebody utters
    // 'block door' / "The exit east seems to blur for a while." / `open exit e` / "That's
    // impossible, I'm afraid."
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":90,"exits":{"e":{"name":"gate","flags":["closed"]}}})"), T0);
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The exit east seems to blur for a while."),
                                      QString{},
                                      T0 + 1)),
             QStringLiteral("e:gate:blocked"));
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "exit", "e", R::CANNOT, "That's impossible, I'm afraid."), T0 + 2)),
             QStringLiteral("(no change)"));
    // MUME still calls it closed: blocked stays.
    QCOMPARE(show(tracker.receiveExitsLine(QStringLiteral("Exits: [east]."), T0 + 3)),
             QStringLiteral("(no change)"));
    // log-2005.10.27-01.19.35.txt:7114: a break door that the block held off.
    RoomDoorTracker held;
    QCOMPARE(show(held.receiveLine(
                 QStringLiteral("The will blocking the trapdoor resisted your spell."),
                 QString{},
                 T0)),
             QStringLiteral(":trapdoor:blocked"));
    // The block spell cast at a door by its name, with no side known: an entry without one.
    RoomDoorTracker named;
    QCOMPARE(show(named.receiveLine(QStringLiteral("The bars seems to blur for a while."),
                                    QString{},
                                    T0)),
             QStringLiteral(":bars:blocked"));
}

void TestRefusedDoors::bashAndBreakTest()
{
    // log-2005.11.17-17.17.33.txt:117926-117927: `bash exit n` / "The irondoor gave away under
    // the pressure."; the command says which side.
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(roomInfo(R"({"id":91,"exits":{"s":{"id":90}}})"), T0);
    tracker.receiveCommand(QStringLiteral("bash exit n"));
    QCOMPARE(show(tracker.receiveLine(
                 QStringLiteral("The irondoor gave away under the pressure."), QString{}, T0 + 1)),
             QStringLiteral("n:irondoor:broken"));
    // log-2006.02.06-18.58.18.txt:3237-3238: Stolb's bash. No side is known for it.
    RoomDoorTracker other;
    QCOMPARE(show(other.receiveLine(QStringLiteral("Stolb tries to bash the door."), QString{}, T0)),
             QStringLiteral("(no change)"));
    QCOMPARE(show(other.receiveLine(QStringLiteral("The door gave away under the pressure."),
                                    QString{},
                                    T0)),
             QStringLiteral(":door:broken"));
    // log-2005.12.19-20.51.29.txt:165354-165359: Kazadoe's break door at the gate.
    RoomDoorTracker lit;
    std::ignore = lit.receiveRoomInfo(
        roomInfo(R"({"id":92,"exits":{"e":{"name":"gate","flags":["closed"]}}})"), T0);
    QCOMPARE(show(lit.receiveLine(QStringLiteral("The gate is filled with a bright light."),
                                  QString{},
                                  T0 + 1)),
             QStringLiteral("e:gate:broken"));
    // What MUME then states of the exit wins over the reading of the light.
    QCOMPARE(show(lit.receiveUpdateExits(
                 parseExitsObject(QJsonDocument::fromJson(R"({"e":{"name":"gate"}})").object()),
                 T0 + 2)),
             QStringLiteral("e:gate:open"));
}

void TestRefusedDoors::movesAndLooksTest()
{
    RoomDoorTracker tracker;
    std::ignore = tracker.receiveRoomInfo(roomInfo(R"({"id":93,"exits":{"w":{"id":92}}})"), T0);
    // log-2006.01.06-01.59.16.txt:54136-54137: `east` / "The gate seems to be closed.": the
    // move's side and MUME's name for the door.
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The gate seems to be closed."),
                                      QStringLiteral("e"),
                                      T0 + 1)),
             QStringLiteral("e:gate:closed"));
    // Without a move in the queue the side is not known.
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The stonedoor seems to be closed."),
                                      QString{},
                                      T0 + 2)),
             QStringLiteral("e:gate:closed :stonedoor:closed"));
    // ... until a command names it by its side and MUME answers.
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "stonedoor", "d", R::LOCKED, "It seems to be locked."), T0 + 3)),
             QStringLiteral("e:gate:closed d:stonedoor:locked"));
    // log-2006.01.04-22.01.05.txt:20040-20043: `l s` / "The hangingbranch is closed."
    tracker.receiveCommand(QStringLiteral("l s"));
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The hangingbranch is closed."),
                                      QString{},
                                      T0 + 4)),
             QStringLiteral("e:gate:closed s:hangingbranch:closed d:stonedoor:locked"));
    // :20906 "The hangingbranch is open.", with no look before it: the name is known by now.
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The hangingbranch is open."), QString{}, T0 + 5)),
             QStringLiteral("e:gate:closed s:hangingbranch:open d:stonedoor:locked"));
    // A chest's line of the same shape, with no look at a side before it, is no door.
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The chest is closed."), QString{}, T0 + 6)),
             QStringLiteral("(no change)"));
    // A look two prompts old no longer explains the line.
    tracker.receiveCommand(QStringLiteral("l n"));
    tracker.receivePrompt();
    tracker.receivePrompt();
    QCOMPARE(show(tracker.receiveLine(QStringLiteral("The bushes is open."), QString{}, T0 + 7)),
             QStringLiteral("(no change)"));
}

void TestRefusedDoors::othersTest()
{
    RoomDoorTracker tracker;
    tracker.setNotDoor([](const QString &word) { return word == QStringLiteral("chest"); });
    std::ignore = tracker.receiveRoomInfo(
        roomInfo(R"({"id":94,"exits":{"n":{"name":"irondoor","flags":["closed"]}}})"), T0);
    const auto line = [&tracker](const char *const text, const int64_t now) {
        return show(tracker.receiveLine(QString::fromUtf8(text), QString{}, now));
    };
    // log-2005.08.31-14.00.43.txt:1746-1747, :1761
    QCOMPARE(line("A brown-skinned orc unlocks the irondoor.", T0 + 1), QStringLiteral("(no change)"));
    QCOMPARE(line("A brown-skinned orc opens the irondoor.", T0 + 2), QStringLiteral("n:irondoor:open"));
    QCOMPARE(line("The irondoor closes quietly.", T0 + 3), QStringLiteral("n:irondoor:closed"));
    // log-2005.09.05-19.23.16.txt:11151 (the wording), and then its unlocking.
    QCOMPARE(line("Gumak the Uruk-hai locks the irondoor.", T0 + 4),
             QStringLiteral("n:irondoor:locked"));
    QCOMPARE(line("Gumak the Uruk-hai unlocks the irondoor.", T0 + 5),
             QStringLiteral("n:irondoor:closed"));
    // log-2005.08.31-14.00.43.txt:1607, log-2005.12.08-02.16.02.txt:33830 (the wordings)
    QCOMPARE(line("The irondoor is opened from the other side.", T0 + 6),
             QStringLiteral("n:irondoor:open"));
    QCOMPARE(line("The irondoor is closed from the other side.", T0 + 7),
             QStringLiteral("n:irondoor:closed"));
    // A chest is opened in the same words, and is no door.
    QCOMPARE(line("Stolb opens the chest.", T0 + 8), QStringLiteral("(no change)"));
    // A door MUME has not named yet, by somebody's hand: known by its name alone.
    QCOMPARE(line("Lungorthin closes the fence.", T0 + 9),
             QStringLiteral("n:irondoor:closed :fence:closed"));
    // ... and MUME's Room.UpdateExits then says where it is: one door, not two.
    QCOMPARE(show(tracker.receiveUpdateExits(
                 parseExitsObject(QJsonDocument::fromJson(R"({"s":{"name":"fence"}})").object()),
                 T0 + 10)),
             QStringLiteral("n:irondoor:closed s:fence:open"));
}

void TestRefusedDoors::hiddenNamesTest()
{
    // The rule (logs/archives/log-2006.07.25-23.49.46.txt:74-112): a hidden door's name must
    // not reach the client by itself. The tracker has no map, so a name can only come from
    // MUME's own output on this visit, or from the player's typing that MUME answered for.
    RoomDoorTracker tracker;
    // The Ice Cave of Moria (room 37368): the map knows a hidden `icewall` north. MUME lists
    // no door there to a player who has not found it.
    std::ignore = tracker.receiveRoomInfo(roomInfo(R"({"id":37368,"exits":{"s":{"id":37367}}})"),
                                          T0);
    QVERIFY(tracker.current().doors.empty());
    // A wrong guess: "You don't see any icewall there." confirms nothing.
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "icewall", "n", R::NOT_FOUND, "You don't see any icewall there."),
                 T0 + 1)),
             QStringLiteral("(no change)"));
    QVERIFY(tracker.current().doors.empty());
    // "exit" names any door and is never a name.
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "exit", "n", R::CANNOT, "That's impossible, I'm afraid."), T0 + 2)),
             QStringLiteral("n::blocked"));
    // log-2006.02.25-19.02.57.txt:31205-31207: `open icewall n` / "That's impossible, I'm
    // afraid.": the player typed the name and MUME answered about that door.
    QCOMPARE(show(tracker.receiveDoorReply(
                 reply(A::OPEN, "icewall", "n", R::CANNOT, "That's impossible, I'm afraid."),
                 T0 + 3)),
             QStringLiteral("n:icewall:blocked"));
    // A typed abbreviation never replaces the name MUME gave.
    RoomDoorTracker named;
    std::ignore = named.receiveRoomInfo(
        roomInfo(R"({"id":9,"exits":{"d":{"name":"stonedoor","flags":["closed"]}}})"), T0);
    QCOMPARE(show(named.receiveDoorReply(reply(A::OPEN, "stoned", "d", R::OPENED, "Ok."), T0 + 1)),
             QStringLiteral("d:stonedoor:open"));
    // Another room, and back: nothing is remembered, the typed name included.
    std::ignore = tracker.receiveRoomInfo(roomInfo(R"({"id":37367,"exits":{"n":{"id":37368}}})"),
                                          T0 + 10);
    const auto back = tracker.receiveRoomInfo(roomInfo(R"({"id":37368,"exits":{"s":{"id":37367}}})"),
                                              T0 + 11);
    QVERIFY(tracker.current().doors.empty());
    QVERIFY(!back.has_value());
}

void TestRefusedDoors::roomChangeTest()
{
    RoomDoorTracker tracker;
    const char *const room = R"({"id":2,"exits":{"e":{"name":"gate","flags":["closed"]}}})";
    std::ignore = tracker.receiveRoomInfo(roomInfo(room), T0);
    std::ignore = tracker.receiveDoorReply(
        reply(A::OPEN, "exit", "e", R::LOCKED, "It seems to be locked."), T0 + 1);
    // The same room again keeps what was learnt; `since` does not move.
    QCOMPARE(show(tracker.receiveRoomInfo(roomInfo(room), T0 + 5)), QStringLiteral("(no change)"));
    QCOMPARE(show(tracker.current()), QStringLiteral("e:gate:locked"));
    QCOMPARE(tracker.current().doors[0].since, T0 + 1);
    // Another room with a door of the same side and name is another door.
    const auto next = tracker.receiveRoomInfo(
        roomInfo(R"({"id":3,"exits":{"e":{"name":"gate","flags":["closed"]}}})"), T0 + 6);
    QCOMPARE(show(next), QStringLiteral("e:gate:closed"));
    QCOMPARE(next->room, std::optional<int64_t>{3});
    QCOMPARE(next->doors[0].since, T0 + 6);
    // A command aimed in the old room does not explain a line in the new one.
    tracker.receiveCommand(QStringLiteral("open exit e"));
    const auto third = tracker.receiveRoomInfo(
        roomInfo(R"({"id":4,"exits":{"e":{"name":"gate","flags":["closed"]},)"
                 R"("w":{"name":"door","flags":["closed"]}}})"),
        T0 + 7);
    QCOMPARE(show(third), QStringLiteral("e:gate:closed w:door:closed"));
    QCOMPARE(show(tracker.receiveLine(
                 QStringLiteral("The ice layer is too thick and prevents you from reaching it."),
                 QString{},
                 T0 + 8)),
             QStringLiteral("e:gate:closed w:door:closed ::iced"));
    // Without MUME's id a Room.Info is taken for a move.
    RoomDoorTracker noId;
    std::ignore = noId.receiveRoomInfo(roomInfo(R"({"exits":{"e":{"name":"gate"}}})"), T0);
    std::ignore = noId.receiveDoorReply(reply(A::CLOSE, "exit", "e", R::CLOSED, "Ok."), T0 + 1);
    QCOMPARE(show(noId.receiveRoomInfo(roomInfo(R"({"exits":{"e":{"name":"gate"}}})"), T0 + 2)),
             QStringLiteral("e:gate:open"));
    // reset() forgets it all, and what was sent: the next room with a door is told again.
    tracker.reset();
    QVERIFY(tracker.current().doors.empty());
    QCOMPARE(show(tracker.receiveRoomInfo(roomInfo(room), T0 + 20)), QStringLiteral("e:gate:closed"));
}

QTEST_MAIN(TestRefusedDoors)
