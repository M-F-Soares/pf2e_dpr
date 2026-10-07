#pragma once

#include "Dice.h"

#include <compare>
#include <set>
#include <string>
#include <vector>

enum class DamageType
{
    Bludgeoning,
    Piercing,
    Slashing,
    Fire,
    Cold,
    Electricity,
    Acid,
    Sonic,
    Vitality,
    Void,
    Force,
    Spirit,
    Mental,
    Poison,
    Bleed,
    // Only meaningful in resistances/weaknesses:
    Physical,  // bludgeoning, piercing, slashing
    All,
};

const char* toString(DamageType t);
bool isPhysical(DamageType t);

enum class DamageCategory
{
    Normal,
    Precision,   // removed by precision immunity, otherwise same type as the attack
    Persistent,  // becomes a persistent damage condition instead of immediate damage
};

struct DamageComponent
{
    int count = 0;  // number of dice
    int die = 0;    // die size (4, 6, 8, ...)
    int flat = 0;
    DamageType type = DamageType::Bludgeoning;
    DamageCategory category = DamageCategory::Normal;
    bool doublesOnCrit = true;  // false for deadly/fatal extra dice, crit-only riders, ...
    int ignoredResistance = 0;  // this much of the target's resistance to this type is ignored (Overwhelming Spellstrike)

    Dist dist() const { return Dist::dice(count, die) + flat; }
    double mean() const { return count * (die + 1) / 2.0 + flat; }
    auto operator<=>(const DamageComponent&) const = default;
};

DamageComponent dmg(int count, int die, int flat, DamageType type);
DamageComponent precision(int count, int die, DamageType type);
DamageComponent persistent(int count, int die, int flat, DamageType type);

enum class DamageScale
{
    None,
    Half,
    Normal,
    Double,
};

struct Resistance
{
    DamageType type = DamageType::All;
    int value = 0;
    std::vector<std::string> except;  // e.g. {"silver"}: ignored against those materials/traits
};

using Weakness = Resistance;  // same shape; `except` is unused

struct DamageDefenses
{
    std::set<std::string> immunities;  // damage type names, "precision", "critical-hits", trait names...
    std::vector<Resistance> resistances;
    std::vector<Weakness> weaknesses;

    bool immuneTo(const std::string& what) const { return immunities.count(what) > 0; }
};

// One damage roll and how it's scaled by the degree of success.
struct DamageRoll
{
    std::vector<DamageComponent> components;
    DamageScale scale = DamageScale::Normal;
};

// Expected damage the target actually takes from one damage roll, after
// doubling/halving, immunity -> weakness -> resistance (applied per damage
// type, exactly over the dice distribution) and the minimum-1 rule.
// Persistent components are ignored here (the engine turns them into conditions).
double expectedDamage(const std::vector<DamageComponent>& components, DamageScale scale,
                      const DamageDefenses& defenses, const std::set<std::string>& attackTraits = {});

// Several damage rolls that land as a single instance (Spellstrike: the Strike and the spell).
// Each roll is doubled/halved on its own, then like types are combined before
// immunity, weakness and resistance, so those apply once.
double expectedDamage(const std::vector<DamageRoll>& rolls, const DamageDefenses& defenses,
                      const std::set<std::string>& attackTraits = {});
