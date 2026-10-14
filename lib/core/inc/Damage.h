/**
 * @file    Damage.h
 * @brief   Damage components, damage defenses, and the exact expected damage of a roll.
 */

#pragma once

#include "Dice.h"

#include <compare>
#include <set>
#include <string>
#include <vector>

/**
 * @brief   Damage types.
 * @details The name from ToString() is also the entry to put in DamageDefenses::immunities for an immunity to
 *          the type.
 */
enum class DamageType
{
    Bludgeoning,  /**< Physical: bludgeoning. */
    Piercing,     /**< Physical: piercing. */
    Slashing,     /**< Physical: slashing. */
    Fire,         /**< Energy: fire. */
    Cold,         /**< Energy: cold. */
    Electricity,  /**< Energy: electricity. */
    Acid,         /**< Energy: acid. */
    Sonic,        /**< Energy: sonic. */
    Vitality,     /**< Energy: vitality. */
    Void,         /**< Energy: void. */
    Force,        /**< Energy: force. */
    Spirit,       /**< Spirit. */
    Mental,       /**< Mental. */
    Poison,       /**< Poison. */
    Bleed,        /**< Bleed. */
    Untyped,      /**< No type, like Implosion's: only resistances and weaknesses to all damage apply. */
    // Only meaningful in resistances/weaknesses:
    Physical,  /**< Resistances and weaknesses only: bludgeoning, piercing and slashing. */
    All,       /**< Resistances and weaknesses only: every damage type. */
};

/**
 * @brief   Lowercase name of a damage type ("fire", "slashing", ...).
 * @param   [in] type - The damage type.
 * @return  A static string; "?" for an out-of-range value.
 */
const char* ToString(DamageType type);

/**
 * @brief   Whether a damage type is bludgeoning, piercing or slashing.
 * @param   [in] type - The damage type.
 * @return  True for the three physical types (not for DamageType::Physical itself).
 */
bool IsPhysical(DamageType type);

/**
 * @brief   How a damage component is dealt.
 */
enum class DamageCategory
{
    Normal,      /**< Ordinary damage. */
    Precision,   /**< Removed by precision immunity, otherwise same type as the attack. */
    Persistent,  /**< Becomes a persistent damage condition instead of immediate damage. */
};

/**
 * @brief   One part of a damage roll: `count`d`die` + `flat` of one type, e.g. 2d6+4 slashing.
 */
struct DamageComponent
{
    int count = 0;                                      /**< Number of dice. */
    int die = 0;                                        /**< Die size (4, 6, 8, ...). */
    int flat = 0;                                       /**< Flat amount added to the dice. */
    DamageType type = DamageType::Bludgeoning;          /**< Damage type. */
    DamageCategory category = DamageCategory::Normal;   /**< Normal, precision or persistent. */
    bool doublesOnCrit = true;                          /**< False for deadly/fatal extra dice, crit-only riders, ... */

    /**
     * @brief   This much of the target's resistance to this type is ignored (Overwhelming Spellstrike).
     */
    int ignoredResistance = 0;

    /**
     * @brief   Exact distribution of the component, before doubling or halving.
     * @return  The distribution of `count`d`die` + `flat`.
     */
    Dist Distribution() const { return Dist::Dice(count, die) + flat; }

    /**
     * @brief   Average of the component, before doubling or halving.
     * @return  The mean of `count`d`die` + `flat`.
     */
    double Mean() const { return count * (die + 1) / 2.0 + flat; }

    /**
     * @brief   Member-wise ordering, so states that hold persistent damage can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const DamageComponent&) const = default;
};

/**
 * @brief   Normal damage, e.g. `Damage(2, 6, 4, DamageType::Slashing)` for 2d6+4 slashing.
 * @param   [in] count - Number of dice.
 * @param   [in] die - Die size.
 * @param   [in] flat - Flat amount added to the dice.
 * @param   [in] type - Damage type.
 * @return  The component.
 */
DamageComponent Damage(int count, int die, int flat, DamageType type);

/**
 * @brief   Precision damage (Sneak Attack, Precision edge).
 * @param   [in] count - Number of dice.
 * @param   [in] die - Die size.
 * @param   [in] type - Damage type, normally the same as the attack's.
 * @return  The component.
 */
DamageComponent Precision(int count, int die, DamageType type);

/**
 * @brief   Persistent damage, e.g. `Persistent(1, 10, 0, DamageType::Fire)` for 1d10 persistent fire.
 * @param   [in] count - Number of dice.
 * @param   [in] die - Die size.
 * @param   [in] flat - Flat amount added to the dice.
 * @param   [in] type - Damage type.
 * @return  The component.
 */
DamageComponent Persistent(int count, int die, int flat, DamageType type);

/**
 * @brief   How a damage roll is scaled by the degree of success.
 */
enum class DamageScale
{
    None,    /**< No damage. */
    Half,    /**< Half damage, rounded down (basic save success). */
    Normal,  /**< Full damage. */
    Double,  /**< Double damage (critical hit, basic save critical failure). */
};

/**
 * @brief   A resistance (or, through the Weakness alias, a weakness) of the target.
 */
struct Resistance
{
    DamageType type = DamageType::All;  /**< The damage type it applies to. */
    int value = 0;                      /**< Amount subtracted (or, for a weakness, added). */
    std::vector<std::string> except;    /**< e.g. {"silver"}: ignored against those materials/traits. */
};

/**
 * @brief   A weakness of the target.
 * @details Same shape as Resistance; `except` is unused.
 */
using Weakness = Resistance;

/**
 * @brief   The target's immunities, resistances and weaknesses.
 */
struct DamageDefenses
{
    std::set<std::string> immunities;       /**< Damage type names, "precision", "critical-hits", trait names... */
    std::vector<Resistance> resistances;    /**< Resistances; the highest applicable one counts. */
    std::vector<Weakness> weaknesses;       /**< Weaknesses; the highest applicable one counts. */

    /**
     * @brief   Whether the target is immune to something.
     * @param   [in] immunityName - A damage type name, condition name, trait, "precision" or "critical-hits".
     * @return  True if @p immunityName is in the immunities.
     */
    bool ImmuneTo(const std::string& immunityName) const { return immunities.count(immunityName) > 0; }
};

/**
 * @brief   One damage roll and how it's scaled by the degree of success.
 */
struct DamageRoll
{
    std::vector<DamageComponent> components;  /**< The components rolled together. */
    DamageScale scale = DamageScale::Normal;  /**< How the whole roll is scaled. */
};

/**
 * @brief   Expected damage the target actually takes from one damage roll.
 * @details Applies doubling/halving, then immunity -> weakness -> resistance (per damage type, exactly over
 *          the dice distribution) and the minimum-1 rule. Persistent components are ignored here (the engine
 *          turns them into conditions).
 * @param   [in] components - The damage roll.
 * @param   [in] scale - How the roll is scaled.
 * @param   [in] defenses - The target's immunities, resistances and weaknesses.
 * @param   [in] attackTraits - Traits and materials of the attack; a resistance whose `except` names one of
 *          them is bypassed.
 * @return  The expected damage.
 */
double ExpectedDamage(const std::vector<DamageComponent>& components, DamageScale scale,
                      const DamageDefenses& defenses, const std::set<std::string>& attackTraits = {});

/**
 * @brief   Several damage rolls that land as a single instance (Spellstrike: the Strike and the spell).
 * @details Each roll is doubled/halved on its own, then like types are combined before immunity, weakness and
 *          resistance, so those apply once.
 * @param   [in] rolls - The damage rolls, each with its own scale.
 * @param   [in] defenses - The target's immunities, resistances and weaknesses.
 * @param   [in] attackTraits - Traits and materials of the attack; a resistance whose `except` names one of
 *          them is bypassed.
 * @return  The expected damage of the combined instance.
 */
double ExpectedDamage(const std::vector<DamageRoll>& rolls, const DamageDefenses& defenses,
                      const std::set<std::string>& attackTraits = {});
