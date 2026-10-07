#include "vendor/doctest.h"

#include "../src/Scenario.h"
#include "../src/content/Actions.h"

#include <functional>

namespace
{
constexpr int S = static_cast<int>(Degree::Success);
constexpr int CS = static_cast<int>(Degree::CritSuccess);

// +attack, 1d8+4 slashing (hit 8.5, crit 17).
Character fighter(const std::string& name = "Fighter", int attack = 23, bool agile = false)
{
    Character c;
    c.name = name;
    c.skills = {{Skill::Intimidation, 10}, {Skill::Deception, 10}, {Skill::Medicine, 10}};
    Weapon w;
    w.name = "Sword";
    w.attack = attack;
    w.dice = 1;
    w.die = 8;
    w.flat = 4;
    w.agile = agile;
    w.finesse = true;
    c.weapons = {w};
    c.spellDC = 25;
    return c;
}

Monster dummy(int ac = 30)
{
    Monster m;
    m.name = "Dummy";
    m.level = 5;
    m.ac = ac;
    m.fort = m.ref = m.will = m.perception = 10;  // DC 20
    return m;
}

double strikeEV(int mod, int ac, double hit = 8.5, double crit = 17.0, Fortune f = Fortune::None)
{
    DegreeDist d = resolveCheck(mod, ac, f);
    return d[S] * hit + d[CS] * crit;
}
} // namespace

TEST_CASE("engine: one Strike matches the hand calculation")
{
    Character f = fighter();
    Scenario s("one strike");
    s.against(dummy()).turn(f, {strike(f, "Sword")});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(0.5 * 8.5 + 0.2 * 17.0));
}

TEST_CASE("engine: multiple attack penalty (normal and agile)")
{
    Character f = fighter();
    Scenario s("three strikes");
    s.against(dummy()).turn(f, {strike(f, "Sword"), strike(f, "Sword"), strike(f, "Sword")});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30) + strikeEV(18, 30) + strikeEV(13, 30)));

    Character a = fighter("Agile", 23, true);
    Scenario agile("agile");
    agile.against(dummy()).turn(a, {strike(a, "Sword"), strike(a, "Sword"), strike(a, "Sword")});
    CHECK(agile.run().ledger.totalDamage() ==
          doctest::Approx(strikeEV(23, 30) + strikeEV(19, 30) + strikeEV(15, 30)));
}

TEST_CASE("engine: a 4th action is skipped")
{
    Character f = fighter();
    Scenario s("four strikes");
    s.against(dummy()).turn(f, {strike(f, "Sword"), strike(f, "Sword"), strike(f, "Sword"), strike(f, "Sword")});
    RunResult r = s.run();
    CHECK(r.ledger.totalDamage() == doctest::Approx(strikeEV(23, 30) + strikeEV(18, 30) + strikeEV(13, 30)));
    CHECK(r.ledger.skipped.at({"Fighter", "Strike (Sword)"}) == doctest::Approx(1.0));
}

TEST_CASE("engine: Demoralize result changes the next Strike (chained probabilities)")
{
    Character f = fighter();
    Scenario s("demoralize then strike");
    s.against(dummy()).turn(f, {demoralize(f), strike(f, "Sword")});

    // Intimidation +10 vs Will DC 20; frightened lowers AC by its value. Demoralize has no attack trait.
    DegreeDist dem = resolveCheck(10, 20);
    double expected = (dem[0] + dem[1]) * strikeEV(23, 30) + dem[S] * strikeEV(23, 29) + dem[CS] * strikeEV(23, 28);
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(expected));
}

TEST_CASE("engine: enemy immune to mental ignores Demoralize")
{
    Character f = fighter();
    Monster m = dummy();
    m.defenses.immunities = {"mental"};
    Scenario s("mindless");
    s.against(m).turn(f, {demoralize(f), strike(f, "Sword")});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30)));
}

TEST_CASE("engine: Feint + Sneak Attack (success = next attack only, crit = all)")
{
    Character rogue = fighter("Rogue");
    rogue.features = {sneakAttack(1)};
    Scenario s("feint");
    s.against(dummy()).turn(rogue, {feint(rogue), strike(rogue, "Sword"), strike(rogue, "Sword")});

    DegreeDist fe = resolveCheck(10, 20);  // Deception vs Perception DC
    double sneakHit = 8.5 + 3.5, sneakCrit = 17.0 + 7.0;
    double none = strikeEV(23, 30) + strikeEV(18, 30);
    double success = strikeEV(23, 28, sneakHit, sneakCrit) + strikeEV(18, 30);
    double crit = strikeEV(23, 28, sneakHit, sneakCrit) + strikeEV(18, 28, sneakHit, sneakCrit);
    double expected = (fe[0] + fe[1]) * none + fe[S] * success + fe[CS] * crit;
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(expected));
}

TEST_CASE("engine: merging branches never changes results")
{
    Character a = fighter("A");
    Character b = fighter("B");
    b.features = {sneakAttack(2)};
    Scenario s("complex");
    s.against(dummy())
        .turn(a, {demoralize(a), strike(a, "Sword"), feint(a)})
        .turn(b, {feint(b), strike(b, "Sword"), strike(b, "Sword")});
    Settings noMerge;
    noMerge.mergeStates = false;
    RunResult merged = s.run();
    RunResult unmerged = s.variant("unmerged").settings(noMerge).run();
    CHECK(merged.ledger.totalDamage() == doctest::Approx(unmerged.ledger.totalDamage()));
    CHECK(merged.finalStates.size() < unmerged.finalStates.size());
}

TEST_CASE("engine: Strike Hard! gives a reaction Strike without MAP, once per round")
{
    Character commander = fighter("Commander");
    Character other = fighter("Other Commander");
    Character ally = fighter("Ally");
    Scenario s("tactic");
    s.against(dummy())
        .turn(commander, {strikeHard(commander, ally, "Sword")})
        .turn(other, {strikeHard(other, ally, "Sword")})
        .turn(ally, {strike(ally, "Sword"), strike(ally, "Sword")});
    RunResult r = s.run();
    // Reaction Strike at full bonus; the 2nd tactic finds the reaction used; the ally's own turn starts at MAP 0.
    CHECK(r.ledger.totalDamage() == doctest::Approx(2 * strikeEV(23, 30) + strikeEV(18, 30)));
    CHECK(r.ledger.skipped.at({"Ally", "Strike (Sword, Strike Hard!)"}) == doctest::Approx(1.0));
}

TEST_CASE("engine: Courageous Anthem adds +1 status to attack and damage")
{
    Character bard = fighter("Bard");
    Character f = fighter();
    Scenario s("anthem");
    s.against(dummy()).turn(bard, {courageousAnthem(bard)}).turn(f, {strike(f, "Sword")});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(24, 30, 9.5, 19.0)));
}

TEST_CASE("engine: Sure Strike gives fortune to the next attack only")
{
    Character f = fighter();
    Scenario s("sure strike");
    s.against(dummy()).turn(f, {sureStrike(f), strike(f, "Sword"), strike(f, "Sword")});
    CHECK(s.run().ledger.totalDamage() ==
          doctest::Approx(strikeEV(23, 30, 8.5, 17.0, Fortune::Fortune) + strikeEV(18, 30)));
}

TEST_CASE("engine: concealed target costs a DC 5 flat check")
{
    Character f = fighter();
    Scenario s("concealed");
    s.against(dummy()).enemyCondition(ConditionKind::Concealed).turn(f, {strike(f, "Sword")});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(0.8 * strikeEV(23, 30)));
}

TEST_CASE("engine: basic save spell uses the enemy's degree")
{
    Character caster = fighter("Caster");
    Scenario s("blast");
    s.against(dummy()).turn(caster, {basicSaveSpell(caster, "Blast", 2, Defense::Ref, {dmg(0, 0, 10, DamageType::Fire)})});
    DegreeDist save = resolveCheck(10, 25);  // Reflex +10 vs DC 25
    double expected = save[S] * 5 + save[1] * 10 + save[0] * 20;
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(expected));
}

TEST_CASE("engine: persistent damage from crits doesn't stack and ticks at the enemy's turn")
{
    Character f = fighter();
    Weapon& w = f.weapons.front();
    w.onCrit = {persistent(0, 0, 5, DamageType::Fire)};
    Scenario s("persistent");
    s.against(dummy()).turn(f, {strike(f, "Sword"), strike(f, "Sword")});
    RunResult r = s.run();
    double pNoCrit = (1 - resolveCheck(23, 30)[CS]) * (1 - resolveCheck(18, 30)[CS]);
    CHECK(r.ledger.damage.at({"(persistent)", "fire"}) == doctest::Approx((1 - pNoCrit) * 5));

    Settings twoTicks;
    twoTicks.persistentTicks = 2;
    RunResult r2 = s.variant("2 ticks").settings(twoTicks).run();
    CHECK(r2.ledger.damage.at({"(persistent)", "fire"}) == doctest::Approx((1 - pNoCrit) * 5 * 1.7));
}

TEST_CASE("engine: effective healing is capped by missing HP and chains between heals")
{
    Character cleric = fighter("Cleric");
    Character hurt = fighter("Hurt");
    hurt.hpMissing = 5;
    Scenario s("heal");
    s.against(dummy()).member(hurt).turn(cleric, {heal(cleric, hurt, 1, 1), heal(cleric, hurt, 1, 1)});
    RunResult r = s.run();

    double effective = 0.0;
    for (int x = 1; x <= 8; ++x)
        for (int y = 1; y <= 8; ++y)
        {
            int first = std::min(x, 5);
            effective += (first + std::min(y, 5 - first)) / 64.0;
        }
    CHECK(r.ledger.totalHealRaw() == doctest::Approx(9.0));
    CHECK(r.ledger.totalHealEffective() == doctest::Approx(effective));
}

TEST_CASE("engine: adaptive step chooses per branch")
{
    Character f = fighter();
    Scenario s("adaptive");
    // Demoralize; then only Strike if it worked, otherwise Demoralize is no longer usable anyway -> Strike.
    s.against(dummy()).turn(f, {demoralize(f), choose([&](const View& v) -> ActionPtr {
                                    return v.enemyCondition(ConditionKind::Frightened) > 0 ? strike(f, "Sword")
                                                                                           : nullptr;
                                })});
    DegreeDist dem = resolveCheck(10, 20);
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(dem[S] * strikeEV(23, 29) + dem[CS] * strikeEV(23, 28)));
}

namespace
{
// Automatic (no check) drained N on the enemy.
ActionPtr drain(int value)
{
    auto a = std::make_shared<Action>();
    a->name = "Drain";
    a->outcomes[S].conditions = {{ConditionKind::Drained, value}};
    return a;
}

double drainedLoss(const RunResult& r, const std::string& who)
{
    auto it = r.ledger.damage.find({who, "Drain (drained HP loss)"});
    return it == r.ledger.damage.end() ? 0.0 : it->second;
}
} // namespace

TEST_CASE("engine: drained HP loss is credited to the actor and ignores resistances")
{
    Character f = fighter();
    Monster m = dummy();
    m.defenses.resistances = {{DamageType::All, 5}};
    Scenario s("drain");
    s.against(m).turn(f, {drain(2)});
    RunResult r = s.run();
    CHECK(drainedLoss(r, "Fighter") == doctest::Approx(10.0));  // level 5 x drained 2
    CHECK(r.ledger.damageBy("Fighter") == doctest::Approx(10.0));
    CHECK(r.ledger.totalDamage() == doctest::Approx(10.0));
}

TEST_CASE("engine: drained HP loss only counts the increase")
{
    Character f = fighter();
    Scenario raise("drained 1 -> 2");
    raise.against(dummy()).enemyCondition(ConditionKind::Drained, 1).turn(f, {drain(2)});
    CHECK(drainedLoss(raise.run(), "Fighter") == doctest::Approx(5.0));

    Scenario lower("drained 2, apply 1");
    lower.against(dummy()).enemyCondition(ConditionKind::Drained, 2).turn(f, {drain(1)});
    CHECK(drainedLoss(lower.run(), "Fighter") == doctest::Approx(0.0));
}

TEST_CASE("engine: drained HP loss respects immunity and the level minimum")
{
    Character f = fighter();
    Monster immune = dummy();
    immune.defenses.immunities = {"drained"};
    Scenario s("immune");
    s.against(immune).turn(f, {drain(2)});
    CHECK(drainedLoss(s.run(), "Fighter") == doctest::Approx(0.0));

    Monster lowLevel = dummy();
    lowLevel.level = 0;
    Scenario low("level 0");
    low.against(lowLevel).turn(f, {drain(2)});
    CHECK(drainedLoss(low.run(), "Fighter") == doctest::Approx(2.0));  // min 1 x drained 2
}

TEST_CASE("engine: drained from a save is weighted by the enemy's degree")
{
    Character caster = fighter("Caster");
    auto a = std::make_shared<Action>();
    a->name = "Drain";
    a->cost = 2;
    a->check.kind = CheckKind::Save;
    a->check.modifier = caster.spellDC;  // DC 25
    a->check.tags = Tag::DC | Tag::Spell;
    a->check.defense = Defense::Fort;
    a->outcomes[static_cast<int>(Degree::Fail)].conditions = {{ConditionKind::Drained, 1}};
    a->outcomes[static_cast<int>(Degree::CritFail)].conditions = {{ConditionKind::Drained, 2}};

    Scenario s("drain save");
    s.against(dummy()).turn(caster, {ActionPtr(a)});
    DegreeDist save = resolveCheck(10, 25);  // Fort +10 vs DC 25
    double expected = save[static_cast<int>(Degree::Fail)] * 5 + save[static_cast<int>(Degree::CritFail)] * 10;
    CHECK(drainedLoss(s.run(), "Caster") == doctest::Approx(expected));
}

namespace
{
// Sword Strike at +23 (1d8+4), spell attack +21 (Int), and a 2-action 2d6 fire spell attack (hit 7, crit 14).
Character magus()
{
    Character c = fighter("Magus");
    c.spellAttack = 21;
    c.keyAttribute = Attribute::Int;
    return c;
}

ActionPtr fireRay(const Character& caster)
{
    return spellAttackSpell(caster, "Fire Ray", 2, {dmg(2, 6, 0, DamageType::Fire)});
}
} // namespace

TEST_CASE("magus: spell attack spell uses the spell attack modifier and doubles on a crit")
{
    Character m = magus();
    Scenario s("fire ray");
    s.against(dummy()).turn(m, {fireRay(m)});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(21, 30, 7.0, 14.0)));
}

TEST_CASE("magus: spell attack after a Strike takes the multiple attack penalty")
{
    Character m = magus();
    Scenario s("strike then spell");
    s.against(dummy()).turn(m, {strike(m, "Sword"), fireRay(m)});
    RunResult r = s.run();
    CHECK(r.ledger.damage.at({"Magus", "Strike (Sword)"}) == doctest::Approx(strikeEV(23, 30)));
    CHECK(r.ledger.damage.at({"Magus", "Fire Ray"}) == doctest::Approx(strikeEV(16, 30, 7.0, 14.0)));
}

TEST_CASE("magus: resistance applies after the crit doubles the spell damage")
{
    Character m = magus();
    Monster target = dummy();
    target.defenses.resistances = {{DamageType::Fire, 3}};
    Scenario s("resisted fire ray");
    s.against(target).turn(m, {fireRay(m)});

    // Exact over 2d6: hit = E[max(0, X - 3)], crit = E[max(0, 2X - 3)].
    double hit = 0.0, crit = 0.0;
    for (int a = 1; a <= 6; ++a)
        for (int b = 1; b <= 6; ++b)
        {
            hit += std::max(0, a + b - 3) / 36.0;
            crit += std::max(0, 2 * (a + b) - 3) / 36.0;
        }
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(21, 30, hit, crit)));
}

TEST_CASE("magus: Courageous Anthem adds +1 to the spell attack roll and its damage")
{
    Character bard = fighter("Bard");
    Character m = magus();
    Scenario s("anthem fire ray");
    s.against(dummy()).turn(bard, {courageousAnthem(bard)}).turn(m, {fireRay(m)});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(22, 30, 8.0, 16.0)));
}

TEST_CASE("magus: Spellstrike uses the Strike's roll for both and adds the spell's damage")
{
    Character m = magus();
    Scenario s("spellstrike");
    s.against(dummy()).turn(m, {spellstrike(m, "Sword", fireRay(m))});
    // +23 (the weapon's attack, not the +21 spell attack); hit 8.5 + 7, crit 17 + 14.
    RunResult r = s.run();
    CHECK(r.ledger.damage.at({"Magus", "Spellstrike (Fire Ray)"}) == doctest::Approx(strikeEV(23, 30, 15.5, 31.0)));
    CHECK(r.ledger.totalDamage() == doctest::Approx(strikeEV(23, 30, 15.5, 31.0)));
}

TEST_CASE("magus: Spellstrike counts as two attacks, applied after it")
{
    Character m = magus();
    Scenario first("spellstrike, strike");
    first.against(dummy()).turn(m, {spellstrike(m, "Sword", fireRay(m)), strike(m, "Sword")});
    CHECK(first.run().ledger.totalDamage() ==
          doctest::Approx(strikeEV(23, 30, 15.5, 31.0) + strikeEV(13, 30)));  // the Strike is at -10

    Scenario second("strike, spellstrike");
    second.against(dummy()).turn(m, {strike(m, "Sword"), spellstrike(m, "Sword", fireRay(m))});
    CHECK(second.run().ledger.totalDamage() ==
          doctest::Approx(strikeEV(23, 30) + strikeEV(18, 30, 15.5, 31.0)));  // the Spellstrike is at -5
}

TEST_CASE("magus: Spellstrike can't be used again until recharged")
{
    Character m = magus();
    Scenario spent("spent");
    spent.against(dummy()).counter(m, "spellstrike-spent").turn(m, {spellstrike(m, "Sword", fireRay(m)), strike(m, "Sword")});
    RunResult r = spent.run();
    CHECK(r.ledger.skipped.at({"Magus", "Spellstrike (Fire Ray)"}) == doctest::Approx(1.0));
    CHECK(r.ledger.totalDamage() == doctest::Approx(strikeEV(23, 30)));  // no actions spent, no MAP added

    Scenario recharged("recharge first");
    recharged.against(dummy())
        .counter(m, "spellstrike-spent")
        .turn(m, {rechargeSpellstrike(m), spellstrike(m, "Sword", fireRay(m))});
    CHECK(recharged.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30, 15.5, 31.0)));
}

TEST_CASE("magus: Spellstrike damage is one instance for weaknesses")
{
    Character m = magus();
    m.weapons.front().onHit = {dmg(1, 6, 0, DamageType::Fire)};  // flaming rune
    Monster target = dummy();
    target.defenses.weaknesses = {{DamageType::Fire, 5}};
    Scenario s("weak to fire");
    s.against(target).turn(m, {spellstrike(m, "Sword", fireRay(m))});
    // Rune fire and spell fire are combined, so weakness 5 applies once:
    // hit 8.5 + (3.5 + 7) + 5, crit 17 + (7 + 14) + 5.
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30, 24.0, 43.0)));
}

TEST_CASE("magus: Spellstrike with a save spell rolls the save unless the Strike critically fails")
{
    Character m = magus();
    ActionPtr blast = basicSaveSpell(m, "Thunder Blast", 2, Defense::Ref, {dmg(0, 0, 10, DamageType::Electricity)});
    Scenario s("save spell");
    s.against(dummy()).turn(m, {spellstrike(m, "Sword", blast)});

    DegreeDist strikeRoll = resolveCheck(23, 30);
    DegreeDist save = resolveCheck(10, 25);  // Reflex +10 vs spell DC 25, independent of the Strike
    double spell = save[S] * 5 + save[static_cast<int>(Degree::Fail)] * 10 + save[static_cast<int>(Degree::CritFail)] * 20;
    double expected = strikeEV(23, 30) + (1.0 - strikeRoll[static_cast<int>(Degree::CritFail)]) * spell;
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(expected));
}

TEST_CASE("magus: Courageous Anthem adds +1 to the Spellstrike roll and to each damage roll")
{
    Character bard = fighter("Bard");
    Character m = magus();
    Scenario s("anthem spellstrike");
    s.against(dummy()).turn(bard, {courageousAnthem(bard)}).turn(m, {spellstrike(m, "Sword", fireRay(m))});
    // Strike damage 9.5 and spell damage 8 (each roll gets +1), doubled on a crit.
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(24, 30, 17.5, 35.0)));
}

TEST_CASE("magus: Spellstrike only takes 1- or 2-action spells with an attack roll or a save")
{
    Character m = magus();
    CHECK_THROWS_AS(spellstrike(m, "Sword", courageousAnthem(m)), std::invalid_argument);
    ActionPtr threeActions = basicSaveSpell(m, "Big Blast", 3, Defense::Ref, {dmg(0, 0, 10, DamageType::Fire)});
    CHECK_THROWS_AS(spellstrike(m, "Sword", threeActions), std::invalid_argument);
}

namespace
{
// 30 fire on a hit, 60 on a crit: flat damage keeps the resistance math exact.
ActionPtr flameLance(const Character& caster, std::vector<DamageComponent> extra = {})
{
    std::vector<DamageComponent> damage = {dmg(0, 0, 30, DamageType::Fire)};
    damage.insert(damage.end(), extra.begin(), extra.end());
    return spellAttackSpell(caster, "Flame Lance", 2, damage);
}

Character level12Magus()
{
    Character m = magus();
    m.level = 12;
    return m;
}
} // namespace

TEST_CASE("overwhelming spellstrike: only the spell's energy damage ignores resistance")
{
    Character m = level12Magus();
    Monster target = dummy();
    target.defenses.resistances = {{DamageType::Physical, 5}, {DamageType::Fire, 15}};
    Scenario plain("plain");
    plain.against(target).turn(m, {spellstrike(m, "Sword", flameLance(m))});
    Scenario over("overwhelming");
    over.against(target).turn(m, {overwhelmingSpellstrike(m, "Sword", flameLance(m))});

    // The spell's fire faces 15 - 12 = 3 instead of 15 (+12 on a hit or crit);
    // the sword's slashing takes physical resistance 5 either way.
    DegreeDist d = resolveCheck(23, 30);
    RunResult r = over.run();
    CHECK(r.ledger.totalDamage() - plain.run().ledger.totalDamage() == doctest::Approx((d[S] + d[CS]) * 12));
    CHECK(r.ledger.damage.count({"Magus", "Overwhelming Spellstrike (Flame Lance)"}) == 1);
}

TEST_CASE("overwhelming spellstrike: generous ruling when the weapon deals the spell's energy type")
{
    Character m = level12Magus();
    m.weapons.front().onHit = {dmg(0, 0, 4, DamageType::Fire)};  // rune fire
    Monster target = dummy();
    target.defenses.resistances = {{DamageType::Fire, 15}};
    Scenario s("rune + spell fire");
    s.against(target).turn(m, {overwhelmingSpellstrike(m, "Sword", flameLance(m))});
    // The lowered resistance (15 - 12 = 3) covers all the combined fire, the rune's included:
    // hit 8.5 + (4 + 30 - 3), crit 17 + (8 + 60 - 3). The strict ruling would give 27 fire on a hit.
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30, 8.5 + 31.0, 17.0 + 65.0)));
}

TEST_CASE("overwhelming spellstrike: the spell's persistent damage ignores resistance too")
{
    Character m = level12Magus();
    Monster target = dummy();
    target.defenses.resistances = {{DamageType::Fire, 15}};
    Scenario s("persistent");
    s.against(target).turn(m, {overwhelmingSpellstrike(m, "Sword",
                                                       flameLance(m, {persistent(0, 0, 10, DamageType::Fire)}))});
    // One tick of 10 - 3 = 7, or 20 - 3 = 17 after a crit (without the feat: 0 and 5).
    DegreeDist d = resolveCheck(23, 30);
    CHECK(s.run().ledger.damage.at({"(persistent)", "fire"}) == doctest::Approx(d[S] * 7 + d[CS] * 17));
}

TEST_CASE("overwhelming spellstrike: immunities and other damage types are unaffected")
{
    Character m = level12Magus();
    Monster immune = dummy();
    immune.defenses.immunities = {"fire"};
    Scenario s("fire immune");
    s.against(immune).turn(m, {overwhelmingSpellstrike(m, "Sword", flameLance(m))});
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30)));  // only the sword's slashing

    Monster forceResistant = dummy();
    forceResistant.defenses.resistances = {{DamageType::Force, 15}};
    ActionPtr forceLance = spellAttackSpell(m, "Force Lance", 2, {dmg(0, 0, 30, DamageType::Force)});
    Scenario force("force");
    force.against(forceResistant).turn(m, {overwhelmingSpellstrike(m, "Sword", forceLance)});
    CHECK(force.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30, 8.5 + 15.0, 17.0 + 45.0)));
}

TEST_CASE("overwhelming spellstrike: resistance to all damage counts as resistance to the spell's type")
{
    Character m = level12Magus();
    Monster target = dummy();
    target.defenses.resistances = {{DamageType::All, 15}};
    Scenario s("resist all");
    s.against(target).turn(m, {overwhelmingSpellstrike(m, "Sword", flameLance(m))});
    // The spell's fire faces 15 - 12 = 3: 27 on a hit, 57 on a crit. The sword's slashing still faces 15:
    // 1d8+4 never gets through on a hit; on a crit E[max(0, 2(1d8+4) - 15)] = 25/8.
    CHECK(s.run().ledger.totalDamage() == doctest::Approx(strikeEV(23, 30, 27.0, 57.0 + 25.0 / 8.0)));
}
