/**
 * @file    Damage.cpp
 * @brief   Exact expected damage after immunities, weaknesses and resistances.
 */

#include "Damage.h"

#include <algorithm>
#include <map>

const char* ToString(DamageType type)
{
    switch (type)
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
    case DamageType::Untyped: return "untyped";
    case DamageType::Physical: return "physical";
    case DamageType::All: return "all";
    }
    return "?";
}

bool IsPhysical(DamageType type)
{
    return type == DamageType::Bludgeoning || type == DamageType::Piercing || type == DamageType::Slashing;
}

DamageComponent Damage(int count, int die, int flat, DamageType type)
{
    return DamageComponent{count, die, flat, type};
}

DamageComponent Precision(int count, int die, DamageType type)
{
    return DamageComponent{count, die, 0, type, DamageCategory::Precision};
}

DamageComponent Persistent(int count, int die, int flat, DamageType type)
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
bool Covers(DamageType entry, DamageType actual)
{
    return entry == actual || entry == DamageType::All || (entry == DamageType::Physical && IsPhysical(actual));
}

/**
 * @brief   The highest resistance (or weakness) that applies to a damage type.
 * @param   [in] list - The resistances or weaknesses.
 * @param   [in] type - The type of the damage.
 * @param   [in] attackTraits - Traits and materials of the attack; entries whose `except` names one are
 *          skipped.
 * @return  The highest value, or 0 if none applies.
 */
int HighestApplicable(const std::vector<Resistance>& list, DamageType type, const std::set<std::string>& attackTraits)
{
    int best = 0;
    for (const auto& resistance : list)
    {
        if (!Covers(resistance.type, type))
            continue;
        bool bypassed = std::any_of(resistance.except.begin(), resistance.except.end(),
                                    [&](const std::string& trait) { return attackTraits.count(trait) > 0; });
        if (!bypassed)
            best = std::max(best, resistance.value);
    }
    return best;
}
} // namespace

double ExpectedDamage(const std::vector<DamageComponent>& components, DamageScale scale,
                      const DamageDefenses& defenses, const std::set<std::string>& attackTraits)
{
    return ExpectedDamage(std::vector<DamageRoll>{DamageRoll{components, scale}}, defenses, attackTraits);
}

double ExpectedDamage(const std::vector<DamageRoll>& rolls, const DamageDefenses& defenses,
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
        for (const auto& component : roll.components)
        {
            if (component.category == DamageCategory::Persistent)
                continue;
            if (component.category == DamageCategory::Precision && defenses.ImmuneTo("precision"))
                continue;
            Dist amount = component.Distribution();
            if (roll.scale == DamageScale::Double && component.doublesOnCrit)
                amount = amount.Doubled();
            if (std::find(order.begin(), order.end(), component.type) == order.end())
                order.push_back(component.type);
            rollByType[component.type] += amount;
            ignoredResistance[component.type] =
                std::max(ignoredResistance[component.type], component.ignoredResistance);
        }
        for (auto& [type, amount] : rollByType)
            byType[type] += roll.scale == DamageScale::Half ? amount.Halved() : amount;
    }
    if (order.empty())
        return 0.0;

    // Penalties never reduce a damaging effect below 1 damage.
    byType[order.front()] = byType[order.front()].AtLeast(1);

    double total = 0.0;
    for (DamageType type : order)
    {
        if (defenses.ImmuneTo(ToString(type)))
            continue;
        int weakness = HighestApplicable(defenses.weaknesses, type, {});
        // Ignored resistance lowers the resistance for this type's whole combined amount, even the
        // part from another roll (Overwhelming Spellstrike "generous" ruling, see README "Rulings").
        int resistance =
            std::max(0, HighestApplicable(defenses.resistances, type, attackTraits) - ignoredResistance[type]);
        total += byType[type].Expected([&](int amount) {
            if (amount <= 0)
                return 0.0;
            return static_cast<double>(std::max(0, amount + weakness - resistance));
        });
    }
    return total;
}
