// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestCombatLines.h"

#include "../src/parser/CombatLines.h"

#include <QtTest/QtTest>

// Every line here is taken from real MUME transcripts in the powwow logs.

namespace {

NODISCARD CombatEvent parsed(const char *const line)
{
    const std::optional<CombatEvent> event = parseCombatLine(QString::fromUtf8(line));
    if (!event.has_value()) {
        QTest::qFail(line, __FILE__, __LINE__);
        return CombatEvent{};
    }
    return *event;
}

} // namespace

void TestCombatLines::hitTest()
{
    const CombatEvent e = parsed(
        "You pound the one-eyed orc's left arm extremely hard and shatter it.");
    QCOMPARE(e.kind, CombatKindEnum::BLOW);
    QCOMPARE(e.outcome, BlowOutcomeEnum::HIT);
    QCOMPARE(e.actor, QString("you"));
    QCOMPARE(e.target, QString("the one-eyed orc"));
    QCOMPARE(e.verb, QString("pound"));
    QCOMPARE(e.part, QString("left arm"));
    QCOMPARE(e.severity, QString("extremely hard"));
    QCOMPARE(e.effect, QString("shatter"));

    // A third party hitting a third party.
    const CombatEvent other = parsed(
        "A mother eagle hits a renegade uruk's right hand very hard and shatters it.");
    QCOMPARE(other.actor, QString("A mother eagle"));
    QCOMPARE(other.target, QString("a renegade uruk"));
    QCOMPARE(other.verb, QString("hit"));
    QCOMPARE(other.part, QString("right hand"));
    QCOMPARE(other.severity, QString("very hard"));
    QCOMPARE(other.effect, QString("shatter"));
}

void TestCombatLines::hitOnYouTest()
{
    const CombatEvent e = parsed("A burly orc lightly stabs your left foot and tickles it.");
    QCOMPARE(e.actor, QString("A burly orc"));
    QCOMPARE(e.target, QString("you"));
    QCOMPARE(e.quality, QString("lightly"));
    QCOMPARE(e.verb, QString("stab"));
    QCOMPARE(e.part, QString("left foot"));
    QVERIFY(e.severity.isEmpty());
    QCOMPARE(e.effect, QString("tickle"));

    const CombatEvent strong = parsed("You strongly slash a dirty uruk's left leg and shatter it.");
    QCOMPARE(strong.quality, QString("strongly"));
    QCOMPARE(strong.verb, QString("slash"));
}

void TestCombatLines::hitWithoutSeverityTest()
{
    const CombatEvent e = parsed("A dirty uruk slashes your head.");
    QCOMPARE(e.target, QString("you"));
    QCOMPARE(e.verb, QString("slash"));
    QCOMPARE(e.part, QString("head"));
    QVERIFY(e.severity.isEmpty());
    QVERIFY(e.effect.isEmpty());
}

void TestCombatLines::possessiveWithoutSTest()
{
    // A name ending in s takes a bare apostrophe.
    const CombatEvent e = parsed(
        "You pound a swarm of blow-flies' body extremely hard and shatter it.");
    QCOMPARE(e.target, QString("a swarm of blow-flies"));
    QCOMPARE(e.part, QString("body"));
}

void TestCombatLines::groupLabelTest()
{
    // "(K)" is the group label MUME puts after a member's name; it is not part of the name.
    const CombatEvent e = parsed("Kazadoe (K) sends a demon wolf sprawling with a powerful bash.");
    QCOMPARE(e.actor, QString("Kazadoe"));
    QCOMPARE(e.target, QString("a demon wolf"));
}

void TestCombatLines::parryTest()
{
    const CombatEvent e = parsed(
        "A renegade uruk tries to pierce you, but your parry is successful.");
    QCOMPARE(e.outcome, BlowOutcomeEnum::PARRY);
    QCOMPARE(e.actor, QString("A renegade uruk"));
    QCOMPARE(e.target, QString("you"));
    QCOMPARE(e.verb, QString("pierce"));

    const CombatEvent theirs = parsed(
        "A mother eagle tries to hit *a Man*, but he parries successfully.");
    QCOMPARE(theirs.target, QString("*a Man*"));

    const CombatEvent yours = parsed("You try to pound a demon wolf, but it parries successfully.");
    QCOMPARE(yours.actor, QString("you"));
    QCOMPARE(yours.target, QString("a demon wolf"));
}

void TestCombatLines::dodgeTest()
{
    // Told from the defender's side, so actor and target swap round in the sentence.
    const CombatEvent e = parsed("You swiftly dodge a dirty uruk's attempt to slash you.");
    QCOMPARE(e.outcome, BlowOutcomeEnum::DODGE);
    QCOMPARE(e.actor, QString("a dirty uruk"));
    QCOMPARE(e.target, QString("you"));

    const CombatEvent theirs = parsed(
        "A mother eagle (bud) swiftly dodges *Fallout the Black Numenorean*'s attempt to slash her.");
    QCOMPARE(theirs.actor, QString("*Fallout the Black Numenorean*"));
    QCOMPARE(theirs.target, QString("A mother eagle"));
}

void TestCombatLines::missTest()
{
    const CombatEvent e = parsed("An Ohurk-uai soldier fails to cleave you.");
    QCOMPARE(e.outcome, BlowOutcomeEnum::MISS);
    QCOMPARE(e.actor, QString("An Ohurk-uai soldier"));
    QCOMPARE(e.target, QString("you"));
    QCOMPARE(e.verb, QString("cleave"));
}

void TestCombatLines::fleeTest()
{
    const CombatEvent them = parsed("*a Man* panics, and attempts to flee.");
    QCOMPARE(them.kind, CombatKindEnum::FLEE);
    QCOMPARE(them.phase, CombatPhaseEnum::ATTEMPT);
    QCOMPARE(them.actor, QString("*a Man*"));

    QCOMPARE(parsed("You flee head over heels.").phase, CombatPhaseEnum::ATTEMPT);

    const CombatEvent away = parsed("You flee down.");
    QCOMPARE(away.phase, CombatPhaseEnum::ESCAPED);
    QCOMPARE(away.detail, QString("down"));

    // Both of MUME's ways of saying it did not work.
    QCOMPARE(parsed("PANIC! You couldn't escape!").phase, CombatPhaseEnum::FAILED);
    QCOMPARE(parsed("PANIC! You can't quit the fight!").phase, CombatPhaseEnum::FAILED);

    const CombatEvent escaped = parsed("The assassin master (buh) escaped the fight.");
    QCOMPARE(escaped.phase, CombatPhaseEnum::ESCAPED);
    QCOMPARE(escaped.actor, QString("The assassin master"));
}

void TestCombatLines::refusedTest()
{
    const CombatEvent e = parsed("No way! You are fighting for your life!");
    QCOMPARE(e.kind, CombatKindEnum::REFUSED);
    QCOMPARE(e.actor, QString("you"));
}

void TestCombatLines::bashTest()
{
    const CombatEvent on_you = parsed("*Stolb* sends you sprawling with a powerful bash.");
    QCOMPARE(on_you.kind, CombatKindEnum::BASH);
    QCOMPARE(on_you.actor, QString("*Stolb*"));
    QCOMPARE(on_you.target, QString("you"));

    const CombatEvent yours = parsed("Your bash at a stone statue (buh) sends it sprawling.");
    QCOMPARE(yours.actor, QString("you"));
    QCOMPARE(yours.target, QString("a stone statue"));

    // Some players have a damage counter turned on; it trails the line and is not part of it.
    const CombatEvent counted = parsed("*Hazad* sends you sprawling. [Damage:1]");
    QCOMPARE(counted.target, QString("you"));
}

void TestCombatLines::backstabTest()
{
    const CombatEvent e = parsed("Suddenly *a Man* stabs you in the back.");
    QCOMPARE(e.kind, CombatKindEnum::BACKSTAB);
    QCOMPARE(e.actor, QString("*a Man*"));
    QCOMPARE(e.target, QString("you"));
}

void TestCombatLines::deathAndConditionTest()
{
    const CombatEvent dead = parsed("A renegade uruk is dead! R.I.P.");
    QCOMPARE(dead.kind, CombatKindEnum::DEATH);
    QCOMPARE(dead.actor, QString("A renegade uruk"));
    QCOMPARE(parsed("*a Man* has drawn his last breath! R.I.P.").actor, QString("*a Man*"));

    const CombatEvent down = parsed(
        "A renegade uruk is incapacitated and will slowly die, if not aided.");
    QCOMPARE(down.kind, CombatKindEnum::CONDITION);
    QCOMPARE(down.detail, QString("incapacitated"));
    QCOMPARE(parsed("A dirty uruk is mortally wounded and will die soon if not aided.").detail,
             QString("mortally wounded"));
}

void TestCombatLines::castTest()
{
    const CombatEvent mine = parsed("You start to concentrate...");
    QCOMPARE(mine.kind, CombatKindEnum::CAST);
    QCOMPARE(mine.phase, CombatPhaseEnum::STARTED);
    QCOMPARE(mine.actor, QString("you"));

    QCOMPARE(parsed("Aye! You cannot concentrate any more...").phase, CombatPhaseEnum::BROKEN);
    QCOMPARE(parsed("You were not able to keep your concentration while moving.").phase,
             CombatPhaseEnum::BROKEN);

    // Somebody else's cast: MUME names the caster and never the target.
    const CombatEvent theirs = parsed("Kazadoe (K) begins some strange incantations...");
    QCOMPARE(theirs.phase, CombatPhaseEnum::STARTED);
    QCOMPARE(theirs.actor, QString("Kazadoe"));
    QVERIFY(theirs.target.isEmpty());

    // The words are plain for a spell the listener knows and garbled for one they do not.
    const CombatEvent known = parsed("Pampa (P) utters the words 'cure light'");
    QCOMPARE(known.phase, CombatPhaseEnum::DONE);
    QCOMPARE(known.detail, QString("cure light"));
    QCOMPARE(parsed("Budach (B) utters the words 'bfzahp ay bfugtizgg'").detail,
             QString("bfzahp ay bfugtizgg"));
}

void TestCombatLines::twiddlersTest()
{
    // The prompt's twiddlers option leaves its spinner on the front of the next line.
    QCOMPARE(parsed("/You start to concentrate...").phase, CombatPhaseEnum::STARTED);
    QCOMPARE(parsed("-\\|/-\\|/You start to concentrate...").phase, CombatPhaseEnum::STARTED);
    // An affect list entry starts with a dash as well, and must not lose it.
    QVERIFY(!parseCombatLine(QStringLiteral("- panic")).has_value());
}

void TestCombatLines::selfTest()
{
    QCOMPARE(parsed("You are stunned and cannot realize what is going on!").phase,
             CombatPhaseEnum::STUNNED);
    QCOMPARE(parsed("You feel sleepy.").phase, CombatPhaseEnum::SLEEPY);
    QCOMPARE(parsed("You stand up.").phase, CombatPhaseEnum::STOOD);
}

void TestCombatLines::notCombatTest()
{
    for (const char *line : {"A burly orc says 'A wall vosg yoa endir yio'ra goid, Lufaka!'",
                             "Exits: north, east, west.",
                             "A falchion lies on the ground.",
                             "You follow Kazadoe.",
                             "An orc of the Ohurk-uai stands here, cursing and grumbling.",
                             "The guard fails to notice you.",
                             "-\\|/-\\|/A blue transparent wall slowly appears around you.",
                             ""}) {
        QVERIFY2(!parseCombatLine(QString::fromUtf8(line)).has_value(), line);
    }
}

QTEST_MAIN(TestCombatLines)
