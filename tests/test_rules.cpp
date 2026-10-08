/**
 * @file    test_rules.cpp
 * @brief   Unit tests for the rules building blocks: checks, stacking, conditions and damage.
 */

#include "vendor/doctest.h"

#include "Check.h"
#include "Condition.h"
#include "Damage.h"
#include "State.h"

namespace
{
/**
 * @brief   The probability of one degree in a distribution.
 * @param   [in] dist - The distribution.
 * @param   [in] degree - The degree.
 * @return  Its probability.
 */
double At(const DegreeDist& dist, Degree degree) { return dist[static_cast<int>(degree)]; }
} // namespace

TEST_CASE("check: +23 vs AC 30")
{
    // Crit on 17-20, hit on 7-16, fail on 2-6, a natural 1 (total 24) drops to crit fail.
    DegreeDist dist = ResolveCheck(23, 30);
    CHECK(At(dist, Degree::CritSuccess) == doctest::Approx(0.20));
    CHECK(At(dist, Degree::Success) == doctest::Approx(0.50));
    CHECK(At(dist, Degree::Fail) == doctest::Approx(0.25));
    CHECK(At(dist, Degree::CritFail) == doctest::Approx(0.05));
}

TEST_CASE("check: natural 20 and natural 1 adjust one degree")
{
    // +0 vs DC 29: a natural 20 (total 20) is a fail raised to success; 19 or less crit fails.
    DegreeDist hard = ResolveCheck(0, 29);
    CHECK(At(hard, Degree::Success) == doctest::Approx(0.05));
    CHECK(At(hard, Degree::CritFail) == doctest::Approx(0.95));

    // +30 vs DC 10: everything is a crit, except a natural 1 drops to success.
    DegreeDist easy = ResolveCheck(30, 10);
    CHECK(At(easy, Degree::CritSuccess) == doctest::Approx(0.95));
    CHECK(At(easy, Degree::Success) == doctest::Approx(0.05));
}

TEST_CASE("check: fortune and misfortune")
{
    // Need an 11+: 50% normally, 75% with fortune, 25% with misfortune.
    auto passChance = [](Fortune fortune) {
        DegreeDist dist = ResolveCheck(0, 11, fortune);
        return At(dist, Degree::Success) + At(dist, Degree::CritSuccess);
    };
    CHECK(passChance(Fortune::None) == doctest::Approx(0.50));
    CHECK(passChance(Fortune::Fortune) == doctest::Approx(0.75));
    CHECK(passChance(Fortune::Misfortune) == doctest::Approx(0.25));
    CHECK(CombineFortune(true, true) == Fortune::None);
    CHECK(CombineFortune(true, false) == Fortune::Fortune);
}

TEST_CASE("check: degree shifting")
{
    DegreeDist dist{0.1, 0.2, 0.3, 0.4};
    DegreeDist up = ShiftDegrees(dist, +1);
    CHECK(up[0] == doctest::Approx(0.0));
    CHECK(up[3] == doctest::Approx(0.7));
    DegreeDist noCrit = RemapDegree(dist, Degree::CritSuccess, Degree::Success);
    CHECK(noCrit[2] == doctest::Approx(0.7));
    CHECK(FlatCheckPassChance(5) == doctest::Approx(0.8));
    CHECK(FlatCheckPassChance(11) == doctest::Approx(0.5));
}

TEST_CASE("stacking: same type keeps the best bonus and the worst penalty")
{
    Stacker stack;
    stack.Add(BonusType::Status, 5);
    stack.Add(BonusType::Status, 5);
    stack.Add(BonusType::Item, 5);
    CHECK(stack.Total() == 10);

    Stacker bonusAndPenalty;
    bonusAndPenalty.Add(BonusType::Status, 1);
    bonusAndPenalty.Add(BonusType::Status, -2);
    CHECK(bonusAndPenalty.Total() == -1);

    Stacker untyped;  // MAP and range penalties stack
    untyped.Add(BonusType::Untyped, -5);
    untyped.Add(BonusType::Untyped, -2);
    CHECK(untyped.Total() == -7);
}

TEST_CASE("conditions: status penalties overlap, off-guard is circumstance")
{
    BattleState state;
    state.creatures.resize(2);
    RollQuery ac{Tag::AC | Tag::DC | Tag::Dex, 1};

    state.AddCondition(kEnemyIndex, ConditionKind::Frightened, 1, {});
    state.AddCondition(kEnemyIndex, ConditionKind::Clumsy, 2, {});
    CHECK(state.ModifierTotal(kEnemyIndex, ac) == -2);  // both status: worst only

    state.AddCondition(kEnemyIndex, ConditionKind::OffGuard, 1, {});
    CHECK(state.ModifierTotal(kEnemyIndex, ac) == -4);  // circumstance -2 stacks with status -2

    state.AddCondition(kEnemyIndex, ConditionKind::Prone, 1, {});
    CHECK(state.ModifierTotal(kEnemyIndex, ac) == -4);  // prone's off-guard doesn't stack with off-guard

    // Will save: frightened applies, clumsy doesn't.
    CHECK(state.ModifierTotal(kEnemyIndex, RollQuery{Tag::Save | Tag::Check | Tag::Will | Tag::Wis, 1}) == -1);
}

TEST_CASE("conditions: same condition keeps the higher value")
{
    BattleState state;
    state.creatures.resize(1);
    state.AddCondition(kEnemyIndex, ConditionKind::Drained, 1, {});
    state.AddCondition(kEnemyIndex, ConditionKind::Drained, 1, {});
    CHECK(state.ConditionValue(kEnemyIndex, ConditionKind::Drained) == 1);
    state.AddCondition(kEnemyIndex, ConditionKind::Drained, 2, {});
    state.AddCondition(kEnemyIndex, ConditionKind::Drained, 1, {});
    CHECK(state.ConditionValue(kEnemyIndex, ConditionKind::Drained) == 2);
}

TEST_CASE("modifiers: cover and Raise a Shield are both circumstance")
{
    BattleState state;
    state.creatures.resize(1);
    state.AddModifier(kEnemyIndex, {Modifier{"cover", BonusType::Circumstance, 2, Select({Tag::AC})}, {}, -1});
    state.AddModifier(kEnemyIndex, {Modifier{"raise-a-shield", BonusType::Circumstance, 2, Select({Tag::AC})}, {}, -1});
    CHECK(state.ModifierTotal(kEnemyIndex, RollQuery{Tag::AC | Tag::DC | Tag::Dex}) == 2);
}

TEST_CASE("modifiers: limited uses are consumed")
{
    BattleState state;
    state.creatures.resize(2);
    state.AddModifier(1, {Modifier{"aid", BonusType::Circumstance, 2, Select({Tag::AttackRoll})}, {}, 1});
    std::vector<std::string> used;
    CHECK(state.ModifierTotal(1, RollQuery{Tag::AttackRoll | Tag::Check}, {}, &used) == 2);
    state.ConsumeUses(1, used);
    CHECK(state.ModifierTotal(1, RollQuery{Tag::AttackRoll | Tag::Check}) == 0);
}

TEST_CASE("damage: resistance and weakness are exact over the dice")
{
    DamageDefenses none;
    CHECK(ExpectedDamage({Damage(1, 6, 0, DamageType::Fire)}, DamageScale::Normal, none) == doctest::Approx(3.5));

    DamageDefenses resist;
    resist.resistances = {{DamageType::Fire, 3}};
    // E[max(0, d6 - 3)] = (0+0+0+1+2+3)/6, not 3.5 - 3
    CHECK(ExpectedDamage({Damage(1, 6, 0, DamageType::Fire)}, DamageScale::Normal, resist) == doctest::Approx(1.0));

    DamageDefenses weak;
    weak.weaknesses = {{DamageType::Fire, 5}};
    CHECK(ExpectedDamage({Damage(1, 6, 0, DamageType::Fire)}, DamageScale::Normal, weak) == doctest::Approx(8.5));

    DamageDefenses immune;
    immune.immunities = {"fire"};
    CHECK(ExpectedDamage({Damage(1, 6, 0, DamageType::Fire), Damage(1, 6, 0, DamageType::Slashing)},
                         DamageScale::Normal, immune) == doctest::Approx(3.5));
}

TEST_CASE("damage: physical resistance, bypass and per-type application")
{
    DamageDefenses defenses;
    defenses.resistances = {{DamageType::Physical, 5, {"silver"}}};
    std::vector<DamageComponent> hit = {Damage(0, 0, 10, DamageType::Slashing), Damage(0, 0, 4, DamageType::Fire)};
    CHECK(ExpectedDamage(hit, DamageScale::Normal, defenses) == doctest::Approx(9.0));
    CHECK(ExpectedDamage(hit, DamageScale::Normal, defenses, {"silver"}) == doctest::Approx(14.0));
}

TEST_CASE("damage: crit doubling, undoubled extras, halving, precision immunity, minimum 1")
{
    DamageDefenses none;
    std::vector<DamageComponent> crit = {Damage(1, 8, 4, DamageType::Piercing),
                                         DamageComponent{1, 10, 0, DamageType::Piercing, DamageCategory::Normal, false}};
    CHECK(ExpectedDamage(crit, DamageScale::Double, none) == doctest::Approx(17.0 + 5.5));

    CHECK(ExpectedDamage({Damage(0, 0, 9, DamageType::Fire)}, DamageScale::Half, none) == doctest::Approx(4.0));

    DamageDefenses noPrecision;
    noPrecision.immunities = {"precision"};
    std::vector<DamageComponent> sneak = {Damage(0, 0, 5, DamageType::Piercing), Precision(2, 6, DamageType::Piercing)};
    CHECK(ExpectedDamage(sneak, DamageScale::Normal, none) == doctest::Approx(12.0));
    CHECK(ExpectedDamage(sneak, DamageScale::Normal, noPrecision) == doctest::Approx(5.0));

    CHECK(ExpectedDamage({Damage(1, 4, -10, DamageType::Bludgeoning)}, DamageScale::Normal, none) == doctest::Approx(1.0));
}
