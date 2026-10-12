/**
 * @file    Creature.cpp
 * @brief   Skill attributes, defense tags, and character and monster lookups.
 */

#include "Creature.h"

#include <stdexcept>

const char* ToString(Skill skill)
{
    switch (skill)
    {
    case Skill::Acrobatics: return "Acrobatics";
    case Skill::Arcana: return "Arcana";
    case Skill::Athletics: return "Athletics";
    case Skill::Crafting: return "Crafting";
    case Skill::Deception: return "Deception";
    case Skill::Diplomacy: return "Diplomacy";
    case Skill::Intimidation: return "Intimidation";
    case Skill::Medicine: return "Medicine";
    case Skill::Nature: return "Nature";
    case Skill::Occultism: return "Occultism";
    case Skill::Performance: return "Performance";
    case Skill::Religion: return "Religion";
    case Skill::Society: return "Society";
    case Skill::Stealth: return "Stealth";
    case Skill::Survival: return "Survival";
    case Skill::Thievery: return "Thievery";
    }
    return "?";
}

const char* ToString(Defense defense)
{
    switch (defense)
    {
    case Defense::AC: return "AC";
    case Defense::Fort: return "Fort";
    case Defense::Ref: return "Ref";
    case Defense::Will: return "Will";
    case Defense::Perception: return "Perception";
    }
    return "?";
}

Attribute SkillAttribute(Skill skill)
{
    switch (skill)
    {
    case Skill::Athletics: return Attribute::Str;
    case Skill::Acrobatics:
    case Skill::Stealth:
    case Skill::Thievery: return Attribute::Dex;
    case Skill::Arcana:
    case Skill::Crafting:
    case Skill::Occultism:
    case Skill::Society: return Attribute::Int;
    case Skill::Medicine:
    case Skill::Nature:
    case Skill::Religion:
    case Skill::Survival: return Attribute::Wis;
    case Skill::Deception:
    case Skill::Diplomacy:
    case Skill::Intimidation:
    case Skill::Performance: return Attribute::Cha;
    }
    return Attribute::None;
}

Tags DefenseRollTags(Defense defense)
{
    switch (defense)
    {
    case Defense::AC: return Tag::AC | Tag::DC | Tag::Dex;
    case Defense::Fort: return Tag::Save | Tag::Check | Tag::Fort | Tag::Con;
    case Defense::Ref: return Tag::Save | Tag::Check | Tag::Ref | Tag::Dex;
    case Defense::Will: return Tag::Save | Tag::Check | Tag::Will | Tag::Wis;
    case Defense::Perception: return Tag::Perception | Tag::Check | Tag::Wis;
    }
    return 0;
}

Tags DefenseDCTags(Defense defense)
{
    switch (defense)
    {
    case Defense::AC: return Tag::AC | Tag::DC | Tag::Dex;
    case Defense::Fort: return Tag::Fort | Tag::DC | Tag::Con;
    case Defense::Ref: return Tag::Ref | Tag::DC | Tag::Dex;
    case Defense::Will: return Tag::Will | Tag::DC | Tag::Wis;
    case Defense::Perception: return Tag::Perception | Tag::DC | Tag::Wis;
    }
    return 0;
}

int Character::SkillModifier(Skill skill) const
{
    auto it = skills.find(skill);
    if (it == skills.end())
        throw std::runtime_error(name + " has no modifier set for that skill");
    return it->second;
}

const Weapon& Character::GetWeapon(const std::string& weaponName) const
{
    for (const auto& weapon : weapons)
        if (weapon.name == weaponName)
            return weapon;
    throw std::runtime_error(name + " has no weapon named " + weaponName);
}

int Monster::DefenseValue(Defense defense) const
{
    switch (defense)
    {
    case Defense::AC: return ac;
    case Defense::Fort: return fort;
    case Defense::Ref: return ref;
    case Defense::Will: return will;
    case Defense::Perception: return perception;
    }
    return 0;
}

int Monster::DefenseDC(Defense defense) const
{
    return defense == Defense::AC ? ac : 10 + DefenseValue(defense);
}
