/**
 * @file    Condition.h
 * @brief   Conditions, and the modifiers and action changes they cause.
 */

#pragma once

#include "Modifier.h"

#include <vector>

/**
 * @brief   The conditions the simulator models.
 * @details Valued conditions use ActiveCondition::value. The name from ToString() is also the entry to put in
 *          DamageDefenses::immunities for an immunity to the condition.
 */
enum class ConditionKind
{
    Frightened,  /**< Status penalty to all checks and DCs. */
    Sickened,    /**< Status penalty to all checks and DCs. */
    Clumsy,      /**< Status penalty to Dexterity-based rolls and DCs (AC, Reflex, ...). */
    Enfeebled,   /**< Status penalty to Strength-based rolls (Strength attacks, Athletics, Strength damage). */
    Drained,     /**< Status penalty to Constitution-based rolls and DCs; on the enemy, also HP loss. */
    Stupefied,   /**< Status penalty to Intelligence-, Wisdom- and Charisma-based rolls and DCs. */
    OffGuard,    /**< -2 circumstance penalty to AC. */
    Prone,       /**< Off-guard, and a -2 circumstance penalty to attack rolls. */
    Grabbed,     /**< Off-guard. */
    Restrained,  /**< Off-guard. */
    Concealed,   /**< Attackers must pass a DC 5 flat check. */
    Hidden,      /**< Attackers must pass a DC 11 flat check. */
    Dazzled,     /**< Everything is concealed to this creature. */
    Slowed,      /**< Loses `value` actions per turn. */
    Stunned,     /**< Loses `value` actions per turn. */
    Quickened,   /**< Gains 1 action per turn. */
    Blinded,     /**< Off-guard (its foes are hidden from it), and its attacks need a DC 11 flat check. */
    Confused,    /**< Off-guard. */
    Paralyzed,   /**< Off-guard, and can't act. */
    Unconscious, /**< Off-guard, a -4 status penalty to AC, Perception and Reflex, and can't act. */
    Fatigued,    /**< -1 status penalty to AC and saves. */
    Encumbered,  /**< Clumsy 1. */
};

/**
 * @brief   Name of a condition as the rules write it ("frightened", "off-guard", ...).
 * @param   [in] kind - The condition.
 * @return  A static string; "?" for an out-of-range value.
 */
const char* ToString(ConditionKind kind);

/**
 * @brief   The modifiers a condition puts on the creature that has it.
 * @details Most valued conditions are status penalties, so they don't stack with each other or with other
 *          status penalties (frightened 1 + clumsy 2 = -2 AC, not -3).
 * @param   [in] kind - The condition.
 * @param   [in] value - Its value (1 for conditions without a value).
 * @return  The modifiers. Empty when @p value is 0 or less, or when the condition gives no modifier
 *          (concealed, slowed, ...).
 */
std::vector<Modifier> ConditionModifiers(ConditionKind kind, int value);

/**
 * @brief   Change to the number of actions per turn (slowed/stunned/quickened, and paralyzed/unconscious).
 * @param   [in] kind - The condition.
 * @param   [in] value - Its value.
 * @return  -value for slowed and stunned, +1 for quickened (when @p value is positive), -3 for paralyzed and
 *          unconscious (no actions), 0 for every other condition.
 */
int ConditionActionDelta(ConditionKind kind, int value);
