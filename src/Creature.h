#pragma once

#include "Damage.h"
#include "Modifier.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class Skill
{
    Acrobatics,
    Arcana,
    Athletics,
    Crafting,
    Deception,
    Diplomacy,
    Intimidation,
    Medicine,
    Nature,
    Occultism,
    Performance,
    Religion,
    Society,
    Stealth,
    Survival,
    Thievery,
};

Attribute skillAttribute(Skill s);

enum class Defense
{
    AC,
    Fort,
    Ref,
    Will,
    Perception,
};

// Tags for the defender rolling a save/Perception check, and for the defense used as a DC.
Tags defenseRollTags(Defense d);
Tags defenseDCTags(Defense d);

struct EffectCtx;

// A passive class feature that changes Strike damage (Sneak Attack, Precision edge...).
// It runs once per damaging outcome and may append damage components or set counters.
struct Feature
{
    std::string name;
    std::function<void(EffectCtx&)> onStrikeDamage;
};

// Numbers are the totals from the character sheet.
struct Weapon
{
    std::string name;
    int attack = 0;   // total attack bonus, potency rune included
    int potency = 0;  // item bonus inside `attack` (so it doesn't stack with other item bonuses)
    int dice = 1;     // 1 + striking rune
    int die = 6;
    DamageType type = DamageType::Slashing;
    int flat = 0;     // attribute + weapon specialization + other always-on flat damage
    bool ranged = false;
    bool agile = false;
    bool finesse = false;
    Attribute attackAttribute = Attribute::Str;
    bool strengthToDamage = true;  // tags the damage roll as Str-based (enfeebled)
    int deadly = 0;                // deadly die size (0 = none)
    int fatal = 0;                 // fatal die size (0 = none)
    std::vector<DamageComponent> onHit;  // property runes, e.g. flaming 1d6 fire
    std::vector<DamageComponent> onCrit; // crit-only riders, e.g. flaming 1d10 persistent fire
    std::set<std::string> traits;        // materials/traits that bypass resistances ("silver", "cold-iron")
};

struct Character
{
    std::string name;
    int level = 1;
    int maxHP = 10;
    int hpMissing = 0;  // damage already taken when the round starts (for effective healing)
    int spellAttack = 0;
    int spellDC = 10;
    int classDC = 10;
    Attribute keyAttribute = Attribute::None;  // spellcasting / class DC attribute
    std::map<Skill, int> skills;
    std::vector<Weapon> weapons;
    std::vector<Feature> features;

    int skill(Skill s) const;
    const Weapon& weapon(const std::string& weaponName) const;
};

struct Monster
{
    std::string name;
    int level = 1;
    int ac = 10;
    int fort = 0;
    int ref = 0;
    int will = 0;
    int perception = 0;
    int hp = 0;
    std::set<std::string> traits;
    DamageDefenses defenses;  // immunities may also name traits ("mental", "fear") or conditions ("frightened")

    int defenseValue(Defense d) const;  // AC, or the save/Perception modifier
    int defenseDC(Defense d) const;     // AC, or 10 + modifier
};
