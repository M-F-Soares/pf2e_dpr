# PF2e DPR Calculator

Computes the **exact expected damage and healing of a full party round** in Pathfinder 2e (Remaster rules), where earlier results change later rolls: a crit that makes the enemy off-guard, a Demoralize that lands frightened 1 or 2, a Feint that enables Sneak Attack, and so on.

An unofficial fan project: not published, endorsed, or specifically approved by Paizo. The code is MIT-licensed; the game rules are used under the ORC License (see [License](#license)).

```
make run    # example: which ranger/bard/rogue plan deals the most damage?
make test   # unit tests (doctest)
make docs   # API reference in build/docs/html (needs doxygen)
make import # spells from Foundry VTT into data/imported (see "Importing spells from Foundry VTT")
```
Run from the project root: the program reads its actions, spells and features from `data/` (or from the folder given as its first argument).

## Project layout
```
lib/
  core/      PF2e rules and types: dice, checks, modifiers, conditions, damage, creatures, battle state, Action
  engine/    the branching simulation (Engine), the Scenario builder and the reports
  content/   the content library: loads data/ and builds actions and features from it
  vendor/    third-party headers (nlohmann/json)
data/        actions, spells and class features, one JSON file each (see "Adding content");
             data/imported/ is made by make import and isn't committed
examples/    the demo program (make run) and its sample party
tests/       unit tests (make test), test-only spells (tests/data) and the vendored doctest framework
tools/       the Foundry VTT spell importer (make import)
.github/     CI: builds, runs the tests and the example on every pull request to main
```
Each group keeps its headers in `inc/` and its sources in `src/`. Every `inc/` folder is on the include path, so includes are just `#include "Action.h"`.

## How it works
Each roll splits the round into branches (crit fail / fail / success / crit success), each with its probability and its own `BattleState`: conditions, modifiers, MAP, actions and reactions left, HP missing. Damage isn't stored in the state. It goes into a ledger as `p(branch) × E[damage]`, so identical states merge and the tree stays small while the result stays exact.

Rules are encoded once, in a few places:
- **Stacking** ([lib/core/src/Modifier.cpp](lib/core/src/Modifier.cpp)): per type (circumstance, item, status), the best bonus plus the worst penalty; untyped modifiers stack.
- **Conditions** ([lib/core/src/Condition.cpp](lib/core/src/Condition.cpp)) produce modifiers. Frightened, clumsy, enfeebled and the like are status penalties; off-guard is −2 circumstance.
- **Checks** ([lib/core/src/Check.cpp](lib/core/src/Check.cpp)): a natural 20 or 1 shifts the result one degree; fortune and misfortune use the exact 2d20 distribution.
- **Damage** ([lib/core/src/Damage.cpp](lib/core/src/Damage.cpp)): totaled per damage type, then immunity → weakness → resistance, exact over the dice, with the minimum-1 rule.
- **Spellstrike** ([data/actions/spellstrike.json](data/actions/spellstrike.json)): `content.Use("Spellstrike", magus, {.weapon = "Sword", .spell = spell})` takes a 1- or 2-action spell with a spell attack roll or a save, e.g. `content.Use("Fire Ray", magus)`. A spell attack spell uses the Strike's roll; with a save spell, the target saves normally unless the Strike critically fails. The Strike's and the spell's damage are combined into one instance, so resistances and weaknesses apply once. Each damage roll gets its own damage bonuses (Courageous Anthem adds +1 to both). Spellstrike counts as two attacks for the multiple attack penalty, applied after it, and can't be used again until the magus uses Recharge Spellstrike. Overwhelming Spellstrike is the same, except the spell's acid, cold, electricity, fire and sonic damage, persistent included, ignores resistance equal to the magus's level. Some of this is a GM call; see [Rulings](#rulings).
- **Drained on the enemy** ([lib/engine/src/Engine.cpp](lib/engine/src/Engine.cpp)): the enemy loses `max(1, level) × increase` HP. The loss is credited to the player who applied it, on its own `(drained HP loss)` line. It isn't damage, so resistances, weaknesses and the minimum-1 rule don't apply. Drained the enemy starts with causes no loss, and only increases count.

## Writing a scenario
```cpp
ContentLibrary content = ContentLibrary::Load("data");
rogue.features = {content.GetFeature("Sneak Attack", rogue)};  // dice by the rogue's level
ActionPtr bowStrike = content.Use("Strike", ranger, {.weapon = "Longbow"});
ActionPtr rapierStrike = content.Use("Strike", rogue, {.weapon = "Rapier"});

Scenario a("Ranger 3 Strikes");
a.Against(brute)
 .AddTurn(ranger, {bowStrike, bowStrike, bowStrike})
 .AddTurn(bard,   {content.Use("Courageous Anthem", bard), content.Use("Heroism", bard, {.ally = &rogue})})
 .AddTurn(rogue,  {content.Use("Feint", rogue), rapierStrike, rapierStrike});

Scenario b = a.Variant("Ranger Demoralize")
              .ReplaceTurn(ranger, {content.Use("Demoralize", ranger), bowStrike, bowStrike});

Compare({a, b});
```
Actions don't change once built, so one action can be used in many turns and scenarios.
- Content options (`UseOptions`), in this order: `rank`, `ally`, `variant`, `weapon`, `allyWeapon`, `spell`, `skill`, `rangeIncrement`. A spell's rank defaults to its own rank, and a cantrip's to half the caster's level, rounded up. Setting an option the action doesn't use is an error.
  - `content.Use("Heal", cleric, {.rank = 3, .ally = &fighter, .variant = "2-action"})`
  - `content.Use("Strike", ranger, {.weapon = "Longbow", .rangeIncrement = 2})`
  - `content.Use("Spellstrike", magus, {.weapon = "Sword", .spell = content.Use("Fire Ray", magus)})`
  - `content.Use("Strike Hard!", commander, {.ally = &fighter, .allyWeapon = "Sword"})`
  - `content.Use("Aid", bard, {.ally = &rogue, .variant = "master", .skill = Skill::Deception})`
- Starting state: `.EnemyCondition(ConditionKind::Concealed)`, `.EnemyModifier(Modifier{"cover", BonusType::Circumstance, 2, Select({Tag::AC})})`, `.Counter(ranger, "prey")`, `.Counter(magus, "spellstrike-spent")`.
- Adaptive plans: `Choose([&](const View& view) { return view.EnemyOffGuardToMe() ? rapierStrike : feint; })`. The choice is made separately in each branch.
- Settings: `persistentTicks` (how many end-of-turn ticks of persistent damage to count) and `mergeStates`.

## Adding content
Actions, spells and class features are JSON files in `data/`, one definition per file (a file can also hold a list). `ContentLibrary::Load("data")` reads them all; a mistake fails at load time with the file, the definition and the problem, e.g. `data/spells/fear.json: Fear: unknown condition "frightend"`. Most content fits a few patterns, so a definition is mostly numbers:

```json
{
    "name": "Demoralize",
    "cost": 1,
    "traits": ["auditory", "concentrate", "emotion", "fear", "mental"],
    "check": {"stat": "skill:Intimidation", "against": "Will"},
    "outcomes": {
        "success": [{"condition": "frightened", "value": 1}],
        "critSuccess": [{"condition": "frightened", "value": 2}]
    },
    "temporaryImmunity": {"on": "enemy", "perActor": true}
}
```
```json
{
    "name": "Electric Arc",
    "cantrip": true,
    "cost": 2,
    "traits": ["electricity"],
    "check": {"stat": "spell", "save": "Ref"},
    "preset": "basicSave",
    "damage": [{"dice": "2d4", "type": "electricity"}],
    "heightened": {"every": 1, "damage": [{"dice": "1d4"}]}
}
```

**Definition keys** (only `name` is required):
- `name`; `displayName` (name in reports; `{ally}`, `{weapon}` and `{spell}` become the names it's used with; by default the name, plus ` -> <ally>` when used on an ally); `reference` (where it's from, for maintainers; not used).
- `cost` (0–3 actions, default 1); `reaction` (`true`: uses the reaction instead).
- `traits`. `attack` gives the multiple attack penalty, `incapacitation` applies incapacitation, and an enemy immune to a trait ignores the action.
- `rank` (spell rank), or `cantrip: true` (heightens to half the caster's level, rounded up).
- `check`: a `stat` (`spell`, `classDC` or `skill:<Skill>`) and one of: `against` (the actor rolls against the enemy's `AC`, `Fort`, `Ref`, `Will` or `Perception` DC), `save` (the enemy rolls `Fort`, `Ref` or `Will` against the actor's DC; outcomes are the enemy's degree), or `dc` (the actor rolls against a fixed DC). A spell attack roll gets the `attack` trait automatically. No `check`: the action just happens.
  - `{"stat": "weapon"}` is a Strike: the attack bonus of the weapon it's used with (`UseOptions::weapon`) against AC, with the `attack` trait, agile, potency and range increments.
  - `{"stat": "chosen", "dc": 15}` rolls whichever it's used with: a weapon's attack bonus (`UseOptions::weapon`) or a skill (`UseOptions::skill`). Aid uses it.
- `attacks`: how many attacks it counts as for the multiple attack penalty, applied after it (Spellstrike: 2). `spends`: a counter that marks the action as used; it's skipped until something clears the counter (Spellstrike's `spellstrike-spent`, cleared by Recharge Spellstrike).
- `damage`: a list of `{"dice": "2d6+4", "type": "fire"}`, with `"category": "precision"` or `"persistent"` when needed.
- `preset`: `basicSave` (none on the enemy's critical success, half on a success, full on a failure, double on a critical failure), `attack` (full on a hit, double on a critical hit), or `strike` (like `attack`, with the weapon's damage: runes, fatal, deadly and crit riders; needs `"stat": "weapon"` and no `damage`).
- `outcomes`: `critSuccess`, `success`, `failure`, `critFailure` and `always` (every degree), each a list of effects. `effects`: the same list, for actions without a check.
- `carriesSpell`: `{"checks": ["attack", "save"], "costs": [1, 2]}`. The action carries the spell it's used with (`UseOptions::spell`), as Spellstrike does: a spell attack uses the action's roll, a save spell's target saves on its own unless the action critically fails, and the damage is combined. `"ignoreResistance": {"types": ["fire", ...], "amount": "level"}` (or a number) lowers the target's resistance to the spell's damage. Needs an attack roll.
- `temporaryImmunity`: `{"on": "enemy"}` or `{"on": "ally"}`. The action can't affect that target again; with `"perActor": true`, only the same character's use is blocked.
- `heightened`: `{"every": 2, "damage": [{"dice": "1d6"}], "heal": "1d8"}` adds that much per step of `every` ranks (a step's damage without a `type` joins the first damage entry). `{"6": {...}, "9": {...}}` changes keys from that rank on (Heightened (6th), (9th)).
- `variants`: named sets of changed keys, picked with `UseOptions::variant` (Heal's `1-action`/`2-action`/`3-action`, Battle Medicine's `trained`...`legendary`). `defaultVariant` is used when none is asked for. Changes merge into the definition: objects merge key by key, and anything else (lists, numbers, text) is replaced.
- `extends`: the name of an action defined earlier in the same file. This definition starts as a copy of it, variants included, and its keys merge in the same way. Overwhelming Spellstrike is `{"name": "Overwhelming Spellstrike", "extends": "Spellstrike", "displayName": "Overwhelming Spellstrike ({spell})", "carriesSpell": {"ignoreResistance": {...}}}`; its `carriesSpell` keeps Spellstrike's `checks` and `costs`.

**Effects** (each has exactly one of these keys):

| Effect | Other keys |
|---|---|
| `{"condition": "frightened", "value": 1}` | `on` (default `enemy`), `until` |
| `{"modifier": "status", "value": 1, "appliesTo": ["AttackRoll", "Skill\|Check"], "on": "ally"}` | `until`; `source` (default: the definition's name; the same source never stacks); `vsActor` (only against the actor); `offGuard` (the target counts as off-guard); `uses` (removed after that many rolls) |
| `{"counter": "prey"}` | `value` (default 1; 0 clears it), `on` (default `actor`) |
| `{"heal": "2d8+10"}` | `on` (default `ally`) |
| `{"damage": "half"}` | `none`, `half`, `normal` or `double`; overrides the preset for that degree |
| `{"hook": "uses-reaction"}` | a named C++ effect registered in `HookRegistry` |
| `{"grant": "Strike"}` | the ally takes that action right away, built with `UseOptions::allyWeapon` as its weapon; `reaction` (`true`: it uses the ally's reaction); `displayName` (with `{weapon}`) |

`on` is `actor`, `ally`, `allies` or `enemy`; `until` is `round` (default), `endOfActorTurn` or `startOfActorTurn`. `appliesTo` lists the rolls a modifier applies to, by the `Tag` names in [lib/core/inc/Modifier.h](lib/core/inc/Modifier.h); `Skill|Check` means a roll with both tags. With `"stat": "chosen"`, `"appliesTo": "chosen"` means the same kind of roll the actor made: attack rolls with a weapon, skill checks with a skill (Aid).

**Features** add damage to Strikes:
```json
{
    "name": "Sneak Attack",
    "strikeDamage": {
        "dice": {"1": 1, "5": 2, "11": 3, "17": 4},
        "die": 6,
        "when": {"enemyOffGuard": true, "weaponTraits": ["agile", "finesse", "ranged"]}
    }
}
```
`dice` is a number or a table by level; `category` defaults to `precision`; `when` can also require an `actorCounter` (Precision Edge: `prey`), and `oncePerRound` names a counter that limits it to the first hit each round.

**When C++ is needed.** For a new *mechanic*, never for a new action. Strike, Aid, Strike Hard! and Spellstrike are data too ([data/actions/](data/actions/)). For a one-off effect inside an otherwise ordinary definition, register a hook (`HookRegistry::Register`) and call it with `{"hook": "name"}`. A mechanic many definitions will share belongs in the format: a new key read in [lib/content/src/DefinitionParser.cpp](lib/content/src/DefinitionParser.cpp) and applied in [lib/content/src/Templates.cpp](lib/content/src/Templates.cpp) to what the engine's `Action` supports ([lib/core/inc/Action.h](lib/core/inc/Action.h)).

## Importing spells from Foundry VTT
`make import` converts the spells of [Foundry VTT's PF2e system](https://github.com/foundryvtt/pf2e) into `data/imported/spells.json`, which the program loads with the rest of `data/`.

- **Fetching.** The first run makes a sparse clone of only Foundry's spell folder into `.cache/foundry-pf2e` (about 20 MB); later runs update it. This is the only target that uses git and the network: `make`, `make test` and `make run` never do. `make import FOUNDRY=/path/to/pf2e` uses a clone you already have instead, and `rm -rf .cache/foundry-pf2e` starts over.
- **When to run it.** No rebuild is needed afterwards, since the program reads `data/` when it runs. Run it again after Foundry's data changes (new books), after changing the importer, or after writing by hand a spell that was imported; otherwise loading fails with "already defined".
- **What's imported.** Spells cast with 1–3 actions, as a free action or as a reaction, that have a spell attack or a save: their damage (basic saves and attacks), healing, both kinds of heightening, and outcomes listed by degree of success when every phrase in them is understood (conditions the engine models, "unaffected", no/half/full/double damage). In October 2026 that was about 270 of Foundry's ~2,000 spells.
- **What's left out**, and why, is in `data/imported/import-report.md`: buffs and utility spells (no attack or save), long casting times, outcomes described in ways the converter doesn't understand, conditions the engine doesn't model (ranked by how many spells each one blocks), and so on. A spell imported without part of what it does (a variant, or an effect outside its outcomes) is listed under "something to check".
- **Hand-written definitions win.** A spell already defined in `data/` isn't imported. To fix an imported spell, write it in `data/spells/` and import again.
- **Known limits.** Focus spells that aren't cantrips import at their base rank; pass `UseOptions::rank` to heighten them. Damage is assumed to be dealt when the spell is cast.
- **License.** Foundry's PF2e data is published under the ORC License or the OGL 1.0a (each spell names its own; see [License](#license)). `data/imported/` stays on your machine: it's git-ignored and shouldn't be committed or published.

The converter is [tools/src/FoundryImport.cpp](tools/src/FoundryImport.cpp); [tests/test_import.cpp](tests/test_import.cpp) shows what it does with each kind of spell.

## Rulings
Places where the rules text leaves room for a GM call. Each one lists the current choice and where to change it.

- **Overwhelming Spellstrike when the weapon also deals the spell's energy type** (for example, a flaming rune with a fire spell).
  - Spellstrike says to "Combine the Strike and spell's damage for the purpose of resistances and weaknesses" ([Magus](https://2e.aonprd.com/Classes.aspx?ID=74), Impossible Magic pg. 11).
  - [Overwhelming Spellstrike](https://2e.aonprd.com/Feats.aspx?ID=9081) (Impossible Magic pg. 24) has the spell ignore resistance equal to your level: "This applies to all damage the spell deals".
  - Neither text says how a lowered resistance applies when only part of the combined amount came from the spell.
  - **Current ruling: generous.** The lowered resistance applies to the whole combined amount of that type, the weapon's share included.
  - **Strict alternative:** the resistance still applies once, but at most (resistance − level) of it can come off the spell's share. Example: a level 12 magus with rune fire 4 + spell fire 30 against fire resistance 15 deals 31 fire with the generous ruling, 27 with the strict one, and 19 without the feat.
  - **To change it:** `ExpectedDamage()` in [lib/core/src/Damage.cpp](lib/core/src/Damage.cpp), where `ignoredResistance` is subtracted. The strict ruling needs the spell's share of each damage type tracked separately. The test "generous ruling when the weapon deals the spell's energy type" pins the current choice.
- **Overwhelming Spellstrike against resistance to all damage.** Treated as the target's resistance to the spell's damage type, so it's lowered for the spell's damage. The alternative is to lower only resistances that name acid, cold, electricity, fire or sonic. Same place in [lib/core/src/Damage.cpp](lib/core/src/Damage.cpp).
- **Damage bonuses on Spellstrike.** The Strike and the spell are two damage rolls, so a bonus to damage rolls applies to each. [Courageous Anthem](https://2e.aonprd.com/Spells.aspx?ID=1763) (Player Core pg. 370), for example, adds +1 to both. Spellstrike only combines them "for the purpose of resistances and weaknesses". The alternative is a single +1 for the whole Spellstrike. The bonus is added in `Engine::DamageComponents()` in [lib/engine/src/Engine.cpp](lib/engine/src/Engine.cpp), which runs once per damage roll.

## Known simplifications
- Only the party's turns are simulated. The enemy's turn only ticks persistent damage and ends durations.
- One enemy. Area spells hit only that enemy.
- Aid's preparation and reaction both happen on the aider's turn.
- Casting a conflux spell doesn't recharge Spellstrike; use the Recharge Spellstrike action.
- Persistent damage from a crit is doubled through each component's `doublesOnCrit` flag. Crit-only weapon riders, like flaming's 1d10, are not doubled.
- On allies, drained lowers current and max HP together, so missing HP (and effective healing) is unchanged; ally HP loss isn't tracked. Drained added from a `custom` hook (by calling `AddCondition` directly) causes no HP loss.
- Precision *resistance* isn't modeled; precision *immunity* is.
- Summons and battle forms aren't supported, and the importer leaves them out. Both swap in another creature's statistics (a stat block for each creature or form, and there are many), and most of what they add comes in later rounds, while the calculator covers one round.

## To-do
Next steps, roughly in order. Counts are from Foundry's data in October 2026.

- [ ] **Import the remaining spells that affect damage or healing.** 736 of the spells the importer leaves out change the party's damage or healing: damage, healing and temporary HP, offensive buffs (invisibility included), debuffs on what the party rolls against, and condition removal.
- [ ] **Import actions.** Foundry has 583 actions (basic, skill, class, ancestry, archetype...); 9 are written by hand in `data/actions/`. Many are exploration or downtime actions with no combat numbers, so they'll need sorting first, as the spells were.
- [ ] **Import features that affect damage or healing,** with the actions they grant. Feats, class features and ancestry features add up to about 7,300 entries in Foundry; most of their effects are rule elements, like buffs' spell effects.
- [ ] **Support summons and battle forms.** Needs creature stat blocks (or a subset of Foundry's bestiaries) and the multi-round simulation below.
- [ ] **Simulate the enemy's turn.** Defensive buffs, debuffs on the enemy's attacks or actions (enfeebled, slowed, stunned), and reactions to its actions can't change the results today. About 270 of the left-out spells are waiting on this.
- [ ] **Simulate more than one round.** Sustained spells, durations, persistent damage over time and spells cast over two turns are worth more over a whole fight.
- [ ] **Fight more than one enemy.** Area spells hit only one enemy today.
- [ ] **Heighten imported focus spells automatically** to the caster's level (they import at their base rank; see "Known limits" under the importer).
- [ ] **More party examples** in `examples/`, covering more classes and levels.
- [ ] **An HTML page to build and compare scenarios,** so trying a party, a target and a plan doesn't mean editing C++.

## License
- **Code**: MIT, see [LICENSE](LICENSE). This covers the source code, build files and documentation written for this project.
- **Third-party code**: [nlohmann/json](https://github.com/nlohmann/json) (`lib/vendor/nlohmann`) and [doctest](https://github.com/doctest/doctest) (`tests/vendor`), both MIT; their license files are next to them.
- **Game rules**: the game mechanics in `data/`, and the rules this README describes, come from Paizo's Pathfinder Second Edition and are used under the ORC License (below). They're Licensed Material under that license, so they stay under ORC rather than MIT. Each definition's `reference` names the book it comes from.
- **Imported spells**: `make import` downloads [Foundry VTT's PF2e system](https://github.com/foundryvtt/pf2e) data to your machine and converts it. That data isn't part of this project and isn't distributed with it; its spells are published under the ORC License or the OGL 1.0a, as each one states. This project isn't affiliated with Foundry Gaming LLC or the PF2e system's developers.

### ORC Notice
This product is licensed under the ORC License located at the Library of Congress at TX 9-307-067 and available online at various locations including paizo.com/orclicense, azoralaw.com/orclicense, and others. All warranties are disclaimed as set forth therein.

**Attribution:** This product is based on the following Licensed Material:
- Pathfinder Player Core © 2023 Paizo Inc., Designed by Logan Bonner, Jason Bulmahn, Stephen Radney-MacFarland, and Mark Seifter. Authors: Alexander Augunas, Kate Baker, Logan Bonner, Jason Bulmahn, Carlos Cabrera, Calder CaDavid, James Case, Eleanor Ferron, Steven Hammond, Joan Hong, Vanessa Hoskins, James Jacobs, Jenny Jarzabski, Erik Keith, Dustin Knight, Lyz Liddell, Luis Loza, Patchen Mortimer, Dennis Muldoon, Stephen Radney-MacFarland, Mikhail Rekun, David N. Ross, Michael Sayre, Mark Seifter, Kendra Leigh Speedling, Mark Thompson, Clark Valentine, Andrew White, Landon Winkler, and Linda Zayas-Palmer.
- Pathfinder Battlecry! © 2025 Paizo Inc., Authors: Logan Bonner, Jason Keeley, and Michael Sayre, with Saif Ansari, Kate Baker, Rigby Bendele, Tineke Bolleman, Jessica Catalan, Jeremy Corff, Rue Dickey, John Godek III, Alastor Guzman, Luis Loza, Dennis Muldoon, Ianara Natividad, Collette Quach, Stephen Radney-MacFarland, Mikhail Rekun, Mark Seifter, Shay Snow, Kendra Leigh Speedling, Andrew White, and Linda Zayas-Palmer.
- Pathfinder Impossible Magic © 2026 Paizo Inc., Authors: Kate Baker, Minty Belmont, Rigby Bendele, Joshua Birdsong, Chris Bissette, Logan Bonner, James Case, Jessica Catalan, Katina Davis, Jesse Decker, Caryn DiMarco, Chris Eng, Ivis K. Flanagan, Leo Glass, Sen H.H.S., Joan Hong, Joshua Kim, Daniel Kwan, Luis Loza, Ron Lundeen, Derry Luttrell, Jonathan “Ryomasa” Mendoza, Jacob W. Michaels, Jaime Reyes Mondragon, Ianara Natividad, Kenneth Olson, Chesley Oxendine, Collette Quach, Stephen Radney-MacFarland, Mikhail Rekun, David N. Ross, Simone D. Sallé, Mark Seifter, Shay Snow, Kendra Leigh Speedling, Shu Qing Tan, Tan Shao Han, Mari Tokuda, Ruvaid Virk, Grady Wang, Andrew White, Landon Winkler, and Isis Wozniakowska.

The credit lines are as Paizo publishes them, listed on [Archives of Nethys](https://2e.aonprd.com/Licenses.aspx).

If you use this project's Licensed Material in your own published work, please credit it as follows: PF2e DPR Calculator © 2026 Maurício Soares.

**Reserved Material:** this product has none of its own. Paizo's Reserved Material (trademarks such as Pathfinder, proper nouns, artwork and trade dress) isn't licensed under ORC; it's used under Paizo's Community Use Policy, below.

**Expressly Designated Licensed Material:** none.

### Paizo Community Use Policy
PF2e DPR Calculator uses trademarks and/or copyrights owned by Paizo Inc., used under Paizo's Community Use Policy (paizo.com/licenses/communityuse). We are expressly prohibited from charging you to use or access this content. PF2e DPR Calculator is not published, endorsed, or specifically approved by Paizo. For more information about Paizo Inc. and Paizo products, visit paizo.com.

Contact: Maurício Soares, through [this project's issues on GitHub](https://github.com/M-F-Soares/pf2e_dpr/issues).
