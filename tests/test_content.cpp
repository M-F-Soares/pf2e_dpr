/**
 * @file    test_content.cpp
 * @brief   Tests of the content library: loading, validation, presets, heightening, variants and features.
 */

#include <doctest.h>

#include "ContentLibrary.h"
#include "Scenario.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr int kCritFail = static_cast<int>(Degree::CritFail);        /**< Action::outcomes index: critical failure. */
constexpr int kFail = static_cast<int>(Degree::Fail);                /**< Action::outcomes index: failure. */
constexpr int kSuccess = static_cast<int>(Degree::Success);          /**< Action::outcomes index: success. */
constexpr int kCritSuccess = static_cast<int>(Degree::CritSuccess);  /**< Action::outcomes index: critical success. */

/**
 * @brief   The project's hand-written data (without a local import), loaded once.
 * @return  The library.
 */
const ContentLibrary& ProjectContent()
{
    static const ContentLibrary library = [] {
        ContentLibrary content;
        content.AddDirectory("data", "data/imported");
        return content;
    }();
    return library;
}

/**
 * @brief   A library with only the given definitions.
 * @param   [in] json - One definition, or a list of them.
 * @return  The library.
 * @throws  ContentError if a definition is invalid.
 */
ContentLibrary FromJson(const std::string& json)
{
    ContentLibrary library;
    library.AddJson(json, "test.json");
    return library;
}

/**
 * @brief   Test character: Intimidation and Deception +10, spell attack +15, spell DC 25, a finesse sword.
 * @param   [in] name - Character name.
 * @param   [in] level - Character level.
 * @return  The character.
 */
Character MakeCharacter(const std::string& name = "Tester", int level = 1)
{
    Character character;
    character.name = name;
    character.level = level;
    character.spellAttack = 15;
    character.spellDC = 25;
    character.keyAttribute = Attribute::Cha;
    character.skills = {{Skill::Intimidation, 10}, {Skill::Deception, 10}};
    Weapon sword;
    sword.name = "Sword";
    sword.attack = 20;
    sword.die = 8;
    sword.flat = 4;
    sword.finesse = true;
    character.weapons = {sword};
    return character;
}

/**
 * @brief   Test enemy: level 1, AC 20, every save and Perception at +10 (DC 20).
 * @return  The monster.
 */
Monster MakeEnemy()
{
    Monster enemy;
    enemy.name = "Enemy";
    enemy.ac = 20;
    enemy.fort = enemy.ref = enemy.will = enemy.perception = 10;
    return enemy;
}

/**
 * @brief   The extra Strike damage a feature adds in one situation.
 * @param   [in] feature - The feature.
 * @param   [in] owner - The character with the feature; their sword is the weapon.
 * @param   [in] before - The state when the Strike is rolled.
 * @return  The damage the feature added (empty when it adds none).
 */
std::vector<DamageComponent> FeatureDamage(const Feature& feature, const Character& owner, const BattleState& before)
{
    Encounter encounter{MakeEnemy(), {owner}};
    ActionPtr strike = ProjectContent().Use("Strike", owner, {.weapon = "Sword"});
    BattleState after = before;
    std::vector<DamageComponent> damage;
    EffectCtx ctx{encounter, *strike, Degree::Success, before, after, 1, &damage};
    feature.onStrikeDamage(ctx);
    return damage;
}

/**
 * @brief   A battle state with the enemy and one party member.
 * @return  The state.
 */
BattleState TwoCreatures()
{
    BattleState state;
    state.creatures.resize(2);
    return state;
}
} // namespace

TEST_CASE("content: the project's data loads")
{
    std::vector<std::string> names = ProjectContent().Names();
    for (const char* name : {"Aid", "Battle Medicine", "Bless", "Courageous Anthem", "Demoralize", "Electric Arc",
                             "Fear", "Feint", "Grapple", "Heal", "Heroism", "Hunt Prey", "Overwhelming Spellstrike",
                             "Precision Edge", "Recharge Spellstrike", "Sneak Attack", "Spellstrike", "Strike",
                             "Strike Hard!", "Sure Strike", "Trip"})
    {
        CAPTURE(name);
        CHECK(std::find(names.begin(), names.end(), name) != names.end());
    }
}

TEST_CASE("content: errors name the file, the definition and the problem")
{
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Typo", "costt": 1})"),
                         doctest::Contains("test.json: Typo: unknown key \"costt\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Scare", "check": {"stat": "spell", "save": "Will"},
                                      "outcomes": {"success": [{"condition": "frightend"}]}})"),
                         doctest::Contains("Scare: unknown condition \"frightend\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Zap", "rank": 1, "check": {"stat": "spell", "against": "AC"},
                                      "preset": "attack", "damage": [{"dice": "2x6", "type": "fire"}]})"),
                         doctest::Contains("bad dice \"2x6\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"([{"name": }])"), doctest::Contains("test.json: invalid JSON"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Both", "check": {"stat": "spell", "against": "AC", "save": "Ref"}})"),
                         doctest::Contains("exactly one of"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Wrong", "check": {"stat": "spell", "against": "AC"},
                                      "preset": "basicSave", "damage": [{"dice": "1d6", "type": "fire"}]})"),
                         doctest::Contains("\"basicSave\" preset needs a check with \"save\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Unused", "check": {"stat": "spell", "save": "Ref"},
                                      "damage": [{"dice": "1d6", "type": "fire"}]})"),
                         doctest::Contains("no outcome deals it"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Mystery", "effects": [{"hook": "no-such-hook"}]})"),
                         doctest::Contains("no hook named \"no-such-hook\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Mixed", "check": {"stat": "spell", "save": "Will"},
                                      "effects": [{"counter": "x"}]})"),
                         doctest::Contains("\"effects\" is for actions without a check"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Skill", "effects": [{"counter": "x"}],
                                      "heightened": {"every": 1, "heal": "1d8"}})"),
                         doctest::Contains("only spells can be heightened"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Heal", "rank": 1, "effects": [{"heal": "1d8"}],
                                      "variants": {"one": {"cost": 1}, "two": {"cots": 2}}})"),
                         doctest::Contains("Heal (variant two): unknown key \"cots\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Buff", "rank": 1, "effects": [{"counter": "x"}],
                                      "heightened": {"3": {"effects": [{"counter": "y", "on": "nobody"}]}}})"),
                         doctest::Contains("Buff (heightened 3): unknown target \"nobody\""), ContentError);

    ContentLibrary library = FromJson(R"({"name": "Once", "effects": [{"counter": "x"}]})");
    CHECK_THROWS_WITH_AS(library.AddJson(R"({"name": "Once", "effects": [{"counter": "y"}]})", "other.json"),
                         doctest::Contains("other.json: Once: already defined in test.json"), ContentError);
}

TEST_CASE("content: lookups name what's wrong")
{
    Character caster = MakeCharacter();
    const ContentLibrary& content = ProjectContent();
    CHECK_THROWS_WITH_AS(content.Use("Nope", caster), doctest::Contains("no action named \"Nope\""), ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Sneak Attack", caster), doctest::Contains("is a feature"), ContentError);
    CHECK_THROWS_WITH_AS(content.GetFeature("Feint", caster), doctest::Contains("is an action"), ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Heroism", caster), doctest::Contains("needs an ally"), ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Feint", caster, {.rank = 2}), doctest::Contains("isn't a spell"),
                         ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Bless", caster, {.rank = 11}), doctest::Contains("can't be cast at rank 11"),
                         ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Heal", caster, {.ally = &caster}),
                         doctest::Contains("needs a variant (UseOptions::variant): 1-action, 2-action, 3-action"),
                         ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Heal", caster, {.ally = &caster, .variant = "4-action"}),
                         doctest::Contains("has no variant \"4-action\""), ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Fear", caster, {.variant = "loud"}), doctest::Contains("has no variants"),
                         ContentError);
}

TEST_CASE("content: presets set the damage of each degree")
{
    ContentLibrary library = FromJson(R"([
        {"name": "Zap", "rank": 1, "cost": 2, "check": {"stat": "spell", "against": "AC"}, "preset": "attack",
         "damage": [{"dice": "2d6", "type": "electricity"}]},
        {"name": "Burst", "rank": 1, "cost": 2, "check": {"stat": "spell", "save": "Ref"}, "preset": "basicSave",
         "damage": [{"dice": "4d6", "type": "fire"}],
         "outcomes": {"critFailure": [{"condition": "dazzled"}], "success": [{"damage": "none"}]}}
    ])");
    Character caster = MakeCharacter();

    ActionPtr zap = library.Use("Zap", caster);
    CHECK(zap->check.kind == CheckKind::Attack);
    CHECK(zap->check.modifier == 15);  // spell attack
    CHECK(zap->check.attackTrait);
    CHECK(zap->traits.count("attack") == 1);
    CHECK(zap->damageTags == (Tag::Damage | Tag::Spell));
    CHECK(zap->outcomes[kCritFail].damage == DamageScale::None);
    CHECK(zap->outcomes[kFail].damage == DamageScale::None);
    CHECK(zap->outcomes[kSuccess].damage == DamageScale::Normal);
    CHECK(zap->outcomes[kCritSuccess].damage == DamageScale::Double);

    ActionPtr burst = library.Use("Burst", caster);
    CHECK(burst->check.kind == CheckKind::Save);
    CHECK(burst->check.modifier == 25);  // spell DC
    CHECK(burst->check.defense == Defense::Ref);
    CHECK(burst->outcomes[kCritSuccess].damage == DamageScale::None);
    CHECK(burst->outcomes[kSuccess].damage == DamageScale::None);  // the outcome overrides the preset
    CHECK(burst->outcomes[kFail].damage == DamageScale::Normal);
    CHECK(burst->outcomes[kCritFail].damage == DamageScale::Double);
    REQUIRE(burst->outcomes[kCritFail].conditions.size() == 1);
    CHECK(burst->outcomes[kCritFail].conditions[0].kind == ConditionKind::Dazzled);
}

TEST_CASE("content: Heightened (+N) adds damage per step, and cantrips heighten to half the caster's level")
{
    ContentLibrary library = FromJson(R"([
        {"name": "Arc", "cantrip": true, "cost": 2, "check": {"stat": "spell", "save": "Ref"}, "preset": "basicSave",
         "damage": [{"dice": "2d4", "type": "electricity"}], "heightened": {"every": 1, "damage": [{"dice": "1d4"}]}},
        {"name": "Bolt", "rank": 3, "cost": 2, "check": {"stat": "spell", "save": "Ref"}, "preset": "basicSave",
         "damage": [{"dice": "4d12", "type": "electricity"}],
         "heightened": {"every": 2, "damage": [{"dice": "1d12+2"}, {"dice": "1d6", "type": "fire"}]}}
    ])");
    CHECK(library.Use("Arc", MakeCharacter("Low", 1))->damage[0].count == 2);   // rank 1
    CHECK(library.Use("Arc", MakeCharacter("Mid", 5))->damage[0].count == 4);   // rank 3
    CHECK(library.Use("Arc", MakeCharacter("High", 20))->damage[0].count == 11);  // rank 10
    CHECK(library.Use("Arc", MakeCharacter("Mid", 5), {.rank = 6})->damage[0].count == 7);

    Character caster = MakeCharacter();
    std::vector<DamageComponent> base = library.Use("Bolt", caster)->damage;
    REQUIRE(base.size() == 1);
    CHECK(base[0].count == 4);
    CHECK(library.Use("Bolt", caster, {.rank = 4})->damage == base);  // no full step yet
    std::vector<DamageComponent> twoSteps = library.Use("Bolt", caster, {.rank = 7})->damage;
    REQUIRE(twoSteps.size() == 2);
    CHECK(twoSteps[0].count == 6);
    CHECK(twoSteps[0].flat == 4);
    CHECK(twoSteps[1].count == 2);
    CHECK(twoSteps[1].die == 6);
    CHECK(twoSteps[1].type == DamageType::Fire);
}

TEST_CASE("content: Electric Arc, the README's example, heightens with the caster's level")
{
    // A level 5 caster casts it at rank 3: 2d4 plus 1d4 per rank above 1, basic Reflex save against DC 25.
    Character caster = MakeCharacter("Caster", 5);
    Scenario scenario("arc");
    scenario.Against(MakeEnemy()).AddTurn(caster, {ProjectContent().Use("Electric Arc", caster)});
    DegreeDist save = ResolveCheck(10, 25);
    // 4d4 averages 10. Half damage rounds down, so an odd total (half of them) loses another 1/2: 4.75.
    double expected = save[kSuccess] * 4.75 + save[kFail] * 10.0 + save[kCritFail] * 20.0;
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(expected));
}

TEST_CASE("content: Heightened (Nth) entries apply from their rank on")
{
    Character caster = MakeCharacter();
    Character ally = MakeCharacter("Ally");
    auto heroismBonus = [&](int rank) {
        ActionPtr heroism = ProjectContent().Use("Heroism", caster, {.rank = rank, .ally = &ally});
        return heroism->outcomes[kSuccess].modifiers.at(0).mod.value;
    };
    CHECK(heroismBonus(3) == 1);
    CHECK(heroismBonus(5) == 1);
    CHECK(heroismBonus(6) == 2);
    CHECK(heroismBonus(8) == 2);
    CHECK(heroismBonus(9) == 3);
    CHECK(heroismBonus(10) == 3);
    CHECK_THROWS_WITH_AS(heroismBonus(2), doctest::Contains("can't be cast at rank 2 (its rank is 3"), ContentError);
}

TEST_CASE("content: variants change the cost, name and amounts")
{
    Character cleric = MakeCharacter("Cleric");
    Character hurt = MakeCharacter("Hurt");
    const ContentLibrary& content = ProjectContent();

    ActionPtr twoAction = content.Use("Heal", cleric, {.rank = 2, .ally = &hurt, .variant = "2-action"});
    CHECK(twoAction->name == "Heal (2a) -> Hurt");
    CHECK(twoAction->cost == 2);
    HealSpec heal = twoAction->outcomes[kSuccess].heal;
    CHECK(heal.count == 2);
    CHECK(heal.die == 8);
    CHECK(heal.flat == 16);
    CHECK(heal.to == Who::Ally);

    ActionPtr threeAction = content.Use("Heal", cleric, {.ally = &hurt, .variant = "3-action"});
    CHECK(threeAction->name == "Heal (3a) -> party");
    CHECK(threeAction->outcomes[kSuccess].heal.to == Who::AllAllies);

    cleric.skills[Skill::Medicine] = 12;
    ActionPtr trained = content.Use("Battle Medicine", cleric, {.ally = &hurt});
    CHECK(trained->name == "Battle Medicine -> Hurt");
    CHECK(trained->check.fixedDC == 15);
    CHECK(trained->outcomes[kSuccess].heal.flat == 0);
    ActionPtr expert = content.Use("Battle Medicine", cleric, {.ally = &hurt, .variant = "expert"});
    CHECK(expert->check.fixedDC == 20);
    CHECK(expert->check.modifier == 12);
    CHECK(expert->outcomes[kSuccess].heal.flat == 10);
    CHECK(expert->outcomes[kCritSuccess].heal.count == 4);
}

TEST_CASE("content: temporary immunity can be per actor")
{
    Character first = MakeCharacter("First");
    Character second = MakeCharacter("Second");
    Scenario scenario("demoralize");
    scenario.Against(MakeEnemy())
        .AddTurn(first, {ProjectContent().Use("Demoralize", first), ProjectContent().Use("Demoralize", first)})
        .AddTurn(second, {ProjectContent().Use("Demoralize", second)})
        .AddTurn(MakeCharacter("Third"), {});
    RunResult result = scenario.Run();

    // Intimidation +10 against Will DC 20. The second Demoralize by the same character does nothing; another
    // character's Demoralize can still work.
    DegreeDist roll = ResolveCheck(10, 20);
    double works = roll[kSuccess] + roll[kCritSuccess];
    REQUIRE(result.snapshots.size() == 3);
    CHECK(result.snapshots[1].enemyConditionChance.at(ConditionKind::Frightened) == doctest::Approx(works));
    CHECK(result.snapshots[2].enemyConditionChance.at(ConditionKind::Frightened) ==
          doctest::Approx(1.0 - (1.0 - works) * (1.0 - works)));
}

TEST_CASE("content: damage bonuses apply to every damage roll, not just spells and Strikes")
{
    // Athletics +10 against Reflex DC 20: only a natural 20 is a critical success, which deals 1d6 bludgeoning,
    // plus Courageous Anthem's +1.
    Character bard = MakeCharacter("Bard");
    Character fighter = MakeCharacter("Fighter");
    fighter.skills[Skill::Athletics] = 10;
    Scenario scenario("anthem trip");
    scenario.Against(MakeEnemy())
        .AddTurn(bard, {ProjectContent().Use("Courageous Anthem", bard)})
        .AddTurn(fighter, {ProjectContent().Use("Trip", fighter)});
    CHECK(scenario.Run().ledger.TotalDamage() == doctest::Approx(ResolveCheck(10, 20)[kCritSuccess] * 4.5));
}

TEST_CASE("content: Battle Medicine's immunity is to that medic only")
{
    // Medicine +12 against DC 15. The first medic's second try does nothing; a second medic can still heal.
    Character first = MakeCharacter("First");
    Character second = MakeCharacter("Second");
    first.skills[Skill::Medicine] = second.skills[Skill::Medicine] = 12;
    Character patient = MakeCharacter("Patient");
    patient.hpMissing = 60;
    Scenario scenario("medicine");
    scenario.Against(MakeEnemy())
        .Member(patient)
        .AddTurn(first, {ProjectContent().Use("Battle Medicine", first, {.ally = &patient}),
                         ProjectContent().Use("Battle Medicine", first, {.ally = &patient})})
        .AddTurn(second, {ProjectContent().Use("Battle Medicine", second, {.ally = &patient})});
    RunResult result = scenario.Run();

    DegreeDist roll = ResolveCheck(12, 15);
    double oneUse = roll[kSuccess] * 9.0 + roll[kCritSuccess] * 18.0;  // 2d8 or 4d8
    CHECK(result.ledger.healRaw.at({"First", "Battle Medicine -> Patient"}) == doctest::Approx(oneUse));
    CHECK(result.ledger.healRaw.at({"Second", "Battle Medicine -> Patient"}) == doctest::Approx(oneUse));
}

TEST_CASE("content: features scale with the owner's level and check their conditions")
{
    const ContentLibrary& content = ProjectContent();
    BattleState offGuard = TwoCreatures();
    offGuard.AddCondition(kEnemyIndex, ConditionKind::OffGuard, 1, {});
    for (auto [level, dice] : {std::pair{1, 1}, {4, 1}, {5, 2}, {11, 3}, {17, 4}, {20, 4}})
    {
        CAPTURE(level);
        Character rogue = MakeCharacter("Rogue", level);
        std::vector<DamageComponent> damage = FeatureDamage(content.GetFeature("Sneak Attack", rogue), rogue, offGuard);
        REQUIRE(damage.size() == 1);
        CHECK(damage[0].count == dice);
        CHECK(damage[0].die == 6);
        CHECK(damage[0].category == DamageCategory::Precision);
    }
    Character rogue = MakeCharacter("Rogue", 5);
    CHECK(FeatureDamage(content.GetFeature("Sneak Attack", rogue), rogue, TwoCreatures()).empty());  // not off-guard

    Character ranger = MakeCharacter("Ranger", 11);
    Feature edge = content.GetFeature("Precision Edge", ranger);
    BattleState hunting = TwoCreatures();
    CHECK(FeatureDamage(edge, ranger, hunting).empty());  // no prey yet
    hunting[1].counters["prey"] = 1;
    std::vector<DamageComponent> damage = FeatureDamage(edge, ranger, hunting);
    REQUIRE(damage.size() == 1);
    CHECK(damage[0].count == 2);
    CHECK(damage[0].die == 8);
    hunting[1].counters["precision-used"] = 1;
    CHECK(FeatureDamage(edge, ranger, hunting).empty());  // once per round
}

TEST_CASE("content: hooks run on their outcome")
{
    HookRegistry hooks = HookRegistry::BuiltIn();
    hooks.Register("mark", [](EffectCtx& ctx) { ctx.state[ctx.actorIndex].counters["marked"] = 1; });
    ContentLibrary library(hooks);
    library.AddJson(R"json([{"name": "Mark", "effects": [{"hook": "mark"}]},
                            {"name": "Brace", "effects": [{"hook": "uses-reaction"}]},
                            {"name": "Reactive Strike", "displayName": "Strike ({weapon})", "reaction": true,
                             "check": {"stat": "weapon"}, "preset": "strike"}])json",
                    "test.json");
    Character fighter = MakeCharacter("Fighter");

    Scenario marking("mark");
    marking.Against(MakeEnemy()).AddTurn(fighter, {library.Use("Mark", fighter)});
    for (const auto& [state, probability] : marking.Run().finalStates)
        CHECK(state.Counter(1, "marked") == 1);

    ActionPtr reactiveStrike = library.Use("Reactive Strike", fighter, {.weapon = "Sword"});
    Scenario bracing("brace");
    bracing.Against(MakeEnemy()).AddTurn(fighter, {library.Use("Brace", fighter), reactiveStrike});
    CHECK(bracing.Run().ledger.skipped.at({"Fighter", "Strike (Sword)"}) == doctest::Approx(1.0));
}

TEST_CASE("content: composition keys are checked when loading")
{
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Swing", "check": {"stat": "skill:Athletics", "against": "AC"},
                                      "preset": "strike"})"),
                         doctest::Contains("\"strike\" preset deals the weapon's damage, so it needs \"stat\": "
                                           "\"weapon\""),
                         ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Swing", "check": {"stat": "weapon"}, "preset": "strike",
                                      "damage": [{"dice": "1d6", "type": "fire"}]})"),
                         doctest::Contains("leave out \"damage\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Swing", "check": {"stat": "weapon", "against": "Ref"}})"),
                         doctest::Contains("a weapon attack is always against AC"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Double", "check": {"stat": "spell", "save": "Ref"}, "attacks": 2})"),
                         doctest::Contains("\"attacks\" is for actions with the attack trait"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Help", "check": {"stat": "skill:Athletics", "dc": 15},
                                      "outcomes": {"success": [{"modifier": "circumstance", "value": 1,
                                                                "appliesTo": "chosen", "on": "ally"}]}})"),
                         doctest::Contains("\"appliesTo\": \"chosen\" needs a check whose \"stat\" is \"chosen\""),
                         ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Cast", "check": {"stat": "spell", "save": "Ref"},
                                      "carriesSpell": {"checks": ["save"], "costs": [1]}})"),
                         doctest::Contains("\"carriesSpell\" needs a check with an attack roll"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Cast", "check": {"stat": "weapon"},
                                      "carriesSpell": {"checks": ["attack"], "costs": [4]}})"),
                         doctest::Contains("each of \"costs\" must be 1 to 3 actions"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Cast", "check": {"stat": "weapon"},
                                      "carriesSpell": {"checks": ["attack"], "costs": [1],
                                                       "ignoreResistance": {"types": ["fire"], "amount": "half"}}})"),
                         doctest::Contains("\"amount\" must be at least 1, or \"level\""), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Order", "effects": [{"grant": "Strike", "weapon": "Sword"}]})"),
                         doctest::Contains("unknown key \"weapon\" in a grant effect"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"json({"name": "Order", "effects": [{"grant": "Strike",
                                                                        "displayName": "Strike ({spell})"}]})json"),
                         doctest::Contains("can only contain the placeholders {weapon}"), ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"json({"name": "Shout", "displayName": "Shout ({target})",
                                          "effects": [{"counter": "x"}]})json"),
                         doctest::Contains("can only contain the placeholders {ally}, {weapon}, {spell}"),
                         ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Copy", "extends": "Missing", "cost": 2})"),
                         doctest::Contains("Copy: \"extends\" \"Missing\", which isn't an action defined earlier in "
                                           "this file"),
                         ContentError);
    CHECK_THROWS_WITH_AS(FromJson(R"([{"name": "Copy", "extends": "Later"},
                                      {"name": "Later", "effects": [{"counter": "x"}]}])"),
                         doctest::Contains("isn't an action defined earlier in this file"), ContentError);
}

TEST_CASE("content: options must give what the action needs, and nothing it doesn't use")
{
    Character fighter = MakeCharacter("Fighter");
    Character ally = MakeCharacter("Ally");
    const ContentLibrary& content = ProjectContent();
    ActionPtr spell = content.Use("Electric Arc", fighter);
    CHECK_THROWS_WITH_AS(content.Use("Strike", fighter), "Strike: needs a weapon (UseOptions::weapon)",
                         ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Spellstrike", fighter, {.weapon = "Sword"}),
                         "Spellstrike: needs a spell (UseOptions::spell)", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Strike Hard!", fighter, {.ally = &ally}),
                         doctest::Contains("Strike Hard!: the ally's \"Strike\" (its weapon is "
                                           "UseOptions::allyWeapon): Strike: needs a weapon (UseOptions::weapon)"),
                         ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Aid", fighter, {.ally = &ally}),
                         "Aid: needs a weapon or a skill (UseOptions::weapon or skill), not neither", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Aid", fighter, {.ally = &ally, .weapon = "Sword", .skill = Skill::Deception}),
                         "Aid: needs a weapon or a skill (UseOptions::weapon or skill), not both", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Demoralize", fighter, {.weapon = "Sword"}),
                         "Demoralize: doesn't use UseOptions::weapon, but it's set", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Strike", fighter, {.weapon = "Sword", .spell = spell}),
                         "Strike: doesn't use UseOptions::spell, but it's set", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Strike", fighter, {.weapon = "Sword", .allyWeapon = "Sword"}),
                         "Strike: doesn't use UseOptions::allyWeapon, but it's set", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Feint", fighter, {.skill = Skill::Deception}),
                         "Feint: doesn't use UseOptions::skill, but it's set", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Aid", fighter, {.ally = &ally, .weapon = "Sword", .rangeIncrement = 2}),
                         "Aid: doesn't use UseOptions::rangeIncrement, but it's set", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Strike", fighter, {.weapon = "Sword", .rangeIncrement = 0}),
                         "Strike: UseOptions::rangeIncrement must be at least 1", ContentError);
    CHECK_THROWS_WITH_AS(content.Use("Strike", fighter, {.weapon = "Axe"}), doctest::Contains("Axe"),
                         std::runtime_error);
}

TEST_CASE("content: a Strike rolls the weapon's attack and deals its damage")
{
    Character fighter = MakeCharacter("Fighter");
    fighter.weapons.front().fatal = 12;
    ActionPtr strike = ProjectContent().Use("Strike", fighter, {.weapon = "Sword"});
    CHECK(strike->name == "Strike (Sword)");
    CHECK(strike->check.kind == CheckKind::Attack);
    CHECK(strike->check.defense == Defense::AC);
    CHECK(strike->check.modifier == 20);
    CHECK(strike->check.tags == (Tag::AttackRoll | Tag::Check | Tag::Melee | Tag::Str));
    CHECK(strike->check.attackTrait);
    CHECK(strike->damageTags == (Tag::Damage | Tag::Melee | Tag::Str));
    REQUIRE(strike->weapon.has_value());
    CHECK(strike->weapon->name == "Sword");
    CHECK(strike->damage.front().die == 8);
    CHECK(strike->critDamage.front().die == 12);  // fatal
    CHECK(strike->critDamage.size() == 2);        // plus one fatal die
    CHECK(strike->outcomes[kSuccess].damage == DamageScale::Normal);
    CHECK(strike->outcomes[kCritSuccess].damage == DamageScale::Double);

    ActionPtr far = ProjectContent().Use("Strike", fighter, {.weapon = "Sword", .rangeIncrement = 3});
    CHECK(far->check.untypedPenalty == -4);
}

TEST_CASE("content: Aid uses the weapon or skill it's used with, and helps the same kind of roll")
{
    Character aider = MakeCharacter("Aider");
    Character ally = MakeCharacter("Ally");
    const ContentLibrary& content = ProjectContent();

    ActionPtr withSword = content.Use("Aid", aider, {.ally = &ally, .weapon = "Sword"});
    CHECK(withSword->name == "Aid Ally");
    CHECK(withSword->check.kind == CheckKind::FixedDC);
    CHECK(withSword->check.fixedDC == 15);
    CHECK(withSword->check.modifier == 20);
    CHECK_FALSE(withSword->check.attackTrait);
    const ApplyModifier& critBonus = withSword->outcomes[kCritSuccess].modifiers.at(0);
    CHECK(critBonus.mod.value == 2);
    CHECK(critBonus.to == Who::Ally);
    CHECK(critBonus.mod.applies.anyOf == std::vector<Tags>{Tag::AttackRoll});

    ActionPtr withSkill = content.Use("Aid", aider, {.ally = &ally, .skill = Skill::Deception});
    CHECK(withSkill->check.modifier == 10);
    CHECK(withSkill->outcomes[kSuccess].modifiers.at(0).mod.applies.anyOf ==
          std::vector<Tags>{Tag::Skill | Tag::Check});

    for (const auto& [variant, value] : {std::pair{"expert", 2}, std::pair{"master", 3}, std::pair{"legendary", 4}})
    {
        CAPTURE(variant);
        ActionPtr aid = content.Use("Aid", aider, {.ally = &ally, .variant = variant, .weapon = "Sword"});
        CHECK(aid->outcomes[kCritSuccess].modifiers.at(0).mod.value == value);
        CHECK(aid->outcomes[kSuccess].modifiers.at(0).mod.value == 1);
        CHECK(aid->outcomes[kCritFail].modifiers.at(0).mod.value == -1);
    }

    // +20 against DC 15: a critical success on 5 or more, a success on 2 to 4, a failure on a 1.
    // The ally's Strike (+20, 1d8+4 against AC 20) gets +2, +1 or nothing.
    Scenario scenario("aid");
    scenario.Against(MakeEnemy())
        .AddTurn(aider, {content.Use("Aid", aider, {.ally = &ally, .weapon = "Sword"})})
        .AddTurn(ally, {content.Use("Strike", ally, {.weapon = "Sword"})});
    auto strikeDamage = [](int modifier) {
        DegreeDist roll = ResolveCheck(modifier, 20);
        return roll[kSuccess] * 8.5 + roll[kCritSuccess] * 17.0;
    };
    CHECK(scenario.Run().ledger.TotalDamage() ==
          doctest::Approx(0.8 * strikeDamage(22) + 0.15 * strikeDamage(21) + 0.05 * strikeDamage(20)));
}

TEST_CASE("content: an action can let the ally take another action")
{
    Character commander = MakeCharacter("Commander");
    Character ally = MakeCharacter("Ally");
    ActionPtr tactic = ProjectContent().Use("Strike Hard!", commander, {.ally = &ally, .allyWeapon = "Sword"});
    CHECK(tactic->name == "Strike Hard! -> Ally");
    CHECK(tactic->cost == 2);
    const std::vector<Grant>& grants = tactic->outcomes[kSuccess].grants;
    REQUIRE(grants.size() == 1);
    CHECK(grants.front().allyName == "Ally");
    CHECK(grants.front().action->name == "Strike (Sword, Strike Hard!)");
    CHECK(grants.front().action->reaction);
    CHECK(grants.front().action->check.modifier == 20);  // the ally's sword

    ContentLibrary library = FromJson(R"([
        {"name": "Rally", "effects": [{"grant": "Shout"}]},
        {"name": "Shout", "effects": [{"counter": "shouted"}]}
    ])");
    CHECK(library.Use("Rally", commander, {.ally = &ally})->outcomes[kSuccess].grants.front().action->name ==
          "Shout");
    CHECK_THROWS_WITH_AS(FromJson(R"({"name": "Rally", "effects": [{"grant": "Nothing"}]})")
                             .Use("Rally", commander, {.ally = &ally}),
                         doctest::Contains("no action named \"Nothing\""), ContentError);
}

TEST_CASE("content: an action can carry a spell")
{
    Character magus = MakeCharacter("Magus", 12);
    const ContentLibrary& content = ProjectContent();
    ActionPtr arc = content.Use("Electric Arc", magus);
    ActionPtr spellstrike = content.Use("Spellstrike", magus, {.weapon = "Sword", .spell = arc});
    CHECK(spellstrike->name == "Spellstrike (Electric Arc)");
    CHECK(spellstrike->cost == 2);
    CHECK(spellstrike->check.attacks == 2);
    CHECK(spellstrike->spentFlag == "spellstrike-spent");
    REQUIRE(spellstrike->coupled.has_value());
    CHECK(spellstrike->coupled->roll == Coupling::OwnRoll);  // Electric Arc has a save
    CHECK_FALSE(spellstrike->coupled->action->check.targeted);
    CHECK(spellstrike->coupled->action->damage.front().ignoredResistance == 0);

    ActionPtr overwhelming = content.Use("Overwhelming Spellstrike", magus, {.weapon = "Sword", .spell = arc});
    CHECK(overwhelming->name == "Overwhelming Spellstrike (Electric Arc)");
    CHECK(overwhelming->check.attacks == 2);  // the rest comes from Spellstrike
    CHECK(overwhelming->coupled->action->damage.front().ignoredResistance == 12);

    ActionPtr demoralize = content.Use("Demoralize", magus);
    CHECK_THROWS_WITH_AS(content.Use("Spellstrike", magus, {.weapon = "Sword", .spell = demoralize}),
                         doctest::Contains("can't carry \"Demoralize\""), ContentError);
}

TEST_CASE("content: \"extends\" starts from an earlier entry, variants and all")
{
    ContentLibrary library = FromJson(R"([
        {"name": "Shove", "cost": 1, "check": {"stat": "skill:Intimidation", "against": "Fort"},
         "outcomes": {"success": [{"condition": "prone"}]},
         "defaultVariant": "light", "variants": {"light": {}, "heavy": {"cost": 2}}},
        {"name": "Big Shove", "extends": "Shove", "traits": ["attack"],
         "outcomes": {"critSuccess": [{"condition": "prone"}, {"condition": "off-guard"}]}},
        {"name": "Bigger Shove", "extends": "Big Shove", "variants": {"heavy": {"cost": 3}}}
    ])");
    Character fighter = MakeCharacter("Fighter");
    ActionPtr big = library.Use("Big Shove", fighter);
    CHECK(big->name == "Big Shove");
    CHECK(big->cost == 1);
    CHECK(big->check.modifier == 10);
    CHECK(big->traits.count("attack") == 1);
    CHECK(big->outcomes[kSuccess].conditions.size() == 1);
    CHECK(big->outcomes[kCritSuccess].conditions.size() == 2);
    CHECK(library.Use("Big Shove", fighter, {.variant = "heavy"})->cost == 2);
    CHECK(library.Use("Bigger Shove", fighter, {.variant = "heavy"})->cost == 3);
    CHECK(library.Use("Bigger Shove", fighter)->outcomes[kCritSuccess].conditions.size() == 2);
    CHECK(library.Use("Shove", fighter)->outcomes[kCritSuccess].conditions.empty());
}

TEST_CASE("content: a folder can be left out when loading a directory")
{
    ContentLibrary library;
    library.AddDirectory("data", "data/actions");
    std::vector<std::string> names = library.Names();
    CHECK(std::find(names.begin(), names.end(), "Fear") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Demoralize") == names.end());
    CHECK(std::find(names.begin(), names.end(), "Sneak Attack") != names.end());
}
