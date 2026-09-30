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
