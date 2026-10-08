/**
 * @file    Damage.cpp
 * @brief   Exact expected damage after immunities, weaknesses and resistances.
 */

#include "Damage.h"

#include <algorithm>
#include <map>

const char* toString(DamageType t)
{
    switch (t)
    {
    case DamageType::Bludgeoning: return "bludgeoning";
    case DamageType::Piercing: return "piercing";
    case DamageType::Slashing: return "slashing";
    case DamageType::Fire: return "fire";
    case DamageType::Cold: return "cold";
    case DamageType::Electricity: return "electricity";
    case DamageType::Acid: return "acid";
    case DamageType::Sonic: return "sonic";
    case DamageType::Vitality: return "vitality";
    case DamageType::Void: return "void";
    case DamageType::Force: return "force";
    case DamageType::Spirit: return "spirit";
    case DamageType::Mental: return "mental";
    case DamageType::Poison: return "poison";
    case DamageType::Bleed: return "bleed";
    case DamageType::Physical: return "physical";
    case DamageType::All: return "all";
    }
    return "?";
}

bool isPhysical(DamageType t)
{
    return t == DamageType::Bludgeoning || t == DamageType::Piercing || t == DamageType::Slashing;
}

DamageComponent dmg(int count, int die, int flat, DamageType type)
{
    return DamageComponent{count, die, flat, type};
}

DamageComponent precision(int count, int die, DamageType type)
{
    return DamageComponent{count, die, 0, type, DamageCategory::Precision};
}

DamageComponent persistent(int count, int die, int flat, DamageType type)
{
    return DamageComponent{count, die, flat, type, DamageCategory::Persistent};
}

namespace
{
/**
 * @brief   Whether a resistance or weakness entry applies to a damage type.
 * @param   [in] entry - The type the resistance or weakness names (may be Physical or All).
 * @param   [in] actual - The type of the damage.
 * @return  True if @p entry is @p actual, All, or Physical for a physical type.
 */
bool covers(DamageType entry, DamageType actual)
{
    return entry == actual || entry == DamageType::All || (entry == DamageType::Physical && isPhysical(actual));
}

/**
 * @brief   The highest resistance (or weakness) that applies to a damage type.
 * @param   [in] list - The resistances or weaknesses.
 * @param   [in] type - The type of the damage.
 * @param   [in] attackTraits - Traits and materials of the attack; entries whose `except` names one are
 *          skipped.
 * @return  The highest value, or 0 if none applies.
 */
int highestApplicable(const std::vector<Resistance>& list, DamageType type, const std::set<std::string>& attackTraits)
{
    int best = 0;
    for (const auto& r : list)
    {
        if (!covers(r.type, type))
            continue;
        bool bypassed = std::any_of(r.except.begin(), r.except.end(),
                                    [&](const std::string& e) { return attackTraits.count(e) > 0; });
        if (!bypassed)
            best = std::max(best, r.value);
    }
    return best;
}
} // namespace

double expectedDamage(const std::vector<DamageComponent>& components, DamageScale scale,
                      const DamageDefenses& defenses, const std::set<std::string>& attackTraits)
{
    return expectedDamage(std::vector<DamageRoll>{DamageRoll{components, scale}}, defenses, attackTraits);
}

double expectedDamage(const std::vector<DamageRoll>& rolls, const DamageDefenses& defenses,
                      const std::set<std::string>& attackTraits)
{
    // Sum each roll per damage type (insertion order kept so the first type is the "main" one),
    // scale it, then combine like types across rolls.
    std::vector<DamageType> order;
    std::map<DamageType, Dist> byType;
    std::map<DamageType, int> ignoredResistance;
    for (const DamageRoll& roll : rolls)
    {
        if (roll.scale == DamageScale::None)
            continue;
        std::map<DamageType, Dist> rollByType;
        for (const auto& c : roll.components)
        {
            if (c.category == DamageCategory::Persistent)
                continue;
            if (c.category == DamageCategory::Precision && defenses.immuneTo("precision"))
                continue;
            Dist d = c.dist();
            if (roll.scale == DamageScale::Double && c.doublesOnCrit)
                d = d.doubled();
            if (std::find(order.begin(), order.end(), c.type) == order.end())
                order.push_back(c.type);
            rollByType[c.type] += d;
            ignoredResistance[c.type] = std::max(ignoredResistance[c.type], c.ignoredResistance);
        }
        for (auto& [type, d] : rollByType)
            byType[type] += roll.scale == DamageScale::Half ? d.halved() : d;
    }
    if (order.empty())
        return 0.0;

    // Penalties never reduce a damaging effect below 1 damage.
    byType[order.front()] = byType[order.front()].atLeast(1);

    double total = 0.0;
    for (DamageType type : order)
    {
        if (defenses.immuneTo(toString(type)))
            continue;
        int weakness = highestApplicable(defenses.weaknesses, type, {});
        // Ignored resistance lowers the resistance for this type's whole combined amount, even the
        // part from another roll (Overwhelming Spellstrike "generous" ruling, see README "Rulings").
        int resistance =
            std::max(0, highestApplicable(defenses.resistances, type, attackTraits) - ignoredResistance[type]);
        total += byType[type].expected([&](int x) {
            if (x <= 0)
                return 0.0;
            return static_cast<double>(std::max(0, x + weakness - resistance));
        });
    }
    return total;
}
