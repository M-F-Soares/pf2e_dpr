/**
 * @file    ExampleParty.cpp
 * @brief   Sheet numbers of the sample characters and enemy.
 */

#include "ExampleParty.h"

#include "Actions.h"

Character exampleRanger()
{
    Character c;
    c.name = "Ranger";
    c.level = 5;
    c.maxHP = 68;
    c.skills = {{Skill::Intimidation, 9}, {Skill::Athletics, 9}};
    Weapon bow;
    bow.name = "Composite Longbow";
    bow.attack = 14;
    bow.potency = 1;
    bow.dice = 2;
    bow.die = 8;
    bow.type = DamageType::Piercing;
    bow.flat = 1;  // propulsive: half Strength
    bow.ranged = true;
    bow.attackAttribute = Attribute::Dex;
    bow.strengthToDamage = false;
    bow.deadly = 10;
    c.weapons = {bow};
    c.features = {precisionEdge(1)};
    return c;
}

Character exampleBard()
{
    Character c;
    c.name = "Bard";
    c.level = 5;
    c.maxHP = 53;
    c.spellAttack = 11;
    c.spellDC = 21;
    c.keyAttribute = Attribute::Cha;
    c.skills = {{Skill::Intimidation, 13}, {Skill::Deception, 13}, {Skill::Performance, 13}};
    Weapon rapier;
    rapier.name = "Rapier";
    rapier.attack = 11;
    rapier.potency = 1;
    rapier.dice = 1;
    rapier.die = 6;
    rapier.type = DamageType::Piercing;
    rapier.finesse = true;
    rapier.attackAttribute = Attribute::Dex;
    rapier.deadly = 8;
    c.weapons = {rapier};
    return c;
}

Character exampleRogue()
{
    Character c;
    c.name = "Rogue";
    c.level = 5;
    c.maxHP = 63;
    c.skills = {{Skill::Deception, 12}, {Skill::Athletics, 7}, {Skill::Intimidation, 10}};
    Weapon rapier;
    rapier.name = "Rapier";
    rapier.attack = 14;
    rapier.potency = 1;
    rapier.dice = 2;
    rapier.die = 6;
    rapier.type = DamageType::Piercing;
    rapier.flat = 4;  // thief racket: Dexterity to damage
    rapier.finesse = true;
    rapier.attackAttribute = Attribute::Dex;
    rapier.strengthToDamage = false;
    rapier.deadly = 8;
    c.weapons = {rapier};
    c.features = {sneakAttack(2)};
    return c;
}

Character exampleCleric()
{
    Character c;
    c.name = "Cleric";
    c.level = 5;
    c.maxHP = 58;
    c.hpMissing = 0;
    c.spellAttack = 13;
    c.spellDC = 23;
    c.keyAttribute = Attribute::Wis;
    c.skills = {{Skill::Medicine, 12}, {Skill::Religion, 12}};
    Weapon mace;
    mace.name = "Mace";
    mace.attack = 10;
    mace.potency = 1;
    mace.dice = 1;
    mace.die = 6;
    mace.type = DamageType::Bludgeoning;
    mace.flat = 3;
    c.weapons = {mace};
    return c;
}

Monster exampleBrute()
{
    Monster m;
    m.name = "Sample Brute (level 5)";
    m.level = 5;
    m.ac = 22;
    m.fort = 14;
    m.ref = 9;
    m.will = 11;
    m.perception = 11;
    m.hp = 75;
    return m;
}

Character seis()
{
    Character c;
    c.name = "Seis";
    c.level = 15;
    c.maxHP = 200;
    Weapon w;
    w.name = "Fatal Blade";
    w.attack = 23;
    w.potency = 2;
    w.dice = 2;  // striking
    w.die = 8;
    w.type = DamageType::Slashing;
    w.flat = 4;  // 2 weapon + 2 class
    w.fatal = 12;
    w.onHit = {dmg(2, 6, 0, DamageType::Fire)};
    w.onCrit = {persistent(1, 10, 0, DamageType::Fire)};  // flaming rune
    c.weapons = {w};
    return c;
}
