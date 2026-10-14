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
constexpr int kEnemyIndex = 0;

/**
 * @brief   When a timed effect ends.
 */
enum class Expiry
{
    Never,        /**< Lasts for the rest of the simulated round. */
    EndOfTurn,    /**< Ends at the end of `creatureIndex`'s turn. */
    StartOfTurn,  /**< Ends at the start of `creatureIndex`'s turn. */
};

/**
 * @brief   When a condition or modifier ends: at the start or end of one creature's turn, or never.
 */
struct Duration
{
    Expiry when = Expiry::Never;  /**< Which moment ends it. */
    int creatureIndex = -1;       /**< Index of the creature whose turn ends it (-1 with Expiry::Never). */

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
 * @brief   A weakness an effect gave a creature.
 */
struct ActiveWeakness
{
    int value = 0;   /**< Extra damage taken. */
    Duration until;  /**< When it ends. */

    /**
     * @brief   Member-wise ordering, so states can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const ActiveWeakness&) const = default;
};

/**
 * @brief   Everything about one creature that can change later rolls.
 */
struct CreatureState
{
    std::map<ConditionKind, ActiveCondition> conditions;  /**< Current conditions. */
    std::map<std::string, ActiveModifier> modifiers;  /**< Keyed by source (+ opponent), see ModifierKey(). */
    std::map<std::string, int> counters;              /**< Free-form flags for features/actions. */
    std::map<DamageType, DamageComponent> persistent;  /**< Persistent damage, at most one per type. */
    std::map<DamageType, ActiveWeakness> weaknesses;   /**< Weaknesses effects gave it, at most one per type. */
    int attacksThisTurn = 0;    /**< For the multiple attack penalty. */
    int actionsUsed = 0;        /**< Actions spent this turn. */
    bool reactionUsed = false;  /**< Whether the reaction has been used since the start of the turn. */
    int hpMissing = 0;          /**< Hit Points below maximum (party members, for effective healing). */
    int tempHp = 0;             /**< Temporary Hit Points (they don't stack: the highest amount stays). */

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
    std::vector<CreatureState> creatures;  /**< Indexed by creature: kEnemyIndex, then the party. */

    /**
     * @brief   A creature's state.
     * @param   [in] creatureIndex - Creature index.
     * @return  The creature's state.
     */
    CreatureState& operator[](int creatureIndex) { return creatures[creatureIndex]; }

    /**
     * @brief   A creature's state.
     * @param   [in] creatureIndex - Creature index.
     * @return  The creature's state.
     */
    const CreatureState& operator[](int creatureIndex) const { return creatures[creatureIndex]; }

    /**
     * @brief   Sum of all applicable modifiers (conditions + active effects + `extra`) after stacking rules.
     * @details Sources with limited uses that applied are appended to `used`.
     * @param   [in] creatureIndex - Index of the creature rolling, or whose DC it is.
     * @param   [in] query - The roll.
     * @param   [in] extra - Modifiers that come from the roll itself (item bonus, MAP, ...).
     * @param   [out] used - If not null, receives the keys of limited-use modifiers that applied, to pass to
     *          ConsumeUses() once the roll is made.
     * @return  The net modifier.
     */
    int ModifierTotal(int creatureIndex, const RollQuery& query, const std::vector<Modifier>& extra = {},
                      std::vector<std::string>* used = nullptr) const;

    /**
     * @brief   Whether a creature counts as off-guard to an attacker (for Sneak Attack, Feint, ...).
     * @param   [in] creatureIndex - Index of the creature that might be off-guard.
     * @param   [in] attackerIndex - Index of the attacking creature.
     * @param   [in] attackTags - Tags of the attack; only its Melee/Ranged bits are used.
     * @return  True if a condition or modifier on @p creatureIndex grants off-guard against this attack.
     */
    bool IsOffGuardTo(int creatureIndex, int attackerIndex, Tags attackTags) const;

    /**
     * @brief   The value of a condition on a creature.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] kind - The condition.
     * @return  Its value, or 0 if the creature doesn't have it.
     */
    int ConditionValue(int creatureIndex, ConditionKind kind) const;

    /**
     * @brief   A counter on a creature.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] counterName - Counter name.
     * @return  Its value, or 0 if it isn't set.
     */
    int Counter(int creatureIndex, const std::string& counterName) const;

    /**
     * @brief   Actions a creature gets each turn.
     * @param   [in] creatureIndex - Creature index.
     * @return  3, changed by slowed, stunned and quickened, and never below 0.
     */
    int ActionsPerTurn(int creatureIndex) const;

    /**
     * @brief   Actions a creature has left this turn.
     * @param   [in] creatureIndex - Creature index.
     * @return  ActionsPerTurn() minus the actions already used.
     */
    int ActionsLeft(int creatureIndex) const
    {
        return ActionsPerTurn(creatureIndex) - creatures[creatureIndex].actionsUsed;
    }

    /**
     * @brief   Gives a creature a condition.
     * @details Same condition doesn't stack: the higher value wins.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] kind - The condition.
     * @param   [in] value - Its value.
     * @param   [in] until - When it ends. Only replaced when the new value is higher.
     */
    void AddCondition(int creatureIndex, ConditionKind kind, int value, Duration until);

    /**
     * @brief   Gives a creature a modifier.
     * @details Same source doesn't stack: the stronger one wins.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] modifier - The modifier. It replaces one with the same key (ModifierKey()) when its value is at
     *          least as large in absolute terms.
     */
    void AddModifier(int creatureIndex, const ActiveModifier& modifier);

    /**
     * @brief   Gives a creature persistent damage.
     * @details Same persistent damage type doesn't stack: the higher amount wins.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] component - The damage, compared to the current one of the same type by its average.
     */
    void AddPersistent(int creatureIndex, const DamageComponent& component);

    /**
     * @brief   Gives a creature a weakness until a duration ends.
     * @details Weaknesses to the same type don't stack: the higher value stays.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] type - The damage type.
     * @param   [in] value - Extra damage taken.
     * @param   [in] until - When it ends.
     */
    void AddWeakness(int creatureIndex, DamageType type, int value, Duration until);

    /**
     * @brief   A creature's defenses, with the weaknesses effects gave it.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] base - Its own immunities, resistances and weaknesses.
     * @return  @p base, plus a weakness for each one in CreatureState::weaknesses (the highest applies).
     */
    DamageDefenses Defenses(int creatureIndex, const DamageDefenses& base) const;

    /**
     * @brief   Counts one use of limited-use modifiers, and removes those that have none left.
     * @param   [in] creatureIndex - Creature index.
     * @param   [in] sources - Modifier keys, as returned through ModifierTotal()'s `used`.
     */
    void ConsumeUses(int creatureIndex, const std::vector<std::string>& sources);

    /**
     * @brief   Starts a creature's turn.
     * @details Resets its attack count, actions and reaction, and ends the effects that last until the start
     *          of its turn.
     * @param   [in] creatureIndex - Creature index.
     */
    void StartTurn(int creatureIndex);

    /**
     * @brief   Ends a creature's turn.
     * @details Resets its attack count and ends the effects that last until the end of its turn.
     * @param   [in] creatureIndex - Creature index.
     */
    void EndTurn(int creatureIndex);

    /**
     * @brief   Member-wise ordering, so identical branches can be found and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const BattleState&) const = default;

private:
    /**
     * @brief   Ends the effects that last until this moment.
     * @details Removes every condition and modifier, on any creature, whose duration is exactly (@p when,
     *          @p creatureIndex).
     * @param   [in] when - Start or end of turn.
     * @param   [in] creatureIndex - Index of the creature whose turn it is.
     */
    void Expire(Expiry when, int creatureIndex);
};

/**
 * @brief   Key of a modifier in CreatureState::modifiers.
 * @param   [in] modifier - The modifier.
 * @return  Its source, followed by `@` and the creature index if it only applies against one creature, so the
 *          same effect against different creatures is kept separately.
 */
std::string ModifierKey(const Modifier& modifier);
