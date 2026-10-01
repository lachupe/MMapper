// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestCombatLines.h"

#include "../src/parser/CombatLines.h"

#include <tuple>

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

    // Somebody else's flee that got away, seen from the room: no way named, no "leaves" after it.
    const CombatEvent gone = parsed("*a Man* (one) flees head over heels.");
    QCOMPARE(gone.kind, CombatKindEnum::FLEE);
    QCOMPARE(gone.phase, CombatPhaseEnum::ESCAPED);
    QCOMPARE(gone.actor, QString("*a Man*"));
    QVERIFY(gone.detail.isEmpty());
    QCOMPARE(parsed("A mother eagle (Bongo) flees head over heels.").actor,
             QString("A mother eagle"));

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

    // The words are plain for a spell the listener knows and garbled for one they do not; the
    // garbled form is looked up and named.
    const CombatEvent known = parsed("Pampa (P) utters the words 'cure light'");
    QCOMPARE(known.phase, CombatPhaseEnum::DONE);
    QCOMPARE(known.detail, QString("cure light"));
    QCOMPARE(parsed("Budach (B) utters the words 'bfzahp ay bfugtizgg'").detail,
             QString("breath of briskness"));
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
                             "|/-\\|/-\\|/Ok.",
                             ""}) {
        QVERIFY2(!parseCombatLine(QString::fromUtf8(line)).has_value(), line);
    }
}

void TestCombatLines::ownCastTest()
{
    // Other ways MUME says the player started: one of them is how it reads for a character the
    // sun troubles, and an older one names the spell and how fast it is cast.
    for (const char *line : {"You muster all of your concentration...",
                             "Struggling against the Yellow Face, you try to concentrate...",
                             "-You muster all of your concentration..."}) {
        const CombatEvent e = parsed(line);
        QCOMPARE(e.kind, CombatKindEnum::CAST);
        QCOMPARE(e.phase, CombatPhaseEnum::STARTED);
        QCOMPARE(e.actor, QString("you"));
    }
    const CombatEvent named = parsed("You start casting burning hands [quickly].");
    QCOMPARE(named.phase, CombatPhaseEnum::STARTED);
    QCOMPARE(named.detail, QString("burning hands"));
    QCOMPARE(named.quality, QString("quickly"));
    const CombatEvent plain = parsed("You start casting lightning bolt.");
    QCOMPARE(plain.detail, QString("lightning bolt"));
    QVERIFY(plain.quality.isEmpty());

    // Every way the logs show the player's concentration breaking.
    for (const char *line : {"You lost your concentration.",
                             "You lost your concentration!",
                             "Ack! You can't concentrate anymore...",
                             "Ack! You cannot concentrate anymore.",
                             "-The cruel light of the sun made you lose your concentration!"}) {
        const CombatEvent e = parsed(line);
        QCOMPARE(e.kind, CombatKindEnum::CAST);
        QCOMPARE(e.phase, CombatPhaseEnum::BROKEN);
        QCOMPARE(e.actor, QString("you"));
    }

    // Refused before it began, with the reason when MUME gives one.
    const CombatEvent resting = parsed("You can't concentrate enough while resting.");
    QCOMPARE(resting.kind, CombatKindEnum::CAST);
    QCOMPARE(resting.phase, CombatPhaseEnum::REFUSED);
    QCOMPARE(resting.detail, QString("resting"));
    const CombatEvent impossible = parsed("Impossible! You can't concentrate enough.");
    QCOMPARE(impossible.phase, CombatPhaseEnum::REFUSED);
    QVERIFY(impossible.detail.isEmpty());
    QCOMPARE(parsed("Impossible! You can't concentrate enough!.").phase, CombatPhaseEnum::REFUSED);
}

void TestCombatLines::ownCastTrackerTest()
{
    const auto feed = [](OwnCastTracker &tracker, const char *const line) {
        if (const auto event = parseCombatLine(QString::fromUtf8(line))) {
            tracker.receiveEvent(*event);
        }
    };

    // "You start to concentrate...", lines while the spinner runs, then the prompt: the spell
    // went off. MUME sends no prompt while the action runs.
    OwnCastTracker tracker;
    feed(tracker, "You start to concentrate...");
    QVERIFY(tracker.casting());
    feed(tracker, "/-\\|/-\\Vardamir (V) begins some strange incantations...");
    feed(tracker, "|/-\\|/-\\|/Ok.");
    QVERIFY(tracker.casting());
    const std::optional<CombatEvent> done = tracker.receivePrompt();
    QVERIFY(done.has_value());
    QCOMPARE(done->kind, CombatKindEnum::CAST);
    QCOMPARE(done->phase, CombatPhaseEnum::DONE);
    QCOMPARE(done->actor, QString("you"));
    QVERIFY(done->text.isEmpty());
    // Only once.
    QVERIFY(!tracker.receivePrompt().has_value());

    // Broken before the prompt: nothing went off.
    feed(tracker, "|You start to concentrate...");
    feed(tracker, "You are burnt by the heat of the Balrog!");
    feed(tracker, "Aye! You cannot concentrate any more...");
    QVERIFY(!tracker.receivePrompt().has_value());

    // Refused, somebody else's cast, and the player's death do not count either.
    feed(tracker, "You can't concentrate enough while resting.");
    QVERIFY(!tracker.receivePrompt().has_value());
    feed(tracker, "Kazadoe (K) begins some strange incantations...");
    QVERIFY(!tracker.receivePrompt().has_value());
    feed(tracker, "You muster all of your concentration...");
    feed(tracker, "You are dead! Sorry...");
    QVERIFY(!tracker.receivePrompt().has_value());

    feed(tracker, "You start to concentrate...");
    tracker.reset();
    QVERIFY(!tracker.receivePrompt().has_value());

    // The spinner, as powwow/logs/gaar_roflmao.mov:1528-1545 recorded a cure serious: "You start
    // to concentrate...", then the chunks "\\\b", "|\b", "/\b", "-\b", ... about 250 ms
    // apart, then the spell's line and the prompt. Each chunk is a step while the spell is cast.
    QVERIFY(!tracker.receiveTwiddler(QByteArray("\\\b")).has_value()); // not casting
    feed(tracker, "You start to concentrate...");
    int steps = 0;
    for (const char *const chunk : {"\\\b", "|\b", "/\b", "-\b", "\\\b", "|\b", "/\b"}) {
        const std::optional<CombatEvent> step = tracker.receiveTwiddler(QByteArray(chunk));
        QVERIFY(step.has_value());
        QCOMPARE(step->kind, CombatKindEnum::CAST);
        QCOMPARE(step->phase, CombatPhaseEnum::STEP);
        QCOMPARE(step->actor, QString("you"));
        QCOMPARE(step->text, QString(QLatin1Char(chunk[0])));
        ++steps;
    }
    QCOMPARE(steps, 7);
    QCOMPARE(to_string_view(CombatPhaseEnum::STEP), std::string_view{"step"});
    // Anything else ending in a backspace is not a turn of the spinner, and none of it ends
    // the cast: only the prompt does.
    QVERIFY(!tracker.receiveTwiddler(QByteArray("\b")).has_value());
    QVERIFY(!tracker.receiveTwiddler(QByteArray("x\b")).has_value());
    QVERIFY(!tracker.receiveTwiddler(QByteArray("|/\b")).has_value());
    QVERIFY(!tracker.receiveTwiddler(QByteArray("|")).has_value());
    QVERIFY(tracker.casting());
    feed(tracker, "You begin to see scars fade away and a feeling of health comes over you.");
    QVERIFY(tracker.receivePrompt().has_value());
    QVERIFY(!tracker.receiveTwiddler(QByteArray("-\b")).has_value());

    // Broken: the spinner after it is no step.
    feed(tracker, "You start to concentrate...");
    QVERIFY(tracker.receiveTwiddler(QByteArray("|\b")).has_value());
    feed(tracker, "You were not able to keep your concentration while moving.");
    QVERIFY(!tracker.receiveTwiddler(QByteArray("/\b")).has_value());
}

void TestCombatLines::bashVariantsTest()
{
    const CombatEvent tail = parsed("The cave-worm whips its tail around, sending Beat sprawling!");
    QCOMPARE(tail.kind, CombatKindEnum::BASH);
    QCOMPARE(tail.phase, CombatPhaseEnum::NONE);
    QCOMPARE(tail.actor, QString("The cave-worm"));
    QCOMPARE(tail.target, QString("Beat"));
    QCOMPARE(parsed("The cave-worm whips its tail around, sending Welder (dd) sprawling!").target,
             QString("Welder"));

    // A bash that missed: the one who tried it is the one who falls.
    const CombatEvent dodged = parsed(
        "You dodge a bash from an ugly forest troll who loses his balance.");
    QCOMPARE(dodged.kind, CombatKindEnum::BASH);
    QCOMPARE(dodged.phase, CombatPhaseEnum::DODGED);
    QCOMPARE(dodged.actor, QString("an ugly forest troll"));
    QCOMPARE(dodged.target, QString("you"));
    QCOMPARE(parsed("You dodge a bash from *a Hobbit* who loses his balance and falls.").actor,
             QString("*a Hobbit*"));
    QCOMPARE(parsed("You dodge a bash from an ugly forest troll (buh) who loses his balance.").actor,
             QString("an ugly forest troll"));

    const CombatEvent evaded = parsed(
        "You evade *Stolb the Orc* [stolb]'s bash, causing him to fall flat on his face.");
    QCOMPARE(evaded.phase, CombatPhaseEnum::DODGED);
    QCOMPARE(evaded.actor, QString("*Stolb the Orc*"));
    QCOMPARE(evaded.target, QString("you"));
    QCOMPARE(parsed("You evade *an Orc*'s bash, causing him to fall flat on his face.").actor,
             QString("*an Orc*"));

    const CombatEvent theirs = parsed(
        "Rubb Grumm (buh) avoids being bashed by Stolb (R) who loses his balance.");
    QCOMPARE(theirs.phase, CombatPhaseEnum::DODGED);
    QCOMPARE(theirs.actor, QString("Stolb"));
    QCOMPARE(theirs.target, QString("Rubb Grumm"));

    const CombatEvent toppled = parsed(
        "As the assassin master (buh) avoids your bash, you topple over and lose your balance.");
    QCOMPARE(toppled.phase, CombatPhaseEnum::DODGED);
    QCOMPARE(toppled.actor, QString("you"));
    QCOMPARE(toppled.target, QString("the assassin master"));
    QCOMPARE(parsed("As Muranog (buh) avoids your bash, you topple over and fall to the ground.")
                 .target,
             QString("Muranog"));

    // Getting over being sent sprawling.
    const CombatEvent over = parsed("Your head stops stinging.");
    QCOMPARE(over.kind, CombatKindEnum::BASH);
    QCOMPARE(over.phase, CombatPhaseEnum::RECOVERED);
    QCOMPARE(over.target, QString("you"));
    QVERIFY(over.actor.isEmpty());
}

void TestCombatLines::ownDeathAndConditionTest()
{
    const CombatEvent dead = parsed("You are dead! Sorry...");
    QCOMPARE(dead.kind, CombatKindEnum::DEATH);
    QCOMPARE(dead.actor, QString("you"));
    QCOMPARE(parsed("You are dead!  Sorry...").actor, QString("you"));

    const CombatEvent incap = parsed("You are incapacitated and will slowly die, if not aided.");
    QCOMPARE(incap.kind, CombatKindEnum::CONDITION);
    QCOMPARE(incap.actor, QString("you"));
    QCOMPARE(incap.detail, QString("incapacitated"));
    QCOMPARE(parsed("You are mortally wounded and will die soon if not aided.").detail,
             QString("mortally wounded"));
    QCOMPARE(parsed("You are mortally wounded, and will die soon, if not aided.").actor,
             QString("you"));

    const CombatEvent stunned = parsed("A dirty uruk is stunned and will probably die soon.");
    QCOMPARE(stunned.actor, QString("A dirty uruk"));
    QCOMPARE(stunned.detail, QString("stunned"));
    QCOMPARE(parsed("*a Man* is stunned, but will probably regain consciousness again.").detail,
             QString("stunned"));

    // Still the player's own state line, not a condition.
    QCOMPARE(parsed("You are stunned and cannot realize what is going on!").kind,
             CombatKindEnum::SELF);
    // Stunned by something else entirely.
    QVERIFY(
        !parseCombatLine(QStringLiteral("You are stunned by the shine that gilmmers in his eyes."))
             .has_value());
}

void TestCombatLines::refusedMovesTest()
{
    struct NODISCARD Case final
    {
        const char *line;
        const char *detail;
        const char *target;
    };
    const Case cases[] = {
        {"No way! You are fighting for your life!", "fighting", ""},
        {"You are too exhausted.", "exhausted", ""},
        {"You are too exhausted to ride.", "exhausted", ""},
        {"A mountain mule (my) is too exhausted.", "mount-exhausted", "A mountain mule"},
        {"Your mount refuses to follow your orders!", "mount-refuses", ""},
        {"ZBLAM! A trained horse (my) doesn't want you riding him anymore.",
         "thrown",
         "A trained horse"},
        {"Nah... You feel too relaxed to do that..", "resting", ""},
        {"Nah... You feel too relaxed to do that.", "resting", ""},
        {"Maybe you should get on your feet first?", "sitting", ""},
        {"In your dreams, or what?", "sleeping", ""},
        {"The door seems to be closed.", "door-closed", "door"},
        {"The bushes seems to be closed.", "door-closed", "bushes"},
        {"The ironbars seem to be closed.", "door-closed", "ironbars"},
        {"Alas, you cannot go that way...", "no-exit", ""},
        {"Alas, you cannot go that way.", "no-exit", ""},
        {"The ascent is too steep, you need to climb to go there.", "climb", ""},
        {"If you still want to try, you must 'climb' there.", "climb", ""},
        {"You failed to climb there and fall down, hurting yourself.", "climb-failed", ""},
        {"You need to swim to go there.", "swim", ""},
        {"You failed swimming there.", "swim-failed", ""},
        {"You can't go into deep water!", "deep-water", ""},
        {"You cannot ride there.", "cannot-ride", ""},
        {"You unsuccessfully try to break through the ice.", "ice", ""},
        // With the twiddlers glued on, as the prompt option leaves them.
        {"\\|/-Alas, you cannot go that way...", "no-exit", ""},
    };
    for (const Case &c : cases) {
        const CombatEvent e = parsed(c.line);
        QVERIFY2(e.kind == CombatKindEnum::REFUSED, c.line);
        QCOMPARE(e.actor, QString("you"));
        QCOMPARE(e.detail, QString(c.detail));
        QCOMPARE(e.target, QString(c.target));
    }

    // Look-alikes that are not refused moves.
    for (const char *line : {"Lei narrates 'ZBLAM! A hungry warg doesn't want you riding it "
                             "anymore.' in Orkish.",
                             "ZBLAM! A hungry warg sends Krkavec flying!",
                             "You are too tired to narrate.",
                             "You feel very confused and can't concentrate any more.",
                             "You could not concentrate enough to refresh 'identify'."}) {
        QVERIFY2(!parseCombatLine(QString::fromUtf8(line)).has_value(), line);
    }
}

namespace {

struct NODISCARD BlowCase final
{
    const char *line;
    BlowOutcomeEnum outcome;
    const char *actor;
    const char *target;
    const char *verb;
    const char *part;
    const char *detail;
};

void checkBlows(const std::initializer_list<BlowCase> cases)
{
    for (const BlowCase &c : cases) {
        const CombatEvent e = parsed(c.line);
        QVERIFY2(e.kind == CombatKindEnum::BLOW, c.line);
        QVERIFY2(e.outcome == c.outcome, c.line);
        QCOMPARE(e.actor, QString(c.actor));
        QCOMPARE(e.target, QString(c.target));
        QCOMPARE(e.verb, QString(c.verb));
        QCOMPARE(e.part, QString(c.part));
        QCOMPARE(e.detail, QString(c.detail));
    }
}

} // namespace

void TestCombatLines::animalPartsTest()
{
    // Beasts' fore- and hindlegs and -feet, a bat's claws, a clump of roots' tip, a fungus'
    // crown, a shrub's leaves, a fish's tail fin.
    const auto H = BlowOutcomeEnum::HIT;
    checkBlows({
        {"You pound a demon wolf's left foreleg extremely hard.",
         H,
         "you",
         "a demon wolf",
         "pound",
         "left foreleg",
         ""},
        {"You pound a demon wolf (buh)'s right hindleg extremely hard and shatter it.",
         H,
         "you",
         "a demon wolf",
         "pound",
         "right hindleg",
         ""},
        {"You pound a ferocious warg's right hindfoot extremely hard and shatter it.",
         H,
         "you",
         "a ferocious warg",
         "pound",
         "right hindfoot",
         ""},
        {"You slash a giant rat's right forefoot extremely hard and shatter it.",
         H,
         "you",
         "a giant rat",
         "slash",
         "right forefoot",
         ""},
        {"You cleave a large bat's left claw extremely hard and shatter it.",
         H,
         "you",
         "a large bat",
         "cleave",
         "left claw",
         ""},
        {"You pound a clump of roots' tip extremely hard and shatter it.",
         H,
         "you",
         "a clump of roots",
         "pound",
         "tip",
         ""},
        {"You barely hit a clump of roots' tip and tickle it.",
         H,
         "you",
         "a clump of roots",
         "hit",
         "tip",
         ""},
        {"You pierce a giant green fungus' crown extremely hard.",
         H,
         "you",
         "a giant green fungus",
         "pierce",
         "crown",
         ""},
        {"You pound a black fungus' leaves extremely hard and shatter it.",
         H,
         "you",
         "a black fungus",
         "pound",
         "leaves",
         ""},
        {"You strongly pierce a wild ox's right foreleg.",
         H,
         "you",
         "a wild ox",
         "pierce",
         "right foreleg",
         ""},
        {"You pierce a large pike's tail fin very hard.",
         H,
         "you",
         "a large pike",
         "pierce",
         "tail fin",
         ""},
        {"*a Dreadful Orc* cleaves your right thigh and wounds it.",
         H,
         "*a Dreadful Orc*",
         "you",
         "cleave",
         "right thigh",
         ""},
        {"*a Dreadful Orc* cleaves your waist and wounds it.",
         H,
         "*a Dreadful Orc*",
         "you",
         "cleave",
         "waist",
         ""},
        {"Harle the Hobbit lightly hits a heap of rooting stems (buh)'s leaves.",
         H,
         "Harle the Hobbit",
         "a heap of rooting stems",
         "hit",
         "leaves",
         ""},
    });
    const CombatEvent e = parsed(
        "You pound a demon wolf's right hindleg extremely hard and shatter it.");
    QCOMPARE(e.severity, QString("extremely hard"));
    QCOMPARE(e.effect, QString("shatter"));
}

void TestCombatLines::damageAnnotationTest()
{
    // A damage counter after the full stop, and a group label in square brackets.
    const CombatEvent e = parsed("*a Dreadful Orc* cleaves your neck and wounds it. [Damage:16]");
    QCOMPARE(e.kind, CombatKindEnum::BLOW);
    QCOMPARE(e.target, QString("you"));
    QCOMPARE(e.part, QString("neck"));
    QCOMPARE(e.effect, QString("wound"));
    QCOMPARE(e.text, QString("*a Dreadful Orc* cleaves your neck and wounds it."));
    const CombatEvent labelled = parsed(
        "*an Orc* [stolb] lightly cleaves your left shoulder and injures it. [Damage:12]");
    QCOMPARE(labelled.actor, QString("*an Orc*"));
    QCOMPARE(labelled.quality, QString("lightly"));
    QCOMPARE(parsed("You strike *an Orc* [dork] with a magical missile. [Damage:4]").target,
             QString("*an Orc*"));
    QCOMPARE(parsed("You stab *Stolb the Orc* [stolb]'s head and shatter it.").target,
             QString("*Stolb the Orc*"));
}

void TestCombatLines::defendedTest()
{
    const auto P = BlowOutcomeEnum::PARRY;
    const auto D = BlowOutcomeEnum::DODGE;
    checkBlows({
        // The player's blow dodged: "your attempt".
        {"A shadow (buh) swiftly dodges your attempt to slash it.",
         D,
         "you",
         "A shadow",
         "slash",
         "",
         ""},
        {"The sage (buh) swiftly dodges your attempt to pierce him.",
         D,
         "you",
         "The sage",
         "pierce",
         "",
         ""},
        {"*Zubr the Dwarf* swiftly dodges your attempt to pierce him.",
         D,
         "you",
         "*Zubr the Dwarf*",
         "pierce",
         "",
         ""},
        // A shadow's icy grasp, which names no verb.
        {"The icy grasp of a shadow blocks your attempt!", P, "you", "a shadow", "", "", "block"},
        {"The icy grasp of a shadow (buh) blocks your attempt!", P, "you", "a shadow", "", "", "block"},
        // One MUME port's blocks and parries, which name the part aimed at.
        {"*a Dreadful Orc* blocks your attempt to stab his head.",
         P,
         "you",
         "*a Dreadful Orc*",
         "stab",
         "head",
         "block"},
        {"You block *Stolb the Orc* [stolb]s' attempt to cleave your head.",
         P,
         "*Stolb the Orc*",
         "you",
         "cleave",
         "head",
         "block"},
        {"You block *an Orc* [dork]s' attempt to cleave your left thigh.",
         P,
         "*an Orc*",
         "you",
         "cleave",
         "left thigh",
         "block"},
        {"You block *a Dreadful Orc*s' attempt to cleave your head.",
         P,
         "*a Dreadful Orc*",
         "you",
         "cleave",
         "head",
         "block"},
        {"*an Orc* [dork] swiftly parries your attempt to stab his waist.",
         P,
         "you",
         "*an Orc*",
         "stab",
         "waist",
         ""},
    });
}

void TestCombatLines::interceptTest()
{
    // Whoever steps in is the one the blow met.
    const auto P = BlowOutcomeEnum::PARRY;
    checkBlows({
        {"*Guthol the Dwarf* intercepts your blow.",
         P,
         "you",
         "*Guthol the Dwarf*",
         "",
         "",
         "intercept"},
        {"You intercept a brown-skinned orc's blow.",
         P,
         "a brown-skinned orc",
         "you",
         "",
         "",
         "intercept"},
        {"You intercept *Breaux the Orc* (buh)'s blow.",
         P,
         "*Breaux the Orc*",
         "you",
         "",
         "",
         "intercept"},
        {"Pampa (P) intercepts *a Troll*'s blow.", P, "*a Troll*", "Pampa", "", "", "intercept"},
        {"Stolb intercepts a swarm of blow-flies' blow.",
         P,
         "a swarm of blow-flies",
         "Stolb",
         "",
         "",
         "intercept"},
    });
    // Failing to step in is not a blow: the blow goes where it was aimed, on a line of its own.
    QVERIFY(!parseCombatLine(QStringLiteral("You fail to intercept *a Troll*'s blow.")).has_value());
}

void TestCombatLines::approachTest()
{
    for (const char *line : {"You approach *a Dwarf* (ff), trying to pound him.",
                             "You approach *Blomma the Mountain Troll*, trying to pound her.",
                             "You approach *an Orc* [dork] and attack him."}) {
        const CombatEvent e = parsed(line);
        QCOMPARE(e.kind, CombatKindEnum::BLOW);
        QCOMPARE(e.phase, CombatPhaseEnum::ATTEMPT);
        QCOMPARE(e.actor, QString("you"));
    }
    const CombatEvent pound = parsed("You approach *a Dwarf* (ff), trying to pound him.");
    QCOMPARE(pound.target, QString("*a Dwarf*"));
    QCOMPARE(pound.verb, QString("pound"));
    const CombatEvent theirs = parsed("*a Troll* approaches Pampa (P), trying to pound him.");
    QCOMPARE(theirs.phase, CombatPhaseEnum::ATTEMPT);
    QCOMPARE(theirs.actor, QString("*a Troll*"));
    QCOMPARE(theirs.target, QString("Pampa"));
    QCOMPARE(parsed("*an Orc* [dork] approaches Grayelf and attacks him.").target,
             QString("Grayelf"));
}

void TestCombatLines::triesTailsTest()
{
    const auto P = BlowOutcomeEnum::PARRY;
    const auto D = BlowOutcomeEnum::DODGE;
    const auto M = BlowOutcomeEnum::MISS;
    checkBlows({
        {"A Skeletal Warrior (buh) tries to kick you, but you manage to avoid his foot.",
         D,
         "A Skeletal Warrior",
         "you",
         "kick",
         "",
         ""},
        {"The bloodwight (buh) tries to bite you, but you dodge swiftly.",
         D,
         "The bloodwight",
         "you",
         "bite",
         "",
         ""},
        {"Amund tries to pound Mehine, but fails.", M, "Amund", "Mehine", "pound", "", ""},
        {"A burly orc tries to slash you, but you keep him at bay.",
         P,
         "A burly orc",
         "you",
         "slash",
         "",
         ""},
        {"A white rat tries to hit Stolb, but he manages to keep him at bay.",
         P,
         "A white rat",
         "Stolb",
         "hit",
         "",
         ""},
        {"*an Orc* tries to cleave your back but you swiftly parry it.",
         P,
         "*an Orc*",
         "you",
         "cleave",
         "back",
         ""},
        {"*an Orc* [dork] tries to cleave your waist but you manage to dodge it.",
         D,
         "*an Orc*",
         "you",
         "cleave",
         "waist",
         ""},
        {"Your attempt to slash a shadow (buh) fails.", M, "you", "a shadow", "slash", "", ""},
        {"Your attempt to pierce the hardened ranger fails.",
         M,
         "you",
         "the hardened ranger",
         "pierce",
         "",
         ""},
    });
}

void TestCombatLines::spellHitTest()
{
    const auto H = BlowOutcomeEnum::HIT;
    checkBlows({
        {"You burn *an Elf*.", H, "you", "*an Elf*", "burn", "", "burning hands"},
        {"You burn *a Dwarf* (buh).", H, "you", "*a Dwarf*", "burn", "", "burning hands"},
        {"*Rhina the Silvan Elf* burns you.",
         H,
         "*Rhina the Silvan Elf*",
         "you",
         "burn",
         "",
         "burning hands"},
        // Between two others, now that the things MUME says burn are kept out.
        {"Auluua (Au) burns *Thralk the Orc*.",
         H,
         "Auluua",
         "*Thralk the Orc*",
         "burn",
         "",
         "burning hands"},
        {"Minde burns the scaly beast.", H, "Minde", "the scaly beast", "burn", "", "burning hands"},
        {"You strike *Stolb the Orc* [stolb] with a magical missile.",
         H,
         "you",
         "*Stolb the Orc*",
         "strike",
         "",
         "magic missile"},
        {"Vardamir throws a glowing magical missile at *Aralos the Noldorin Elf*.",
         H,
         "Vardamir",
         "*Aralos the Noldorin Elf*",
         "strike",
         "",
         "magic missile"},
        {"Your magic missile hits Nagash the Dark (buh).",
         H,
         "you",
         "Nagash the Dark",
         "hit",
         "",
         "magic missile"},
        {"Your fireball completely envelops a shadow (buh) in flames.",
         H,
         "you",
         "a shadow",
         "burn",
         "",
         "fireball"},
        {"Your fireball hits a shadow with full force, causing an immediate death.",
         H,
         "you",
         "a shadow",
         "burn",
         "",
         "fireball"},
        {"The sage's fireball completely envelops you, causing infernal pain.",
         H,
         "The sage",
         "you",
         "burn",
         "",
         "fireball"},
    });
}

void TestCombatLines::notBlowTest()
{
    // Look-alikes of the new forms, all from the logs.
    for (const char *line :
         {"A small campfire burns here, its low flames giving off only a little light.",
          "The bloodwight lets out a long wailing cry which freezes your spine!",
          "South - The glare of the sun burns your eyes.",
          "You tip your hat.",
          "You strike out into the wall of thornbushes....",
          "You aim your spell at the ice layer.",
          "Your power blocking the plank resisted a breaking attempt!",
          "Wobbler the Man Acolyte [Retired]"}) {
        const std::optional<CombatEvent> e = parseCombatLine(QString::fromUtf8(line));
        QVERIFY2(!e.has_value() || e->kind != CombatKindEnum::BLOW, line);
    }
}

namespace {

// A line's kind, phase, the two sides and the detail.
struct NODISCARD EventCase final
{
    const char *line;
    CombatKindEnum kind;
    CombatPhaseEnum phase;
    const char *actor;
    const char *target;
    const char *detail;
};

void checkEvents(const std::initializer_list<EventCase> cases)
{
    for (const EventCase &c : cases) {
        const CombatEvent e = parsed(c.line);
        QVERIFY2(e.kind == c.kind, c.line);
        QVERIFY2(e.phase == c.phase, c.line);
        QCOMPARE(e.actor, QString::fromUtf8(c.actor));
        QCOMPARE(e.target, QString::fromUtf8(c.target));
        QCOMPARE(e.detail, QString::fromUtf8(c.detail));
    }
}

} // namespace

void TestCombatLines::tunicTest()
{
    // The wearer's line used to read as a miss by "Your ebony tunic shimmers as a mother eagle".
    const auto D = BlowOutcomeEnum::DODGE;
    checkBlows({
        {"Your ebony tunic shimmers as a mother eagle fails to hit you.",
         D,
         "a mother eagle",
         "you",
         "hit",
         "",
         "shimmer"},
        {"Your grey tunic shimmers as an Ohurk-uai orc-guard fails to slash you.",
         D,
         "an Ohurk-uai orc-guard",
         "you",
         "slash",
         "",
         "shimmer"},
        {"Your frayed tunic shimmers as a great warg fails to engage you.",
         D,
         "a great warg",
         "you",
         "engage",
         "",
         "shimmer"},
        {"Your tunic shimmers and a great warg fails to engage you.",
         D,
         "a great warg",
         "you",
         "engage",
         "",
         "shimmer"},
        {"A lithe orc (two) tries to engage *a noble Elf*, but his ebony tunic shimmers and shifts "
         "him.",
         D,
         "A lithe orc",
         "*a noble Elf*",
         "engage",
         "",
         "shimmer"},
        {"A lithe orc (one) tries to engage *Láminë the Noldorin Elf*, but her tunic shimmers and "
         "shifts her.",
         D,
         "A lithe orc",
         "*Láminë the Noldorin Elf*",
         "engage",
         "",
         "shimmer"},
        {"A spirit tries to hit *Lonewülf the Black Númenórean*, but his tunic shimmers and shifts "
         "him.",
         D,
         "A spirit",
         "*Lonewülf the Black Númenórean*",
         "hit",
         "",
         "shimmer"},
    });
    // The tunic as MUME named it; "engage" is an opening turned aside, a blow is not.
    const CombatEvent ebony = parsed(
        "Your ebony tunic shimmers as a mother eagle fails to hit you.");
    QCOMPARE(ebony.effect, QString("ebony tunic"));
    QCOMPARE(ebony.phase, CombatPhaseEnum::NONE);
    const CombatEvent engaged = parsed("Your tunic shimmers and a great warg fails to engage you.");
    QCOMPARE(engaged.effect, QString("tunic"));
    QCOMPARE(engaged.phase, CombatPhaseEnum::ATTEMPT);
}

void TestCombatLines::openingsTest()
{
    const auto H = BlowOutcomeEnum::HIT;
    checkBlows({
        {"A mother eagle quickly approaches, trying to hit you.",
         H,
         "A mother eagle",
         "you",
         "hit",
         "",
         ""},
        {"*an Orc* quickly approaches, trying to pound you.", H, "*an Orc*", "you", "pound", "", ""},
        {"A dwarven cityguard quickly approaches, trying to cleave you.",
         H,
         "A dwarven cityguard",
         "you",
         "cleave",
         "",
         ""},
        // Held off, and failing to hold off: the one kept at bay is the attacker.
        {"You approach *a dreadful Orc*, but he manages to keep you at bay.",
         BlowOutcomeEnum::PARRY,
         "you",
         "*a dreadful Orc*",
         "",
         "",
         "keep-at-bay"},
        {"You try to keep *Ful the Orc* at bay, but fail.",
         H,
         "*Ful the Orc*",
         "you",
         "",
         "",
         "keep-at-bay"},
        {"You try to keep a tree-snake at bay, but fail.",
         H,
         "a tree-snake",
         "you",
         "",
         "",
         "keep-at-bay"},
        {"*an Orc* tries to keep you at bay, but fails.", H, "you", "*an Orc*", "", "", "keep-at-bay"},
        {"A lithe orc tries to keep a mother eagle (YKS) at bay, but fails.",
         H,
         "a mother eagle",
         "A lithe orc",
         "",
         "",
         "keep-at-bay"},
        // A called shot; the blow it makes comes on a line of its own.
        {"*an Orc* strikes for a weakness in your armour!", H, "*an Orc*", "you", "", "", "armour-gap"},
        {"*Variant the Orc* (x) aims for a gap in your armour!",
         H,
         "*Variant the Orc*",
         "you",
         "",
         "",
         "armour-gap"},
        {"You aim for a gap in *Víí the Half-Elf* (DEAD)'s armour!",
         H,
         "you",
         "*Víí the Half-Elf*",
         "",
         "",
         "armour-gap"},
        {"You strike for a weakness in a huge tarantula's chitinous armour!",
         H,
         "you",
         "a huge tarantula",
         "",
         "",
         "armour-gap"},
    });
    for (const char *line : {"A mother eagle quickly approaches, trying to hit you.",
                             "You approach *a dreadful Orc*, but he manages to keep you at bay.",
                             "*an Orc* tries to keep you at bay, but fails.",
                             "*an Orc* strikes for a weakness in your armour!"}) {
        QVERIFY2(parsed(line).phase == CombatPhaseEnum::ATTEMPT, line);
    }
}

void TestCombatLines::blowVariantsTest()
{
    const auto H = BlowOutcomeEnum::HIT;
    checkBlows({
        // A beast's bite or sting with no part.
        {"A huge tarantula bites you!", H, "A huge tarantula", "you", "bite", "", ""},
        {"A great warg bites you!", H, "A great warg", "you", "bite", "", ""},
        {"A pack leader bites an elite Dunadan soldier!",
         H,
         "A pack leader",
         "an elite Dunadan soldier",
         "bite",
         "",
         ""},
        {"A huge tarantula (bud) bites *a Dwarf*!", H, "A huge tarantula", "*a Dwarf*", "bite", "", ""},
        {"Mormaeg stings you.", H, "Mormaeg", "you", "sting", "", ""},
        {"A huge queen bee (george) deeply stings *Flex the Orc*!",
         H,
         "A huge queen bee",
         "*Flex the Orc*",
         "sting",
         "",
         ""},
        // A mounted charge.
        {"An orkish warg-rider barely charges your body and tickles it.",
         H,
         "An orkish warg-rider",
         "you",
         "charge",
         "body",
         ""},
        {"You swiftly dodge an orkish warg-rider's attempt to charge you.",
         BlowOutcomeEnum::DODGE,
         "an orkish warg-rider",
         "you",
         "charge",
         "",
         ""},
        {"An orkish warg-rider tries to charge you, but your parry is successful.",
         BlowOutcomeEnum::PARRY,
         "An orkish warg-rider",
         "you",
         "charge",
         "",
         ""},
        // A fumble lands on the one who swung.
        {"Ooops! You fumble... and hit yourself HARD!", H, "you", "you", "hit", "", "fumble"},
        {"*Alfonso the Black Númenórean* fumbles and hits himself HARD.",
         H,
         "*Alfonso the Black Númenórean*",
         "*Alfonso the Black Númenórean*",
         "hit",
         "",
         "fumble"},
    });
    QCOMPARE(parsed("A huge queen bee (george) deeply stings *Flex the Orc*!").quality,
             QString("deeply"));

    // The top tier of damage in its other wording.
    const CombatEvent fragments = parsed(
        "*a Troll* hits Dragomir's body and reduce it to small fragments.");
    QCOMPARE(fragments.kind, CombatKindEnum::BLOW);
    QCOMPARE(fragments.target, QString("Dragomir"));
    QCOMPARE(fragments.part, QString("body"));
    QCOMPARE(fragments.effect, QString("fragment"));
    const CombatEvent mine = parsed(
        "You slash *a Troll*'s left leg and reduce it to small fragments.");
    QCOMPARE(mine.actor, QString("you"));
    QCOMPARE(mine.part, QString("left leg"));
    QCOMPARE(mine.effect, QString("fragment"));
}

void TestCombatLines::fleeVariantsTest()
{
    const auto F = CombatKindEnum::FLEE;
    checkEvents({
        {"*an Orc* panics, but can't stop fighting to flee.",
         F,
         CombatPhaseEnum::FAILED,
         "*an Orc*",
         "",
         ""},
        {"*a Man* panics, but can't stop fighting to flee.",
         F,
         CombatPhaseEnum::FAILED,
         "*a Man*",
         "",
         ""},
        {"You try to flee, but cannot!", F, CombatPhaseEnum::FAILED, "you", "", ""},
        {"You failed to escape the fight!", F, CombatPhaseEnum::FAILED, "you", "", ""},
        {"You can't seem to escape the roots!", F, CombatPhaseEnum::FAILED, "you", "", ""},
        {"You can't seem to escape a clump of roots!", F, CombatPhaseEnum::FAILED, "you", "", ""},
        // The escape skill.
        {"You seek to escape...", F, CombatPhaseEnum::ATTEMPT, "you", "", ""},
        {"You successfully escaped the fight!", F, CombatPhaseEnum::ESCAPED, "you", "", ""},
        // Disengage, not the escape: the help files file the room's line under disengage.
        {"*an Orc* seems to avoid the fight.", F, CombatPhaseEnum::DISENGAGED, "*an Orc*", "", ""},
        {"*a noble Elf* (dead) seems to avoid the fight.",
         F,
         CombatPhaseEnum::DISENGAGED,
         "*a noble Elf*",
         "",
         ""},
        {"*Gilhdur the Half-Elf* tried to escape but failed.",
         F,
         CombatPhaseEnum::FAILED,
         "*Gilhdur the Half-Elf*",
         "",
         ""},
        {"Someone tried to escape but failed.", F, CombatPhaseEnum::FAILED, "Someone", "", ""},
    });
    // Still an attempt.
    QCOMPARE(parsed("*an Orc* panics, and attempts to flee.").phase, CombatPhaseEnum::ATTEMPT);
}

void TestCombatLines::bashRecoveredTest()
{
    const auto B = CombatKindEnum::BASH;
    const auto R = CombatPhaseEnum::RECOVERED;
    checkEvents({
        {"*an Orc* seems to have recovered his senses.", B, R, "", "*an Orc*", ""},
        {"A mother eagle seems to have recovered her senses.", B, R, "", "A mother eagle", ""},
        {"Glorizmaeg (g) seems to have recovered his senses.", B, R, "", "Glorizmaeg", ""},
        {"*an Orc* (DEAD) seems to have recovered his senses.", B, R, "", "*an Orc*", ""},
        {"You have recovered from being bashed!", B, R, "", "you", ""},
    });
}

void TestCombatLines::castVariantsTest()
{
    const auto C = CombatKindEnum::CAST;
    checkEvents({
        // A spell of one word.
        {"*Farrah the Black Númenórean* utters the word 'gwahpzf'",
         C,
         CombatPhaseEnum::DONE,
         "*Farrah the Black Númenórean*",
         "",
         "smother"},
        {"*an Elf* utters the word 'earthquake'",
         C,
         CombatPhaseEnum::DONE,
         "*an Elf*",
         "",
         "earthquake"},
        {"Kinghal (KI) utters the word 'armour'", C, CombatPhaseEnum::DONE, "Kinghal", "", "armour"},
        // A stored spell recalled.
        {"You quickly recall your stored spell...", C, CombatPhaseEnum::STARTED, "you", "", "stored"},
        {"Argh! You cannot concentrate any more...", C, CombatPhaseEnum::BROKEN, "you", "", ""},
        {"Alas, not enough mana flows through you...", C, CombatPhaseEnum::REFUSED, "you", "", "mana"},
        // Backfires.
        {"Your spell backfired! You feel drained.", C, CombatPhaseEnum::BROKEN, "you", "", "backfire"},
        {"You have a sudden lapse of memory... Your spell backfired! You feel drained.",
         C,
         CombatPhaseEnum::BROKEN,
         "you",
         "",
         "backfire"},
        {"You mispronounced the magical words... Your spell backfired! You feel exhausted.",
         C,
         CombatPhaseEnum::BROKEN,
         "you",
         "",
         "backfire"},
        {"Your spell backfired! You feel your life draining away.",
         C,
         CombatPhaseEnum::BROKEN,
         "you",
         "",
         "backfire"},
        {"Barclay (BB)'s spell backfires, and he squeals in surprise!",
         C,
         CombatPhaseEnum::BROKEN,
         "Barclay",
         "",
         "backfire"},
        {"Oedipus' spell backfires, and he squeals in surprise!",
         C,
         CombatPhaseEnum::BROKEN,
         "Oedipus",
         "",
         "backfire"},
    });
    QCOMPARE(parsed("You quickly recall your stored spell...").quality, QString("quick"));

    // A stored spell starts the cast the prompt ends like any other.
    OwnCastTracker tracker;
    tracker.receiveEvent(parsed("You quickly recall your stored spell..."));
    QVERIFY(tracker.casting());
    tracker.receiveEvent(parsed("Argh! You cannot concentrate any more..."));
    QVERIFY(!tracker.receivePrompt().has_value());
}

void TestCombatLines::attackSpellTest()
{
    const auto H = BlowOutcomeEnum::HIT;
    checkBlows({
        // Lightning bolt: the caster's line names no caster, and is left to OwnCastTracker
        // (unnamedCasterTest).
        {"The lightning bolt hits *an Elf* with full impact.",
         H,
         "",
         "*an Elf*",
         "hit",
         "",
         "lightning bolt"},
        {"The lightning bolt hits *a dreadful Orc* (x) with full impact.",
         H,
         "",
         "*a dreadful Orc*",
         "hit",
         "",
         "lightning bolt"},
        {"*an Elf* sends a powerful lightning bolt at you, you stagger from the impact.",
         H,
         "*an Elf*",
         "you",
         "hit",
         "",
         "lightning bolt"},
        {"Glorizmaeg (g) staggers back as the lightning bolt sent by *Ripple the Black Númenórean* "
         "hits him.",
         H,
         "*Ripple the Black Númenórean*",
         "Glorizmaeg",
         "hit",
         "",
         "lightning bolt"},
        // Earthquake strikes the room.
        {"The earth trembles beneath your feet!", H, "you", "", "", "", "earthquake"},
        {"*an Elf* makes the earth tremble and shiver.", H, "*an Elf*", "", "", "", "earthquake"},
        {"The earth trembles and shivers.", H, "", "", "", "", "earthquake"},
        {"Some debris falls on you from above.", H, "", "you", "", "", "earthquake"},
        // Dispel evil and harm.
        {"As you call upon Elbereth, *an Orc* shivers in pain.",
         H,
         "you",
         "*an Orc*",
         "",
         "",
         "dispel evil"},
        {"*a Half-Elf* makes your evil soul suffer with his goodness.",
         H,
         "*a Half-Elf*",
         "you",
         "",
         "",
         "dispel evil"},
        {"Minde cries 'Elbereth Gilthoniel' and makes the scaly beast shiver in pain.",
         H,
         "Minde",
         "the scaly beast",
         "",
         "",
         "dispel evil"},
        {"A shadow is dissolved by your goodness.", H, "you", "A shadow", "", "", "dispel evil"},
        {"As you call on ancient powers, the scaly beast (k) twists in great pain.",
         H,
         "you",
         "the scaly beast",
         "",
         "",
         "harm"},
        {"*an Orc* raises his voice and calls great pain upon you.",
         H,
         "*an Orc*",
         "you",
         "",
         "",
         "harm"},
        {"Warathrum (WA) raises his voice and ancient powers make *an Orc* twist in pain.",
         H,
         "Warathrum",
         "*an Orc*",
         "",
         "",
         "harm"},
        // Colour spray.
        {"You spray *an Orc* with many-coloured rays of bright light.",
         H,
         "you",
         "*an Orc*",
         "spray",
         "",
         "colour spray"},
        {"*an Elf* sprays you with piercing rays of many-coloured light.",
         H,
         "*an Elf*",
         "you",
         "spray",
         "",
         "colour spray"},
        {"Boltok sprays the Balrog with painfully bright, concentrated rays of light.",
         H,
         "Boltok",
         "the Balrog",
         "spray",
         "",
         "colour spray"},
        {"As Róva (MM) completes her incantations, *Gâgzík the Orc*'s body is ripped apart by rays "
         "of light.",
         H,
         "Róva",
         "*Gâgzík the Orc*",
         "spray",
         "",
         "colour spray"},
        // Shocking grasp and chill touch.
        {"You grasp at *Thralk the Orc*, shocking him.",
         H,
         "you",
         "*Thralk the Orc*",
         "grasp",
         "",
         "shocking grasp"},
        {"You get a shock as *an Orc* grasps at you.",
         H,
         "*an Orc*",
         "you",
         "grasp",
         "",
         "shocking grasp"},
        {"A shadow looks shocked as Cârzah grasps at it.",
         H,
         "Cârzah",
         "A shadow",
         "grasp",
         "",
         "shocking grasp"},
        {"You feel drained of life as a wight bodyguard touches you.",
         H,
         "a wight bodyguard",
         "you",
         "touch",
         "",
         "chill touch"},
        {"A wight bodyguard chills *a Hobbit* who suddenly seems less lively.",
         H,
         "A wight bodyguard",
         "*a Hobbit*",
         "touch",
         "",
         "chill touch"},
        // Smother.
        {"Your lungs seem to burst as *Farrah the Black Númenórean* squeezes the air out of them.",
         H,
         "*Farrah the Black Númenórean*",
         "you",
         "",
         "",
         "smother"},
        {"As a dark wraith reaches towards him, Rûhn chokes and shivers in pain.",
         H,
         "a dark wraith",
         "Rûhn",
         "",
         "",
         "smother"},
        // Fireball, magic missile and burning hands in the forms read here.
        {"Sumba (L) throws a fireball at a shadow, completely enveloping it in flames.",
         H,
         "Sumba",
         "a shadow",
         "burn",
         "",
         "fireball"},
        {"The fireball sent by Merilder hits *a grim Woman* (ff) with full force, causing an "
         "immediate death.",
         H,
         "Merilder",
         "*a grim Woman*",
         "burn",
         "",
         "fireball"},
        {"A magic missile sent by *Imlach the Half-Elf* hits you, causing some pain.",
         H,
         "*Imlach the Half-Elf*",
         "you",
         "hit",
         "",
         "magic missile"},
        {"Rhaerys reaches out for *a Bear* and burns him to death.",
         H,
         "Rhaerys",
         "*a Bear*",
         "burn",
         "",
         "burning hands"},
        // The magic missile that kills, seen from the room, which used to read as a blow by "A
        // demon wolf falls to the ground in a lifeless heap, as Kaja" on "it".
        {"A demon wolf falls to the ground in a lifeless heap, as Kaja's magic missile hits it.",
         H,
         "Kaja",
         "A demon wolf",
         "hit",
         "",
         "magic missile"},
        // Call lightning, by the player, killing, on the player.
        {"With a crack of thunder, you call down lightning on Old-Man Willow (k).",
         H,
         "you",
         "Old-Man Willow",
         "",
         "",
         "call lightning"},
        {"With a crack of thunder, you call down lightning on *Smiles the Mountain Troll*.",
         H,
         "you",
         "*Smiles the Mountain Troll*",
         "",
         "",
         "call lightning"},
        {"As you call down lightning, *Ghurgor the Orc* is scorched to death.",
         H,
         "you",
         "*Ghurgor the Orc*",
         "",
         "",
         "call lightning"},
        {"A loud crack of thunder can be heard as *an Elf* calls down lightning on you.",
         H,
         "*an Elf*",
         "you",
         "",
         "",
         "call lightning"},
        // Black breath strikes all in its path: no target.
        {"Your exhalation of a black wind withers and weakens all in its path...",
         H,
         "you",
         "",
         "",
         "",
         "black breath"},
    });
    // Only the 2003-2006 powwow logs have these: call lightning seen from the room and killing
    // the player, shocking grasp killing, and chill touch's "seem".
    checkBlows({
        {"*Quavair the Noldorin Elf* strikes Stolb with a mighty bolt of lightning from the sky.",
         H,
         "*Quavair the Noldorin Elf*",
         "Stolb",
         "",
         "",
         "call lightning"},
        {"*Pagan the Beorning Man* calls down lightning from the sky, killing you.",
         H,
         "*Pagan the Beorning Man*",
         "you",
         "",
         "",
         "call lightning"},
        {"A shadow dies as you shock it.", H, "you", "A shadow", "grasp", "", "shocking grasp"},
        {"*Smitsy the Stoor Hobbit* (buh) dies as you shock him.",
         H,
         "you",
         "*Smitsy the Stoor Hobbit*",
         "grasp",
         "",
         "shocking grasp"},
        {"Falenor chills *a dreadful Orc* who suddenly seem less lively.",
         H,
         "Falenor",
         "*a dreadful Orc*",
         "touch",
         "",
         "chill touch"},
    });
    // Lines with no caster in them never are the player's by themselves.
    QVERIFY(parsed("The lightning bolt hits *an Elf* with full impact.").casterUnnamed);
    QVERIFY(!parsed("The impact of your lightning bolt kills *a Half-Elf*.").casterUnnamed);
    QVERIFY(!parsed("Some debris falls on you from above.").casterUnnamed);
}

void TestCombatLines::unnamedCasterTest()
{
    // What MumeXmlParser does with each line: attribute, then receiveEvent.
    const auto read = [](OwnCastTracker &tracker, const char *const line) {
        std::optional<CombatEvent> event = parseCombatLine(QString::fromUtf8(line));
        if (event.has_value()) {
            tracker.attribute(*event);
            tracker.receiveEvent(*event);
        }
        return event.value_or(CombatEvent{});
    };
    const auto feed = [&read](OwnCastTracker &tracker, const char *const line) {
        std::ignore = read(tracker, line);
    };
    const char *const bolt = "The lightning bolt hits *an Elf* with full impact.";

    // The player's own bolt: "You start to concentrate...", the spinner, "Ok." and the bolt,
    // then the prompt.
    OwnCastTracker tracker;
    QCOMPARE(read(tracker, bolt).actor, QString()); // nobody is casting: unknown
    feed(tracker, "You start to concentrate...");
    feed(tracker, "|/-\\|/-\\Ok.");
    const CombatEvent own = read(tracker, bolt);
    QCOMPARE(own.actor, QString("you"));
    QCOMPARE(own.target, QString("*an Elf*"));
    QCOMPARE(own.detail, QString("lightning bolt"));
    QVERIFY(tracker.receivePrompt().has_value());
    // The spell's line just after the prompt that ended the cast is still the player's; after
    // the next prompt it is nobody's.
    QCOMPARE(read(tracker, bolt).actor, QString("you"));
    QVERIFY(!tracker.receivePrompt().has_value());
    QCOMPARE(read(tracker, bolt).actor, QString());

    // A stored spell recalled is a cast too.
    feed(tracker, "You quickly recall your stored spell...");
    QCOMPARE(read(tracker, bolt).actor, QString("you"));
    QVERIFY(tracker.receivePrompt().has_value());

    // A broken cast releases nothing.
    QVERIFY(!tracker.receivePrompt().has_value());
    feed(tracker, "You start to concentrate...");
    feed(tracker, "Aye! You cannot concentrate any more...");
    QCOMPARE(read(tracker, bolt).actor, QString());
    QVERIFY(!tracker.receivePrompt().has_value());

    // Somebody else casting is not the player casting; and a line that names its caster keeps
    // it, whoever is casting.
    feed(tracker, "Kazadoe (K) begins some strange incantations...");
    QCOMPARE(read(tracker, bolt).actor, QString());
    feed(tracker, "You start to concentrate...");
    QCOMPARE(read(tracker,
                  "*an Elf* sends a powerful lightning bolt at you, you stagger from the impact.")
                 .actor,
             QString("*an Elf*"));
    QCOMPARE(read(tracker, "Some debris falls on you from above.").actor, QString());
    tracker.reset();
    QCOMPARE(read(tracker, bolt).actor, QString());
}

void TestCombatLines::fellTest()
{
    for (const char *line : {"You fall, and hit yourself!", "You lose your balance and fall!"}) {
        const CombatEvent e = parsed(line);
        QCOMPARE(e.kind, CombatKindEnum::SELF);
        QCOMPARE(e.phase, CombatPhaseEnum::FELL);
        QCOMPARE(e.actor, QString("you"));
    }
    // The earthquake's line for the one it throws says so; the other fall does not.
    QCOMPARE(parsed("You fall, and hit yourself!").detail, QString("earthquake"));
    QVERIFY(parsed("You lose your balance and fall!").detail.isEmpty());
    const CombatEvent flash = parsed("An extremely bright flash of light stuns you!");
    QCOMPARE(flash.kind, CombatKindEnum::SELF);
    QCOMPARE(flash.phase, CombatPhaseEnum::STUNNED);
}

void TestCombatLines::affectTest()
{
    const auto A = CombatKindEnum::AFFECT;
    const auto UP = CombatPhaseEnum::UP;
    const auto DOWN = CombatPhaseEnum::DOWN;
    const auto RE = CombatPhaseEnum::REFRESH;
    checkEvents({
        {"A blue transparent wall slowly appears around you.", A, UP, "you", "", "armour"},
        // The twiddlers come off first, as for any line.
        {"-\\|/-\\|/A blue transparent wall slowly appears around you.", A, UP, "you", "", "armour"},
        {"You feel less protected.", A, DOWN, "you", "", "armour"},
        {"Your magic armour is revitalised.", A, RE, "you", "", "armour"},
        {"You feel protected.", A, UP, "you", "", "shield"},
        {"Your magical shield wears off.", A, DOWN, "you", "", "shield"},
        {"Your protection is revitalised.", A, RE, "you", "", "shield"},
        {"You start glowing.", A, UP, "you", "", "sanctuary"},
        {"Ugúlukk (ug) is surrounded by a white aura.", A, UP, "Ugúlukk", "", "sanctuary"},
        {"Fedrianne is surrounded by a dim white aura.", A, UP, "Fedrianne", "", "sanctuary"},
        {"Walo is surrounded by a brilliant white aura.", A, UP, "Walo", "", "sanctuary"},
        {"The white aura around your body fades.", A, DOWN, "you", "", "sanctuary"},
        {"Your aura glows more intensely.", A, RE, "you", "", "sanctuary"},
        {"You begin to feel the light of Aman shine upon you.", A, UP, "you", "", "bless"},
        {"The light of Aman fades away from you.", A, DOWN, "you", "", "bless"},
        {"You feel a renewed light shine upon you.", A, RE, "you", "", "bless"},
        {"You feel stronger.", A, UP, "you", "", "strength"},
        {"You feel weaker.", A, DOWN, "you", "", "strength"},
        {"The duration of the strength spell has been improved.", A, RE, "you", "", "strength"},
        {"An energy begins to flow within your legs as your body becomes lighter.",
         A,
         UP,
         "you",
         "",
         "breath of briskness"},
        {"Your legs feel heavier.", A, DOWN, "you", "", "breath of briskness"},
        {"The energy in your legs is refreshed.", A, RE, "you", "", "breath of briskness"},
        {"You are surrounded by a misty shroud.", A, UP, "you", "", "shroud"},
        {"You feel yourself exposed.", A, DOWN, "you", "", "shroud"},
        {"You feel your awareness improve.", A, UP, "you", "", "sense life"},
        {"You feel less aware of your surroundings.", A, DOWN, "you", "", "sense life"},
        // MUME spells it both ways: 3454 lines with the z in the powwow logs, 88 with the s.
        {"Your magic armour is revitalized.", A, RE, "you", "", "armour"},
        {"Your protection is revitalized.", A, RE, "you", "", "shield"},
        {"Your misty shroud is renewed.", A, RE, "you", "", "shroud"},
        {"Your awareness is refreshed.", A, RE, "you", "", "sense life"},
        {"You become sensitive of magical auras.", A, UP, "you", "", "detect magic"},
        {"Your awareness of magical auras is renewed.", A, RE, "you", "", "detect magic"},
        {"Your perception of magical auras wears off.", A, DOWN, "you", "", "detect magic"},
        {"Your vision blurs.", A, DOWN, "you", "", "night vision"},
        {"The detect invisible wears off.", A, DOWN, "you", "", "detect invisibility"},
        {"Hearing the horn blow, you feel your urge to battle increase!",
         A,
         UP,
         "you",
         "",
         "battle glory"},
        {"You feel your newfound strength leaving you again.", A, DOWN, "you", "", "battle glory"},
        {"The warm taste of blood in your mouth vanishes.", A, DOWN, "you", "", "blood of sauron"},
        {"You have a righteous feeling!", A, UP, "you", "", "protection from evil"},
        {"You feel less righteous.", A, DOWN, "you", "", "protection from evil"},
        {"You feel aware of this place.", A, UP, "you", "", "watch room"},
        {"Your awareness decreases.", A, DOWN, "you", "", "watch room"},
        {"[xanscasoebb] Your awareness decreases.", A, DOWN, "you", "", "watch room"},
        {"The draught burns down your throat, and a fiery feeling fills your limbs.",
         A,
         UP,
         "you",
         "",
         "orkish draught"},
        {"As the warmth of the draught recedes from your limbs, you feel less energetic.",
         A,
         DOWN,
         "you",
         "",
         "orkish draught"},
        {"You feel your muscles relax and your pulse slow as the strength that welled within you "
         "subsides.",
         A,
         UP,
         "you",
         "",
         "tiredness"},
        {"You feel your muscles regain some of their former energy.", A, DOWN, "you", "", "tiredness"},
        {"You feel a sudden flash of dizziness causing you to pause before getting your "
         "directional bearings back.",
         A,
         UP,
         "you",
         "",
         "haggardness"},
        {"You feel steadier now.", A, DOWN, "you", "", "haggardness"},
        // Both endings: the powwow action's and the logs'.
        {"You feel a sudden loss of energy as the power that once mingled with your own has now "
         "vanished.",
         A,
         UP,
         "you",
         "",
         "lethargy"},
        {"You feel a sudden loss of energy as the power that once mingled with your own vanishes.",
         A,
         UP,
         "you",
         "",
         "lethargy"},
        {"You feel your magic energy coming back to you.", A, DOWN, "you", "", "lethargy"},
        // MUME breaks this one after "has"; a wider terminal may not.
        {"Alas, you realize that yet again the mighty knowledge of drowned Numenor has",
         A,
         UP,
         "you",
         "",
         "depression"},
        {"Alas, you realise that yet again the mighty knowledge of drowned Númenor has been "
         "lost... Despair settles on you.",
         A,
         UP,
         "you",
         "",
         "depression"},
        {"Your heart feels lighter.", A, DOWN, "you", "", "depression"},
        {"A haze seems to cloud your eyes, blurring your vision, making it difficult for you to "
         "see anything clearly.",
         A,
         UP,
         "you",
         "",
         "disorientation"},
        {"You feel less disoriented.", A, DOWN, "you", "", "disorientation"},
        {"You feel bolder.", A, DOWN, "you", "", "panic"},
        // Heals, on the player and on others.
        {"Your scratches and bruises disappear.", A, UP, "you", "", "heal"},
        {"You begin to see scars fade away and a feeling of health comes over you.",
         A,
         UP,
         "you",
         "",
         "heal"},
        {"A warm feeling fills your body.", A, UP, "you", "", "heal"},
        {"You feel a surge of healing power flow through you.", A, UP, "you", "", "heal"},
        {"You heal yourself.", A, UP, "you", "", "heal"},
        {"You heal Torkild (T).", A, UP, "Torkild", "", "heal"},
        {"*Xoone the Orc* (k) heals himself.", A, UP, "*Xoone the Orc*", "", "heal"},
        {"*a Half-Elf* heals *Kuntet the Dwarf*.", A, UP, "*Kuntet the Dwarf*", "", "heal"},
        {"Ibuki glows briefly as healing energy flows into her.", A, UP, "Ibuki", "", "heal"},
        {"*Stitch the Half-Elf* looks better.", A, UP, "*Stitch the Half-Elf*", "", "heal"},
    });

    // Night vision's line taking hold, and detect invisibility's, and a cure blindness on
    // somebody who sees: the line does not say which, so it is no event. The second half of the
    // depression's line says nothing the first did not.
    for (const char *const line : {"Your eyes tingle.",
                                   "been lost... Despair settles on you.",
                                   "You feel bolder than ever.",
                                   "Your vision blurs as the smoke thickens."}) {
        QVERIFY2(!parseCombatLine(QString::fromUtf8(line)).has_value(), line);
    }
}

void TestCombatLines::harmfulConditionTest()
{
    const auto C = CombatKindEnum::CONDITION;
    const auto N = CombatPhaseEnum::NONE;
    const auto A = CombatKindEnum::AFFECT;
    const auto DOWN = CombatPhaseEnum::DOWN;
    checkEvents({
        {"You bleed from open wounds.", C, N, "you", "", "bleeding"},
        {"A mother eagle (Kongo) bleeds from open wounds.", C, N, "A mother eagle", "", "bleeding"},
        {"*a Dwarf* bleeds from open wounds.", C, N, "*a Dwarf*", "", "bleeding"},
        {"You wish that your wounds would stop BLEEDING so much!", C, N, "you", "", "bleeding"},
        {"Your body turns numb as the poison speeds to your brain!", C, N, "you", "", "poisoned"},
        {"*Stitch the Half-Elf*'s body turns numb as the poison speeds to his brain!",
         C,
         N,
         "*Stitch the Half-Elf*",
         "",
         "poisoned"},
        {"The venom enters your body!", C, N, "you", "", "poisoned"},
        {"You suddenly feel a terrible headache!", C, N, "you", "", "poisoned"},
        {"A warm feeling runs through your body, you feel better.", A, DOWN, "you", "", "poisoned"},
        {"You fight the web to get free, but just become more entangled.",
         C,
         N,
         "you",
         "",
         "entangled"},
        {"*Svarten the Mountain Troll* struggles to free himself from the thick cobwebs.",
         C,
         N,
         "*Svarten the Mountain Troll*",
         "",
         "entangled"},
        {"You break free as the cobwebs around you go up in flames.", A, DOWN, "you", "", "entangled"},
        {"Bert the stone-troll seems to be blinded!", C, N, "Bert the stone-troll", "", "blind"},
        {"You have been blinded!", C, N, "you", "", "blind"},
        {"You feel a cloak of blindness dissolve.", A, DOWN, "you", "", "blind"},
    });
}

void TestCombatLines::notFix16Test()
{
    // Look-alikes of the new forms, all from the logs: chat that names them, room contents and
    // descriptions, and other things MUME says burn, bleed, glow or charge.
    for (const char *line :
         {"Amargwath narrates 'fark shimmer rip on trophy'",
          "An elven scout says 'Elbereth Gilthoniel!'",
          "Dego narrates 'he recovered?'",
          "Frtana narrates 'saai blinded'  ## Now is my best chance! ##",
          "Stromso tells the group 'got poisoned'",
          "Zûd tells the group 'can you flee?'",
          "Akallabêth tells you 'low mana'",
          "A black candle burns in a silver stand, giving off little light. (blue aura).",
          // "The draught burns down your throat, ..." was here as no burn; it is none still, but
          // it is the Orkish draught taking hold, an affect (affectTest).
          "*South* - The glare of the sun burns your eyes.",
          "The sun burns you! You slowly turn into stone.",
          "A cobweb burns away completely.",
          "Barclay (BB) burns a cobweb.",
          "The wild ox seems ready to charge you.",
          "A young goat playfully charges and hops away.",
          "A crayfish is here, snapping its claws at you.",
          "A shimmering golden aura briefly surrounds you.",
          "A moaning ghost advances towards you, shimmering with a pale light.",
          "You blink and feel weaker under the cruel light of the sun.",
          "An ancient fungus smothers the boulders.",
          "== Smack! And it bleeds! ==",
          "You feel hot with occasional chills.",
          "Your armour provides an average protection of 90%.",
          "Your eyes tingle."}) {
        QVERIFY2(!parseCombatLine(QString::fromUtf8(line)).has_value(), line);
    }
}

void TestCombatLines::killRefusedTest()
{
    // The refusals of kill and hit (help matrix counts in docs/combat-messages.md section 5d,
    // row 25): none of them a move.
    const auto R = CombatKindEnum::REFUSED;
    const auto N = CombatPhaseEnum::NONE;
    checkEvents({
        {"Nobody here by that name.", R, N, "you", "", "no-target"},
        {"You don't see any *orc* here.", R, N, "you", "*orc*", "no-target"},
        {"You don't see any troll here.", R, N, "you", "troll", "no-target"},
        {"Alas! There was no clear line of sight to him!", R, N, "you", "", "no-line-of-sight"},
        {"Alas! There was no clear line of sight to her!", R, N, "you", "", "no-line-of-sight"},
        {"Alas! There is no fighting space left to reach him!", R, N, "you", "", "no-space"},
        {"You're already fighting!", R, N, "you", "", "already-fighting"},
        {"You're already fighting him!", R, N, "you", "", "already-fighting"},
        {"Alas! You failed to reach him through the melee.", R, N, "you", "", "melee"},
        {"Your victim has disappeared!", R, N, "you", "", "victim-gone"},
    });

    // A spell whose target has gone did not go off: the prompt after it is not the cast done.
    OwnCastTracker tracker;
    tracker.receiveEvent(parsed("You start to concentrate..."));
    QVERIFY(tracker.casting());
    tracker.receiveEvent(parsed("Your victim has disappeared!"));
    QVERIFY(!tracker.casting());
    QVERIFY(!tracker.receivePrompt().has_value());
}

void TestCombatLines::rescueTest()
{
    const auto R = CombatKindEnum::RESCUE;
    const auto N = CombatPhaseEnum::NONE;
    checkEvents({
        // The rescuer's own line (28), the one rescued's (58), the room's (99).
        {"Heroically you come to Eve's rescue!", R, N, "you", "Eve", ""},
        {"Heroically you come to Aramarth (ara)'s rescue!", R, N, "you", "Aramarth", ""},
        {"Heroically you come to a mother eagle (BUFF)'s rescue!",
         R,
         N,
         "you",
         "a mother eagle",
         ""},
        {"You are rescued by Glorizmaeg (g), you are confused!", R, N, "Glorizmaeg", "you", ""},
        {"You are rescued by a mountain troll (aa), you are confused!",
         R,
         N,
         "a mountain troll",
         "you",
         ""},
        {"You are rescued by a thief, you are confused!", R, N, "a thief", "you", ""},
        {"Kohrn (koh) heroically rescues Malantur (mal).", R, N, "Kohrn", "Malantur", ""},
        {"*Eiluvial the Eriadorian Man* heroically rescues *Gilhdur the Half-Elf*.",
         R,
         N,
         "*Eiluvial the Eriadorian Man*",
         "*Gilhdur the Half-Elf*",
         ""},
        {"An orc-guard heroically rescues Brolg the orkish shaman.",
         R,
         N,
         "An orc-guard",
         "Brolg the orkish shaman",
         ""},
        // The failure (32) and the refusals (25, 68).
        {"You fail the rescue.", R, CombatPhaseEnum::FAILED, "you", "", ""},
        {"Who do you want to rescue?", R, CombatPhaseEnum::REFUSED, "you", "", "no-target"},
        {"But nobody is fighting him?", R, CombatPhaseEnum::REFUSED, "you", "", "not-fighting"},
    });
}

void TestCombatLines::assistTest()
{
    const auto A = CombatKindEnum::ASSIST;
    const auto N = CombatPhaseEnum::NONE;
    checkEvents({
        // The room's lines (3,336 and 1,805), the player's own (386) and the refusal (467).
        {"Tâcö joins Ugúlukk (lead)'s fight.", A, N, "Tâcö", "Ugúlukk", ""},
        {"An enslaved shadow joins *an Orc*'s fight.", A, N, "An enslaved shadow", "*an Orc*", ""},
        {"An orc-soldier joins an orc-soldier's fight.",
         A,
         N,
         "An orc-soldier",
         "an orc-soldier",
         ""},
        {"A mother eagle (AA) joins your fight.", A, N, "A mother eagle", "you", ""},
        {"A lithe orc joins your fight.", A, N, "A lithe orc", "you", ""},
        {"You assist Ugúlukk (lead).", A, N, "you", "Ugúlukk", ""},
        {"Who do you want to assist?", A, CombatPhaseEnum::REFUSED, "you", "", "no-target"},
    });
    // A client's own wording is not MUME's.
    QVERIFY(!parseCombatLine(QStringLiteral("A mother eagle (y) [JOINS] your fight.")).has_value()
            || parseCombatLine(QStringLiteral("A mother eagle (y) [JOINS] your fight."))->kind
                   != CombatKindEnum::ASSIST);
}

void TestCombatLines::backstabSetTest()
{
    const auto B = CombatKindEnum::BACKSTAB;
    const auto N = CombatPhaseEnum::NONE;
    checkEvents({
        // Landing: on the player (41, the killing one too), the player's own (45 and 20), and
        // somebody else's killing one told to the room.
        {"Suddenly *a Hobbit* stabs you in the back.", B, N, "*a Hobbit*", "you", ""},
        {"Suddenly *an Orc* stabs you in the back, you die.", B, N, "*an Orc*", "you", ""},
        {"A hillman-warrior makes a strange sound but is suddenly very silent, as you place a "
         "black runed dagger in his back.",
         B,
         N,
         "you",
         "A hillman-warrior",
         ""},
        {"A dirty uruk makes a strange sound, as you place a nimble blade in his back.",
         B,
         N,
         "you",
         "A dirty uruk",
         ""},
        {"Belish places a sharp thorn in the back of a shadow, resulting in some strange noises, a "
         "lot of blood and a corpse.",
         B,
         N,
         "Belish",
         "a shadow",
         ""},
        // Setting one up: the player's (287), on the player (45), on somebody else (12).
        {"You begin to move silently to the back of your victim...",
         B,
         CombatPhaseEnum::ATTEMPT,
         "you",
         "",
         ""},
        {"An assassin tries to sneak behind you!",
         B,
         CombatPhaseEnum::ATTEMPT,
         "An assassin",
         "you",
         ""},
        {"*Dalpha the Black Numenorean* tries to sneak behind you!",
         B,
         CombatPhaseEnum::ATTEMPT,
         "*Dalpha the Black Numenorean*",
         "you",
         ""},
        {"*Dalpha the Black Numenorean* tries to sneak behind a mother eagle (barry)...",
         B,
         CombatPhaseEnum::ATTEMPT,
         "*Dalpha the Black Numenorean*",
         "a mother eagle",
         ""},
        {"Zamdar tries to sneak behind *Mâlak the Orc*...",
         B,
         CombatPhaseEnum::ATTEMPT,
         "Zamdar",
         "*Mâlak the Orc*",
         ""},
        // Noticed (19, 14), refused (15, 184, 4).
        {"Oops, your victim seems to have sensed a danger!",
         B,
         CombatPhaseEnum::FAILED,
         "you",
         "",
         "sensed"},
        {"Oops, your victim caught you by surprise!",
         B,
         CombatPhaseEnum::FAILED,
         "you",
         "",
         "caught"},
        {"You can't backstab a fighting person, too alert!",
         B,
         CombatPhaseEnum::REFUSED,
         "you",
         "",
         "fighting"},
        {"Backstab whom?", B, CombatPhaseEnum::REFUSED, "you", "", "no-target"},
        {"For a successful backstab you need to be wielding a suitable weapon.",
         B,
         CombatPhaseEnum::REFUSED,
         "you",
         "",
         "weapon"},
    });
}

void TestCombatLines::spellWordsTest()
{
    // docs/combat-messages.md section 6.5: every incantation of the table names its spell, one
    // word ("utters the word") or several.
    const struct
    {
        const char *words;
        const char *spell;
    } table[] = {
        {"diesilla barh", "lightning bolt"},
        {"zabrahpdjatz", "earthquake"},
        {"qahijf gsfal", "colour spray"},
        {"eugszr zzur", "dispel evil"},
        {"yufzbarr", "fireball"},
        {"mosailla paieg", "burning hands"},
        {"noselacri", "blindness"},
        {"judicandus dies", "cure light"},
        {"pabraw", "harm"},
        {"braqt eaaf", "block door"},
        {"judicandus gzfuajg", "cure serious"},
        {"gpaqtuio ofags", "shocking grasp"},
        {"bfzahp ay bfugtizgg", "breath of briskness"},
        {"gwahpzf", "smother"},
        {"waouq wuggurz", "magic missile"},
        {"grzzs", "sleep"},
        {"gaiqhjabral", "sanctuary"},
        {"pzar", "heal"},
        {"eabratizgg", "darkness"},
        {"qfzahz yaae", "create food"},
        {"qpurr hajqp", "black breath"},
        {"safhar", "portal"},
    };
    for (const auto &row : table) {
        const QString words = QString::fromLatin1(row.words);
        const QString line = QStringLiteral("*an Elf* utters the %1 '%2'")
                                 .arg(words.contains(u' ') ? QStringLiteral("words")
                                                           : QStringLiteral("word"),
                                      words);
        const std::optional<CombatEvent> e = parseCombatLine(line);
        QVERIFY2(e.has_value(), row.words);
        QVERIFY2(e->kind == CombatKindEnum::CAST && e->phase == CombatPhaseEnum::DONE, row.words);
        QCOMPARE(e->actor, QString("*an Elf*"));
        QCOMPARE(e->detail, QString::fromLatin1(row.spell));
    }
    // Words the table does not know, and words understood, stay as they were heard.
    QCOMPARE(parsed("Pampa (P) utters the words 'lightning bolt'").detail,
             QString("lightning bolt"));
    QCOMPARE(parsed("*an Elf* utters the word 'xyzzyq'").detail, QString("xyzzyq"));
    // A client's own rendering of a cast is not MUME's.
    for (const char *line : {"*an Elf* utters the words '<-~~~~ bolt ~~~~->'",
                             "Cast Quick 'Lightning Bolt'",
                             "(((C-A-S-T-I-N-G)))"}) {
        const std::optional<CombatEvent> e = parseCombatLine(QString::fromUtf8(line));
        QVERIFY2(!e.has_value() || e->detail != QStringLiteral("lightning bolt"), line);
    }
    // Client-made escape and shatter lines are never patterns.
    for (const char *line : {"*Kur the Orc* tries to <E S C A P E>.", "-SHATTERS-"}) {
        QVERIFY2(!parseCombatLine(QString::fromUtf8(line)).has_value(), line);
    }
}

void TestCombatLines::fightRefusedTest()
{
    // docs/combat-messages.md section 5d rows 25, 28 and 30 and section 6.4, with the help
    // matrix's counts: what a fight rules out, disengage's refusals and the framing refusals of
    // cast. None of them a move.
    const auto R = CombatKindEnum::REFUSED;
    const auto C = CombatKindEnum::CAST;
    const auto N = CombatPhaseEnum::NONE;
    const auto X = CombatPhaseEnum::REFUSED;
    checkEvents({
        {"You can't do that while fighting.", R, N, "you", "", "while-fighting"},
        {"Do you not consider fighting as standing?", R, N, "you", "", "already-standing"},
        {"You are not fighting.", R, N, "you", "", "not-fighting"},
        {"Nobody was fighting you.", R, N, "you", "", "nobody-fighting-you"},
        {"What should the spell be cast upon?", C, X, "you", "", "no-target"},
        {"What should the spell be cast UPon?", C, X, "you", "", "no-target"},
        {"Try learning some spells first!", C, X, "you", "", "no-spells"},
    });
    // Twiddlers in front come off; a typed command glued in front is left alone (TODO FIX.16,
    // glue), so it is not read.
    QCOMPARE(parsed("|/You can't do that while fighting.").detail, QString("while-fighting"));
    QVERIFY(!parseCombatLine(QStringLiteral("scYou can't do that while fighting.")).has_value());

    // A refused cast never started: the next prompt is not the player's cast done.
    OwnCastTracker tracker;
    tracker.receiveEvent(parsed("What should the spell be cast upon?"));
    QVERIFY(!tracker.casting());
    QVERIFY(!tracker.receivePrompt().has_value());
}

void TestCombatLines::rangedTest()
{
    // docs/combat-messages.md sections 3.11, 5d row 37 and 6.4 (shoot): a missile weapon made
    // ready is a blow attempt with the verb shoot, outcome not known (hit), the weapon in effect.
    const auto aimed = parsed("A dark orkish archer is aiming a vicious bow at you.");
    QCOMPARE(aimed.kind, CombatKindEnum::BLOW);
    QCOMPARE(aimed.phase, CombatPhaseEnum::ATTEMPT);
    QCOMPARE(aimed.outcome, BlowOutcomeEnum::HIT);
    QCOMPARE(aimed.actor, QString("A dark orkish archer"));
    QCOMPARE(aimed.target, QString("you"));
    QCOMPARE(aimed.verb, QString("shoot"));
    QCOMPARE(aimed.effect, QString("vicious bow"));
    QCOMPARE(aimed.detail, QString("aim"));

    for (const char *line : {"You load a metal-cased bolt into your crossbow.",
                             "You load a bolt into your crossbow.",
                             "You load a blackened bolt into your crossbow."}) {
        const CombatEvent e = parsed(line);
        QVERIFY2(e.kind == CombatKindEnum::BLOW && e.phase == CombatPhaseEnum::ATTEMPT, line);
        QCOMPARE(e.actor, QString("you"));
        QCOMPARE(e.target, QString());
        QCOMPARE(e.verb, QString("shoot"));
        QCOMPARE(e.effect, QString("crossbow"));
        QCOMPARE(e.detail, QString("load"));
    }

    const struct
    {
        const char *line;
        const char *actor;
        const char *weapon;
    } nocks[] = {
        {"A dark orkish archer nocks a missile in an orkish shortbow.",
         "A dark orkish archer",
         "orkish shortbow"},
        {"*Buford the Orc* nocks a missile in a black horn shortbow.",
         "*Buford the Orc*",
         "black horn shortbow"},
        {"*Variant the Orc* (x) nocks a missile in an embellished longbow.",
         "*Variant the Orc*",
         "embellished longbow"},
        {"Demure nocks a missile in an embellished longbow.", "Demure", "embellished longbow"},
    };
    for (const auto &row : nocks) {
        const CombatEvent e = parsed(row.line);
        QVERIFY2(e.kind == CombatKindEnum::BLOW && e.phase == CombatPhaseEnum::ATTEMPT, row.line);
        QCOMPARE(e.actor, QString::fromUtf8(row.actor));
        QCOMPARE(e.target, QString());
        QCOMPARE(e.verb, QString("shoot"));
        QCOMPARE(e.effect, QString::fromUtf8(row.weapon));
        QCOMPARE(e.detail, QString("nock"));
    }

    // Loading a loaded one is refused, the weapon as what was in the way.
    checkEvents({
        {"But your crossbow is already loaded!",
         CombatKindEnum::REFUSED,
         CombatPhaseEnum::NONE,
         "you",
         "crossbow",
         "already-loaded"},
    });

    // The shot itself stays an ordinary blow.
    const CombatEvent shot = parsed("You strongly shoot the sage (k)'s body and tickle it.");
    QCOMPARE(shot.kind, CombatKindEnum::BLOW);
    QCOMPARE(shot.phase, CombatPhaseEnum::NONE);
    QCOMPARE(shot.verb, QString("shoot"));
    QCOMPARE(shot.target, QString("the sage"));
}

QTEST_MAIN(TestCombatLines)
