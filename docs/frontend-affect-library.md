# Affect message library (MMapper.Char.Affects)

MUME gives no durations. MMapper tracks the effects on the player's character from the
lines each effect produces when it goes up, comes down or is refreshed, and sets the list
right with every `stat`. The table is `g_affectLines[]` in `src/parser/CombatLines.cpp`
(events: kind `affect` or `condition`, phase `up` / `down` / `refresh`, detail = effect);
`CharAffectsTracker` in `src/parser/CharAffects.cpp` keeps the list and
`MMapper.Char.Affects` publishes it. Tests: `TestCombatLines::affectTest`,
`affectLibraryTest`, `harmfulConditionTest`, and `TestFrontend` (tracker).

Sources: the user's powwow patterns (`mume3d/data/combat/combat-powwow-patterns.tsv`),
the old MMapper `spells.ini` (found at `~/data/dev/Branch/MMapper/build/src/spells.ini`,
14 spells, all already in the table), and real lines from the powwow logs
(`~/data/powwow/logs/archives`, 2005-2006; a log-wide scan compared the lines between
consecutive `Affected by:` listings to find what turns an effect on or off).

Wording notes (variants the table accepts): MUME spells both "revitalised" and
"revitalized"; the pager or the twiddler may leave `\|/-` characters before a line, and
MMapper strips them first; a watch room's line may carry the room key
(`[xanscasoebb] Your awareness decreases.`); a long line may be broken after "has" (the
depression line); lines are matched whole, so a line only alike is no event.
Somebody else's effect gives the same line as one's own for most spells, so only lines
with the player as the actor are tracked.

## Coverage

"Before" = in MMapper's table before this work. Phase: U up, D down, R refresh.
Condition = a CONDITION event the tracker turns into the effect.

| Effect | Up | Down | Refresh | Source | Before |
|---|---|---|---|---|---|
| armour | A blue transparent wall slowly appears around you. | You feel less protected. | Your magic armour is revitalised/-ized. | spells.ini, logs | yes |
| shield | You feel protected. | Your magical shield wears off. | Your protection is revitalised/-ized. | spells.ini, logs | yes |
| sanctuary | You start glowing. (others: X is surrounded by a [dim/brilliant] white aura.) | The white aura around your body fades. | Your aura glows more intensely. | spells.ini, logs | yes |
| bless | You begin to feel the light of Aman shine upon you. | The light of Aman fades away from you. | You feel a renewed light shine upon you. | spells.ini, logs | yes |
| strength | You feel stronger. | You feel weaker. | The duration of the strength spell has been improved. | spells.ini, logs | yes |
| breath of briskness | An energy begins to flow within your legs as your body becomes lighter. | Your legs feel heavier. | The energy in your legs is refreshed. | spells.ini, logs | yes |
| shroud | You are surrounded by a misty shroud. | You feel yourself exposed. | Your misty shroud is renewed. | powwow, logs | yes |
| sense life | You feel your awareness improve. | You feel less aware of your surroundings. | Your awareness is refreshed. | spells.ini, logs | yes |
| detect magic | You become sensitive of magical auras. | Your perception of magical auras wears off. | Your awareness of magical auras is renewed. | powwow, logs | yes |
| night vision | (Your eyes tingle. is ambiguous, see below) | Your vision blurs. | - | spells.ini | yes (down only) |
| detect invisibility | (Your eyes tingle., ambiguous) | The detect invisible wears off. | - | powwow | yes (down only) |
| detect evil | You feel aware of all that is foul and evil. | (Your awareness decreases. is watch room's, ambiguous) | - | powwow action | NEW (up) |
| battle glory | Hearing the horn blow, you feel your urge to battle increase! | You feel your newfound strength leaving you again. | - | spells.ini, powwow | yes |
| blood of sauron | - | The warm taste of blood in your mouth vanishes. | - | powwow | yes (down only) |
| protection from evil | You have a righteous feeling! | You feel less righteous. | - | logs | yes |
| watch room | You feel aware of this place. / this room. | [key] Your awareness decreases. | - | powwow, logs | yes ("this room" NEW) |
| orkish draught | The draught burns down your throat, and a fiery feeling fills your limbs. | As the warmth of the draught recedes from your limbs, you feel less energetic. | - | logs | yes |
| tiredness | You feel your muscles relax and your pulse slow as the strength that welled within you subsides. | You feel your muscles regain some of their former energy. | - | spells.ini, logs | yes |
| haggardness | You feel a sudden flash of dizziness causing you to pause before getting your directional bearings back. | You feel steadier now. | - | spells.ini, logs | yes |
| lethargy | You feel a sudden loss of energy as the power that once mingled with your own has now vanished. / ...vanishes. | You feel your magic energy coming back to you. | - | spells.ini, logs | yes |
| depression | Alas, you realise/realize that yet again the mighty knowledge of drowned X has (been lost... Despair settles on you.) | Your heart feels lighter. | - | powwow, logs | yes |
| disorientation | A haze seems to cloud your eyes, blurring your vision, making it difficult for you to see anything clearly. | You feel less disoriented. | - | powwow | yes |
| blindness | You have been blinded! (condition) | You feel a cloak of blindness dissolve. / Your vision returns! | - | spells.ini, logs | yes |
| poison | The venom enters your body! / Your body turns numb as the poison speeds to your brain! / You suddenly feel a terrible headache! (conditions) | A warm feeling runs through your body, you feel better. | - | powwow, logs | yes |
| panic | You flee head over heels. (a flee event; the tracker turns it into panic up) | You feel bolder. | - | logs (402 stats show it) | NEW (up); down yes |
| disease | (no line found) | You feel cured. | - | logs | NEW (down) |
| smother | (no line found) | The grip on your lungs recedes. | - | logs | NEW (down) |
| chill touch | (no line found) | You feel warmer. | - | logs | NEW (down) |
| sleep | The drowsiness completely overwhelms you. / You feel very sleepy... zzzzzz | You feel less tired. | - | logs | NEW |
| paralysis | You are paralysed! | You can move again. | - | logs, powwow | NEW |
| hunger | You begin to feel hungry. | (none; see below) | You are hungry. (repeats) | logs | NEW |
| thirst | You begin to feel thirsty. | (none; see below) | You are thirsty. (repeats) | logs | NEW |
| antidote | (none) | You feel a strange taste in your mouth. | - | powwow action, 1 log line | NEW (down) |
| bleeding / wound / entangled | see `CharAffects.h` | | | logs | yes |

Count: 28 effect names before (27 lasting or condition, plus `heal`), 37 now: 9 new names
(disease, smother, chill touch, sleep, paralysis, hunger, thirst, detect evil, antidote) and
the panic up line, plus the watch room "this room" wording.

## Unknown or untestable

- **Ambiguous lines**, no event on purpose: "Your eyes tingle." (night vision or detect
  invisibility; `stat` settles it), "Your awareness decreases." (watch room; detect evil's
  down is the same line), "A warm feeling fills your body." (heals and cure disease).
- **Effects `stat` lists with no line found in any log**: potion, miruvor, invisibility,
  growth, mandos sleep, and the start of disease, smother, chill touch. Only `stat` sets
  them (the tracker adds them without a time).
- **No message at all**: noquit, novoid, nosneak; the end of hunger and thirst (eating and
  drinking both say "You are full.").
- **Not an affect `stat` lists**, so tracking them would flicker: the orc/troll sunlight
  penalty ("You blink and feel weaker under the cruel light of the sun." / "You feel so much
  better hiding in the shadows!"; in `rules.json` only), the strength ring's "The blood begins
  coursing through your veins..." (item), the draught's side effects ("You feel bad.", "You
  feel sick.").
- **Seen but unclear**: "The world starts to speed up as you feel your muscles start to
  stiffen." after "The sage makes a fast gesture towards you."; "You feel terribly sick."
  (probably poison, followed by remove poison in a log; one case, not added).
- **Old MUME versions in the 2005 logs** (not modern wording): "You are affected by:" lists
  (steelskin, stoneskin, inertial armor, energy shield...), "The shimmering light has faded
  from you."
- The two `spells.ini` addon entries (`addontest`, `mytest`) are empty test stubs.
