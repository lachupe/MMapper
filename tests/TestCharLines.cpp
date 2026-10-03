// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestCharLines.h"

#include "../src/parser/CharLines.h"

#include <QtTest/QtTest>

// Every reply here is copied from the powwow logs (colour and prompts taken off), unless a
// comment says it was changed; the file each comes from is named above it.

namespace {

/// Marks where MUME's prompt came in a transcript fed to feed().
const char *const PROMPT = "\x01prompt";

/// Feeds `lines` through a fresh tracker, a prompt at each PROMPT, and gathers what came out.
NODISCARD CharReplies feed(const std::initializer_list<const char *> lines)
{
    CharLinesTracker tracker;
    CharReplies all;
    for (const char *const line : lines) {
        if (QByteArray{line} == QByteArray{PROMPT}) {
            all.append(tracker.receivePrompt());
        } else {
            all.append(tracker.receiveLine(QString::fromUtf8(line)));
        }
    }
    return all;
}

NODISCARD QStringList list(const std::initializer_list<const char *> names)
{
    QStringList result;
    for (const char *const name : names) {
        result.append(QString::fromUtf8(name));
    }
    return result;
}

} // namespace

void TestCharLines::statCurrentTest()
{
    // powwow/logs/log-"+year+"...".txt, January 2026: the colour MUME put on each figure is
    // removed before the parser sees the line, as MumeXmlParser does.
    const CharReplies r = feed({"OB: \x1b[32m7\x1b[0m%, DB: \x1b[32m-45\x1b[0m%, PB: "
                                "\x1b[32m0\x1b[0m%, Armour: \x1b[32m0\x1b[0m%. Wimpy: "
                                "\x1b[32m36\x1b[0m. Mood: \x1b[32mwimpy\x1b[0m.",
                                "Needed: 999 xp, 0 tp. Gold: 0. Alert: normal. Condition: hungry.",
                                "Affected by:",
                                "- hunger",
                                "",
                                PROMPT});
    QCOMPARE(r.stats.size(), size_t{1});
    QVERIFY(r.scores.empty());
    const CharStat &s = r.stats.front();
    QCOMPARE(s.ob, std::optional<int64_t>{7});
    QCOMPARE(s.db, std::optional<int64_t>{-45});
    QCOMPARE(s.pb, std::optional<int64_t>{0});
    QCOMPARE(s.armour, std::optional<int64_t>{0});
    QCOMPARE(s.wimpy, std::optional<int64_t>{36});
    QCOMPARE(s.mood, QString("wimpy"));
    QCOMPARE(s.neededXp, std::optional<int64_t>{999});
    QCOMPARE(s.neededTp, std::optional<int64_t>{0});
    QCOMPARE(s.gold, std::optional<int64_t>{0});
    QCOMPARE(s.alert, QString("normal"));
    QCOMPARE(s.condition, list({"hungry"}));
    QCOMPARE(s.affects, list({"hunger"}));
    QVERIFY(s.wounds.isEmpty());
    QVERIFY(!s.wp.has_value());
    QVERIFY(s.text.startsWith("OB: 7%, DB: -45%"));
    QVERIFY(s.text.endsWith("- hunger"));

    // The shape the task names, with thousands separators (powwow/logs/logs).
    const CharReplies known = feed(
        {"OB: 60%, DB: 62%, PB: 60%, Armour: 66%. Wimpy: 120. Mood: wimpy.",
         "Needed: 1,136,776 xp, 0 tp. Gold: 103. Alert: normal.",
         "",
         PROMPT});
    QCOMPARE(known.stats.size(), size_t{1});
    QCOMPARE(known.stats.front().neededXp, std::optional<int64_t>{1136776});
    QCOMPARE(known.stats.front().gold, std::optional<int64_t>{103});
}

void TestCharLines::statEmptyAffectsTest()
{
    // powwow/logs/stolb.balrog.txt: no "Affected by:" at all, so the list is known and empty.
    const CharReplies r = feed({"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.",
                                "Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal.",
                                "",
                                PROMPT});
    QCOMPARE(r.stats.size(), size_t{1});
    const CharStat &s = r.stats.front();
    QCOMPARE(s.ob, std::optional<int64_t>{131});
    QCOMPARE(s.neededXp, std::optional<int64_t>{1108995});
    QVERIFY(s.affects.isEmpty());
    QVERIFY(s.wounds.isEmpty());
    QVERIFY(s.condition.isEmpty());
}

void TestCharLines::statOldWordingTest()
{
    // powwow/logs/luke/logs/wizkill.txt: "Armour: none.", "xp and tp", "Wp:", no Alert.
    const CharReplies r = feed({"OB: 96%, DB: -14%, PB: 0%, Armour: none. Wimpy: 0. Mood: wimpy.",
                                "Needed: 653,899 xp and 2,337 tp. Wp: 15. Gold: 0.",
                                "Affected by:",
                                "- Mandos sleep for 12 hours.",
                                "",
                                PROMPT});
    QCOMPARE(r.stats.size(), size_t{1});
    const CharStat &s = r.stats.front();
    QCOMPARE(s.armour, std::optional<int64_t>{0});
    QCOMPARE(s.wimpy, std::optional<int64_t>{0});
    QCOMPARE(s.neededXp, std::optional<int64_t>{653899});
    QCOMPARE(s.neededTp, std::optional<int64_t>{2337});
    QCOMPARE(s.wp, std::optional<int64_t>{15});
    QCOMPARE(s.gold, std::optional<int64_t>{0});
    QVERIFY(s.alert.isEmpty());
    QCOMPARE(s.affects, list({"Mandos sleep for 12 hours"}));

    // powwow/logs/luke/logs/orgonon.txt: "Armour: 3/8." is no percentage and is left out.
    const CharReplies frac = feed(
        {"OB: 57%, DB: 44%, PB: 85%, Armour: 3/8. Wimpy: 150. Mood: wimpy.",
         "Needed: 1,276,928 xp and 0 tp. Wp: 22. Gold: 8.",
         "Affected by:",
         "- noquit",
         "- armour",
         "",
         PROMPT});
    QCOMPARE(frac.stats.size(), size_t{1});
    QVERIFY(!frac.stats.front().armour.has_value());
    QCOMPARE(frac.stats.front().wimpy, std::optional<int64_t>{150});
    QCOMPARE(frac.stats.front().affects, list({"noquit", "armour"}));
}

void TestCharLines::statVariantsTest()
{
    // powwow/logs/old/new/macho.txt: M_OB between OB and DB.
    {
        const CharReplies r = feed(
            {"OB: 126%, M_OB: 81%, DB: 23%, PB: 0%, Armour: 6%. Wimpy: 0. Mood: aggressive.",
             "Needed: 224,034 xp, 0 tp. Gold: 16. Alert: normal.",
             "Affected by:",
             "- noquit",
             "- novoid",
             "",
             PROMPT});
        QCOMPARE(r.stats.size(), size_t{1});
        QCOMPARE(r.stats.front().ob, std::optional<int64_t>{126});
        QCOMPARE(r.stats.front().db, std::optional<int64_t>{23});
        QCOMPARE(r.stats.front().mood, QString("aggressive"));
    }
    // powwow/logs/old/roma/viir_rip.txt: "Lauren" for gold.
    {
        const CharReplies r = feed(
            {"OB: 80%, DB: 71%, PB: 29%, Armour: 54%. Wimpy: 150. Mood: aggressive.",
             "Needed: 856,386 xp, 0 tp. Wp: 97. Lauren: 73. Alert: normal.",
             "Affected by:",
             "- panic",
             "- noquit",
             "- novoid",
             "- bless",
             "- sense life",
             "",
             PROMPT});
        QCOMPARE(r.stats.front().gold, std::optional<int64_t>{73});
        QCOMPARE(r.stats.front().wp, std::optional<int64_t>{97});
        QCOMPARE(r.stats.front().affects.size(), qsizetype{5});
    }
    // powwow/logs/soft/azazello.txt, level 100: no Needed line at all.
    {
        const CharReplies r = feed(
            {"OB: 104%, DB: 33%, PB: 107%, Armour: 91%. Wimpy: 250. Mood: wimpy.",
             "Gold: 519. Alert: normal.",
             "",
             PROMPT});
        QCOMPARE(r.stats.size(), size_t{1});
        QVERIFY(!r.stats.front().neededXp.has_value());
        QVERIFY(!r.stats.front().neededTp.has_value());
        QCOMPARE(r.stats.front().gold, std::optional<int64_t>{519});
    }
    // Alert paranoid, a gold figure with a separator; two conditions.
    {
        const CharReplies r = feed(
            {"OB: 147%, DB: 51%, PB: 48%, Armour: 90%. Wimpy: 50. Mood: aggressive.",
             "Needed: 2,241,148 xp, 0 tp. Gold: 2,138. Alert: paranoid.",
             "Affected by:",
             "- novoid",
             "- noquit",
             "",
             PROMPT});
        QCOMPARE(r.stats.front().gold, std::optional<int64_t>{2138});
        QCOMPARE(r.stats.front().alert, QString("paranoid"));
        const CharReplies both = feed(
            {"OB: 49%, DB: 48%, PB: 53%, Armour: 75%. Wimpy: 50. Mood: wimpy.",
             "Needed: 253,873 xp, 0 tp. Gold: 1,344. Alert: normal. Condition: hungry thirsty.",
             "Affected by:",
             "- stored spell earthquake",
             "",
             PROMPT});
        QCOMPARE(both.stats.front().condition, list({"hungry", "thirsty"}));
        QCOMPARE(both.stats.front().affects, list({"stored spell earthquake"}));
    }
}

void TestCharLines::statProxyAdditionsTest()
{
    // powwow/logs/archives/log-2006.08.02-17.30.57.txt: Pandora's "Timers:" and "(up for ...)";
    // a wound, which goes to its own list with MUME's word for its state.
    const CharReplies r = feed({"OB: 117%, DB: 45%, PB: 85%, Armour: 61%. Wimpy: 50. Mood: normal.",
                                "Needed: 336,676 xp, 0 tp. Gold: 379. Alert: normal.",
                                "Timers:",
                                "Affected by:",
                                "- a light wound at the head (clean)",
                                "- noquit",
                                "- novoid",
                                "- bless (up for - 01:37)",
                                "- strength (up for - 39:12)",
                                "- potion",
                                "",
                                PROMPT});
    QCOMPARE(r.stats.size(), size_t{1});
    const CharStat &s = r.stats.front();
    QCOMPARE(s.mood, QString("normal"));
    QCOMPARE(s.affects, list({"noquit", "novoid", "bless", "strength", "potion"}));
    QCOMPARE(s.wounds, list({"a light wound at the head (clean)"}));

    // powwow/logs/moria.gjurza.txt: "Countdowns:" lists what the proxy timed, not affects.
    const CharReplies c = feed(
        {"OB: 142%, DB: 35%, PB: 24%, Armour: 74%. Wimpy: 200. Mood: aggressive.",
         "Needed: 901,185 xp, 0 tp. Gold: 18. Alert: normal.",
         "Countdowns:",
         "- blinded < A demon wolf > (up for - 01:25, left - 00:04)",
         "- blinded < The orkish bodyguard > (up for - 00:06, left - 01:23)",
         "Affected by:",
         "- noquit",
         "- armour (up for - 09:10)",
         "- shield (up for - 09:12)",
         "",
         PROMPT});
    QCOMPARE(c.stats.front().affects, list({"noquit", "armour", "shield"}));

    // powwow/logs/soft/azazello.txt: "(unknown time)" is the proxy's too.
    const CharReplies u = feed({"OB: 104%, DB: -2%, PB: 107%, Armour: 93%. Wimpy: 473. Mood: wimpy.",
                                "Gold: 519. Alert: normal.",
                                "Timers:",
                                "Affected by:",
                                "- shield (up for - 04:56)",
                                "- sanctuary (unknown time)",
                                "",
                                PROMPT});
    QCOMPARE(u.stats.front().affects, list({"shield", "sanctuary"}));
}

void TestCharLines::statBlankAndPromptTest()
{
    // powwow/logs/old/xp/balrog: a blank line between the first two lines, and a line of one
    // space closing the list.
    const CharReplies r = feed({"OB: 180%, DB: 17%, PB: 35%, Armour: 5%. Wimpy: 0. Mood: berserk.",
                                "",
                                "Needed: 84,639 xp, 0 tp. Wp: 9. Gold: 19. Alert: normal.",
                                "Affected by:",
                                "- sanctuary",
                                "- bless",
                                "- strength",
                                "- miruvor",
                                " ",
                                PROMPT});
    QCOMPARE(r.stats.size(), size_t{1});
    QCOMPARE(r.stats.front().mood, QString("berserk"));
    QCOMPARE(r.stats.front().neededXp, std::optional<int64_t>{84639});
    QCOMPARE(r.stats.front().affects, list({"sanctuary", "bless", "strength", "miruvor"}));

    // The prompt alone closes it too; the blank line is not needed.
    const CharReplies p = feed({"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.",
                                "Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal.",
                                PROMPT});
    QCOMPARE(p.stats.size(), size_t{1});

    // A line that is no part of it ends it before the second line and is not swallowed.
    CharLinesTracker tracker;
    QVERIFY(tracker.receiveLine("OB: 7%, DB: -45%, PB: 0%, Armour: 0%. Wimpy: 36. Mood: wimpy.")
                .empty());
    const CharReplies cut = tracker.receiveLine("37/37 hits, 66/66 mana, and 13/121 moves.");
    QCOMPARE(cut.stats.size(), size_t{1});
    QCOMPARE(cut.scores.size(), size_t{1});
    QVERIFY(!cut.stats.front().neededXp.has_value());
}

void TestCharLines::scoreLineTest()
{
    // powwow/logs/log-"+year+"...".txt, January 2026, and powwow/logs/stolb.balrog.txt.
    const std::optional<CharScore> now = parseScoreLine("37/37 hits, 66/66 mana, and 13/121 moves.");
    QVERIFY(now.has_value());
    QCOMPARE(now->reply, QString("score"));
    QCOMPARE(now->hp, std::optional<int64_t>{37});
    QCOMPARE(now->maxhp, std::optional<int64_t>{37});
    QCOMPARE(now->mana, std::optional<int64_t>{66});
    QCOMPARE(now->maxmana, std::optional<int64_t>{66});
    QCOMPARE(now->mp, std::optional<int64_t>{13});
    QCOMPARE(now->maxmp, std::optional<int64_t>{121});
    QVERIFY(!now->effectsKnown);
    QVERIFY(!now->ob.has_value());

    const std::optional<CharScore> big = parseScoreLine(
        "523/523 hits, 53/53 mana, and 155/155 moves.");
    QVERIFY(big.has_value());
    QCOMPARE(big->hp, std::optional<int64_t>{523});

    // Older wordings: "hit", and no comma before "and".
    const std::optional<CharScore> hit = parseScoreLine(
        "203/243 hit, 25/112 mana, and 85/125 moves.");
    QVERIFY(hit.has_value());
    QCOMPARE(hit->hp, std::optional<int64_t>{203});
    QCOMPARE(hit->maxmp, std::optional<int64_t>{125});
    const std::optional<CharScore> noComma = parseScoreLine(
        "136/179 hit, 75/134 mana and 47/110 moves.");
    QVERIFY(noComma.has_value());
    QCOMPARE(noComma->mana, std::optional<int64_t>{75});

    // Through the tracker it comes out at once.
    CharLinesTracker tracker;
    const CharReplies r = tracker.receiveLine("37/37 hits, 66/66 mana, and 14/121 moves.");
    QCOMPARE(r.scores.size(), size_t{1});
    QCOMPARE(r.scores.front().mp, std::optional<int64_t>{14});
}

void TestCharLines::infoSheetTest()
{
    // powwow/logs/archives/log-2006.03.06-01.08.16.txt, the reply to `info`.
    const CharReplies r = feed(
        {"You are a male Dwarf.",
         "You are 55 years, 11 months and 29 days old.",
         "You have played 21 days and 14 hours (real time). Session: 4 mins.",
         "This ranks you as Ennor VI (level 58).",
         "You are four feet eight and weigh fourteen stone and three pounds.",
         "Perception: vision 40, hearing 6, smell -21. Alertness: normal.",
         "You must have been sent to Arda to free it from the sorrows that weigh upon it.",
         "You are welcome in Bree, Fornost, the Grey Havens, Rivendell, and the Blue Mountains.",
         "Your equipment weighs one hundred fourteen pounds. Heavy, but we will manage...",
         "Your base abilities are: Str:19 Int:18 Wis:16 Dex:13 Con:14 Wil:14 Per:12.",
         "Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.",
         "Your armour provides an average protection of 77%.",
         "You have 318/318 hit, 81/117 mana, and 135/135 movement points.",
         "Your mood is wimpy. You will flee if your hit points go below 315.",
         "You have scored 49,186,097 experience points and you have 301,832 travel points.",
         "You have gained some renown in battles against the minions of the Dark Lord (20 wp).",
         "You need 813,903 exp. points and 0 travel points to reach the next level.",
         "You have 262 gold coins, 14 silver pennies, and 59 copper pennies.",
         "You are speaking Westron.",
         "You will swim if necessary.",
         "You will try to climb even under unsafe conditions.",
         "",
         "You are subjected to the following temporary effects:",
         "- shield",
         "- strength",
         "- armour",
         "- tiredness",
         "",
         PROMPT});
    QCOMPARE(r.burdens.size(), size_t{1});
    QCOMPARE(r.burdens.front().pounds, int64_t{114});
    QCOMPARE(r.burdens.front().word, QString("Heavy, but we will manage..."));
    QCOMPARE(r.scores.size(), size_t{1});
    QVERIFY(r.stats.empty());
    const CharScore &s = r.scores.front();
    QCOMPARE(s.reply, QString("info"));
    QCOMPARE(s.abilities[0], std::optional<int64_t>{19});
    QCOMPARE(s.abilities[1], std::optional<int64_t>{18});
    QCOMPARE(s.abilities[2], std::optional<int64_t>{16});
    QCOMPARE(s.abilities[3], std::optional<int64_t>{13});
    QCOMPARE(s.abilities[4], std::optional<int64_t>{14});
    QCOMPARE(s.abilities[5], std::optional<int64_t>{14});
    QCOMPARE(s.abilities[6], std::optional<int64_t>{12});
    QCOMPARE(s.ob, std::optional<int64_t>{93});
    QCOMPARE(s.db, std::optional<int64_t>{53});
    QCOMPARE(s.pb, std::optional<int64_t>{93});
    QCOMPARE(s.armour, std::optional<int64_t>{77});
    QCOMPARE(s.hp, std::optional<int64_t>{318});
    QCOMPARE(s.maxhp, std::optional<int64_t>{318});
    QCOMPARE(s.mana, std::optional<int64_t>{81});
    QCOMPARE(s.maxmana, std::optional<int64_t>{117});
    QCOMPARE(s.mp, std::optional<int64_t>{135});
    QCOMPARE(s.maxmp, std::optional<int64_t>{135});
    QCOMPARE(s.mood, QString("wimpy"));
    QCOMPARE(s.wimpy, std::optional<int64_t>{315});
    QCOMPARE(s.xp, std::optional<int64_t>{49186097});
    QCOMPARE(s.tp, std::optional<int64_t>{301832});
    QCOMPARE(s.renown,
             QString(
                 "You have gained some renown in battles against the minions of the Dark Lord."));
    QCOMPARE(s.wp, std::optional<int64_t>{20});
    QCOMPARE(s.neededXp, std::optional<int64_t>{813903});
    QCOMPARE(s.neededTp, std::optional<int64_t>{0});
    QCOMPARE(s.gold, std::optional<int64_t>{262});
    QCOMPARE(s.silver, std::optional<int64_t>{14});
    QCOMPARE(s.copper, std::optional<int64_t>{59});
    QCOMPARE(s.language, QString("Westron"));
    QCOMPARE(s.swim, QString("You will swim if necessary."));
    QCOMPARE(s.climb, QString("You will try to climb even under unsafe conditions."));
    QVERIFY(s.effectsKnown);
    QCOMPARE(s.effects, list({"shield", "strength", "armour", "tiredness"}));
    QVERIFY(s.wounds.isEmpty());
    QVERIFY(s.text.startsWith("You are a male Dwarf."));
    QVERIFY(s.text.endsWith("- tiredness"));
}

void TestCharLines::infoNoArmourTest()
{
    // powwow/logs/stolb.balrog.txt: no armour worn, copper only, no swim line, no effects list,
    // "weighs nothing".
    const CharReplies r = feed(
        {"You are a male Tarkhnarb Orc.",
         "You are 97 years, 10 months and 27 days old.",
         "You have played 2 months, 21 days and 17 hours (real time). Session: 1 min.",
         "This ranks you as Stolb The Master Executor of Damage Inc (level 96).",
         "You are five feet five and weigh ten stone and six pounds.",
         "Perception: vision 28, hearing 13, smell 24. Alertness: normal.",
         "You are totally corrupted by the Evilness of Morgoth!",
         "You are welcome in Goblin Town.",
         "Your equipment weighs nothing. Peanuts.",
         "Your base abilities are: Str:19 Int:6 Wis:7 Dex:13 Con:20 Wil:14 Per:12.",
         "Offensive Bonus: 131%, Dodging Bonus: 24%, Parrying Bonus: 0%.",
         "You are not wearing any armour.",
         "You have 523/523 hit, 53/53 mana, and 155/155 movement points.",
         "Your mood is wimpy. You will flee if your hit points go below 111.",
         "You have scored 146,891,005 experience points and you have 695,761 travel points.",
         "You have been victorious in several battles against the enemies of the Dark Lord (19 wp).",
         "You need 1,108,995 exp. points and 0 travel points to reach the next level.",
         "You have 0 copper.",
         "You are speaking Orkish.",
         "You will try to climb even under unsafe conditions.",
         "",
         PROMPT});
    QCOMPARE(r.burdens.size(), size_t{1});
    QCOMPARE(r.burdens.front().pounds, int64_t{0});
    QCOMPARE(r.burdens.front().word, QString("Peanuts."));
    QCOMPARE(r.scores.size(), size_t{1});
    const CharScore &s = r.scores.front();
    QCOMPARE(s.armour, std::optional<int64_t>{0});
    QCOMPARE(s.copper, std::optional<int64_t>{0});
    QVERIFY(!s.gold.has_value());
    QVERIFY(!s.silver.has_value());
    QVERIFY(s.swim.isEmpty());
    QCOMPARE(s.language, QString("Orkish"));
    QCOMPARE(s.wp, std::optional<int64_t>{19});
    // Complete at the prompt, without a list: no effects.
    QVERIFY(s.effectsKnown);
    QVERIFY(s.effects.isEmpty());
}

void TestCharLines::infoMissingArmourTest()
{
    // No sheet in the logs lacks its armour line; this is powwow/logs/soft/azazello.txt's with
    // that line taken out, to show that a missing line leaves the figure unset, not zero.
    const CharReplies r = feed({"Offensive Bonus: 104%, Dodging Bonus: 33%, Parrying Bonus: 107%.",
                                "You have 474/474 hit, 80/80 mana, and 154/154 movement points.",
                                "Your mood is wimpy. You will flee if your hit points go below 473.",
                                "",
                                PROMPT});
    QCOMPARE(r.scores.size(), size_t{1});
    QVERIFY(!r.scores.front().armour.has_value());
    QCOMPARE(r.scores.front().ob, std::optional<int64_t>{104});
    QCOMPARE(r.scores.front().hp, std::optional<int64_t>{474});
}

void TestCharLines::infoOldWordingTest()
{
    // powwow/logs/old/new/2.txt: "You are carrying N pounds of equipment.", no mana, no mood
    // line, a renown line without "(N wp)".
    {
        const CharReplies r = feed(
            {"You are a female Mountain Troll.",
             "You are 47 years, 5 months, and 18 days old.",
             "You have played 7 days and 19 hours (real time).",
             "This ranks you as Duvel the Grand Master of Martial Arts (level 35).",
             "You are six feet ten and weigh thirty-six stone and four pounds.",
             "Perception: vision 28, hearing 25, smell 26. Alertness: normal.",
             "You are totally corrupted by the Evilness of Morgoth!",
             "You are carrying 0 pounds of equipment. Peanuts.",
             "Your base abilities are: Str:24 Int:6 Wis:5 Dex:13 Con:22 Wil:16 Per:8.",
             "Offensive Bonus: 100%, Dodging Bonus: -34%, Parrying Bonus: 77%.",
             "You are not wearing any armour.",
             "You have 4/633 hit, and 90/133 movement points.",
             "You have scored 15,831,700 experience points and you have 33,857 travel points.",
             "You have fought various battles for the forces of the Dark Lord.",
             "You need 218,300 exp. points and 0 travel points to reach the next level.",
             "You have 0 copper.",
             "You are speaking Morbeth.",
             "",
             PROMPT});
        QCOMPARE(r.burdens.size(), size_t{1});
        QCOMPARE(r.burdens.front().pounds, int64_t{0});
        QCOMPARE(r.scores.size(), size_t{1});
        const CharScore &s = r.scores.front();
        QCOMPARE(s.db, std::optional<int64_t>{-34});
        QCOMPARE(s.hp, std::optional<int64_t>{4});
        QCOMPARE(s.maxhp, std::optional<int64_t>{633});
        QVERIFY(!s.mana.has_value());
        QCOMPARE(s.maxmp, std::optional<int64_t>{133});
        QVERIFY(s.mood.isEmpty());
        QVERIFY(!s.wimpy.has_value());
        QCOMPARE(s.renown,
                 QString("You have fought various battles for the forces of the Dark Lord."));
        QVERIFY(!s.wp.has_value());
        QCOMPARE(s.neededXp, std::optional<int64_t>{218300});
    }
    // powwow/logs/luke/logs/space.txt: the language first, "armour absorbs", no separators,
    // coins without commas, and lines this reader does not know.
    {
        const CharReplies r = feed(
            {"You are speaking in orcish.",
             "You are a male Orc.",
             "You are 36 years, 4 months and 0 days old.",
             "You have played 7 days and 8 hours (real time).",
             "This ranks you as Space the Uruk-hai (level 22).",
             "Your height is 5 feet, 9 inches and you weigh 9 stone, 8 pounds.",
             "Perception: vision 0 hearing -24 smelling -24.",
             "You are totally corrupted by the Evilness of Morgoth !",
             "You are a citizen of: GoblinTown",
             "You are carrying 130 pounds of equipment. Heavy, but we will manage...",
             "Your base abilities are: Str:18 Int:9 Wi s:10 Dex:16 Con:17 Wil:11 Per:11.",
             "Offensive Bonus: 133%, Dodging Bonus: 13%, Parrying Bonus: 33%.",
             "Your armour absorbs, on average, 90% of damage.",
             "You have 314/314 hit, 13/59 mana and 124/150 movement points.",
             "You have scored 4607876 experience points and you have 19681 travel points.",
             "You need 592124 exp. points and 619 travel points to reach the next level.",
             "You have 44 gold coins 16 silver pennies 37 copper pennies.",
             "You are standing.",
             "",
             PROMPT});
        QCOMPARE(r.burdens.size(), size_t{1});
        QCOMPARE(r.burdens.front().pounds, int64_t{130});
        QCOMPARE(r.scores.size(), size_t{1});
        const CharScore &s = r.scores.front();
        QCOMPARE(s.language, QString("orcish"));
        // The log's own "Wi s:10" is read as far as it goes: Wis is left out.
        QCOMPARE(s.abilities[1], std::optional<int64_t>{9});
        QVERIFY(!s.abilities[2].has_value());
        QCOMPARE(s.abilities[3], std::optional<int64_t>{16});
        QCOMPARE(s.armour, std::optional<int64_t>{90});
        QCOMPARE(s.mana, std::optional<int64_t>{13});
        QCOMPARE(s.xp, std::optional<int64_t>{4607876});
        QCOMPARE(s.neededTp, std::optional<int64_t>{619});
        QCOMPARE(s.gold, std::optional<int64_t>{44});
        QCOMPARE(s.silver, std::optional<int64_t>{16});
        QCOMPARE(s.copper, std::optional<int64_t>{37});
        // "You need ..." came straight after the experience line: no renown line.
        QVERIFY(s.renown.isEmpty());
    }
}

void TestCharLines::infoHighestLevelTest()
{
    // powwow/logs/soft/azazello.txt, level 100.
    const CharReplies r = feed(
        {"Your equipment weighs one hundred thirty-three pounds. Heavy, but we will manage...",
         "Your base abilities are: Str:18 Int:11 Wis:11 Dex:16 Con:19 Wil:12 Per:14.",
         "Offensive Bonus: 104%, Dodging Bonus: 33%, Parrying Bonus: 107%.",
         "Your armour provides an average protection of 91%.",
         "You have 474/474 hit, 80/80 mana, and 154/154 movement points.",
         "Your mood is wimpy. You will flee if your hit points go below 473.",
         "You have scored 159,124,674 experience points and you have 395,275 travel points.",
         "You are not known for any acts of war.",
         "You have reached the highest level.",
         "You have 519 gold coins and 6 silver pennies.",
         "You are trying to speak Westron.",
         "You will swim if necessary.",
         "You will try to climb even under unsafe conditions.",
         "",
         PROMPT});
    QCOMPARE(r.burdens.front().pounds, int64_t{133});
    QCOMPARE(r.scores.size(), size_t{1});
    const CharScore &s = r.scores.front();
    QCOMPARE(s.xp, std::optional<int64_t>{159124674});
    QCOMPARE(s.renown, QString("You are not known for any acts of war."));
    QVERIFY(!s.neededXp.has_value());
    QCOMPARE(s.gold, std::optional<int64_t>{519});
    QCOMPARE(s.silver, std::optional<int64_t>{6});
    QVERIFY(!s.copper.has_value());
    QCOMPARE(s.language, QString("Westron"));

    // "You will fight to the death." is a wimpy of 0. From powwow/logs, less its coins line,
    // which the log has broken in two.
    const CharReplies death = feed(
        {"Your mood is aggressive. You will fight to the death.",
         "You have scored 20,968,620 experience points and you have 98,081 travel points.",
         "You have fought a few battles for the forces of the Dark Lord (3 wp).",
         "You need 881,380 exp. points and 0 travel points to reach the next level.",
         "You are speaking Orkish.",
         "You will swim if necessary.",
         "You will try to climb even under unsafe conditions.",
         "",
         PROMPT});
    QCOMPARE(death.scores.size(), size_t{1});
    QCOMPARE(death.scores.front().wimpy, std::optional<int64_t>{0});
    QCOMPARE(death.scores.front().mood, QString("aggressive"));
    QCOMPARE(death.scores.front().wp, std::optional<int64_t>{3});

    // powwow/logs/trolls_roots.txt: the third way to climb.
    const CharReplies safe = feed(
        {"You have 137/137 hit, 139/139 mana, and 81/81 movement points.",
         "Your mood is wimpy. You will flee if your hit points go below 50.",
         "You have scored 12,374,414 experience points and you have 71,449 travel points.",
         "You are not known for any acts of war.",
         "You need 675,586 exp. points and 0 travel points to reach the next level.",
         "You have 127 gold coins, 4 silver pennies, and 63 copper pennies.",
         "You are speaking Sindarin.",
         "You will swim if necessary.",
         "You will climb only when it is reasonably safe to do so.",
         "",
         PROMPT});
    QCOMPARE(safe.scores.size(), size_t{1});
    QCOMPARE(safe.scores.front().climb,
             QString("You will climb only when it is reasonably safe to do so."));
    QCOMPARE(safe.scores.front().language, QString("Sindarin"));
}

void TestCharLines::infoLiveTest()
{
    // The reply to `info` on 2026-10-02 (a level 2 character), whole and in MUME's order; not
    // from the powwow logs.
    const CharReplies r = feed(
        {"You are a male Eriadorian.",
         "You are 19 years and 6 months old.",
         "You have played 4 hours (real time). Session: 10 mins.",
         "This ranks you as Idwar the Man Adventurer (level 2).",
         "You are five feet nine and weigh eleven stone and eleven pounds.",
         "Perception: vision 40, hearing -31, smell -60. Alertness: normal.",
         "You are a well-meaning person, always glad to help your friends.",
         "You are welcome in Fornost.",
         "Your equipment weighs forty-nine pounds. A tad uncomfortable, but no problem.",
         "Your base abilities are: Str:18 Int:11 Wis:9 Dex:17 Con:16 Wil:15 Per:15.",
         "Offensive Bonus: 20%, Dodging Bonus: -24%, Parrying Bonus: 26%.",
         "Your armour provides an average protection of 19%.",
         "You have 52/52 hit, 64/64 mana, and 126/126 movement points.",
         "Your mood is wimpy. You will flee if your hit points go below 13.",
         "You have scored 1,478 experience points and you have 241 travel points.",
         "You are not known for any acts of war.",
         "You need 1,522 exp. points and 59 travel points to reach the next level.",
         "You have 3 silver pennies and 70 copper pennies.",
         "You are speaking Westron.",
         PROMPT});
    QCOMPARE(r.burdens.size(), size_t{1});
    QCOMPARE(r.burdens.front().pounds, int64_t{49});
    QCOMPARE(r.burdens.front().word, QString("A tad uncomfortable, but no problem."));
    QVERIFY(r.stats.empty());
    QCOMPARE(r.wimpies.size(), size_t{1});
    QCOMPARE(r.wimpies.front().wimpy, int64_t{13});
    QCOMPARE(r.scores.size(), size_t{1});
    const CharScore &s = r.scores.front();
    QCOMPARE(s.reply, QString("info"));

    // The head of the sheet.
    QCOMPARE(s.sex, QString("male"));
    QCOMPARE(s.race, QString("Eriadorian"));
    QCOMPARE(s.ageYears, std::optional<int64_t>{19});
    QCOMPARE(s.ageMonths, std::optional<int64_t>{6});
    QVERIFY(!s.ageDays.has_value());
    QCOMPARE(s.played, QString("4 hours"));
    QCOMPARE(s.session, QString("10 mins"));
    QCOMPARE(s.name, QString("Idwar"));
    QCOMPARE(s.title, QString("the Man Adventurer"));
    QCOMPARE(s.level, std::optional<int64_t>{2});
    QCOMPARE(s.height, QString("five feet nine"));
    QCOMPARE(s.weight, QString("eleven stone and eleven pounds"));
    QCOMPARE(s.vision, std::optional<int64_t>{40});
    QCOMPARE(s.hearing, std::optional<int64_t>{-31});
    QCOMPARE(s.smell, std::optional<int64_t>{-60});
    QCOMPARE(s.alertness, QString("normal"));
    QCOMPARE(s.alignment,
             QString("You are a well-meaning person, always glad to help your friends."));
    QCOMPARE(s.welcome, list({"Fornost"}));

    // The figures, as before.
    QCOMPARE(s.abilities[0], std::optional<int64_t>{18});
    QCOMPARE(s.abilities[1], std::optional<int64_t>{11});
    QCOMPARE(s.abilities[2], std::optional<int64_t>{9});
    QCOMPARE(s.abilities[3], std::optional<int64_t>{17});
    QCOMPARE(s.abilities[4], std::optional<int64_t>{16});
    QCOMPARE(s.abilities[5], std::optional<int64_t>{15});
    QCOMPARE(s.abilities[6], std::optional<int64_t>{15});
    QCOMPARE(s.ob, std::optional<int64_t>{20});
    QCOMPARE(s.db, std::optional<int64_t>{-24});
    QCOMPARE(s.pb, std::optional<int64_t>{26});
    QCOMPARE(s.armour, std::optional<int64_t>{19});
    QCOMPARE(s.hp, std::optional<int64_t>{52});
    QCOMPARE(s.maxhp, std::optional<int64_t>{52});
    QCOMPARE(s.mana, std::optional<int64_t>{64});
    QCOMPARE(s.maxmana, std::optional<int64_t>{64});
    QCOMPARE(s.mp, std::optional<int64_t>{126});
    QCOMPARE(s.maxmp, std::optional<int64_t>{126});
    QCOMPARE(s.mood, QString("wimpy"));
    QCOMPARE(s.wimpy, std::optional<int64_t>{13});
    QCOMPARE(s.xp, std::optional<int64_t>{1478});
    QCOMPARE(s.tp, std::optional<int64_t>{241});
    // The sentence is `war`, and `renown` as it always was.
    QCOMPARE(s.war, QString("You are not known for any acts of war."));
    QCOMPARE(s.renown, QString("You are not known for any acts of war."));
    QVERIFY(!s.wp.has_value());
    QCOMPARE(s.neededXp, std::optional<int64_t>{1522});
    QCOMPARE(s.neededTp, std::optional<int64_t>{59});
    QVERIFY(!s.gold.has_value());
    QCOMPARE(s.silver, std::optional<int64_t>{3});
    QCOMPARE(s.copper, std::optional<int64_t>{70});
    QCOMPARE(s.language, QString("Westron"));
    QVERIFY(s.swim.isEmpty());
    QVERIFY(s.climb.isEmpty());
    QVERIFY(s.effectsKnown);
    QVERIFY(s.effects.isEmpty());
    QVERIFY(s.wounds.isEmpty());
    QVERIFY(s.text.startsWith("You are a male Eriadorian."));
    QVERIFY(s.text.endsWith("You are speaking Westron."));
    QCOMPARE(s.text.split(QLatin1Char('\n')).size(), qsizetype{19});
}

void TestCharLines::languagesTest()
{
    // powwow/logs (a bare `cha lang`): the heading, a row per language, the star on the one
    // spoken, a blank line.
    const CharReplies r = feed({"You have the following knowledge in these languages:",
                                "   60   Westron             ",
                                "   49   Khuzdul             ",
                                "  100 * Orkish              ",
                                "   31   Animal              ",
                                "",
                                PROMPT});
    QCOMPARE(r.languages.size(), size_t{1});
    const CharLanguages &l = r.languages.front();
    QCOMPARE(l.rows.size(), size_t{4});
    QCOMPARE(l.rows[0].name, QString("Westron"));
    QCOMPARE(l.rows[0].knowledge, int64_t{60});
    QVERIFY(!l.rows[0].speaking);
    QCOMPARE(l.rows[2].name, QString("Orkish"));
    QVERIFY(l.rows[2].speaking);
    QVERIFY(r.scores.empty() && r.stats.empty());
    // Closed by the prompt as well, with no blank line.
    const CharReplies cut = feed({"You have the following knowledge in these languages:",
                                  "   60   Westron", PROMPT});
    QCOMPARE(cut.languages.size(), size_t{1});
    QCOMPARE(cut.languages.front().rows.size(), size_t{1});
}

void TestCharLines::infoHeadLinesTest()
{
    // The head's lines state no figure, so each is fed with one that does.
    const char *const FIGURE = "Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.";

    // Perception: the live line with its figures changed, and powwow/logs/luke/logs/space.txt's
    // old wording, which names no alertness.
    {
        const CharReplies r = feed(
            {"Perception: vision -5, hearing -31, smell -60. Alertness: paranoid.", FIGURE, PROMPT});
        QCOMPARE(r.scores.size(), size_t{1});
        QCOMPARE(r.scores.front().vision, std::optional<int64_t>{-5});
        QCOMPARE(r.scores.front().hearing, std::optional<int64_t>{-31});
        QCOMPARE(r.scores.front().smell, std::optional<int64_t>{-60});
        QCOMPARE(r.scores.front().alertness, QString("paranoid"));
        const CharReplies old = feed(
            {"Perception: vision 0 hearing -24 smelling -24.", FIGURE, PROMPT});
        QCOMPARE(old.scores.size(), size_t{1});
        QCOMPARE(old.scores.front().vision, std::optional<int64_t>{0});
        QCOMPARE(old.scores.front().hearing, std::optional<int64_t>{-24});
        QCOMPARE(old.scores.front().smell, std::optional<int64_t>{-24});
        QVERIFY(old.scores.front().alertness.isEmpty());
    }

    // The alignment sentence: the live one, and the logs' two with their old spellings.
    for (const char *const line :
         {"You are a well-meaning person, always glad to help your friends.",
          "You must have been sent to Arda to free it from the sorrows that weigh upon it.",
          "You must have been sent on Arda to free it from the sorrows that weigh on it !",
          "You are totally corrupted by the Evilness of Morgoth!",
          "You are totally corrupted by the Evilness of Morgoth !"}) {
        const CharReplies r = feed({line, FIGURE, PROMPT});
        QVERIFY2(r.scores.size() == 1, line);
        QCOMPARE(r.scores.front().alignment, QString(line));
    }
    {
        // A wording not seen (this one is made up) is taken by its place after the perception
        // line, and only there: elsewhere it is none of the sheet's own.
        const char *const unseen = "You are a force for good in these lands.";
        const CharReplies placed = feed(
            {"Perception: vision 40, hearing 6, smell -21. Alertness: normal.",
             unseen,
             FIGURE,
             PROMPT});
        QCOMPARE(placed.scores.size(), size_t{1});
        QCOMPARE(placed.scores.front().alignment, QString(unseen));
        const CharReplies stray = feed({FIGURE, unseen, PROMPT});
        QCOMPARE(stray.scores.size(), size_t{1});
        QVERIFY(stray.scores.front().alignment.isEmpty());
        QVERIFY(stray.scores.front().text.endsWith(QString(unseen)));
        // Other "You are ..." lines stay what they were.
        const CharReplies other = feed({"You are speaking Westron.", PROMPT});
        QCOMPARE(other.scores.size(), size_t{1});
        QVERIFY(other.scores.front().alignment.isEmpty());
        QCOMPARE(other.scores.front().language, QString("Westron"));
    }

    // Welcome: two lines add up (the second is made up; MUME prints one), and the logs' list
    // with and without the comma before "and".
    {
        const CharReplies two = feed({"You are welcome in Fornost.",
                                      "You are welcome in Bree and the Grey Havens.",
                                      FIGURE,
                                      PROMPT});
        QCOMPARE(two.scores.size(), size_t{1});
        QCOMPARE(two.scores.front().welcome, list({"Fornost", "Bree", "the Grey Havens"}));
        const CharReplies many = feed(
            {"You are welcome in Bree, Fornost, the Grey Havens, Rivendell, and the Blue "
             "Mountains.",
             FIGURE,
             PROMPT});
        QCOMPARE(many.scores.front().welcome,
                 list({"Bree", "Fornost", "the Grey Havens", "Rivendell", "the Blue Mountains"}));
        const CharReplies noComma = feed(
            {"You are welcome in Bree, Fornost, the Grey Havens, Rivendell and the Blue Mountains.",
             FIGURE,
             PROMPT});
        QCOMPARE(noComma.scores.front().welcome,
                 list({"Bree", "Fornost", "the Grey Havens", "Rivendell", "the Blue Mountains"}));
    }

    // Lines of powwow/logs/archives/log-2006.03.06-01.08.16.txt and stolb.balrog.txt put
    // together: days in the age, a longer time played, a title that is no "the ...", and a
    // rank with no title at all (the logs' "This ranks you as Sardar  (level 26).").
    {
        const CharReplies r = feed(
            {"You are a male Tarkhnarb Orc.",
             "You are 55 years, 11 months and 29 days old.",
             "You have played 2 months, 21 days and 17 hours (real time). Session: 1 min.",
             "This ranks you as Ennor VI (level 58).",
             "You are four feet eight and weigh fourteen stone and three pounds.",
             FIGURE,
             PROMPT});
        QCOMPARE(r.scores.size(), size_t{1});
        const CharScore &s = r.scores.front();
        QCOMPARE(s.sex, QString("male"));
        QCOMPARE(s.race, QString("Tarkhnarb Orc"));
        QCOMPARE(s.ageYears, std::optional<int64_t>{55});
        QCOMPARE(s.ageMonths, std::optional<int64_t>{11});
        QCOMPARE(s.ageDays, std::optional<int64_t>{29});
        QCOMPARE(s.played, QString("2 months, 21 days and 17 hours"));
        QCOMPARE(s.session, QString("1 min"));
        QCOMPARE(s.name, QString("Ennor"));
        QCOMPARE(s.title, QString("VI"));
        QCOMPARE(s.level, std::optional<int64_t>{58});
        QCOMPARE(s.height, QString("four feet eight"));
        QCOMPARE(s.weight, QString("fourteen stone and three pounds"));
        QVERIFY(s.alignment.isEmpty());
        QVERIFY(s.welcome.isEmpty());
        QVERIFY(!s.vision.has_value());

        const CharReplies bare = feed({"You have played 7 days and 19 hours (real time).",
                                       "This ranks you as Sardar  (level 26).",
                                       FIGURE,
                                       PROMPT});
        QCOMPARE(bare.scores.size(), size_t{1});
        QCOMPARE(bare.scores.front().played, QString("7 days and 19 hours"));
        QVERIFY(bare.scores.front().session.isEmpty());
        QCOMPARE(bare.scores.front().name, QString("Sardar"));
        QVERIFY(bare.scores.front().title.isEmpty());
        QCOMPARE(bare.scores.front().level, std::optional<int64_t>{26});
    }

    // `war` is the one sentence, wherever it stands; another renown line leaves it empty.
    {
        const CharReplies alone = feed({"You are not known for any acts of war.", FIGURE, PROMPT});
        QCOMPARE(alone.scores.size(), size_t{1});
        QCOMPARE(alone.scores.front().war, QString("You are not known for any acts of war."));
        QVERIFY(alone.scores.front().renown.isEmpty());
        const CharReplies fought = feed(
            {"You have scored 20,968,620 experience points and you have 98,081 travel points.",
             "You have fought a few battles for the forces of the Dark Lord (3 wp).",
             PROMPT});
        QCOMPARE(fought.scores.size(), size_t{1});
        QVERIFY(fought.scores.front().war.isEmpty());
        QCOMPARE(fought.scores.front().renown,
                 QString("You have fought a few battles for the forces of the Dark Lord."));
        QCOMPARE(fought.scores.front().wp, std::optional<int64_t>{3});
    }

    // The head alone is no sheet to publish, as before.
    QVERIFY(feed({"You are a male Dwarf.", "You are welcome in Tharbad.", PROMPT}).scores.empty());
}

void TestCharLines::burdenTest()
{
    // Each of MUME's words for a burden, in lines from the logs. The word goes by the weight
    // against the character's strength, not by pounds alone.
    const struct
    {
        const char *line;
        int64_t pounds;
        const char *word;
    } cases[] = {
        {"Your equipment weighs seventy-nine pounds. Heavy, but we will manage...",
         79,
         "Heavy, but we will manage..."},
        {"Your equipment weighs one hundred twenty-one pounds. Heavy, but we will manage...",
         121,
         "Heavy, but we will manage..."},
        {"Your equipment weighs nothing. Peanuts.", 0, "Peanuts."},
        {"Your equipment weighs fifty pounds. A tad uncomfortable, but no problem.",
         50,
         "A tad uncomfortable, but no problem."},
        {"Your equipment weighs one hundred six pounds. That is REALLY heavy!",
         106,
         "That is REALLY heavy!"},
        {"Your equipment weighs one hundred seventy-three pounds. Every single move is sheer "
         "torture...",
         173,
         "Every single move is sheer torture..."},
        {"Your equipment weighs two hundred forty-eight pounds. You are trying to forget the "
         "pain...",
         248,
         "You are trying to forget the pain..."},
    };
    for (const auto &c : cases) {
        const std::optional<CharBurden> b = parseBurdenLine(c.line);
        QVERIFY2(b.has_value(), c.line);
        QCOMPARE(b->pounds, c.pounds);
        QCOMPARE(b->word, QString(c.word));
        QCOMPARE(b->text, QString(c.line));
    }
    QVERIFY(
        !parseBurdenLine("Your equipment weighs a lot. Heavy, but we will manage...").has_value());

    // Alone, the line is still published: `info` may be cut short, or the rest gagged.
    CharLinesTracker tracker;
    const CharReplies r = tracker.receiveLine(
        "Your equipment weighs eighty pounds. Heavy, but we will manage...");
    QCOMPARE(r.burdens.size(), size_t{1});
    QVERIFY(tracker.receivePrompt().scores.empty());
}

void TestCharLines::numberWordsTest()
{
    QCOMPARE(parseNumberWords("zero"), std::optional<int64_t>{0});
    QCOMPARE(parseNumberWords("seventeen"), std::optional<int64_t>{17});
    QCOMPARE(parseNumberWords("seventy-nine"), std::optional<int64_t>{79});
    QCOMPARE(parseNumberWords("one hundred"), std::optional<int64_t>{100});
    QCOMPARE(parseNumberWords("one hundred and four"), std::optional<int64_t>{104});
    QCOMPARE(parseNumberWords("two hundred thirty-three"), std::optional<int64_t>{233});
    QCOMPARE(parseNumberWords("one thousand two hundred"), std::optional<int64_t>{1200});
    QVERIFY(!parseNumberWords("").has_value());
    QVERIFY(!parseNumberWords("heavy").has_value());
}

void TestCharLines::notCharTest()
{
    CharLinesTracker tracker;
    // The reply to `wimpy`, and speech that quotes a stat line, are not `stat`.
    QVERIFY(tracker.receiveLine("Wimpy set to 0 hit points. Mood: normal.").empty());
    QVERIFY(tracker.receiveLine("Zubr says 'OB: 60%, DB: 62%, PB: 60%, Armour: 66%.'").empty());
    // Coins alone may answer something else.
    QVERIFY(tracker.receiveLine("You have 1,000 gold coins.").empty());
    QVERIFY(tracker.receiveLine("You have reached the highest level.").empty());
    QVERIFY(tracker.receivePrompt().empty());
    QVERIFY(!parseScoreLine("You have 318/318 hit, 81/117 mana, and 135/135 movement points.")
                 .has_value());
    // A lone swim line is published, but claims nothing about the effects.
    QVERIFY(tracker.receiveLine("You will swim if necessary.").empty());
    const CharReplies swim = tracker.receivePrompt();
    QCOMPARE(swim.scores.size(), size_t{1});
    QVERIFY(!swim.scores.front().effectsKnown);
}

void TestCharLines::levelLineTest()
{
    // CHAR_LEVEL_REQUEST is MMapper's own format, so no log has its reply. That `info`'s format
    // keys print a bare number is from powwow/logs/archives/log-2006.02.06-18.58.18.txt:183090
    // ("info %t" answered "46523"); the figures are those of the sheet in
    // powwow/logs/archives/log-2005.12.22-02.05.51.txt (level 56, "You have scored 45,370,716
    // experience points and you have 271,013 travel points.", "You need 1,029,284 exp. points and
    // 0 travel points to reach the next level.").
    QCOMPARE(QString(CHAR_LEVEL_REQUEST), QString("info MMXP %l %x %X %t %T"));
    const std::optional<CharLevel> bare = parseCharLevelLine("MMXP 56 45370716 1029284 271013 0");
    QVERIFY(bare.has_value());
    QCOMPARE(bare->level, int64_t{56});
    QCOMPARE(bare->xp, std::optional<int64_t>{45370716});
    QCOMPARE(bare->neededXp, std::optional<int64_t>{1029284});
    QCOMPARE(bare->tp, std::optional<int64_t>{271013});
    QCOMPARE(bare->neededTp, std::optional<int64_t>{0});
    QCOMPARE(bare->text, QString("MMXP 56 45370716 1029284 271013 0"));

    // Thousand separators (the player's setting, or %,), colour, a line ending and padding.
    const std::optional<CharLevel> commas = parseCharLevelLine(
        "\x1b[32mMMXP  56 45,370,716 1,029,284 271,013 0\x1b[0m\r\n");
    QVERIFY(commas.has_value());
    QCOMPARE(commas->level, int64_t{56});
    QCOMPARE(commas->xp, std::optional<int64_t>{45370716});
    QCOMPARE(commas->neededXp, std::optional<int64_t>{1029284});
    QCOMPARE(commas->tp, std::optional<int64_t>{271013});

    // Enough experience but travel points short: both figures are kept as MUME printed them.
    const std::optional<CharLevel> tpShort = parseCharLevelLine("MMXP 25 7200000 0 40100 1900");
    QVERIFY(tpShort.has_value());
    QCOMPARE(tpShort->neededXp, std::optional<int64_t>{0});
    QCOMPARE(tpShort->neededTp, std::optional<int64_t>{1900});
    QVERIFY(parseCharLevelLine("MMXP 25 7200000 -5 40100 1900")->neededXp == int64_t{-5});

    // What MUME prints at the highest level is unknown: a word is left unset, not zero.
    const std::optional<CharLevel> top = parseCharLevelLine("MMXP 100 158000000 none 288600 none");
    QVERIFY(top.has_value());
    QVERIFY(!top->neededXp.has_value());
    QVERIFY(!top->neededTp.has_value());
    QCOMPARE(top->xp, std::optional<int64_t>{158000000});

    // Not the line.
    QVERIFY(!parseCharLevelLine("MMXP").has_value());
    QVERIFY(!parseCharLevelLine("MMXP 56 45370716 1029284 271013").has_value());
    QVERIFY(!parseCharLevelLine("MMXP 56 45370716 1029284 271013 0 7").has_value());
    QVERIFY(!parseCharLevelLine("MMXP level 45370716 1029284 271013 0").has_value());
    QVERIFY(!parseCharLevelLine("Bob says 'MMXP 56 1 2 3 4'").has_value());
    QVERIFY(!parseCharLevelLine("46523").has_value());

    // The stat and info readers take nothing from it, and it does not end or spoil a sheet
    // that is open around it.
    CharLinesTracker tracker;
    QVERIFY(tracker.receiveLine("MMXP 56 45370716 1029284 271013 0").empty());
    QVERIFY(tracker.receivePrompt().empty());
    QVERIFY(tracker.receiveLine("Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.")
                .empty());
    QVERIFY(tracker.receiveLine("MMXP 56 45370716 1029284 271013 0").empty());
    QVERIFY(tracker.receiveLine("Your armour provides an average protection of 77%.").empty());
    const CharReplies sheet = tracker.receivePrompt();
    QCOMPARE(sheet.scores.size(), size_t{1});
    QCOMPARE(sheet.scores.front().ob, std::optional<int64_t>{93});
    QCOMPARE(sheet.scores.front().armour, std::optional<int64_t>{77});
    QVERIFY(!sheet.scores.front().xp.has_value());
    QVERIFY(!sheet.scores.front().text.contains("MMXP"));
}

void TestCharLines::wimpyTest()
{
    // powwow/logs/archives/log-2006.02.09-19.48.52.txt:488-489: "cha wimpy 120" answered
    // "Wimpy set to: 120"; Mochomurka/.../logs/thundur.txt:865 has it set to 0.
    const std::optional<CharWimpy> set = parseWimpyLine("Wimpy set to: 120");
    QVERIFY(set.has_value());
    QCOMPARE(set->wimpy, int64_t{120});
    QCOMPARE(parseWimpyLine("Wimpy set to: 0")->wimpy, int64_t{0});
    // Colour, a line ending, a thousand separator and a full stop are tolerated.
    QCOMPARE(parseWimpyLine("\x1b[32mWimpy set to: 1,200.\x1b[0m\r\n")->wimpy, int64_t{1200});

    // Not the reply: another game's wording (log-2005.09.03-02.28.34.txt:161), speech, a
    // figure that is none.
    QVERIFY(!parseWimpyLine("Wimpy set to 0 hit points. Mood: wimpy.").has_value());
    QVERIFY(!parseWimpyLine("Zubr says 'Wimpy set to: 120'").has_value());
    QVERIFY(!parseWimpyLine("Wimpy set to: high").has_value());
    QVERIFY(!parseWimpyLine("Wimpy set to: 120 hit points").has_value());

    // The tracker gives it at once, and it neither ends nor spoils a sheet open around it.
    CharLinesTracker tracker;
    const CharReplies reply = tracker.receiveLine("Wimpy set to: 165");
    QCOMPARE(reply.wimpies.size(), size_t{1});
    QCOMPARE(reply.wimpies.front().wimpy, int64_t{165});
    QVERIFY(reply.stats.empty() && reply.scores.empty() && reply.burdens.empty());
    QVERIFY(tracker.receivePrompt().empty());
    QVERIFY(tracker.receiveLine("Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.")
                .empty());
    QCOMPARE(tracker.receiveLine("Wimpy set to: 50").wimpies.size(), size_t{1});
    const CharReplies sheet = tracker.receivePrompt();
    QCOMPARE(sheet.scores.size(), size_t{1});
    QVERIFY(!sheet.scores.front().text.contains("Wimpy set"));
    // That sheet stated no wimpy, so none is repeated.
    QVERIFY(sheet.wimpies.empty());

    // `stat` and `info` state it too, and it comes out with them: one figure for a frontend,
    // whichever reply was the last.
    const CharReplies stat = feed({"OB: 131%, DB: 24%, PB: 0%, Armour: 0%. Wimpy: 111. Mood: wimpy.",
                                   "Needed: 1,108,995 xp, 0 tp. Gold: 0. Alert: normal.",
                                   "",
                                   PROMPT});
    QCOMPARE(stat.stats.size(), size_t{1});
    QCOMPARE(stat.wimpies.size(), size_t{1});
    QCOMPARE(stat.wimpies.front().wimpy, int64_t{111});
    const CharReplies info = feed({"Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.",
                                   "Your mood is wimpy. You will flee if your hit points go below "
                                   "315.",
                                   PROMPT});
    QCOMPARE(info.scores.size(), size_t{1});
    QCOMPARE(info.wimpies.size(), size_t{1});
    QCOMPARE(info.wimpies.front().wimpy, int64_t{315});
    // A `stat` line without the figure (old wordings) repeats none.
    const CharReplies bare = feed({"OB: 60%, DB: 62%, PB: 60%, Armour: 66%.", "", PROMPT});
    QCOMPARE(bare.stats.size(), size_t{1});
    QVERIFY(bare.wimpies.empty());
}

void TestCharLines::resetTest()
{
    CharLinesTracker tracker;
    QVERIFY(tracker.receiveLine("OB: 7%, DB: -45%, PB: 0%, Armour: 0%. Wimpy: 36. Mood: wimpy.")
                .empty());
    tracker.reset();
    QVERIFY(tracker.receivePrompt().empty());
    QVERIFY(tracker.receiveLine("Offensive Bonus: 93%, Dodging Bonus: 53%, Parrying Bonus: 93%.")
                .empty());
    tracker.reset();
    QVERIFY(tracker.receivePrompt().empty());
}

QTEST_MAIN(TestCharLines)
