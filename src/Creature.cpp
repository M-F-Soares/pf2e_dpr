#include "Creature.h"

#include <stdexcept>

Attribute skillAttribute(Skill s)
{
    switch (s)
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

Tags defenseRollTags(Defense d)
{
    switch (d)
    {
    case Defense::AC: return Tag::AC | Tag::DC | Tag::Dex;
    case Defense::Fort: return Tag::Save | Tag::Check | Tag::Fort | Tag::Con;
    case Defense::Ref: return Tag::Save | Tag::Check | Tag::Ref | Tag::Dex;
    case Defense::Will: return Tag::Save | Tag::Check | Tag::Will | Tag::Wis;
    case Defense::Perception: return Tag::Perception | Tag::Check | Tag::Wis;
    }
    return 0;
}

Tags defenseDCTags(Defense d)
{
    switch (d)
    {
    case Defense::AC: return Tag::AC | Tag::DC | Tag::Dex;
    case Defense::Fort: return Tag::Fort | Tag::DC | Tag::Con;
    case Defense::Ref: return Tag::Ref | Tag::DC | Tag::Dex;
    case Defense::Will: return Tag::Will | Tag::DC | Tag::Wis;
    case Defense::Perception: return Tag::Perception | Tag::DC | Tag::Wis;
    }
    return 0;
}

int Character::skill(Skill s) const
{
    auto it = skills.find(s);
    if (it == skills.end())
        throw std::runtime_error(name + " has no modifier set for that skill");
    return it->second;
}

const Weapon& Character::weapon(const std::string& weaponName) const
{
    for (const auto& w : weapons)
        if (w.name == weaponName)
            return w;
    throw std::runtime_error(name + " has no weapon named " + weaponName);
}

int Monster::defenseValue(Defense d) const
{
    switch (d)
    {
    case Defense::AC: return ac;
    case Defense::Fort: return fort;
    case Defense::Ref: return ref;
    case Defense::Will: return will;
    case Defense::Perception: return perception;
    }
    return 0;
}

int Monster::defenseDC(Defense d) const
{
    return d == Defense::AC ? ac : 10 + defenseValue(d);
}
