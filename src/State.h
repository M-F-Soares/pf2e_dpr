/**
 * @file    State.h
 * @brief   The battle state of one branch: conditions, modifiers, counters and action economy.
 */

#pragma once

#include "Condition.h"
#include "Damage.h"
#include "Modifier.h"

#include <compare>
#include <map>
#include <string>
#include <vector>

/**
 * @brief   Creature indices inside a BattleState: 0 is the enemy, 1..N the party in scenario order.
 */
constexpr int kEnemy = 0;

/**
 * @brief   When a timed effect ends.
 */
enum class Expiry
{
    Never,        /**< Lasts for the rest of the simulated round. */
    EndOfTurn,    /**< Ends at the end of `creature`'s turn. */
    StartOfTurn,  /**< Ends at the start of `creature`'s turn. */
};

/**
 * @brief   When a condition or modifier ends: at the start or end of one creature's turn, or never.
 */
struct Duration
{
    Expiry when = Expiry::Never;  /**< Which moment ends it. */
    int creature = -1;            /**< Index of the creature whose turn ends it (-1 with Expiry::Never). */

    /**
     * @brief   Member-wise ordering, so states can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const Duration&) const = default;
};

/**
 * @brief   A condition a creature has.
 */
struct ActiveCondition
{
    int value = 0;   /**< Condition value (1 for conditions without a value). */
    Duration until;  /**< When it ends. */

    /**
     * @brief   Member-wise ordering, so states can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const ActiveCondition&) const = default;
};

/**
 * @brief   A modifier on a creature from an effect (spell, Feint, Aid, ...).
 */
struct ActiveModifier
{
    Modifier mod;    /**< The modifier. */
    Duration until;  /**< When it ends. */
    int uses = -1;   /**< -1 = unlimited; otherwise removed after this many rolls it applied to. */

    /**
     * @brief   Member-wise ordering, so states can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const ActiveModifier&) const = default;
};

/**
 * @brief   Everything about one creature that can change later rolls.
 */
struct CreatureState
{
    std::map<ConditionKind, ActiveCondition> conditions;  /**< Current conditions. */
    std::map<std::string, ActiveModifier> modifiers;  /**< Keyed by source (+ opponent), see modifierKey(). */
    std::map<std::string, int> counters;              /**< Free-form flags for features/actions. */
    std::map<DamageType, DamageComponent> persistent;  /**< Persistent damage, at most one per type. */
    int attacksThisTurn = 0;    /**< For the multiple attack penalty. */
    int actionsUsed = 0;        /**< Actions spent this turn. */
    bool reactionUsed = false;  /**< Whether the reaction has been used since the start of the turn. */
    int hpMissing = 0;          /**< Hit Points below maximum (party members, for effective healing). */

    /**
     * @brief   Member-wise ordering, so states can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const CreatureState&) const = default;
};

/**
 * @brief   Everything that can change later rolls.
 * @details Damage dealt is NOT part of the state (it goes to the ledger), which keeps identical branches
 *          mergeable.
 */
struct BattleState
{
    std::vector<CreatureState> creatures;  /**< Indexed by creature: kEnemy, then the party. */

    /**
     * @brief   A creature's state.
     * @param   [in] i - Creature index.
     * @return  The creature's state.
     */
    CreatureState& operator[](int i) { return creatures[i]; }

    /**
     * @brief   A creature's state.
     * @param   [in] i - Creature index.
     * @return  The creature's state.
     */
    const CreatureState& operator[](int i) const { return creatures[i]; }

    /**
     * @brief   Sum of all applicable modifiers (conditions + active effects + `extra`) after stacking rules.
     * @details Sources with limited uses that applied are appended to `used`.
     * @param   [in] creature - Index of the creature rolling, or whose DC it is.
     * @param   [in] q - The roll.
     * @param   [in] extra - Modifiers that come from the roll itself (item bonus, MAP, ...).
     * @param   [out] used - If not null, receives the keys of limited-use modifiers that applied, to pass to
     *          consumeUses() once the roll is made.
     * @return  The net modifier.
     */
    int modifierTotal(int creature, const RollQuery& q, const std::vector<Modifier>& extra = {},
                      std::vector<std::string>* used = nullptr) const;

    /**
     * @brief   Whether a creature counts as off-guard to an attacker (for Sneak Attack, Feint, ...).
     * @param   [in] creature - Index of the creature that might be off-guard.
     * @param   [in] attacker - Index of the attacking creature.
     * @param   [in] attackTags - Tags of the attack; only its Melee/Ranged bits are used.
     * @return  True if a condition or modifier on @p creature grants off-guard against this attack.
     */
    bool isOffGuardTo(int creature, int attacker, Tags attackTags) const;

    /**
     * @brief   The value of a condition on a creature.
     * @param   [in] creature - Creature index.
     * @param   [in] kind - The condition.
     * @return  Its value, or 0 if the creature doesn't have it.
     */
    int conditionValue(int creature, ConditionKind kind) const;

    /**
     * @brief   A counter on a creature.
     * @param   [in] creature - Creature index.
     * @param   [in] name - Counter name.
     * @return  Its value, or 0 if it isn't set.
     */
    int counter(int creature, const std::string& name) const;

    /**
     * @brief   Actions a creature gets each turn.
     * @param   [in] creature - Creature index.
     * @return  3, changed by slowed, stunned and quickened, and never below 0.
     */
    int actionsPerTurn(int creature) const;

    /**
     * @brief   Actions a creature has left this turn.
     * @param   [in] creature - Creature index.
     * @return  actionsPerTurn() minus the actions already used.
     */
    int actionsLeft(int creature) const { return actionsPerTurn(creature) - creatures[creature].actionsUsed; }

    /**
     * @brief   Gives a creature a condition.
     * @details Same condition doesn't stack: the higher value wins.
     * @param   [in] creature - Creature index.
     * @param   [in] kind - The condition.
     * @param   [in] value - Its value.
     * @param   [in] until - When it ends. Only replaced when the new value is higher.
     */
    void addCondition(int creature, ConditionKind kind, int value, Duration until);

    /**
     * @brief   Gives a creature a modifier.
     * @details Same source doesn't stack: the stronger one wins.
     * @param   [in] creature - Creature index.
     * @param   [in] m - The modifier. It replaces one with the same key (modifierKey()) when its value is at
     *          least as large in absolute terms.
     */
    void addModifier(int creature, const ActiveModifier& m);

    /**
     * @brief   Gives a creature persistent damage.
     * @details Same persistent damage type doesn't stack: the higher amount wins.
     * @param   [in] creature - Creature index.
     * @param   [in] c - The damage, compared to the current one of the same type by its average.
     */
    void addPersistent(int creature, const DamageComponent& c);

    /**
     * @brief   Counts one use of limited-use modifiers, and removes those that have none left.
     * @param   [in] creature - Creature index.
     * @param   [in] sources - Modifier keys, as returned through modifierTotal()'s `used`.
     */
    void consumeUses(int creature, const std::vector<std::string>& sources);

    /**
     * @brief   Starts a creature's turn.
     * @details Resets its attack count, actions and reaction, and ends the effects that last until the start
     *          of its turn.
     * @param   [in] creature - Creature index.
     */
    void startTurn(int creature);

    /**
     * @brief   Ends a creature's turn.
     * @details Resets its attack count and ends the effects that last until the end of its turn.
     * @param   [in] creature - Creature index.
     */
    void endTurn(int creature);

    /**
     * @brief   Member-wise ordering, so identical branches can be found and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const BattleState&) const = default;

private:
    /**
     * @brief   Ends the effects that last until this moment.
     * @details Removes every condition and modifier, on any creature, whose duration is exactly (@p when,
     *          @p creature).
     * @param   [in] when - Start or end of turn.
     * @param   [in] creature - Index of the creature whose turn it is.
     */
    void expire(Expiry when, int creature);
};

/**
 * @brief   Key of a modifier in CreatureState::modifiers.
 * @param   [in] m - The modifier.
 * @return  Its source, followed by `@` and the creature index if it only applies against one creature, so the
 *          same effect against different creatures is kept separately.
 */
std::string modifierKey(const Modifier& m);
