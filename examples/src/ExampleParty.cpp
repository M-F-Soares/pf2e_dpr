/**
 * @file    ExampleParty.cpp
 * @brief   Sheet numbers of the sample characters and enemy.
 */

#include "ExampleParty.h"

#include "Actions.h"

Character ExampleRanger()
{
    Character ranger;
    ranger.name = "Ranger";
    ranger.level = 5;
    ranger.maxHP = 68;
    ranger.skills = {{Skill::Intimidation, 9}, {Skill::Athletics, 9}};
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
    ranger.weapons = {bow};
    ranger.features = {PrecisionEdge(1)};
    return ranger;
}

Character ExampleBard()
{
    Character bard;
    bard.name = "Bard";
    bard.level = 5;
    bard.maxHP = 53;
    bard.spellAttack = 11;
    bard.spellDC = 21;
    bard.keyAttribute = Attribute::Cha;
    bard.skills = {{Skill::Intimidation, 13}, {Skill::Deception, 13}, {Skill::Performance, 13}};
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
    bard.weapons = {rapier};
    return bard;
}

Character ExampleRogue()
{
    Character rogue;
    rogue.name = "Rogue";
    rogue.level = 5;
    rogue.maxHP = 63;
    rogue.skills = {{Skill::Deception, 12}, {Skill::Athletics, 7}, {Skill::Intimidation, 10}};
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
    rogue.weapons = {rapier};
    rogue.features = {SneakAttack(2)};
    return rogue;
}

Character ExampleCleric()
{
    Character cleric;
    cleric.name = "Cleric";
    cleric.level = 5;
    cleric.maxHP = 58;
    cleric.hpMissing = 0;
    cleric.spellAttack = 13;
    cleric.spellDC = 23;
    cleric.keyAttribute = Attribute::Wis;
    cleric.skills = {{Skill::Medicine, 12}, {Skill::Religion, 12}};
    Weapon mace;
    mace.name = "Mace";
    mace.attack = 10;
    mace.potency = 1;
    mace.dice = 1;
    mace.die = 6;
    mace.type = DamageType::Bludgeoning;
    mace.flat = 3;
    cleric.weapons = {mace};
    return cleric;
}

Monster ExampleBrute()
{
    Monster brute;
    brute.name = "Sample Brute (level 5)";
    brute.level = 5;
    brute.ac = 22;
    brute.fort = 14;
    brute.ref = 9;
    brute.will = 11;
    brute.perception = 11;
    brute.hp = 75;
    return brute;
}

Character Seis()
{
    Character character;
    character.name = "Seis";
    character.level = 15;
    character.maxHP = 200;
    Weapon blade;
    blade.name = "Fatal Blade";
    blade.attack = 23;
    blade.potency = 2;
    blade.dice = 2;  // striking
    blade.die = 8;
    blade.type = DamageType::Slashing;
    blade.flat = 4;  // 2 weapon + 2 class
    blade.fatal = 12;
    blade.onHit = {Damage(2, 6, 0, DamageType::Fire)};
    blade.onCrit = {Persistent(1, 10, 0, DamageType::Fire)};  // flaming rune
    character.weapons = {blade};
    return character;
}
