/**
 * @file    test_engine.cpp
 * @brief   End-to-end tests of the engine, the built-in actions and the magus's Spellstrike.
 * @details Results are checked against hand calculations.
 */

#include "vendor/doctest.h"

#include "ContentLibrary.h"
#include "Scenario.h"

#include <functional>

namespace
{
constexpr int kSuccess = static_cast<int>(Degree::Success);          /**< DegreeDist index: success. */
constexpr int kCritSuccess = static_cast<int>(Degree::CritSuccess);  /**< DegreeDist index: critical success. */

/**
 * @brief   The project's content (data/) plus the test spells (tests/data/), loaded once.
 * @return  The library.
 */
const ContentLibrary& Content()
{
    static const ContentLibrary library = [] {
        ContentLibrary content = ContentLibrary::Load("data");
        content.AddDirectory("tests/data");
        return content;
    }();
    return library;
}

/**
 * @brief   An action from the content library.
 * @param   [in] name - The action's name.
 * @param   [in] actor - The character using it.
 * @param   [in] options - Rank, ally and variant.
 * @return  The action.
 */
ActionPtr Use(const std::string& name, const Character& actor, const UseOptions& options = {})
{
    return Content().Use(name, actor, options);
}

/**
 * @brief   A Strike from the content library.
 * @param   [in] actor - The attacker.
 * @param   [in] weaponName - Name of one of @p actor's weapons.
 * @return  The action.
 */
ActionPtr Strike(const Character& actor, const std::string& weaponName)
{
    return Use("Strike", actor, {.weapon = weaponName});
}

/**
 * @brief   Test character with a finesse sword (1d8+4 slashing: hit 8.5, crit 17).
 * @details Intimidation, Deception and Medicine at +10, and spell DC 25.
 * @param   [in] name - Character name.
 * @param   [in] attack - Sword attack bonus.
 * @param   [in] agile - Whether the sword is agile.
 * @return  The character.
 */
Character MakeFighter(const std::string& name = "Fighter", int attack = 23, bool agile = false)
{
    Character fighter;
    fighter.name = name;
    fighter.skills = {{Skill::Intimidation, 10}, {Skill::Deception, 10}, {Skill::Medicine, 10}};
    Weapon sword;
    sword.name = "Sword";
    sword.attack = attack;
    sword.dice = 1;
    sword.die = 8;
    sword.flat = 4;
    sword.agile = agile;
    sword.finesse = true;
    fighter.weapons = {sword};
    fighter.spellDC = 25;
    return fighter;
}

/**
 * @brief   Test enemy: level 5, every save and Perception at +10 (DC 20), no defenses.
 * @param   [in] ac - Armor Class.
 * @return  The monster.
 */
Monster MakeDummy(int ac = 30)
{
    Monster dummy;
    dummy.name = "Dummy";
    dummy.level = 5;
    dummy.ac = ac;
    dummy.fort = dummy.ref = dummy.will = dummy.perception = 10;  // DC 20
    return dummy;
}

/**
 * @brief   Expected damage of one Strike, by hand: P(hit) x hit damage + P(crit) x crit damage.
 * @param   [in] mod - Attack modifier.
 * @param   [in] ac - Target AC.
 * @param   [in] hit - Expected damage on a hit.
 * @param   [in] crit - Expected damage on a critical hit.
 * @param   [in] fortune - Fortune state of the attack roll.
 * @return  The expected damage.
 */
double StrikeEV(int mod, int ac, double hit = 8.5, double crit = 17.0, Fortune fortune = Fortune::None)
{
    DegreeDist dist = ResolveCheck(mod, ac, fortune);
    return dist[kSuccess] * hit + dist[kCritSuccess] * crit;
}
} // namespace

TEST_CASE("engine: one Strike matches the hand calculation")
{
    Character fighter = MakeFighter();
    Scenario scenario("one strike");
    scenario.Against(MakeDummy()).AddTurn(fighter, {Strike(fighter, "Sword")});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(0.5 * 8.5 + 0.2 * 17.0));
}

TEST_CASE("engine: multiple attack penalty (normal and agile)")
{
    Character fighter = MakeFighter();
    Scenario scenario("three strikes");
    scenario.Against(MakeDummy())
        .AddTurn(fighter, {Strike(fighter, "Sword"), Strike(fighter, "Sword"), Strike(fighter, "Sword")});
    CHECK(scenario.Run().ledger.TotalDamage() ==
          doctest::Approx(StrikeEV(23, 30) + StrikeEV(18, 30) + StrikeEV(13, 30)));

    Character agileFighter = MakeFighter("Agile", 23, true);
    Scenario agile("agile");
    agile.Against(MakeDummy())
        .AddTurn(agileFighter, {Strike(agileFighter, "Sword"), Strike(agileFighter, "Sword"),
                                Strike(agileFighter, "Sword")});
    CHECK(agile.Run().ledger.TotalDamage() ==
          doctest::Approx(StrikeEV(23, 30) + StrikeEV(19, 30) + StrikeEV(15, 30)));
}

TEST_CASE("engine: a 4th action is skipped")
{
    Character fighter = MakeFighter();
    Scenario scenario("four strikes");
    scenario.Against(MakeDummy())
        .AddTurn(fighter, {Strike(fighter, "Sword"), Strike(fighter, "Sword"), Strike(fighter, "Sword"),
                           Strike(fighter, "Sword")});
    RunResult result = scenario.Run();
    CHECK(result.ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30) + StrikeEV(18, 30) + StrikeEV(13, 30)));
    CHECK(result.ledger.skipped.at({"Fighter", "Strike (Sword)"}) == doctest::Approx(1.0));
}

TEST_CASE("engine: Demoralize result changes the next Strike (chained probabilities)")
{
    Character fighter = MakeFighter();
    Scenario scenario("demoralize then strike");
    scenario.Against(MakeDummy()).AddTurn(fighter, {Use("Demoralize", fighter), Strike(fighter, "Sword")});

    // Intimidation +10 vs Will DC 20; frightened lowers AC by its value. Demoralize has no attack trait.
    DegreeDist demoralizeRoll = ResolveCheck(10, 20);
    double expected = (demoralizeRoll[0] + demoralizeRoll[1]) * StrikeEV(23, 30) +
                      demoralizeRoll[kSuccess] * StrikeEV(23, 29) + demoralizeRoll[kCritSuccess] * StrikeEV(23, 28);
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(expected));
}

TEST_CASE("engine: enemy immune to mental ignores Demoralize")
{
    Character fighter = MakeFighter();
    Monster enemy = MakeDummy();
    enemy.defenses.immunities = {"mental"};
    Scenario scenario("mindless");
    scenario.Against(enemy).AddTurn(fighter, {Use("Demoralize", fighter), Strike(fighter, "Sword")});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30)));
}

TEST_CASE("engine: Feint + Sneak Attack (success = next attack only, crit = all)")
{
    Character rogue = MakeFighter("Rogue");
    rogue.features = {Content().GetFeature("Sneak Attack", rogue)};
    Scenario scenario("feint");
    scenario.Against(MakeDummy()).AddTurn(rogue, {Use("Feint", rogue), Strike(rogue, "Sword"), Strike(rogue, "Sword")});

    DegreeDist feintRoll = ResolveCheck(10, 20);  // Deception vs Perception DC
    double sneakHit = 8.5 + 3.5, sneakCrit = 17.0 + 7.0;
    double none = StrikeEV(23, 30) + StrikeEV(18, 30);
    double success = StrikeEV(23, 28, sneakHit, sneakCrit) + StrikeEV(18, 30);
    double crit = StrikeEV(23, 28, sneakHit, sneakCrit) + StrikeEV(18, 28, sneakHit, sneakCrit);
    double expected =
        (feintRoll[0] + feintRoll[1]) * none + feintRoll[kSuccess] * success + feintRoll[kCritSuccess] * crit;
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(expected));
}

TEST_CASE("engine: merging branches never changes results")
{
    Character fighterA = MakeFighter("A");
    Character fighterB = MakeFighter("B");
    fighterB.level = 5;  // 2d6 Sneak Attack
    fighterB.features = {Content().GetFeature("Sneak Attack", fighterB)};
    Scenario scenario("complex");
    scenario.Against(MakeDummy())
        .AddTurn(fighterA, {Use("Demoralize", fighterA), Strike(fighterA, "Sword"), Use("Feint", fighterA)})
        .AddTurn(fighterB, {Use("Feint", fighterB), Strike(fighterB, "Sword"), Strike(fighterB, "Sword")});
    Settings noMerge;
    noMerge.mergeStates = false;
    RunResult merged = scenario.Run();
    RunResult unmerged = scenario.Variant("unmerged").WithSettings(noMerge).Run();
    CHECK(merged.ledger.TotalDamage() == doctest::Approx(unmerged.ledger.TotalDamage()));
    CHECK(merged.finalStates.size() < unmerged.finalStates.size());
}

TEST_CASE("engine: Strike Hard! gives a reaction Strike without MAP, once per round")
{
    Character commander = MakeFighter("Commander");
    Character other = MakeFighter("Other Commander");
    Character ally = MakeFighter("Ally");
    Scenario scenario("tactic");
    scenario.Against(MakeDummy())
        .AddTurn(commander, {Use("Strike Hard!", commander, {.ally = &ally, .allyWeapon = "Sword"})})
        .AddTurn(other, {Use("Strike Hard!", other, {.ally = &ally, .allyWeapon = "Sword"})})
        .AddTurn(ally, {Strike(ally, "Sword"), Strike(ally, "Sword")});
    RunResult result = scenario.Run();
    // Reaction Strike at full bonus; the 2nd tactic finds the reaction used; the ally's own turn starts at MAP 0.
    CHECK(result.ledger.TotalDamage() == doctest::Approx(2 * StrikeEV(23, 30) + StrikeEV(18, 30)));
    CHECK(result.ledger.skipped.at({"Ally", "Strike (Sword, Strike Hard!)"}) == doctest::Approx(1.0));
}

TEST_CASE("engine: Courageous Anthem adds +1 status to attack and damage")
{
    Character bard = MakeFighter("Bard");
    Character fighter = MakeFighter();
    Scenario scenario("anthem");
    scenario.Against(MakeDummy())
        .AddTurn(bard, {Use("Courageous Anthem", bard)})
        .AddTurn(fighter, {Strike(fighter, "Sword")});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(24, 30, 9.5, 19.0)));
}

TEST_CASE("engine: Sure Strike gives fortune to the next attack only")
{
    Character fighter = MakeFighter();
    Scenario scenario("sure strike");
    scenario.Against(MakeDummy())
        .AddTurn(fighter, {Use("Sure Strike", fighter), Strike(fighter, "Sword"), Strike(fighter, "Sword")});
    CHECK(scenario.Run().ledger.TotalDamage() ==
          doctest::Approx(StrikeEV(23, 30, 8.5, 17.0, Fortune::Fortune) + StrikeEV(18, 30)));
}

TEST_CASE("engine: concealed target costs a DC 5 flat check")
{
    Character fighter = MakeFighter();
    Scenario scenario("concealed");
    scenario.Against(MakeDummy()).EnemyCondition(ConditionKind::Concealed).AddTurn(fighter, {Strike(fighter, "Sword")});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(0.8 * StrikeEV(23, 30)));
}

TEST_CASE("engine: basic save spell uses the enemy's degree")
{
    Character caster = MakeFighter("Caster");
    Scenario scenario("blast");
    scenario.Against(MakeDummy())
        .AddTurn(caster, {Use("Blast", caster)});
    DegreeDist save = ResolveCheck(10, 25);  // Reflex +10 vs DC 25
    double expected = save[kSuccess] * 5 + save[1] * 10 + save[0] * 20;
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(expected));
}

TEST_CASE("engine: persistent damage from crits doesn't stack and ticks at the enemy's turn")
{
    Character fighter = MakeFighter();
    Weapon& sword = fighter.weapons.front();
    sword.onCrit = {Persistent(0, 0, 5, DamageType::Fire)};
    Scenario scenario("persistent");
    scenario.Against(MakeDummy()).AddTurn(fighter, {Strike(fighter, "Sword"), Strike(fighter, "Sword")});
    RunResult result = scenario.Run();
    double pNoCrit = (1 - ResolveCheck(23, 30)[kCritSuccess]) * (1 - ResolveCheck(18, 30)[kCritSuccess]);
    CHECK(result.ledger.damage.at({"(persistent)", "fire"}) == doctest::Approx((1 - pNoCrit) * 5));

    Settings twoTicks;
    twoTicks.persistentTicks = 2;
    RunResult twoTickResult = scenario.Variant("2 ticks").WithSettings(twoTicks).Run();
    CHECK(twoTickResult.ledger.damage.at({"(persistent)", "fire"}) == doctest::Approx((1 - pNoCrit) * 5 * 1.7));
}

TEST_CASE("engine: effective healing is capped by missing HP and chains between heals")
{
    Character cleric = MakeFighter("Cleric");
    Character hurt = MakeFighter("Hurt");
    hurt.hpMissing = 5;
    Scenario scenario("heal");
    ActionPtr heal = Use("Heal", cleric, {.ally = &hurt, .variant = "1-action"});
    scenario.Against(MakeDummy()).Member(hurt).AddTurn(cleric, {heal, heal});
    RunResult result = scenario.Run();

    double effective = 0.0;
    for (int firstRoll = 1; firstRoll <= 8; ++firstRoll)
        for (int secondRoll = 1; secondRoll <= 8; ++secondRoll)
        {
            int first = std::min(firstRoll, 5);
            effective += (first + std::min(secondRoll, 5 - first)) / 64.0;
        }
    CHECK(result.ledger.TotalHealRaw() == doctest::Approx(9.0));
    CHECK(result.ledger.TotalHealEffective() == doctest::Approx(effective));
}

TEST_CASE("engine: adaptive step chooses per branch")
{
    Character fighter = MakeFighter();
    Scenario scenario("adaptive");
    // Demoralize; then only Strike if it worked, otherwise Demoralize is no longer usable anyway -> Strike.
    scenario.Against(MakeDummy())
        .AddTurn(fighter, {Use("Demoralize", fighter), Choose([&](const View& view) -> ActionPtr {
                            return view.EnemyCondition(ConditionKind::Frightened) > 0 ? Strike(fighter, "Sword")
                                                                                      : nullptr;
                        })});
    DegreeDist demoralizeRoll = ResolveCheck(10, 20);
    CHECK(scenario.Run().ledger.TotalDamage() ==
          doctest::Approx(demoralizeRoll[kSuccess] * StrikeEV(23, 29) +
                          demoralizeRoll[kCritSuccess] * StrikeEV(23, 28)));
}

namespace
{
/**
 * @brief   Automatic (no check) drained N on the enemy.
 * @param   [in] value - The drained value.
 * @return  The action.
 */
ActionPtr Drain(int value)
{
    auto action = std::make_shared<Action>();
    action->name = "Drain";
    action->outcomes[kSuccess].conditions = {{ConditionKind::Drained, value}};
    return action;
}

/**
 * @brief   Drained HP loss credited to a character by Drain().
 * @param   [in] result - The run's result.
 * @param   [in] actorName - Character name.
 * @return  The expected HP loss, or 0 if there is no such ledger line.
 */
double DrainedLoss(const RunResult& result, const std::string& actorName)
{
    auto it = result.ledger.damage.find({actorName, "Drain (drained HP loss)"});
    return it == result.ledger.damage.end() ? 0.0 : it->second;
}
} // namespace

TEST_CASE("engine: drained HP loss is credited to the actor and ignores resistances")
{
    Character fighter = MakeFighter();
    Monster enemy = MakeDummy();
    enemy.defenses.resistances = {{DamageType::All, 5}};
    Scenario scenario("drain");
    scenario.Against(enemy).AddTurn(fighter, {Drain(2)});
    RunResult result = scenario.Run();
    CHECK(DrainedLoss(result, "Fighter") == doctest::Approx(10.0));  // level 5 x drained 2
    CHECK(result.ledger.DamageBy("Fighter") == doctest::Approx(10.0));
    CHECK(result.ledger.TotalDamage() == doctest::Approx(10.0));
}

TEST_CASE("engine: drained HP loss only counts the increase")
{
    Character fighter = MakeFighter();
    Scenario raise("drained 1 -> 2");
    raise.Against(MakeDummy()).EnemyCondition(ConditionKind::Drained, 1).AddTurn(fighter, {Drain(2)});
    CHECK(DrainedLoss(raise.Run(), "Fighter") == doctest::Approx(5.0));

    Scenario lower("drained 2, apply 1");
    lower.Against(MakeDummy()).EnemyCondition(ConditionKind::Drained, 2).AddTurn(fighter, {Drain(1)});
    CHECK(DrainedLoss(lower.Run(), "Fighter") == doctest::Approx(0.0));
}

TEST_CASE("engine: drained HP loss respects immunity and the level minimum")
{
    Character fighter = MakeFighter();
    Monster immune = MakeDummy();
    immune.defenses.immunities = {"drained"};
    Scenario scenario("immune");
    scenario.Against(immune).AddTurn(fighter, {Drain(2)});
    CHECK(DrainedLoss(scenario.Run(), "Fighter") == doctest::Approx(0.0));

    Monster lowLevel = MakeDummy();
    lowLevel.level = 0;
    Scenario low("level 0");
    low.Against(lowLevel).AddTurn(fighter, {Drain(2)});
    CHECK(DrainedLoss(low.Run(), "Fighter") == doctest::Approx(2.0));  // min 1 x drained 2
}

TEST_CASE("engine: drained from a save is weighted by the enemy's degree")
{
    Character caster = MakeFighter("Caster");
    auto action = std::make_shared<Action>();
    action->name = "Drain";
    action->cost = 2;
    action->check.kind = CheckKind::Save;
    action->check.modifier = caster.spellDC;  // DC 25
    action->check.tags = Tag::DC | Tag::Spell;
    action->check.defense = Defense::Fort;
    action->outcomes[static_cast<int>(Degree::Fail)].conditions = {{ConditionKind::Drained, 1}};
    action->outcomes[static_cast<int>(Degree::CritFail)].conditions = {{ConditionKind::Drained, 2}};

    Scenario scenario("drain save");
    scenario.Against(MakeDummy()).AddTurn(caster, {ActionPtr(action)});
    DegreeDist save = ResolveCheck(10, 25);  // Fort +10 vs DC 25
    double expected = save[static_cast<int>(Degree::Fail)] * 5 + save[static_cast<int>(Degree::CritFail)] * 10;
    CHECK(DrainedLoss(scenario.Run(), "Caster") == doctest::Approx(expected));
}

namespace
{
/**
 * @brief   Test magus: Sword Strike at +23 (1d8+4) and spell attack +21 (Int).
 * @return  The character.
 */
Character MakeMagus()
{
    Character magus = MakeFighter("Magus");
    magus.spellAttack = 21;
    magus.keyAttribute = Attribute::Int;
    return magus;
}

/**
 * @brief   A 2-action 2d6 fire spell attack (hit 7, crit 14), from tests/data/spells.json.
 * @param   [in] caster - The caster.
 * @return  The spell.
 */
ActionPtr FireRay(const Character& caster)
{
    return Use("Fire Ray", caster);
}

/**
 * @brief   A Spellstrike from the content library.
 * @param   [in] magus - The magus.
 * @param   [in] weaponName - Name of the weapon to Strike with.
 * @param   [in] spell - The spell it carries.
 * @return  The action.
 */
ActionPtr Spellstrike(const Character& magus, const std::string& weaponName, const ActionPtr& spell)
{
    return Use("Spellstrike", magus, {.weapon = weaponName, .spell = spell});
}

/**
 * @brief   An Overwhelming Spellstrike from the content library.
 * @param   [in] magus - The magus; their level is the resistance ignored.
 * @param   [in] weaponName - Name of the weapon to Strike with.
 * @param   [in] spell - The spell it carries.
 * @return  The action.
 */
ActionPtr OverwhelmingSpellstrike(const Character& magus, const std::string& weaponName, const ActionPtr& spell)
{
    return Use("Overwhelming Spellstrike", magus, {.weapon = weaponName, .spell = spell});
}
} // namespace

TEST_CASE("magus: spell attack spell uses the spell attack modifier and doubles on a crit")
{
    Character magus = MakeMagus();
    Scenario scenario("fire ray");
    scenario.Against(MakeDummy()).AddTurn(magus, {FireRay(magus)});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(21, 30, 7.0, 14.0)));
}

TEST_CASE("magus: spell attack after a Strike takes the multiple attack penalty")
{
    Character magus = MakeMagus();
    Scenario scenario("strike then spell");
    scenario.Against(MakeDummy()).AddTurn(magus, {Strike(magus, "Sword"), FireRay(magus)});
    RunResult result = scenario.Run();
    CHECK(result.ledger.damage.at({"Magus", "Strike (Sword)"}) == doctest::Approx(StrikeEV(23, 30)));
    CHECK(result.ledger.damage.at({"Magus", "Fire Ray"}) == doctest::Approx(StrikeEV(16, 30, 7.0, 14.0)));
}

TEST_CASE("magus: resistance applies after the crit doubles the spell damage")
{
    Character magus = MakeMagus();
    Monster target = MakeDummy();
    target.defenses.resistances = {{DamageType::Fire, 3}};
    Scenario scenario("resisted fire ray");
    scenario.Against(target).AddTurn(magus, {FireRay(magus)});

    // Exact over 2d6: hit = E[max(0, X - 3)], crit = E[max(0, 2X - 3)].
    double hit = 0.0, crit = 0.0;
    for (int firstDie = 1; firstDie <= 6; ++firstDie)
        for (int secondDie = 1; secondDie <= 6; ++secondDie)
        {
            hit += std::max(0, firstDie + secondDie - 3) / 36.0;
            crit += std::max(0, 2 * (firstDie + secondDie) - 3) / 36.0;
        }
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(21, 30, hit, crit)));
}

TEST_CASE("magus: Courageous Anthem adds +1 to the spell attack roll and its damage")
{
    Character bard = MakeFighter("Bard");
    Character magus = MakeMagus();
    Scenario scenario("anthem fire ray");
    scenario.Against(MakeDummy()).AddTurn(bard, {Use("Courageous Anthem", bard)}).AddTurn(magus, {FireRay(magus)});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(22, 30, 8.0, 16.0)));
}

TEST_CASE("magus: Spellstrike uses the Strike's roll for both and adds the spell's damage")
{
    Character magus = MakeMagus();
    Scenario scenario("spellstrike");
    scenario.Against(MakeDummy()).AddTurn(magus, {Spellstrike(magus, "Sword", FireRay(magus))});
    // +23 (the weapon's attack, not the +21 spell attack); hit 8.5 + 7, crit 17 + 14.
    RunResult result = scenario.Run();
    CHECK(result.ledger.damage.at({"Magus", "Spellstrike (Fire Ray)"}) ==
          doctest::Approx(StrikeEV(23, 30, 15.5, 31.0)));
    CHECK(result.ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30, 15.5, 31.0)));
}

TEST_CASE("magus: Spellstrike counts as two attacks, applied after it")
{
    Character magus = MakeMagus();
    Scenario first("spellstrike, strike");
    first.Against(MakeDummy()).AddTurn(magus, {Spellstrike(magus, "Sword", FireRay(magus)), Strike(magus, "Sword")});
    CHECK(first.Run().ledger.TotalDamage() ==
          doctest::Approx(StrikeEV(23, 30, 15.5, 31.0) + StrikeEV(13, 30)));  // the Strike is at -10

    Scenario second("strike, spellstrike");
    second.Against(MakeDummy()).AddTurn(magus, {Strike(magus, "Sword"), Spellstrike(magus, "Sword", FireRay(magus))});
    CHECK(second.Run().ledger.TotalDamage() ==
          doctest::Approx(StrikeEV(23, 30) + StrikeEV(18, 30, 15.5, 31.0)));  // the Spellstrike is at -5
}

TEST_CASE("magus: Spellstrike can't be used again until recharged")
{
    Character magus = MakeMagus();
    Scenario spent("spent");
    spent.Against(MakeDummy())
        .Counter(magus, "spellstrike-spent")
        .AddTurn(magus, {Spellstrike(magus, "Sword", FireRay(magus)), Strike(magus, "Sword")});
    RunResult result = spent.Run();
    CHECK(result.ledger.skipped.at({"Magus", "Spellstrike (Fire Ray)"}) == doctest::Approx(1.0));
    CHECK(result.ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30)));  // no actions spent, no MAP added

    Scenario recharged("recharge first");
    recharged.Against(MakeDummy())
        .Counter(magus, "spellstrike-spent")
        .AddTurn(magus, {Use("Recharge Spellstrike", magus), Spellstrike(magus, "Sword", FireRay(magus))});
    CHECK(recharged.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30, 15.5, 31.0)));
}

TEST_CASE("magus: Spellstrike damage is one instance for weaknesses")
{
    Character magus = MakeMagus();
    magus.weapons.front().onHit = {Damage(1, 6, 0, DamageType::Fire)};  // flaming rune
    Monster target = MakeDummy();
    target.defenses.weaknesses = {{DamageType::Fire, 5}};
    Scenario scenario("weak to fire");
    scenario.Against(target).AddTurn(magus, {Spellstrike(magus, "Sword", FireRay(magus))});
    // Rune fire and spell fire are combined, so weakness 5 applies once:
    // hit 8.5 + (3.5 + 7) + 5, crit 17 + (7 + 14) + 5.
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30, 24.0, 43.0)));
}

TEST_CASE("magus: Spellstrike with a save spell rolls the save unless the Strike critically fails")
{
    Character magus = MakeMagus();
    ActionPtr blast = Use("Thunder Blast", magus);
    Scenario scenario("save spell");
    scenario.Against(MakeDummy()).AddTurn(magus, {Spellstrike(magus, "Sword", blast)});

    DegreeDist strikeRoll = ResolveCheck(23, 30);
    DegreeDist save = ResolveCheck(10, 25);  // Reflex +10 vs spell DC 25, independent of the Strike
    double spell = save[kSuccess] * 5 + save[static_cast<int>(Degree::Fail)] * 10 +
                   save[static_cast<int>(Degree::CritFail)] * 20;
    double expected = StrikeEV(23, 30) + (1.0 - strikeRoll[static_cast<int>(Degree::CritFail)]) * spell;
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(expected));
}

TEST_CASE("magus: Courageous Anthem adds +1 to the Spellstrike roll and to each damage roll")
{
    Character bard = MakeFighter("Bard");
    Character magus = MakeMagus();
    Scenario scenario("anthem spellstrike");
    scenario.Against(MakeDummy())
        .AddTurn(bard, {Use("Courageous Anthem", bard)})
        .AddTurn(magus, {Spellstrike(magus, "Sword", FireRay(magus))});
    // Strike damage 9.5 and spell damage 8 (each roll gets +1), doubled on a crit.
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(24, 30, 17.5, 35.0)));
}

TEST_CASE("magus: Spellstrike only takes 1- or 2-action spells with an attack roll or a save")
{
    Character magus = MakeMagus();
    CHECK_THROWS_WITH_AS(Spellstrike(magus, "Sword", Use("Courageous Anthem", magus)),
                         "Spellstrike: can't carry \"Courageous Anthem\": it needs a spell with a spell attack roll "
                         "or a saving throw",
                         ContentError);
    ActionPtr threeActions = Use("Big Blast", magus);
    CHECK_THROWS_WITH_AS(Spellstrike(magus, "Sword", threeActions),
                         "Spellstrike: can't carry \"Big Blast\": it needs a spell that takes 1 or 2 actions to cast",
                         ContentError);
}

namespace
{
/**
 * @brief   30 fire on a hit, 60 on a crit: flat damage keeps the resistance math exact.
 * @details From tests/data/spells.json, like "Searing Lance": the same plus 10 persistent fire.
 * @param   [in] caster - The caster.
 * @return  The spell.
 */
ActionPtr FlameLance(const Character& caster)
{
    return Use("Flame Lance", caster);
}

/**
 * @brief   The test magus at level 12, the level Overwhelming Spellstrike becomes available.
 * @return  The character.
 */
Character MakeLevel12Magus()
{
    Character magus = MakeMagus();
    magus.level = 12;
    return magus;
}
} // namespace

TEST_CASE("overwhelming spellstrike: only the spell's energy damage ignores resistance")
{
    Character magus = MakeLevel12Magus();
    Monster target = MakeDummy();
    target.defenses.resistances = {{DamageType::Physical, 5}, {DamageType::Fire, 15}};
    Scenario plain("plain");
    plain.Against(target).AddTurn(magus, {Spellstrike(magus, "Sword", FlameLance(magus))});
    Scenario over("overwhelming");
    over.Against(target).AddTurn(magus, {OverwhelmingSpellstrike(magus, "Sword", FlameLance(magus))});

    // The spell's fire faces 15 - 12 = 3 instead of 15 (+12 on a hit or crit);
    // the sword's slashing takes physical resistance 5 either way.
    DegreeDist strikeRoll = ResolveCheck(23, 30);
    RunResult result = over.Run();
    CHECK(result.ledger.TotalDamage() - plain.Run().ledger.TotalDamage() ==
          doctest::Approx((strikeRoll[kSuccess] + strikeRoll[kCritSuccess]) * 12));
    CHECK(result.ledger.damage.count({"Magus", "Overwhelming Spellstrike (Flame Lance)"}) == 1);
}

TEST_CASE("overwhelming spellstrike: generous ruling when the weapon deals the spell's energy type")
{
    Character magus = MakeLevel12Magus();
    magus.weapons.front().onHit = {Damage(0, 0, 4, DamageType::Fire)};  // rune fire
    Monster target = MakeDummy();
    target.defenses.resistances = {{DamageType::Fire, 15}};
    Scenario scenario("rune + spell fire");
    scenario.Against(target).AddTurn(magus, {OverwhelmingSpellstrike(magus, "Sword", FlameLance(magus))});
    // The lowered resistance (15 - 12 = 3) covers all the combined fire, the rune's included:
    // hit 8.5 + (4 + 30 - 3), crit 17 + (8 + 60 - 3). The strict ruling would give 27 fire on a hit.
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30, 8.5 + 31.0, 17.0 + 65.0)));
}

TEST_CASE("overwhelming spellstrike: the spell's persistent damage ignores resistance too")
{
    Character magus = MakeLevel12Magus();
    Monster target = MakeDummy();
    target.defenses.resistances = {{DamageType::Fire, 15}};
    Scenario scenario("persistent");
    ActionPtr lance = Use("Searing Lance", magus);  // Flame Lance plus 10 persistent fire
    scenario.Against(target).AddTurn(magus, {OverwhelmingSpellstrike(magus, "Sword", lance)});
    // One tick of 10 - 3 = 7, or 20 - 3 = 17 after a crit (without the feat: 0 and 5).
    DegreeDist strikeRoll = ResolveCheck(23, 30);
    CHECK(scenario.Run().ledger.damage.at({"(persistent)", "fire"}) ==
          doctest::Approx(strikeRoll[kSuccess] * 7 + strikeRoll[kCritSuccess] * 17));
}

TEST_CASE("overwhelming spellstrike: immunities and other damage types are unaffected")
{
    Character magus = MakeLevel12Magus();
    Monster immune = MakeDummy();
    immune.defenses.immunities = {"fire"};
    Scenario scenario("fire immune");
    scenario.Against(immune).AddTurn(magus, {OverwhelmingSpellstrike(magus, "Sword", FlameLance(magus))});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30)));  // only the sword's slashing

    Monster forceResistant = MakeDummy();
    forceResistant.defenses.resistances = {{DamageType::Force, 15}};
    ActionPtr forceLance = Use("Force Lance", magus);
    Scenario force("force");
    force.Against(forceResistant).AddTurn(magus, {OverwhelmingSpellstrike(magus, "Sword", forceLance)});
    CHECK(force.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30, 8.5 + 15.0, 17.0 + 45.0)));
}

TEST_CASE("overwhelming spellstrike: resistance to all damage counts as resistance to the spell's type")
{
    Character magus = MakeLevel12Magus();
    Monster target = MakeDummy();
    target.defenses.resistances = {{DamageType::All, 15}};
    Scenario scenario("resist all");
    scenario.Against(target).AddTurn(magus, {OverwhelmingSpellstrike(magus, "Sword", FlameLance(magus))});
    // The spell's fire faces 15 - 12 = 3: 27 on a hit, 57 on a crit. The sword's slashing still faces 15:
    // 1d8+4 never gets through on a hit; on a crit E[max(0, 2(1d8+4) - 15)] = 25/8.
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(StrikeEV(23, 30, 27.0, 57.0 + 25.0 / 8.0)));
}
