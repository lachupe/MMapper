// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "CombatLines.h"

#include <QRegularExpression>

namespace {

// Every attack verb MUME's logs show, in each form it takes ("charges" is a mounted charge: "An
// orkish warg-rider barely charges your body and tickles it."). Listing them is what anchors the
// parse: attacker and target are both free text of any length, and without a known verb
// between them a greedy match reads "You pound the one-eyed orc's arm" as the verb "the".
#define VERBS \
    "hits?|slash(?:es)?|pierces?|pounds?|crush(?:es)?|cleaves?|stabs?|whips?|smites?|bites?|" \
    "claws?|stings?|shoots?|strikes?|punch(?:es)?|kicks?|lash(?:es)?|gores?|butts?|mauls?|" \
    "burns?|freezes?|shocks?|rams?|tramples?|charges?"

// The same verbs in the base form MUME uses after "tries to", "attempt to" and "fails to". Kept
// as a list rather than "any word", because "<someone> fails to <word> <someone>." also fits
// "The guard fails to notice you.", which is not a blow.
#define BASE_VERBS \
    "hit|slash|pierce|pound|crush|cleave|stab|whip|smite|bite|claw|sting|shoot|strike|punch|" \
    "kick|lash|gore|butt|maul|burn|freeze|shock|ram|trample|charge"

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

// A tunic that turns a blow aside, told to its wearer ("Your ebony tunic shimmers as a mother
// eagle fails to hit you.", "Your tunic shimmers and a great warg fails to engage you.") and to
// the room ("A lithe orc tries to engage *a noble Elf*, but his ebony tunic shimmers and shifts
// him."). "engage" is the opening of a fight, which the tunic turns aside as well; no other line
// in the logs uses it. The tunic is named as MUME names it: "ebony tunic", "frayed tunic", "grey
// tunic", "tunic".
#define TUNIC R"((?<item>(?:[a-z]+ )?tunic))"
const QRegularExpression g_shimmer{
    QStringLiteral(R"(^Your )" TUNIC R"( shimmers (?:as|and) (?<a>.+?) fails to (?<v>)" BASE_VERBS
                   R"(|engage) you\.$)")};
const QRegularExpression g_shimmerOther{QStringLiteral(
    R"(^(?<a>.+?) tries to (?<v>)" BASE_VERBS R"(|engage) (?<t>.+?), but (?:his|her|its) )" TUNIC
    R"( shimmers and shifts (?:him|her|it)\.$)")};

// The top tier of damage has a second wording, "*a Troll* hits Dragomir's body and reduce it to
// small fragments." (sic), read as the effect "fragment".
const QRegularExpression g_hit{QStringLiteral(
    R"(^(?<a>.+?) (?:(?<q>barely|lightly|strongly|brutally|savagely) )?(?<v>)" VERBS
    R"() (?:(?<you>your)|(?<t>.+?)'s?) (?<p>)" PARTS
    R"()(?: (?<s>(?:very |extremely |incredibly )?hard))?(?: and (?<e>[a-z]+) (?:it|them)| and reduce (?:it|them) to small (?<frag>fragments))?[.!]$)")};
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
// Somebody opening a fight on the player, the counterpart of "You approach X, trying to ...":
// "A mother eagle quickly approaches, trying to hit you."
const QRegularExpression g_quickApproach{
    QStringLiteral(R"(^(?<a>.+?) quickly approaches, trying to (?<v>)" BASE_VERBS R"() you\.$)")};
// The player's opening held off: "You approach *a dreadful Orc*, but he manages to keep you at
// bay."
const QRegularExpression g_approachHeld{
    QStringLiteral(R"(^You approach (?<t>.+?), but (?:he|she|it) manages to keep you at bay\.$)")};
// The defender failing to hold an opening off, so the one who approached gets through: "You try
// to keep *Ful the Orc* at bay, but fail.", "*Jir the Black Numenorean* tries to keep you at bay,
// but fails.", "A lithe orc tries to keep a mother eagle at bay, but fails." The one kept at bay
// is the attacker.
const QRegularExpression g_atBayFailed{QStringLiteral(
    R"(^(?:(?<dyou>You) try|(?<d>.+?) tries) to keep (?:(?<ayou>you)|(?<a>.+?)) at bay, but fails?\.$)")};
// A beast's bite or sting that names no part: "A huge tarantula bites you!", "A pack leader bites
// a lithe orc!", "Mormaeg stings you.", "A huge queen bee deeply stings *Flex the Orc*!" A part
// ("bites your arm") is a blow of the ordinary kind, so the target may hold no apostrophe.
const QRegularExpression g_bite{QStringLiteral(
    R"(^(?<a>.+?) (?:(?<q>deeply) )?(?<v>bites|stings) (?:(?<you>you)|(?!your )(?<t>[^']+?))[.!]$)")};
// A called shot at a gap in the armour; the blow itself follows on a line of its own: "*an Orc*
// strikes for a weakness in your armour!", "You aim for a gap in *a Hobbit*'s armour!", "You
// strike for a weakness in a giant termite's chitinous armour!"
const QRegularExpression g_armourGap{QStringLiteral(
    R"(^(?:(?<ayou>You) (?:strike|aim)|(?<a>.+?) (?:strikes|aims)) for a (?:weakness|gap) in (?:(?<you>your)|(?<t>.+?)'s?(?: [a-z]+)?) armour!$)")};
// A fumble that lands on the one who swung: "Ooops! You fumble... and hit yourself HARD!",
// "*an Orc* fumbles and hits himself HARD."
const QRegularExpression g_fumble{QStringLiteral(
    R"(^(?:Ooops! (?<you>You) fumble\.\.\. and hit yourself HARD!|(?<a>.+?) fumbles and hits (?:himself|herself|itself) HARD\.)$)")};

// Attack spells that land, in the forms that name both sides: burning hands ("You burn *an
// Elf*.", "*an Elf* burns you.", "Auluua (Au) burns *Thralk the Orc*."), magic missile and
// fireball ("Your fireball hits a shadow with full force, causing an immediate death."). The
// burn between two others must not read the things MUME says burn: "A small campfire burns here,
// its low flames ...", "The fiery liquid burns down your throat, ...", "A candle burns in a
// silver stand, ...", "A cobweb burns away completely.", "X burns a cobweb." (the spell, on no
// one), "The glare of the sun burns your eyes.", "... burns you! You slowly turn into stone."
const QRegularExpression g_burn{QStringLiteral(
    R"(^(?:(?<ayou>You) burn (?!your )(?<t>.+?)|(?<a>.+?) burns (?:(?<you>you)|(?!(?:here|your|down|DOWN|in|away)\b|a cobweb\.)(?<t2>[^,!]+?)))\.$)")};
const QRegularExpression g_missileStrike{
    QStringLiteral(R"(^You strike (?<t>.+?) with a magical missile\.$)")};
const QRegularExpression g_missileThrown{QStringLiteral(
    R"(^(?<a>.+?) throws a glowing magical missile at (?:(?<you>you)|(?<t>.+?))\.$)")};
const QRegularExpression g_spellHit{QStringLiteral(
    R"(^(?:(?<ayou>Your)|(?<a>.+?)'s?) (?<sp>magic missile|fireball) (?:hits|completely envelops) (?:(?<you>you)|(?<t>.+?))(?: in flames|, causing infernal pain| with full force, causing an immediate death)?\.$)")};

/// An attack spell landing, as MUME tells it to the caster, to the one it struck and to the
/// room, and the killing forms. A named group "a" is the caster and "t" the one struck; where a
/// line names neither, `yours` and `onYou` say the player is the one. The spell's name is the
/// event's detail, and the verb the line's own where it has one.
struct NODISCARD SpellLine final
{
    QRegularExpression pattern;
    const char *spell;
    const char *verb;
    bool yours;
    bool onYou;
};

const SpellLine g_spellLines[] = {
    // Lightning bolt. The caster's line names no caster: "The lightning bolt hits *an Elf* with
    // full impact." follows the player's own "You start to concentrate..." and "Ok.", and is
    // the only strength the logs show.
    {QRegularExpression{QStringLiteral(R"(^The lightning bolt hits (?<t>.+?) with full impact\.$)")},
     "lightning bolt",
     "hit",
     true,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) sends a powerful lightning bolt at you, you stagger from the impact\.$)")},
     "lightning bolt",
     "hit",
     false,
     true},
    {QRegularExpression{QStringLiteral(
         R"(^(?<t>.+?) staggers back as the lightning bolt sent by (?<a>.+?) hits (?:him|her|it)\.$)")},
     "lightning bolt",
     "hit",
     false,
     false},
    {QRegularExpression{QStringLiteral(R"(^The impact of your lightning bolt kills (?<t>.+?)\.$)")},
     "lightning bolt",
     "hit",
     true,
     false},
    {QRegularExpression{QStringLiteral(R"(^A lightning bolt from (?<a>.+?) shatters (?<t>.+?)\.$)")},
     "lightning bolt",
     "hit",
     false,
     false},
    // Earthquake strikes the room, so no one is its target: the caster's line, the room's,
    // the one heard from nearby with no caster, and the debris on the player (the caster too).
    {QRegularExpression{QStringLiteral(R"(^The earth trembles beneath your feet!$)")},
     "earthquake",
     "",
     true,
     false},
    {QRegularExpression{QStringLiteral(R"(^(?<a>.+?) makes the earth tremble and shiver\.$)")},
     "earthquake",
     "",
     false,
     false},
    {QRegularExpression{QStringLiteral(R"(^The earth trembles and shivers\.$)")},
     "earthquake",
     "",
     false,
     false},
    {QRegularExpression{QStringLiteral(R"(^Some debris falls on you from above\.$)")},
     "earthquake",
     "",
     false,
     true},
    // Dispel evil, Elbereth's name on the good side.
    {QRegularExpression{
         QStringLiteral(R"(^As you call upon Elbereth, (?<t>.+?) shivers in pain\.$)")},
     "dispel evil",
     "",
     true,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) makes your evil soul suffer with (?:his|her|its) goodness\.$)")},
     "dispel evil",
     "",
     false,
     true},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) cries 'Elbereth Gilthoniel' and makes (?<t>.+?) shiver in pain\.$)")},
     "dispel evil",
     "",
     false,
     false},
    {QRegularExpression{QStringLiteral(R"(^(?<t>.+?) is dissolved by your goodness\.$)")},
     "dispel evil",
     "",
     true,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) completely dissolves (?<t>.+?) with (?:his|her|its) goodness\.$)")},
     "dispel evil",
     "",
     false,
     false},
    // Harm, the dark powers on the other.
    {QRegularExpression{
         QStringLiteral(R"(^As you call on ancient powers, (?<t>.+?) twists in great pain\.$)")},
     "harm",
     "",
     true,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) raises (?:his|her|its) voice and calls great pain upon you\.$)")},
     "harm",
     "",
     false,
     true},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) raises (?:his|her|its) voice and ancient powers make (?<t>.+?) twist in pain\.$)")},
     "harm",
     "",
     false,
     false},
    // Colour spray.
    {QRegularExpression{
         QStringLiteral(R"(^You spray (?<t>.+?) with many-coloured rays of bright light\.$)")},
     "colour spray",
     "spray",
     true,
     false},
    {QRegularExpression{
         QStringLiteral(R"(^(?<a>.+?) sprays you with piercing rays of many-coloured light\.$)")},
     "colour spray",
     "spray",
     false,
     true},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) sprays (?<t>.+?) with painfully bright, concentrated rays of light\.$)")},
     "colour spray",
     "spray",
     false,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^Your spray of light rips (?<t>.+?) apart completely, killing (?:him|her|it)\.$)")},
     "colour spray",
     "spray",
     true,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^As (?<a>.+?) completes (?:his|her|its) incantations, (?<t>.+?)'s? body is ripped apart by rays of light\.$)")},
     "colour spray",
     "spray",
     false,
     false},
    // Shocking grasp.
    {QRegularExpression{QStringLiteral(R"(^You grasp at (?<t>.+?), shocking (?:him|her|it)\.$)")},
     "shocking grasp",
     "grasp",
     true,
     false},
    {QRegularExpression{QStringLiteral(R"(^You get a shock as (?<a>.+?) grasps at you\.$)")},
     "shocking grasp",
     "grasp",
     false,
     true},
    {QRegularExpression{
         QStringLiteral(R"(^(?<t>.+?) looks shocked as (?<a>.+?) grasps at (?:him|her|it)\.$)")},
     "shocking grasp",
     "grasp",
     false,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) looks pleased as (?:he|she|it) shocks (?<t>.+?) to death\.$)")},
     "shocking grasp",
     "grasp",
     false,
     false},
    // Chill touch, which the logs show from wights and players alike ('qpurr hajqp').
    {QRegularExpression{QStringLiteral(R"(^You feel drained of life as (?<a>.+?) touches you\.$)")},
     "chill touch",
     "touch",
     false,
     true},
    {QRegularExpression{
         QStringLiteral(R"(^(?<a>.+?) chills (?<t>.+?) who suddenly seems less lively\.$)")},
     "chill touch",
     "touch",
     false,
     false},
    // Smother ('gwahpzf').
    {QRegularExpression{QStringLiteral(
         R"(^Your lungs seem to burst as (?<a>.+?) squeezes the air out of them\.$)")},
     "smother",
     "",
     false,
     true},
    {QRegularExpression{QStringLiteral(
         R"(^As (?<a>.+?) reaches towards (?:him|her|it), (?<t>.+?) chokes and shivers in pain\.$)")},
     "smother",
     "",
     false,
     false},
    // Fireball and magic missile in the forms g_spellHit does not read.
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) throws a fireball at (?<t>.+?), completely enveloping (?:him|her|it) in flames\.$)")},
     "fireball",
     "burn",
     false,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^The fireball sent by (?<a>.+?) hits (?<t>.+?) with full force, causing an immediate death\.$)")},
     "fireball",
     "burn",
     false,
     false},
    {QRegularExpression{
         QStringLiteral(R"(^A magic missile sent by (?<a>.+?) hits you, causing some pain\.$)")},
     "magic missile",
     "hit",
     false,
     true},
    {QRegularExpression{QStringLiteral(
         R"(^Your magic missile blows the life force away from (?<t>.+?), killing (?:him|her|it)\.$)")},
     "magic missile",
     "hit",
     true,
     false},
    // Burning hands that kills; the plain burn is g_burn.
    {QRegularExpression{QStringLiteral(R"(^You burned (?<t>.+?) to death\.$)")},
     "burning hands",
     "burn",
     true,
     false},
    {QRegularExpression{QStringLiteral(
         R"(^(?<a>.+?) reaches out for (?<t>.+?) and burns (?:him|her|it) to death\.$)")},
     "burning hands",
     "burn",
     false,
     false},
};

// Somebody else's flee, and the same flee failing: "*an Orc* panics, but can't stop fighting to
// flee."
const QRegularExpression g_fleeAttempt{QStringLiteral(
    R"(^(?<w>.+?) panics, (?:and attempts to flee|(?<failed>but can't stop fighting to flee))\.$)")};
const QRegularExpression g_fleeHeels{QStringLiteral(R"(^You flee head over heels\.$)")};
const QRegularExpression g_fleeDirection{
    QStringLiteral(R"(^You flee (?<dir>north|south|east|west|up|down)\.$)")};
// The player's flee or escape failing, roots holding the player among them.
const QRegularExpression g_fleeFailed{QStringLiteral(
    R"(^(?:PANIC! You (?:couldn't escape|can't quit the fight)!|You try to flee, but cannot!|You failed to escape the fight!|You can't seem to escape (?:the|a clump of) roots!)$)")};
const QRegularExpression g_escaped{QStringLiteral(R"(^(?<w>.+?) escaped the fight\.$)")};
// The escape skill, a slower way out of a fight: the player's "You seek to escape...", then
// "You successfully escaped the fight!"; seen from the room, "*an Orc* seems to avoid the fight."
// (the escape under way; "*an Orc* escaped the fight." follows) or "X tried to escape but
// failed."
const QRegularExpression g_escapeStart{QStringLiteral(R"(^You seek to escape\.\.\.$)")};
const QRegularExpression g_escapedYou{QStringLiteral(R"(^You successfully escaped the fight!$)")};
const QRegularExpression g_avoidsFight{QStringLiteral(R"(^(?<w>.+?) seems to avoid the fight\.$)")};
const QRegularExpression g_escapeFailed{
    QStringLiteral(R"(^(?<w>.+?) tried to escape but failed\.$)")};

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
    // Today's wording, and the older one; the path machine does not know either yet.
    {QRegularExpression{QStringLiteral(
         R"(^(?:OOPS! You cannot go there while riding|Oops! You cannot go there riding)!$)")},
     "cannot-ride"},
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
const QRegularExpression g_bashOver{
    QStringLiteral(R"(^(?:Your head stops stinging\.|You have recovered from being bashed!)$)")};
// Somebody else getting over a bash: "*an Orc* seems to have recovered his senses."
const QRegularExpression g_senses{
    QStringLiteral(R"(^(?<w>.+?) seems to have recovered (?:his|her|its) senses\.$)")};
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
// A stored spell cast: quick, since it was prepared beforehand.
const QRegularExpression g_recall{QStringLiteral(R"(^You quickly recall your stored spell\.\.\.$)")};
const QRegularExpression g_concentrationBroken{QStringLiteral(
    R"(^(?:Aye! You cannot concentrate any more\.\.\.|Argh! You cannot concentrate any more\.\.\.|You were not able to keep your concentration while moving\.|You lost your concentration[.!]|Ack! You can(?:'t|not) concentrate any ?more(?:\.\.\.|\.)|The cruel light of the sun made you lose your concentration!)$)")};
// Before any concentration began: "resting" when MUME says why, otherwise no detail.
const QRegularExpression g_castRefused{QStringLiteral(
    R"(^(?:You can't concentrate enough while (?<why>resting)\.|Impossible! You can't concentrate enough!?\.|You can't concentrate enough\.|Alas, not enough (?<mana>mana) flows through you\.\.\.)$)")};
// A spell that went wrong on its caster: "You have a sudden lapse of memory... Your spell
// backfired! You feel drained.", "*X*'s spell backfires, and he squeals in surprise!"
const QRegularExpression g_backfire{QStringLiteral(
    R"(^(?:(?:You have a sudden lapse of memory|You mispronounced the magical words)\.\.\. )?Your spell backfired!(?: You feel (?:drained|exhausted|your life draining away)\.)?$)")};
const QRegularExpression g_backfireOther{
    QStringLiteral(R"(^(?<a>.+?)'s? spell backfires, and (?:he|she|it) squeals in surprise!$)")};
const QRegularExpression g_incantation{
    QStringLiteral(R"(^(?<a>.+?) begins some strange incantations\.\.\.$)")};
// "utters the words 'lightning bolt'", and for a spell of one word "utters the word 'pabraw'".
const QRegularExpression g_utters{QStringLiteral(R"(^(?<a>.+?) utters the words? '(?<w>[^']+)'$)")};

const QRegularExpression g_stunned{
    QStringLiteral(R"(^You are stunned and cannot realize what is going on!$)")};
const QRegularExpression g_sleepy{QStringLiteral(R"(^You feel sleepy\.$)")};
const QRegularExpression g_stood{QStringLiteral(R"(^You stand up\.$)")};
// Thrown down: by an earthquake ("You fall, and hit yourself!"), or by the ground giving way.
const QRegularExpression g_fell{
    QStringLiteral(R"(^(?:You fall, and hit yourself|You lose your balance and fall)!$)")};
// A thrown pouch of powder.
const QRegularExpression g_flash{
    QStringLiteral(R"(^An extremely bright flash of light stuns you!$)")};

/// Lasting effects taking hold, wearing off and being renewed, heals landing, and the harmful
/// states that are conditions. Each line is MUME's own as the logs and the help show it. A named
/// group "w" is the one it is on; without one it is the player.
struct NODISCARD AffectLine final
{
    QRegularExpression pattern;
    CombatKindEnum kind;
    CombatPhaseEnum phase;
    const char *detail;
};

#define AFFECT_LINE(re, phase, detail) \
    {QRegularExpression{QStringLiteral(re)}, CombatKindEnum::AFFECT, CombatPhaseEnum::phase, detail}
#define CONDITION_LINE(re, detail) \
    {QRegularExpression{QStringLiteral(re)}, \
     CombatKindEnum::CONDITION, \
     CombatPhaseEnum::NONE, \
     detail}

const AffectLine g_affectLines[] = {
    AFFECT_LINE(R"(^A blue transparent wall slowly appears around you\.$)", UP, "armour"),
    AFFECT_LINE(R"(^You feel less protected\.$)", DOWN, "armour"),
    AFFECT_LINE(R"(^Your magic armour is revitalised\.$)", REFRESH, "armour"),
    AFFECT_LINE(R"(^You feel protected\.$)", UP, "shield"),
    AFFECT_LINE(R"(^Your magical shield wears off\.$)", DOWN, "shield"),
    AFFECT_LINE(R"(^Your protection is revitalised\.$)", REFRESH, "shield"),
    AFFECT_LINE(R"(^You start glowing\.$)", UP, "sanctuary"),
    AFFECT_LINE(R"(^(?<w>.+?) is surrounded by a (?:dim |brilliant )?white aura\.$)",
                UP,
                "sanctuary"),
    AFFECT_LINE(R"(^The white aura around your body fades\.$)", DOWN, "sanctuary"),
    AFFECT_LINE(R"(^Your aura glows more intensely\.$)", REFRESH, "sanctuary"),
    AFFECT_LINE(R"(^You begin to feel the light of Aman shine upon you\.$)", UP, "bless"),
    AFFECT_LINE(R"(^The light of Aman fades away from you\.$)", DOWN, "bless"),
    AFFECT_LINE(R"(^You feel a renewed light shine upon you\.$)", REFRESH, "bless"),
    AFFECT_LINE(R"(^You feel stronger\.$)", UP, "strength"),
    AFFECT_LINE(R"(^You feel weaker\.$)", DOWN, "strength"),
    AFFECT_LINE(R"(^The duration of the strength spell has been improved\.$)", REFRESH, "strength"),
    AFFECT_LINE(R"(^An energy begins to flow within your legs as your body becomes lighter\.$)",
                UP,
                "breath of briskness"),
    AFFECT_LINE(R"(^Your legs feel heavier\.$)", DOWN, "breath of briskness"),
    AFFECT_LINE(R"(^The energy in your legs is refreshed\.$)", REFRESH, "breath of briskness"),
    AFFECT_LINE(R"(^You are surrounded by a misty shroud\.$)", UP, "shroud"),
    AFFECT_LINE(R"(^You feel yourself exposed\.$)", DOWN, "shroud"),
    AFFECT_LINE(R"(^You feel your awareness improve\.$)", UP, "sense life"),
    AFFECT_LINE(R"(^You feel less aware of your surroundings\.$)", DOWN, "sense life"),
    // Heals: cure light, cure serious, cure critic, heal, a pale blue stone, and on others.
    AFFECT_LINE(R"(^Your scratches and bruises disappear\.$)", UP, "heal"),
    AFFECT_LINE(R"(^You begin to see scars fade away and a feeling of health comes over you\.$)",
                UP,
                "heal"),
    AFFECT_LINE(R"(^You can feel the broken bones within you heal and reshape themselves\.$)",
                UP,
                "heal"),
    AFFECT_LINE(R"(^A warm feeling fills your body\.$)", UP, "heal"),
    AFFECT_LINE(R"(^You feel a surge of healing power flow through you\.$)", UP, "heal"),
    AFFECT_LINE(R"(^You heal yourself\.$)", UP, "heal"),
    AFFECT_LINE(R"(^You heal (?<w>.+?)\.$)", UP, "heal"),
    AFFECT_LINE(R"(^(?<w>.+?) heals (?:himself|herself|itself)\.$)", UP, "heal"),
    AFFECT_LINE(R"(^.+? heals (?<w>.+?)\.$)", UP, "heal"),
    AFFECT_LINE(R"(^(?<w>.+?) glows briefly as healing energy flows into (?:him|her|it)\.$)",
                UP,
                "heal"),
    AFFECT_LINE(R"(^(?<w>.+?) looks better\.$)", UP, "heal"),
    // Poison: arachnia, venom, psylonia; and remove poison.
    CONDITION_LINE(
        R"(^(?:Your|(?<w>.+?)'s?) body turns numb as the poison speeds to (?:your|his|her|its) brain!$)",
        "poisoned"),
    CONDITION_LINE(R"(^The venom (?:enters your body|runs into your veins)!$)", "poisoned"),
    CONDITION_LINE(R"(^(?<w>.+?) is shaken with spasm as the venom enters (?:his|her|its) body!$)",
                   "poisoned"),
    CONDITION_LINE(R"(^You suddenly feel a terrible headache!$)", "poisoned"),
    AFFECT_LINE(R"(^A warm feeling runs through your body, you feel better\.$)", DOWN, "poisoned"),
    CONDITION_LINE(R"(^(?:You bleed|(?<w>.+?) bleeds) from open wounds\.$)", "bleeding"),
    CONDITION_LINE(R"(^You wish that your wounds would stop BLEEDING so much!$)", "bleeding"),
    // Webs.
    CONDITION_LINE(R"(^You fight the web to get free, but just become more entangled\.$)",
                   "entangled"),
    CONDITION_LINE(R"(^You try to break free, but stay entangled to (?:foul spider webs|something)\.$)",
                   "entangled"),
    CONDITION_LINE(R"(^Thick, sticky cobwebs stick to your limbs, preventing you from moving\.$)",
                   "entangled"),
    CONDITION_LINE(
        R"(^(?<w>.+?) struggles to free (?:himself|herself|itself) from the thick cobwebs\.$)",
        "entangled"),
    AFFECT_LINE(R"(^You break free as the cobwebs around you go up in flames\.$)",
                DOWN,
                "entangled"),
    CONDITION_LINE(R"(^(?<w>.+?) seems to be blinded!$)", "blind"),
    CONDITION_LINE(R"(^You have been blinded!$)", "blind"),
    AFFECT_LINE(R"(^(?:You feel a cloak of blindness dissolve\.|Your vision returns!)$)",
                DOWN,
                "blind"),
};

#undef AFFECT_LINE
#undef CONDITION_LINE

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

    if ((m = g_shimmer.match(line)).hasMatch() || (m = g_shimmerOther.match(line)).hasMatch()) {
        // Tried before anything else: the wearer's line otherwise reads as a miss by "Your ebony
        // tunic shimmers as a mother eagle".
        CombatEvent event = blow(BlowOutcomeEnum::DODGE,
                                 line,
                                 who(m.captured(u"a")),
                                 m.captured(u"t").isEmpty() ? QStringLiteral("you")
                                                            : who(m.captured(u"t")));
        event.verb = m.captured(u"v");
        if (event.verb == QStringLiteral("engage")) {
            event.phase = CombatPhaseEnum::ATTEMPT;
        }
        event.effect = m.captured(u"item");
        event.detail = QStringLiteral("shimmer");
        return event;
    }
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
        event.effect = m.captured(u"frag").isEmpty() ? base(m.captured(u"e"))
                                                     : QStringLiteral("fragment");
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
    if ((m = g_quickApproach.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 who(m.captured(u"a")),
                                 QStringLiteral("you"));
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.verb = m.captured(u"v");
        return event;
    }
    if ((m = g_approachHeld.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::PARRY,
                                 line,
                                 QStringLiteral("you"),
                                 who(m.captured(u"t")));
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.detail = QStringLiteral("keep-at-bay");
        return event;
    }
    if ((m = g_atBayFailed.match(line)).hasMatch()) {
        // The one who was to be kept off gets through: an opening, whose blow follows.
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"dyou", u"d"));
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.detail = QStringLiteral("keep-at-bay");
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
    if ((m = g_bite.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 who(m.captured(u"a")),
                                 side(m, u"you", u"t"));
        event.quality = m.captured(u"q");
        event.verb = base(m.captured(u"v"));
        return event;
    }
    if ((m = g_armourGap.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 side(m, u"you", u"t"));
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.detail = QStringLiteral("armour-gap");
        return event;
    }
    if ((m = g_fumble.match(line)).hasMatch()) {
        const QString fumbler = side(m, u"you", u"a");
        CombatEvent event = blow(BlowOutcomeEnum::HIT, line, fumbler, fumbler);
        event.verb = QStringLiteral("hit");
        event.detail = QStringLiteral("fumble");
        return event;
    }
    // Before g_burn, which would read "X reaches out for Y and burns her to death." as a burn of
    // "her to death".
    for (const SpellLine &spell : g_spellLines) {
        if ((m = spell.pattern.match(line)).hasMatch()) {
            const QString actor = side(m, u"ayou", u"a");
            const QString target = side(m, u"you", u"t");
            CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                     line,
                                     actor.isEmpty() && spell.yours ? QStringLiteral("you") : actor,
                                     target.isEmpty() && spell.onYou ? QStringLiteral("you")
                                                                     : target);
            event.verb = QString::fromLatin1(spell.verb);
            event.detail = QString::fromLatin1(spell.spell);
            return event;
        }
    }
    if ((m = g_burn.match(line)).hasMatch()) {
        CombatEvent event = blow(BlowOutcomeEnum::HIT,
                                 line,
                                 side(m, u"ayou", u"a"),
                                 m.captured(u"t2").isEmpty() ? side(m, u"you", u"t")
                                                             : who(m.captured(u"t2")));
        event.verb = QStringLiteral("burn");
        event.detail = QStringLiteral("burning hands");
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
    if ((m = g_fleeAttempt.match(line)).hasMatch() || (m = g_avoidsFight.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = m.captured(u"failed").isEmpty() ? CombatPhaseEnum::ATTEMPT
                                                      : CombatPhaseEnum::FAILED;
        event.actor = who(m.captured(u"w"));
        return event;
    }
    if ((m = g_escapeFailed.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::FAILED;
        event.actor = who(m.captured(u"w"));
        return event;
    }
    if (g_escapeStart.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::ATTEMPT;
        event.actor = QStringLiteral("you");
        return event;
    }
    if (g_escapedYou.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::FLEE, line);
        event.phase = CombatPhaseEnum::ESCAPED;
        event.actor = QStringLiteral("you");
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
    if ((m = g_senses.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::BASH, line);
        event.phase = CombatPhaseEnum::RECOVERED;
        event.target = who(m.captured(u"w"));
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
    if (g_recall.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::STARTED;
        event.actor = QStringLiteral("you");
        event.quality = QStringLiteral("quick");
        event.detail = QStringLiteral("stored");
        return event;
    }
    if (g_concentrationBroken.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::BROKEN;
        event.actor = QStringLiteral("you");
        return event;
    }
    if (g_backfire.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::BROKEN;
        event.actor = QStringLiteral("you");
        event.detail = QStringLiteral("backfire");
        return event;
    }
    if ((m = g_backfireOther.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::BROKEN;
        event.actor = who(m.captured(u"a"));
        event.detail = QStringLiteral("backfire");
        return event;
    }
    if ((m = g_castRefused.match(line)).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::CAST, line);
        event.phase = CombatPhaseEnum::REFUSED;
        event.actor = QStringLiteral("you");
        event.detail = m.captured(u"why").isEmpty() ? m.captured(u"mana") : m.captured(u"why");
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
    if (g_fell.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::SELF, line);
        event.phase = CombatPhaseEnum::FELL;
        event.actor = QStringLiteral("you");
        return event;
    }
    if (g_flash.match(line).hasMatch()) {
        CombatEvent event = make(CombatKindEnum::SELF, line);
        event.phase = CombatPhaseEnum::STUNNED;
        event.actor = QStringLiteral("you");
        return event;
    }
    for (const AffectLine &affect : g_affectLines) {
        if ((m = affect.pattern.match(line)).hasMatch()) {
            CombatEvent event = make(affect.kind, line);
            event.phase = affect.phase;
            event.actor = m.captured(u"w").isEmpty() ? QStringLiteral("you")
                                                     : who(m.captured(u"w"));
            event.detail = QString::fromLatin1(affect.detail);
            return event;
        }
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
    case CombatKindEnum::AFFECT:
        return "affect";
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
    case CombatPhaseEnum::FELL:
        return "fell";
    case CombatPhaseEnum::UP:
        return "up";
    case CombatPhaseEnum::DOWN:
        return "down";
    case CombatPhaseEnum::REFRESH:
        return "refresh";
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
