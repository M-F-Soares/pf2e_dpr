#include "vendor/doctest.h"

#include "../src/Check.h"
#include "../src/Condition.h"
#include "../src/Damage.h"
#include "../src/State.h"

namespace
{
double at(const DegreeDist& d, Degree g) { return d[static_cast<int>(g)]; }
} // namespace

TEST_CASE("check: +23 vs AC 30")
{
    // Crit on 17-20, hit on 7-16, fail on 2-6, a natural 1 (total 24) drops to crit fail.
    DegreeDist d = resolveCheck(23, 30);
    CHECK(at(d, Degree::CritSuccess) == doctest::Approx(0.20));
    CHECK(at(d, Degree::Success) == doctest::Approx(0.50));
    CHECK(at(d, Degree::Fail) == doctest::Approx(0.25));
    CHECK(at(d, Degree::CritFail) == doctest::Approx(0.05));
}

TEST_CASE("check: natural 20 and natural 1 adjust one degree")
{
    // +0 vs DC 29: a natural 20 (total 20) is a fail raised to success; 19 or less crit fails.
    DegreeDist hard = resolveCheck(0, 29);
    CHECK(at(hard, Degree::Success) == doctest::Approx(0.05));
    CHECK(at(hard, Degree::CritFail) == doctest::Approx(0.95));

    // +30 vs DC 10: everything is a crit, except a natural 1 drops to success.
    DegreeDist easy = resolveCheck(30, 10);
    CHECK(at(easy, Degree::CritSuccess) == doctest::Approx(0.95));
    CHECK(at(easy, Degree::Success) == doctest::Approx(0.05));
}

TEST_CASE("check: fortune and misfortune")
{
    // Need an 11+: 50% normally, 75% with fortune, 25% with misfortune.
    auto passChance = [](Fortune f) {
        DegreeDist d = resolveCheck(0, 11, f);
        return at(d, Degree::Success) + at(d, Degree::CritSuccess);
    };
    CHECK(passChance(Fortune::None) == doctest::Approx(0.50));
    CHECK(passChance(Fortune::Fortune) == doctest::Approx(0.75));
    CHECK(passChance(Fortune::Misfortune) == doctest::Approx(0.25));
    CHECK(combineFortune(true, true) == Fortune::None);
    CHECK(combineFortune(true, false) == Fortune::Fortune);
}

TEST_CASE("check: degree shifting")
{
    DegreeDist d{0.1, 0.2, 0.3, 0.4};
    DegreeDist up = shiftDegrees(d, +1);
    CHECK(up[0] == doctest::Approx(0.0));
    CHECK(up[3] == doctest::Approx(0.7));
    DegreeDist noCrit = remapDegree(d, Degree::CritSuccess, Degree::Success);
    CHECK(noCrit[2] == doctest::Approx(0.7));
    CHECK(flatCheckPassChance(5) == doctest::Approx(0.8));
    CHECK(flatCheckPassChance(11) == doctest::Approx(0.5));
}

TEST_CASE("stacking: same type keeps the best bonus and the worst penalty")
{
    Stacker s;
    s.add(BonusType::Status, 5);
    s.add(BonusType::Status, 5);
    s.add(BonusType::Item, 5);
    CHECK(s.total() == 10);

    Stacker bonusAndPenalty;
    bonusAndPenalty.add(BonusType::Status, 1);
    bonusAndPenalty.add(BonusType::Status, -2);
    CHECK(bonusAndPenalty.total() == -1);

    Stacker untyped;  // MAP and range penalties stack
    untyped.add(BonusType::Untyped, -5);
    untyped.add(BonusType::Untyped, -2);
    CHECK(untyped.total() == -7);
}

TEST_CASE("conditions: status penalties overlap, off-guard is circumstance")
{
    BattleState s;
    s.creatures.resize(2);
    RollQuery ac{Tag::AC | Tag::DC | Tag::Dex, 1};

    s.addCondition(kEnemy, ConditionKind::Frightened, 1, {});
    s.addCondition(kEnemy, ConditionKind::Clumsy, 2, {});
    CHECK(s.modifierTotal(kEnemy, ac) == -2);  // both status: worst only

    s.addCondition(kEnemy, ConditionKind::OffGuard, 1, {});
    CHECK(s.modifierTotal(kEnemy, ac) == -4);  // circumstance -2 stacks with status -2

    s.addCondition(kEnemy, ConditionKind::Prone, 1, {});
    CHECK(s.modifierTotal(kEnemy, ac) == -4);  // prone's off-guard doesn't stack with off-guard

    // Will save: frightened applies, clumsy doesn't.
    CHECK(s.modifierTotal(kEnemy, RollQuery{Tag::Save | Tag::Check | Tag::Will | Tag::Wis, 1}) == -1);
}

TEST_CASE("conditions: same condition keeps the higher value")
{
    BattleState s;
    s.creatures.resize(1);
    s.addCondition(kEnemy, ConditionKind::Drained, 1, {});
    s.addCondition(kEnemy, ConditionKind::Drained, 1, {});
    CHECK(s.conditionValue(kEnemy, ConditionKind::Drained) == 1);
    s.addCondition(kEnemy, ConditionKind::Drained, 2, {});
    s.addCondition(kEnemy, ConditionKind::Drained, 1, {});
    CHECK(s.conditionValue(kEnemy, ConditionKind::Drained) == 2);
}

TEST_CASE("modifiers: cover and Raise a Shield are both circumstance")
{
    BattleState s;
    s.creatures.resize(1);
    s.addModifier(kEnemy, {Modifier{"cover", BonusType::Circumstance, 2, select({Tag::AC})}, {}, -1});
    s.addModifier(kEnemy, {Modifier{"raise-a-shield", BonusType::Circumstance, 2, select({Tag::AC})}, {}, -1});
    CHECK(s.modifierTotal(kEnemy, RollQuery{Tag::AC | Tag::DC | Tag::Dex}) == 2);
}

TEST_CASE("modifiers: limited uses are consumed")
{
    BattleState s;
    s.creatures.resize(2);
    s.addModifier(1, {Modifier{"aid", BonusType::Circumstance, 2, select({Tag::AttackRoll})}, {}, 1});
    std::vector<std::string> used;
    CHECK(s.modifierTotal(1, RollQuery{Tag::AttackRoll | Tag::Check}, {}, &used) == 2);
    s.consumeUses(1, used);
    CHECK(s.modifierTotal(1, RollQuery{Tag::AttackRoll | Tag::Check}) == 0);
}

TEST_CASE("damage: resistance and weakness are exact over the dice")
{
    DamageDefenses none;
    CHECK(expectedDamage({dmg(1, 6, 0, DamageType::Fire)}, DamageScale::Normal, none) == doctest::Approx(3.5));

    DamageDefenses resist;
    resist.resistances = {{DamageType::Fire, 3}};
    // E[max(0, d6 - 3)] = (0+0+0+1+2+3)/6, not 3.5 - 3
    CHECK(expectedDamage({dmg(1, 6, 0, DamageType::Fire)}, DamageScale::Normal, resist) == doctest::Approx(1.0));

    DamageDefenses weak;
    weak.weaknesses = {{DamageType::Fire, 5}};
    CHECK(expectedDamage({dmg(1, 6, 0, DamageType::Fire)}, DamageScale::Normal, weak) == doctest::Approx(8.5));

    DamageDefenses immune;
    immune.immunities = {"fire"};
    CHECK(expectedDamage({dmg(1, 6, 0, DamageType::Fire), dmg(1, 6, 0, DamageType::Slashing)},
                         DamageScale::Normal, immune) == doctest::Approx(3.5));
}

TEST_CASE("damage: physical resistance, bypass and per-type application")
{
    DamageDefenses d;
    d.resistances = {{DamageType::Physical, 5, {"silver"}}};
    std::vector<DamageComponent> hit = {dmg(0, 0, 10, DamageType::Slashing), dmg(0, 0, 4, DamageType::Fire)};
    CHECK(expectedDamage(hit, DamageScale::Normal, d) == doctest::Approx(9.0));
    CHECK(expectedDamage(hit, DamageScale::Normal, d, {"silver"}) == doctest::Approx(14.0));
}

TEST_CASE("damage: crit doubling, undoubled extras, halving, precision immunity, minimum 1")
{
    DamageDefenses none;
    std::vector<DamageComponent> crit = {dmg(1, 8, 4, DamageType::Piercing),
                                         DamageComponent{1, 10, 0, DamageType::Piercing, DamageCategory::Normal, false}};
    CHECK(expectedDamage(crit, DamageScale::Double, none) == doctest::Approx(17.0 + 5.5));

    CHECK(expectedDamage({dmg(0, 0, 9, DamageType::Fire)}, DamageScale::Half, none) == doctest::Approx(4.0));

    DamageDefenses noPrecision;
    noPrecision.immunities = {"precision"};
    std::vector<DamageComponent> sneak = {dmg(0, 0, 5, DamageType::Piercing), precision(2, 6, DamageType::Piercing)};
    CHECK(expectedDamage(sneak, DamageScale::Normal, none) == doctest::Approx(12.0));
    CHECK(expectedDamage(sneak, DamageScale::Normal, noPrecision) == doctest::Approx(5.0));

    CHECK(expectedDamage({dmg(1, 4, -10, DamageType::Bludgeoning)}, DamageScale::Normal, none) == doctest::Approx(1.0));
}
