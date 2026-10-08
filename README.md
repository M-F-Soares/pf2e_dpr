# PF2e DPR Calculator

Computes the **exact expected damage and healing of a full party round** in Pathfinder 2e (Remaster rules), where earlier results change later rolls: a crit that makes the enemy off-guard, a Demoralize that lands frightened 1 or 2, a Feint that enables Sneak Attack, and so on.

```
make run    # example: which ranger/bard/rogue plan deals the most damage?
make test   # unit tests (doctest)
make docs   # API reference in build/docs/html (needs doxygen)
```

## Project layout
```
lib/
  core/      PF2e rules and data: dice, checks, modifiers, conditions, damage, creatures, battle state, Action
  engine/    the branching simulation (Engine), the Scenario builder and the reports
  content/   ready-made actions, spells and class features, built on Action
examples/    the demo program (make run) and its sample party
tests/       unit tests (make test) and the vendored doctest framework
```
Each group keeps its headers in `inc/` and its sources in `src/`. Every `inc/` folder is on the include path, so includes are just `#include "Action.h"`.

## How it works
Each roll splits the round into branches (crit fail / fail / success / crit success), each with its probability and its own `BattleState`: conditions, modifiers, MAP, actions and reactions left, HP missing. Damage isn't stored in the state. It goes into a ledger as `p(branch) × E[damage]`, so identical states merge and the tree stays small while the result stays exact.

Rules are encoded once, in a few places:
- **Stacking** ([lib/core/src/Modifier.cpp](lib/core/src/Modifier.cpp)): per type (circumstance, item, status), the best bonus plus the worst penalty; untyped modifiers stack.
- **Conditions** ([lib/core/src/Condition.cpp](lib/core/src/Condition.cpp)) produce modifiers. Frightened, clumsy, enfeebled and the like are status penalties; off-guard is −2 circumstance.
- **Checks** ([lib/core/src/Check.cpp](lib/core/src/Check.cpp)): a natural 20 or 1 shifts the result one degree; fortune and misfortune use the exact 2d20 distribution.
- **Damage** ([lib/core/src/Damage.cpp](lib/core/src/Damage.cpp)): totaled per damage type, then immunity → weakness → resistance, exact over the dice, with the minimum-1 rule.
- **Spellstrike** ([lib/content/src/Actions.cpp](lib/content/src/Actions.cpp)): `Spellstrike(magus, "Sword", spell)` takes a spell from `SpellAttackSpell()` or `BasicSaveSpell()`. A spell attack spell uses the Strike's roll; with a save spell, the target saves normally unless the Strike critically fails. The Strike's and the spell's damage are combined into one instance, so resistances and weaknesses apply once. Each damage roll gets its own damage bonuses (Courageous Anthem adds +1 to both). Spellstrike counts as two attacks for the multiple attack penalty, applied after it, and can't be used again until `RechargeSpellstrike()`. `OverwhelmingSpellstrike()` is the same, except the spell's acid, cold, electricity, fire and sonic damage, persistent included, ignores resistance equal to the magus's level. Some of this is a GM call; see [Rulings](#rulings).
- **Drained on the enemy** ([lib/engine/src/Engine.cpp](lib/engine/src/Engine.cpp)): the enemy loses `max(1, level) × increase` HP. The loss is credited to the player who applied it, on its own `(drained HP loss)` line. It isn't damage, so resistances, weaknesses and the minimum-1 rule don't apply. Drained the enemy starts with causes no loss, and only increases count.

## Writing a scenario
```cpp
Scenario a("Ranger 3 Strikes");
a.Against(brute)
 .AddTurn(ranger, {Strike(ranger, "Longbow"), Strike(ranger, "Longbow"), Strike(ranger, "Longbow")})
 .AddTurn(bard,   {CourageousAnthem(bard), Heroism(bard, rogue)})
 .AddTurn(rogue,  {Feint(rogue), Strike(rogue, "Rapier"), Strike(rogue, "Rapier")});

Scenario b = a.Variant("Ranger Demoralize")
              .ReplaceTurn(ranger, {Demoralize(ranger), Strike(ranger, "Longbow"), Strike(ranger, "Longbow")});

Compare({a, b});
```
- Starting state: `.EnemyCondition(ConditionKind::Concealed)`, `.EnemyModifier(Modifier{"cover", BonusType::Circumstance, 2, Select({Tag::AC})})`, `.Counter(ranger, "prey")`, `.Counter(magus, "spellstrike-spent")`.
- Adaptive plans: `Choose([&](const View& view) { return view.EnemyOffGuardToMe() ? Strike(...) : Feint(...); })`. The choice is made separately in each branch.
- Settings: `persistentTicks` (how many end-of-turn ticks of persistent damage to count) and `mergeStates`.

## Adding a new action
Fill in an `Action` (see [lib/core/inc/Action.h](lib/core/inc/Action.h)) and add a factory to [lib/content/src/Actions.cpp](lib/content/src/Actions.cpp):
- `check`: what is rolled. `Attack` means the actor rolls against the enemy's AC or a DC; `Save` means the enemy rolls against the actor's DC; there's also a fixed DC or none.
- `outcomes[degree]`: what each degree does. The options are damage scale, conditions on the enemy, modifiers for self/ally/party, counters (a value of 0 clears one), healing, actions granted to an ally, and a `custom` hook for anything else.
- `check.attacks`: how many attacks the action counts as for the multiple attack penalty (Spellstrike: 2).
- `spentFlag`: for frequency-limited actions. The action is skipped while the actor has that counter, and taking it sets the counter.
- `coupled`: a second action resolved together with this one, with its damage combined into the same instance. It either uses this action's roll or rolls its own check (Spellstrike).
- Class features that change Strike damage go in `Character::features`. See `SneakAttack` and `PrecisionEdge`.

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
- Casting a conflux spell doesn't recharge Spellstrike; use `RechargeSpellstrike()`.
- Persistent damage from a crit is doubled through each component's `doublesOnCrit` flag. Crit-only weapon riders, like flaming's 1d10, are not doubled.
- On allies, drained lowers current and max HP together, so missing HP (and effective healing) is unchanged; ally HP loss isn't tracked. Drained added from a `custom` hook (by calling `AddCondition` directly) causes no HP loss.
- Precision *resistance* isn't modeled; precision *immunity* is.
