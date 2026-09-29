// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CombatLines.h"

#include <QRegularExpression>

namespace {

// Every attack verb MUME's logs show, in each form it takes. Listing them is what anchors the
// parse: attacker and target are both free text of any length, and without a known verb
// between them a greedy match reads "You pound the one-eyed orc's arm" as the verb "the".
#define VERBS \
    "hits?|slash(?:es)?|pierces?|pounds?|crush(?:es)?|cleaves?|stabs?|whips?|smites?|bites?|" \
    "claws?|stings?|shoots?|strikes?|punch(?:es)?|kicks?|lash(?:es)?|gores?|butts?|mauls?|" \
    "burns?|freezes?|shocks?|rams?|tramples?"

// The same verbs in the base form MUME uses after "tries to", "attempt to" and "fails to". Kept
// as a list rather than "any word", because "<someone> fails to <word> <someone>." also fits
// "The guard fails to notice you.", which is not a blow.
#define BASE_VERBS \
    "hit|slash|pierce|pound|crush|cleave|stab|whip|smite|bite|claw|sting|shoot|strike|punch|" \
    "kick|lash|gore|butt|maul|burn|freeze|shock|ram|trample"

// Where a blow lands, every part the logs name. Beasts have fore- and hindlegs and -feet, bats
// claws, a clump of roots a tip, a fungus a crown, a shrub leaves. Words MUME also uses for a
// body outside a blow ("burns your eyes", "freezes your spine") are left out.
#define PARTS \
    "(?:left |right )?(?:head|neck|body|arm|hand|leg|foot|wing|tail|back|chest|shoulder|face|" \
    "trunk|root|branch|paw|horn|belly|flank|side|waist|thigh|fin|foreleg|hindleg|forefoot|" \
    "hindfoot|claw|tip|crown|leaves|tail fin)"

// A group label trails a name in parentheses, "Kazadoe (K)", "the sage (buh)", or in square
// brackets, "*Stolb the Orc* [stolb]". One MUME port writes a possessive as "[stolb]s'" and
// "*an Orc*s'", so an s straight after the brackets or the stars comes off with them.
const QRegularExpression g_label{
    QStringLiteral(R"((?:\s+(?:\([^()]{1,8}\)|\[[^\[\]]{1,12}\]s?)|(?<=\*)s)$)")};
// The "twiddlers" prompt option draws \|/- while a delayed action runs, and they end up on the
// front of whatever line comes next. Backspaces come with them when the terminal is live.
const QRegularExpression g_twiddlers{QStringLiteral(R"(^[\\|/\-\x08]+(?=[A-Z*]))")};
// Some players have MUME add a counter after the line's last full stop: "*Hazad* sends you
// sprawling. [Damage:1]". It is not part of the sentence, and comes off before any pattern below
// is tried.
const QRegularExpression g_annotation{QStringLiteral(R"((?<=[.!?])\s+\[[A-Za-z]+: ?-?\d+\]$)")};

const QRegularExpression g_hit{QStringLiteral(
    R"(^(?<a>.+?) (?:(?<q>barely|lightly|strongly|brutally|savagely) )?(?<v>)" VERBS
    R"() (?:(?<you>your)|(?<t>.+?)'s?) (?<p>)" PARTS
    R"()(?: (?<s>(?:very |extremely |incredibly )?hard))?(?: and (?<e>[a-z]+) (?:it|them))?[.!]$)")};
// "<someone> tries to <verb> <someone>, but ...", where the tail says how it ended: a parry, a
// dodge ("you dodge swiftly", a kick's "you manage to avoid his foot"), or nothing ("but fails").
// Keeping somebody at bay ("but you keep him at bay", "but she manages to keep it at bay") is
// a parry.
// One MUME port names the part aimed at and drops the comma: "*an Orc* tries to cleave your neck
// but you manage to dodge it."
const QRegularExpression g_tries{QStringLiteral(
    R"(^(?<a>.+?) (?:tries|try) to (?<v>)" BASE_VERBS R"() (?:(?<you>you)(?:r (?<p>)" PARTS
    R"())?|(?<t>.+?)),? but (?:(?<parry>your parry is successful|(?:he|she|it) parries successfully|you swiftly parry it|(?:you|(?:he|she|it) manages to) keep (?:him|her|it|them) at bay)|(?<dodge>you dodge swiftly|you manage to (?:avoid (?:his|her|its) foot|dodge it))|(?<miss>fails?))\.$)")};
// A blow stopped, told from the defender's side, so the attacker comes second: "You swiftly
// dodge a dirty uruk's attempt to slash you.", "A shadow swiftly dodges your attempt to pierce
// it.", and one port's "*a Dreadful Orc* blocks your attempt to stab his head." and "X swiftly
// parries Y's attempt to cleave his neck."
const QRegularExpression g_defended{QStringLiteral(
    R"(^(?:(?<dyou>You)|(?<d>.+?)) (?:swiftly )?(?<how>dodges?|parry|parries|blocks?) (?:(?<ayou>your)|(?<a>.+?)'s?) attempt to (?<v>)" BASE_VERBS
    R"() (?:you|him|her|it|them|(?:your|his|her|its) (?<p>)" PARTS R"())\.$)")};
// A shadow's own defence, which names neither verb nor part: "The icy grasp of a shadow blocks
// your attempt!"
const QRegularExpression g_grasp{QStringLiteral(
    R"(^The icy grasp of (?<d>.+?) blocks (?:(?<ayou>your)|(?<a>.+?)'s?) attempt!$)")};
// Somebody stepping in to take a blow meant for someone else: "Pampa (P) intercepts *a Troll*'s
// blow.", "*Guthol the Dwarf* intercepts your blow.", "You intercept a burly orc's blow." -- but
// not "You fail to intercept ...", after which the blow goes where it was aimed.
const QRegularExpression g_intercept{QStringLiteral(
    R"(^(?:(?<dyou>You) intercept|(?<d>.+?) intercepts) (?:(?<ayou>your)|(?<a>.+?)'s?) blow\.$)")};
// The first swing of a fight, closing in: "You approach *a Dwarf* (ff), trying to pound him.",
// "*a Troll* approaches Pampa (P), trying to pound him.", and one port's "You approach *an Orc*
// [dork] and attack him." Where it lands, if it does, is a line of its own.
const QRegularExpression g_approach{QStringLiteral(
    R"(^(?:(?<ayou>You) approach|(?<a>.+?) approaches) (?:(?<you>you)|(?<t>.+?))(?:, trying to (?<v>)" BASE_VERBS
    R"() (?:you|him|her|it|them)| and attacks? (?:you|him|her|it|them))\.$)")};
const QRegularExpression g_miss{
    QStringLiteral(R"(^(?<a>.+?) fails? to (?<v>)" BASE_VERBS R"() (?<t>.+?)\.$)")};
// One port's own miss: "Your attempt to stab *an Orc* [stolb] fails."
const QRegularExpression g_yourMiss{
    QStringLiteral(R"(^Your attempt to (?<v>)" BASE_VERBS R"() (?<t>.+?) fails\.$)")};

// Attack spells that land, in the forms that name both sides: burning hands ("You burn *an
// Elf*.", "*an Elf* burns you."; somebody else's reads like "A small campfire burns here." and is
// left alone), magic missile and fireball ("Your fireball hits a shadow with full force, causing
// an immediate death.").
const QRegularExpression g_burn{
    QStringLiteral(R"(^(?:(?<ayou>You) burn (?!your )(?<t>.+?)|(?<a>.+?) burns (?<you>you))\.$)")};
const QRegularExpression g_missileStrike{
    QStringLiteral(R"(^You strike (?<t>.+?) with a magical missile\.$)")};
const QRegularExpression g_missileThrown{QStringLiteral(
    R"(^(?<a>.+?) throws a glowing magical missile at (?:(?<you>you)|(?<t>.+?))\.$)")};
const QRegularExpression g_spellHit{QStringLiteral(
    R"(^(?:(?<ayou>Your)|(?<a>.+?)'s?) (?<sp>magic missile|fireball) (?:hits|completely envelops) (?:(?<you>you)|(?<t>.+?))(?: in flames|, causing infernal pain| with full force, causing an immediate death)?\.$)")};

const QRegularExpression g_fleeAttempt{
    QStringLiteral(R"(^(?<w>.+?) panics, and attempts to flee\.$)")};
const QRegularExpression g_fleeHeels{QStringLiteral(R"(^You flee head over heels\.$)")};
const QRegularExpression g_fleeDirection{
    QStringLiteral(R"(^You flee (?<dir>north|south|east|west|up|down)\.$)")};
const QRegularExpression g_fleeFailed{
    QStringLiteral(R"(^PANIC! You (?:couldn't escape|can't quit the fight)!$)")};
const QRegularExpression g_escaped{QStringLiteral(R"(^(?<w>.+?) escaped the fight\.$)")};

/// A move MUME refused, and why. These are the lines MMapper's path machine drops a queued move
/// for (MumeXmlParserBase::initActionMap in AbstractParser-Actions.cpp), taken a little wider
/// where the logs show MUME wording one differently -- "Alas, you cannot go that way." with one
/// full stop, "Nah... You feel too relaxed to do that.." with two. A named group "t" is the
/// thing in the way: the door, or the mount.
struct NODISCARD Refusal final
{
    QRegularExpression pattern;
    const char *detail;
};

const Refusal g_refusals[] = {
    {QRegularExpression{QStringLiteral(R"(^No way! You are fighting for your life!$)")}, "fighting"},
    {QRegularExpression{QStringLiteral(R"(^You are too exhausted(?: to ride)?[.!]$)")}, "exhausted"},
    // A pack animal or a mount: "A pack horse (my) is too exhausted."
    {QRegularExpression{QStringLiteral(R"(^(?<t>.+?) is too exhausted\.$)")}, "mount-exhausted"},
    {QRegularExpression{QStringLiteral(R"(^Your mount refuses to follow your orders!$)")},
     "mount-refuses"},
    {QRegularExpression{
         QStringLiteral(R"(^ZBLAM! (?<t>.+?) doesn't want you riding (?:him|her|it) anymore\.$)")},
     "thrown"},
    {QRegularExpression{QStringLiteral(R"(^Nah\.\.\. You feel too relaxed(?: to do that)?\.+$)")},
     "resting"},
    {QRegularExpression{QStringLiteral(R"(^Maybe you should get on your feet first\?$)")},
     "sitting"},
    {QRegularExpression{QStringLiteral(R"(^In your dreams, or what\?$)")}, "sleeping"},
    // "The door seems to be closed.", "The bushes seems to be closed.", "The bars seem to be
    // closed.": the door's name as MUME gave it, which is its keyword.
    {QRegularExpression{QStringLiteral(R"(^The (?<t>.+?) seems? to be closed\.$)")}, "door-closed"},
    {QRegularExpression{QStringLiteral(R"(^Alas, you cannot go that way\.+$)")}, "no-exit"},
    {QRegularExpression{QStringLiteral(
         R"(^(?:The (?:ascent|descent) is too steep, you need to climb to go there\.|If you still want to try, you must 'climb' there\.)$)")},
     "climb"},
    {QRegularExpression{QStringLiteral(R"(^You failed to climb there(?: and .+)?\.$)")},
     "climb-failed"},
    {QRegularExpression{QStringLiteral(R"(^You need to swim to go there\.$)")}, "swim"},
    {QRegularExpression{QStringLiteral(R"(^You failed swimming there\.$)")}, "swim-failed"},
    {QRegularExpression{QStringLiteral(R"(^You (?:can't go|cannot ride) into deep water!$)")},
     "deep-water"},
    {QRegularExpression{QStringLiteral(R"(^You cannot ride there\.$)")}, "cannot-ride"},
    {QRegularExpression{QStringLiteral(R"(^You unsuccessfully try to break through the ice\.$)")},
     "ice"},
    {QRegularExpression{QStringLiteral(R"(^Your boat cannot enter this place\.$)")}, "boat"},
};

const QRegularExpression g_bash{QStringLiteral(
    R"(^(?<a>.+?) sends (?<t>.+?) sprawling(?: with a powerful bash)?[.!](?: \[.*\])?$)")};
const QRegularExpression g_yourBash{
    QStringLiteral(R"(^Your bash at (?<t>.+?) sends (?:him|her|it|them) sprawling\.$)")};
// "The cave-worm whips its tail around, sending Beat sprawling!"
const QRegularExpression g_tailBash{QStringLiteral(
    R"(^(?<a>.+?) whips (?:his|her|its) tail around, sending (?<t>.+?) sprawling!$)")};
// A bash that missed floors the one who tried it, whoever tells it: the one who dodged, a
// bystander, or the one who fell.
const QRegularExpression g_bashDodged{QStringLiteral(
    R"(^You dodge a bash from (?<a>.+?) who loses (?:his|her|its) balance(?: and falls)?\.$)")};
const QRegularExpression g_bashEvaded{QStringLiteral(
    R"(^You evade (?<a>.+?)(?: \[[^\]]+\])?'s? bash, causing (?:him|her|it) to fall flat on (?:his|her|its) face\.$)")};
const QRegularExpression g_bashAvoided{QStringLiteral(
    R"(^(?<t>.+?) avoids being bashed by (?<a>.+?) who loses (?:his|her|its) balance(?: and falls)?\.$)")};
const QRegularExpression g_yourBashAvoided{QStringLiteral(
    R"(^As (?<t>.+?) avoids your bash, you topple over and (?:lose your balance|fall to the ground)\.$)")};
// Most often a few rounds after the player was sent sprawling (624 of 723 times in the logs,
// against 5 after the player's own bash), so it is read as getting over being bashed.
const QRegularExpression g_bashOver{QStringLiteral(R"(^Your head stops stinging\.$)")};
const QRegularExpression g_backstab{
    QStringLiteral(R"(^Suddenly (?<a>.+?) stabs you in the back\.$)")};

const QRegularExpression g_death{QStringLiteral(
    R"(^(?<w>.+?) (?:is dead! R\.I\.P\.|has drawn (?:his|her|its) last breath! R\.I\.P\.)$)")};
const QRegularExpression g_youDead{QStringLiteral(R"(^You are dead!\s+Sorry\.\.\.$)")};
// The tail is MUME's prognosis, "and will slowly die, if not aided", and nothing else, so that
// "You are stunned by the shine that glimmers in his eyes." is not taken for a condition.
const QRegularExpression g_condition{QStringLiteral(
    R"(^(?:(?<you>You) are|(?<w>.+?) is) (?<c>incapacitated|mortally wounded|stunned)(?:,? (?:and|an|but) will .+)?\.$)")};

// "You muster all of your concentration..." is the same start, and "Struggling against the
// Yellow Face" is how it reads for a character the sun troubles.
const QRegularExpression g_concentrate{QStringLiteral(
    R"(^(?:You start to concentrate|You muster all of your concentration|Struggling against the Yellow Face, you try to concentrate)\.\.\.$)")};
// An older form that names the spell: "You start casting burning hands [quickly]."
const QRegularExpression g_startCasting{
    QStringLiteral(R"(^You start casting (?<s>[a-z' ]+?) ?(?:\[(?<q>[a-z]+)\])?\.$)")};
const QRegularExpression g_concentrationBroken{QStringLiteral(
    R"(^(?:Aye! You cannot concentrate any more\.\.\.|You were not able to keep your concentration while moving\.|You lost your concentration[.!]|Ack! You can(?:'t|not) concentrate any ?more(?:\.\.\.|\.)|The cruel light of the sun made you lose your concentration!)$)")};
// Before any concentration began: "resting" when MUME says why, otherwise no detail.
const QRegularExpression g_castRefused{QStringLiteral(
    R"(^(?:You can't concentrate enough while (?<why>resting)\.|Impossible! You can't concentrate enough!?\.|You can't concentrate enough\.)$)")};
const QRegularExpression g_incantation{
    QStringLiteral(R"(^(?<a>.+?) begins some strange incantations\.\.\.$)")};
const QRegularExpression g_utters{QStringLiteral(R"(^(?<a>.+?) utters the words '(?<w>[^']+)'$)")};

const QRegularExpression g_stunned{
    QStringLiteral(R"(^You are stunned and cannot realize what is going on!$)")};
const QRegularExpression g_sleepy{QStringLiteral(R"(^You feel sleepy\.$)")};
const QRegularExpression g_stood{QStringLiteral(R"(^You stand up\.$)")};

/// "you" for the player in any grammatical form, otherwise the name without its group label.
NODISCARD QString who(QString name)
{
    name = name.trimmed();
    if (name.compare(QStringLiteral("you"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("your"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("you");
    }
    name.remove(g_label);
    return name;
}

/// A verb or effect as MUME conjugates it for a third person, back to its base form:
/// "slashes" to "slash", "tickles" to "tickle", "hits" to "hit".
NODISCARD QString base(const QString &word)
{
    for (const char *ending : {"ches", "shes", "sses"}) {
        if (word.endsWith(QLatin1String(ending)) && word.length() > 4) {
            return word.left(word.length() - 2);
        }
    }
    if (word.endsWith(QLatin1Char('s')) && word.length() > 2) {
        return word.left(word.length() - 1);
    }
    return word;
}

NODISCARD CombatEvent make(const CombatKindEnum kind, const QString &text)
{
    CombatEvent event;
    event.kind = kind;
    event.text = text;
    return event;
}

/// One side of a blow from a pattern that has a group for the player ("you", "your") beside the
/// group for a name: "you" when the first matched, otherwise the name.
NODISCARD QString side(const QRegularExpressionMatch &m,
                       const QStringView you,
                       const QStringView name)
{
    return m.captured(you).isEmpty() ? who(m.captured(name)) : QStringLiteral("you");
}

NODISCARD CombatEvent blow(const BlowOutcomeEnum outcome,
                           const QString &line,
                           const QString &actor,
                           const QString &target)
{
    CombatEvent event = make(CombatKindEnum::BLOW, line);
    event.outcome = outcome;
    event.actor = actor;
    event.target = target;
    return event;
}

} // namespace

std::optional<CombatEvent> parseCombatLine(const QString &raw)
{
    QString line = raw.trimmed();
    line.remove(g_twiddlers);
    line.remove(g_annotation);
    if (line.isEmpty()) {
        return std::nullopt;
    }

    QRegularExpressionMatch m;

    if ((m = g_defended.match(line)).hasMatch()) {
        // Told from the defender's side: "You swiftly dodge X's attempt". Tried before a hit,
        // which "You block X's attempt to cleave your head." would otherwise read as.
        const QString how = m.captured(u"how");
        const bool dodged = how.startsWith(QLatin1String("dodge"));
        CombatEvent event = blow(dodged ? BlowOutcomeEnum::DODGE : BlowOutcomeEnum::PARRY,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"dyou", u"d"));
        event.verb = m.captured(u"v");
        event.part = m.captured(u"p");
        if (how.startsWith(QLatin1String("block"))) {
            event.detail = QStringLiteral("block");
        }
        return event;
    }
    if ((m = g_hit.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 who(m.captured(u"a")),
                                 side(m, u"you", u"t"));
        event.quality = m.captured(u"q");
        event.verb = base(m.captured(u"v"));
        event.part = m.captured(u"p");
        event.severity = m.captured(u"s");
        event.effect = base(m.captured(u"e"));
        return event;
    }
    if ((m = g_tries.match(line)).hasMatch()) {
        const BlowOutcomeEnum outcome = !m.captured(u"parry").isEmpty()   ? BlowOutcomeEnum::PARRY
                                        : !m.captured(u"dodge").isEmpty() ? BlowOutcomeEnum::DODGE
                                                                          : BlowOutcomeEnum::MISS;
        CombatEvent event = blow(outcome, line, who(m.captured(u"a")), side(m, u"you", u"t"));
        event.verb = m.captured(u"v");
        event.part = m.captured(u"p");
        return event;
    }
    if ((m = g_grasp.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::PARRY,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 who(m.captured(u"d")));
        event.detail = QStringLiteral("block");
        return event;
    }
    if ((m = g_intercept.match(line)).hasMatch()) {
        // The one who stepped in is the one the blow met.
        CombatEvent event = blow(BlowOutcomeEnum::PARRY,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"dyou", u"d"));
        event.detail = QStringLiteral("intercept");
        return event;
    }
    if ((m = g_approach.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"you", u"t"));
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.verb = m.captured(u"v");
        return event;
    }
    if ((m = g_yourMiss.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::MISS,
                                 line,
                                 QStringLiteral("you"),
                                 who(m.captured(u"t")));
        event.verb = m.captured(u"v");
        return event;
    }
    if ((m = g_burn.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"you", u"t"));
        event.verb = QStringLiteral("burn");
        return event;
    }
    if ((m = g_missileStrike.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 QStringLiteral("you"),
                                 who(m.captured(u"t")));
        event.verb = QStringLiteral("strike");
        event.detail = QStringLiteral("magic missile");
        return event;
    }
    if ((m = g_missileThrown.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 who(m.captured(u"a")),
                                 side(m, u"you", u"t"));
        event.verb = QStringLiteral("strike");
        event.detail = QStringLiteral("magic missile");
        return event;
    }
    if ((m = g_spellHit.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"you", u"t"));
        event.detail = m.captured(u"sp");
        event.verb = event.detail == QStringLiteral("fireball") ? QStringLiteral("burn")
                                                                : QStringLiteral("hit");
        return event;
    }
    if ((m = g_fleeAttempt.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.actor = who(m.captured(u"w"));
        return event;
    }
    if (g_fleeHeels.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.actor = QStringLiteral("you");
        return event;
    }
    if ((m = g_fleeDirection.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::ESCAPED;
        event.actor = QStringLiteral("you");
        event.detail = m.captured(u"dir");
        return event;
    }
    if (g_fleeFailed.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::FAILED;
        event.actor = QStringLiteral("you");
        return event;
    }
    if ((m = g_escaped.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::ESCAPED;
        event.actor = who(m.captured(u"w"));
        return event;
    }
    for (const Refusal &refusal : g_refusals) {
        if ((m = refusal.pattern.match(line)).hasMatch()) {
            CombatEvent event = make(CombatKindEnum::REFUSED, line);
            event.actor = QStringLiteral("you");
            event.target = who(m.captured(u"t"));
            event.detail = QString::fromLatin1(refusal.detail);
            return event;
        }
    }
    if ((m = g_yourBash.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.actor = QStringLiteral("you");
        event.target = who(m.captured(u"t"));
        return event;
    }
    if ((m = g_bash.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.actor = who(m.captured(u"a"));
        event.target = who(m.captured(u"t"));
        return event;
    }
    if ((m = g_tailBash.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.actor = who(m.captured(u"a"));
        event.target = who(m.captured(u"t"));
        return event;
    }
    if ((m = g_bashDodged.match(line)).hasMatch() || (m = g_bashEvaded.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.phase = CombatPhaseEnum::DODGED;
        event.actor = who(m.captured(u"a"));
        event.target = QStringLiteral("you");
        return event;
    }
    if ((m = g_yourBashAvoided.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.phase = CombatPhaseEnum::DODGED;
        event.actor = QStringLiteral("you");
        event.target = who(m.captured(u"t"));
        return event;
    }
    if ((m = g_bashAvoided.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.phase = CombatPhaseEnum::DODGED;
        event.actor = who(m.captured(u"a"));
        event.target = who(m.captured(u"t"));
        return event;
    }
    if (g_bashOver.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.phase = CombatPhaseEnum::RECOVERED;
        event.target = QStringLiteral("you");
        return event;
    }
    if ((m = g_backstab.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BACKSTAB, line);
        event.actor = who(m.captured(u"a"));
        event.target = QStringLiteral("you");
        return event;
    }
    if ((m = g_death.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::DEATH, line);
        event.actor = who(m.captured(u"w"));
        return event;
    }
    if (g_youDead.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::DEATH, line);
        event.actor = QStringLiteral("you");
        return event;
    }
    if ((m = g_condition.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CONDITION, line);
        event.actor = m.captured(u"you").isEmpty() ? who(m.captured(u"w")) : QStringLiteral("you");
        event.detail = m.captured(u"c");
        return event;
    }
    if (g_concentrate.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::STARTED;
        event.actor = QStringLiteral("you");
        return event;
    }
    if ((m = g_startCasting.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::STARTED;
        event.actor = QStringLiteral("you");
        event.detail = m.captured(u"s");
        event.quality = m.captured(u"q");
        return event;
    }
    if (g_concentrationBroken.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::BROKEN;
        event.actor = QStringLiteral("you");
        return event;
    }
    if ((m = g_castRefused.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::REFUSED;
        event.actor = QStringLiteral("you");
        event.detail = m.captured(u"why");
        return event;
    }
    if ((m = g_incantation.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::STARTED;
        event.actor = who(m.captured(u"a"));
        return event;
    }
    if ((m = g_utters.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::DONE;
        event.actor = who(m.captured(u"a"));
        event.detail = m.captured(u"w");
        return event;
    }
    if (g_stunned.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::SELF, line);
        event.phase = CombatPhaseEnum::STUNNED;
        event.actor = QStringLiteral("you");
        return event;
    }
    if (g_sleepy.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::SELF, line);
        event.phase = CombatPhaseEnum::SLEEPY;
        event.actor = QStringLiteral("you");
        return event;
    }
    if (g_stood.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::SELF, line);
        event.phase = CombatPhaseEnum::STOOD;
        event.actor = QStringLiteral("you");
        return event;
    }
    // Last, because its shape -- "<someone> fails to <verb> <someone>." -- is the loosest, and
    // any line that fits a stricter one above is better read that way.
    if ((m = g_miss.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::MISS,
                                 line,
                                 who(m.captured(u"a")),
                                 who(m.captured(u"t")));
        event.verb = m.captured(u"v");
        return event;
    }
    return std::nullopt;
}

std::string_view to_string_view(const CombatKindEnum kind)
{
    switch (kind) {
    case CombatKindEnum::BLOW:
        return "blow";
    case CombatKindEnum::FLEE:
        return "flee";
    case CombatKindEnum::REFUSED:
        return "refused";
    case CombatKindEnum::BASH:
        return "bash";
    case CombatKindEnum::BACKSTAB:
        return "backstab";
    case CombatKindEnum::CONDITION:
        return "condition";
    case CombatKindEnum::DEATH:
        return "death";
    case CombatKindEnum::CAST:
        return "cast";
    case CombatKindEnum::SELF:
        return "self";
    }
    return "unknown";
}

std::string_view to_string_view(const BlowOutcomeEnum outcome)
{
    switch (outcome) {
    case BlowOutcomeEnum::HIT:
        return "hit";
    case BlowOutcomeEnum::PARRY:
        return "parry";
    case BlowOutcomeEnum::DODGE:
        return "dodge";
    case BlowOutcomeEnum::MISS:
        return "miss";
    }
    return "unknown";
}

std::string_view to_string_view(const CombatPhaseEnum phase)
{
    switch (phase) {
    case CombatPhaseEnum::NONE:
        return "";
    case CombatPhaseEnum::ATTEMPT:
        return "attempt";
    case CombatPhaseEnum::FAILED:
        return "failed";
    case CombatPhaseEnum::ESCAPED:
        return "escaped";
    case CombatPhaseEnum::STARTED:
        return "started";
    case CombatPhaseEnum::DONE:
        return "done";
    case CombatPhaseEnum::BROKEN:
        return "broken";
    case CombatPhaseEnum::STUNNED:
        return "stunned";
    case CombatPhaseEnum::SLEEPY:
        return "sleepy";
    case CombatPhaseEnum::STOOD:
        return "stood";
    case CombatPhaseEnum::REFUSED:
        return "refused";
    case CombatPhaseEnum::DODGED:
        return "dodged";
    case CombatPhaseEnum::RECOVERED:
        return "recovered";
    }
    return "unknown";
}

void OwnCastTracker::receiveEvent(const CombatEvent &event)
{
    const bool yours = event.actor == QStringLiteral("you");
    if (event.kind == CombatKindEnum::CAST && yours) {
        m_casting = event.phase == CombatPhaseEnum::STARTED;
    } else if (event.kind == CombatKindEnum::DEATH && yours) {
        m_casting = false;
    }
}

std::optional<CombatEvent> OwnCastTracker::receivePrompt()
{
    if (!m_casting) {
        return std::nullopt;
    }
    m_casting = false;
    CombatEvent event;
    event.kind = CombatKindEnum::CAST;
    event.phase = CombatPhaseEnum::DONE;
    event.actor = QStringLiteral("you");
    return event;
}
