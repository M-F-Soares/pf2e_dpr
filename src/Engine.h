/**
 * @file    Engine.h
 * @brief   The branching simulation of a round, and the ledger of expected results.
 */

#pragma once

#include "Action.h"

#include <compare>
#include <map>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief   Options for a run.
 */
struct Settings
{
    /**
     * @brief   How many end-of-turn ticks of persistent damage to count.
     * @details Each tick after the first happens only if the DC 15 recovery check failed. 1 = only the tick
     *          at the end of the enemy's next turn.
     */
    int persistentTicks = 1;
    bool mergeStates = true;  /**< Merge identical branches (never changes results, only speed). */
};

/**
 * @brief   One line of the ledger: who did it, and with what.
 */
struct LedgerKey
{
    std::string who;   /**< Character name, or "(persistent)" for persistent damage. */
    std::string what;  /**< Action name, or damage type for persistent damage. */

    /**
     * @brief   Ordered by `who`, then `what`, so reports group lines by character.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const LedgerKey&) const = default;
};

/**
 * @brief   Expected values, accumulated across all branches (linearity of expectation).
 */
struct Ledger
{
    std::map<LedgerKey, double> damage;         /**< Expected damage dealt to the enemy. */
    std::map<LedgerKey, double> healRaw;        /**< Expected healing rolled, summed over targets. */
    std::map<LedgerKey, double> healEffective;  /**< Capped by the target's missing HP. */
    std::map<LedgerKey, double> skipped;        /**< Probability the action couldn't be taken. */

    /**
     * @brief   Expected damage of the whole round.
     * @return  The sum of every damage line.
     */
    double totalDamage() const;

    /**
     * @brief   Expected healing rolled in the whole round.
     * @return  The sum of every raw healing line.
     */
    double totalHealRaw() const;

    /**
     * @brief   Expected healing that restored missing HP in the whole round.
     * @return  The sum of every effective healing line.
     */
    double totalHealEffective() const;

    /**
     * @brief   Expected damage credited to one character.
     * @param   [in] who - The character's name.
     * @return  The sum of the damage lines whose LedgerKey::who is @p who.
     */
    double damageBy(const std::string& who) const;
};

/**
 * @brief   One character's turn: the steps they take, in order.
 */
struct Turn
{
    std::string actor;        /**< Name of the party member taking the turn. */
    std::vector<Step> steps;  /**< The actions, fixed or chosen per branch. */
};

/**
 * @brief   Probability picture of the enemy right before someone's turn.
 */
struct Snapshot
{
    std::string beforeTurnOf;  /**< Name of the character whose turn is about to start. */
    std::map<ConditionKind, double> enemyConditionChance;  /**< Chance the enemy has each condition. */
    std::map<ConditionKind, double> enemyConditionMean;    /**< Expected value of each condition (0 when absent). */
    double enemyOffGuardChance = 0.0;  /**< Off-guard to this actor's melee attacks. */
    std::size_t branches = 0;          /**< Number of distinct branches at this point. */
};

/**
 * @brief   The possible states at one point of the round, each with its probability.
 */
using Branches = std::vector<std::pair<BattleState, double>>;

/**
 * @brief   Everything a run produces.
 */
struct RunResult
{
    std::string name;                /**< Scenario name. */
    Ledger ledger;                   /**< Expected damage and healing. */
    std::vector<Snapshot> snapshots;  /**< The enemy before each turn, in turn order. */
    Branches finalStates;            /**< The possible states at the end of the round. */
    std::size_t peakBranches = 0;    /**< Largest number of branches after any step, before merging. */
};

/**
 * @brief   Simulates a round exactly by branching on every roll.
 * @details Each check splits a branch into one branch per degree of success, weighted by its probability.
 *          Damage and healing go to the Ledger as probability x expected amount, and branches with identical
 *          states are merged, so the tree stays small and the result exact.
 */
class Engine
{
public:
    /**
     * @brief   Creates an engine for an encounter.
     * @param   [in] enc - The creatures. Kept by reference, so it must outlive the engine.
     * @param   [in] settings - Options for the run.
     */
    Engine(const Encounter& enc, Settings settings) : m_enc(enc), m_settings(settings) {}

    /**
     * @brief   Simulates the turns in order, then the enemy's end of turn.
     * @details At the enemy's end of turn, persistent damage ticks and the effects that last until then end.
     * @param   [in] name - Name to put in the result.
     * @param   [in] initial - The state when the round starts; it needs one CreatureState per creature.
     * @param   [in] turns - The party's turns, in order.
     * @return  The ledger, a snapshot before each turn, and the final branches.
     */
    RunResult run(const std::string& name, const BattleState& initial, const std::vector<Turn>& turns);

    /**
     * @brief   Runs one action on one branch and returns the resulting branches.
     * @details The action is skipped (recorded in Ledger::skipped) when its spent flag is set, or when the
     *          actor has no reaction or not enough actions left.
     * @param   [in] action - The action.
     * @param   [in] state - The branch's state.
     * @param   [in] p - The branch's probability.
     * @param   [in] actor - Index of the creature doing the action.
     * @param   [in] turnOwner - Index of the creature whose turn it is; the multiple attack penalty only
     *          counts when it's the actor.
     * @param   [in, out] ledger - Receives the expected damage, healing and skips.
     * @return  The resulting branches; their probabilities add up to @p p.
     */
    Branches execute(const Action& action, const BattleState& state, double p, int actor, int turnOwner,
                     Ledger& ledger) const;

private:
    /**
     * @brief   One outcome being resolved.
     * @details Its state, its probability, and the damage rolls that land on the enemy as one instance.
     */
    struct OutcomeBranch
    {
        BattleState state;              /**< State being built for this outcome. */
        double p = 0.0;                 /**< Probability of this outcome. */
        std::vector<DamageRoll> rolls;  /**< Damage rolls that land as one instance. */
    };

    /**
     * @brief   Applies one degree's damage roll, conditions, modifiers, counters and custom hook to a branch.
     * @details Healing and grants are applied later, by execute().
     * @param   [in] a - The action.
     * @param   [in] degree - The degree of success.
     * @param   [in, out] b - The branch; its state changes, and the outcome's damage roll (if any) is
     *          appended.
     * @param   [in] before - The state when the check was rolled.
     * @param   [in] actor - Index of the creature doing the action.
     * @param   [in] ally - Index of the action's ally target, or -1.
     * @param   [in] key - Ledger line for drained HP loss.
     * @param   [in, out] ledger - Receives drained HP loss on the enemy.
     */
    void applyOutcome(const Action& a, Degree degree, OutcomeBranch& b, const BattleState& before, int actor,
                      int ally, const LedgerKey& key, Ledger& ledger) const;

    /**
     * @brief   Resolves the coupled spell of @p a (Spellstrike) on one outcome of the Strike.
     * @details A spell attack spell takes the Strike's degree (a critical success becomes a success under
     *          incapacitation). A save spell rolls its own save, unless the Strike critically failed.
     * @param   [in] a - The action with the coupled spell.
     * @param   [in] degree - The Strike's degree of success.
     * @param   [in] base - The Strike's outcome branch.
     * @param   [in] before - The state when the Strike was rolled.
     * @param   [in] actor - Index of the creature doing the action.
     * @param   [in] turnOwner - Index of the creature whose turn it is.
     * @param   [in] ally - Index of the action's ally target, or -1.
     * @param   [in] key - Ledger line for drained HP loss.
     * @param   [in, out] ledger - Receives drained HP loss on the enemy.
     * @return  One branch per outcome of the spell, with the spell's damage roll appended.
     */
    std::vector<OutcomeBranch> resolveCoupled(const Action& a, Degree degree, OutcomeBranch base,
                                              const BattleState& before, int actor, int turnOwner, int ally,
                                              const LedgerKey& key, Ledger& ledger) const;

    /**
     * @brief   Builds the damage components of an outcome.
     * @details Uses the crit damage when doubled, adds extra damage from the actor's features (Strikes only),
     *          and adds damage bonuses and penalties to the first normal component.
     * @param   [in] a - The action.
     * @param   [in] scale - How the outcome scales the damage.
     * @param   [in, out] ctx - The outcome's context; features may change its state.
     * @return  The components, not yet scaled.
     */
    std::vector<DamageComponent> damageComponents(const Action& a, DamageScale scale, EffectCtx& ctx) const;

    /**
     * @brief   Puts a roll's persistent components on the enemy.
     * @details They're doubled or halved with the roll. Types the enemy is immune to are skipped.
     * @param   [in] roll - The damage roll.
     * @param   [in, out] state - The branch's state.
     */
    void addPersistent(const DamageRoll& roll, BattleState& state) const;

    /**
     * @brief   Applies a heal.
     * @details One roll heals every target, and the branch splits on the amount so later heals see the right
     *          missing HP.
     * @param   [in] h - The heal.
     * @param   [in] state - The branch's state.
     * @param   [in] p - The branch's probability.
     * @param   [in] actor - Index of the healer.
     * @param   [in] ally - Index of the action's ally target, or -1.
     * @param   [in] key - Ledger line for the healing.
     * @param   [in, out] ledger - Receives raw and effective healing.
     * @return  One branch per amount healed (merged), or the branch unchanged if @p h heals nothing.
     */
    Branches heal(const HealSpec& h, BattleState state, double p, int actor, int ally, const LedgerKey& key,
                  Ledger& ledger) const;

    /**
     * @brief   Whether the enemy is immune to one of the action's traits.
     * @param   [in] a - The action.
     * @return  True if one of its traits is in the enemy's immunities.
     */
    bool immuneToTraits(const Action& a) const;

    /**
     * @brief   Combines branches with identical states, adding up their probabilities.
     * @param   [in] in - The branches.
     * @return  The merged branches, or @p in unchanged when Settings::mergeStates is off.
     */
    Branches merge(Branches in) const;

    /**
     * @brief   Summarizes the enemy's state across branches.
     * @param   [in] b - The branches.
     * @param   [in] who - Name of the character whose turn is about to start.
     * @param   [in] actor - That character's index, for the off-guard chance.
     * @return  The snapshot.
     */
    Snapshot snapshot(const Branches& b, const std::string& who, int actor) const;

    /**
     * @brief   The enemy's end of turn.
     * @details Persistent damage ticks (see Settings::persistentTicks), then the effects that last until the
     *          end of its turn end.
     * @param   [in, out] b - The branches.
     * @param   [in, out] ledger - Receives the persistent damage.
     */
    void enemyEndOfTurn(Branches& b, Ledger& ledger) const;

    /**
     * @brief   The creature indices an effect lands on.
     * @param   [in] who - Who the effect targets.
     * @param   [in] actor - Index of the creature doing the action.
     * @param   [in] ally - Index of the action's ally target, or -1.
     * @return  The indices.
     * @throws  std::runtime_error if @p who is Who::Ally and @p ally is -1.
     */
    std::vector<int> resolve(Who who, int actor, int ally) const;

    /**
     * @brief   The Duration of an effect.
     * @param   [in] d - When it ends, relative to the creatures of the action.
     * @param   [in] actor - Index of the creature doing the action.
     * @param   [in] ally - Index of the action's ally target, or -1.
     * @return  The duration, tied to a creature index.
     */
    Duration resolve(const DurationSpec& d, int actor, int ally) const;

    /**
     * @brief   Rolls an action's check.
     * @details Applies modifiers, the multiple attack penalty, fortune, crit immunity, incapacitation and
     *          concealment flat checks.
     * @param   [in] a - The action.
     * @param   [in] before - The state when the check is rolled.
     * @param   [in, out] after - Gets the attack count, and loses the fortune counters and limited-use
     *          modifiers the roll used.
     * @param   [in] actor - Index of the creature doing the action.
     * @param   [in] turnOwner - Index of the creature whose turn it is.
     * @param   [in] ally - Index of the action's ally target, or -1.
     * @return  The degree distribution. It adds up to less than 1 when the action can have no effect
     *          (immunity, `usable` false, failed flat check).
     */
    DegreeDist roll(const Action& a, const BattleState& before, BattleState& after, int actor, int turnOwner,
                    int ally) const;

    const Encounter& m_enc;  /**< The creatures in the scenario. */
    Settings m_settings;     /**< Options for the run. */
};
