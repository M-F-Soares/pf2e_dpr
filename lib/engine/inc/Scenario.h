/**
 * @file    Scenario.h
 * @brief   Fluent description of a party round, and the reports that compare rounds.
 */

#pragma once

#include "Engine.h"

#include <iosfwd>
#include <tuple>
#include <string>
#include <vector>

/**
 * @brief   A full party round against one enemy, described with a fluent builder.
 * @code
 *   Scenario a("Ranger 3 strikes");
 *   a.Against(brute)
 *    .AddTurn(ranger, {Strike(ranger, "Longbow"), Strike(ranger, "Longbow"), Strike(ranger, "Longbow")})
 *    .AddTurn(rogue,  {Strike(rogue, "Rapier"), Strike(rogue, "Rapier")});
 *   Scenario b = a.Variant("Ranger strike + Demoralize")
 *                 .ReplaceTurn(ranger, {Strike(ranger, "Longbow"), Demoralize(ranger)});
 *   Compare({a, b});
 * @endcode
 */
class Scenario
{
public:
    /**
     * @brief   Creates an empty scenario.
     * @param   [in] name - Name shown in reports.
     */
    explicit Scenario(std::string name) : m_name(std::move(name)) {}

    /**
     * @brief   Sets the enemy.
     * @param   [in] enemy - The enemy's stat block.
     * @return  This scenario, for chaining.
     */
    Scenario& Against(const Monster& enemy);

    /**
     * @brief   Adds a party member. AddTurn() adds members automatically.
     * @details The order members are added in gives their creature indices. A character with the same name as
     *          an existing member isn't added again.
     * @param   [in] character - The character.
     * @return  This scenario, for chaining.
     */
    Scenario& Member(const Character& character);

    /**
     * @brief   Adds a turn after the existing ones, and adds the actor to the party if needed.
     * @param   [in] actor - The character taking the turn.
     * @param   [in] steps - The actions, in order.
     * @return  This scenario, for chaining.
     */
    Scenario& AddTurn(const Character& actor, std::vector<Step> steps);

    /**
     * @brief   Replaces the steps of the actor's first turn, or adds a turn if they have none.
     * @param   [in] actor - The character whose turn changes.
     * @param   [in] steps - The new actions, in order.
     * @return  This scenario, for chaining.
     */
    Scenario& ReplaceTurn(const Character& actor, std::vector<Step> steps);

    /**
     * @brief   Starts the enemy with a condition that lasts the whole round.
     * @details For a condition from an earlier round, for example.
     * @param   [in] kind - The condition.
     * @param   [in] value - Its value.
     * @return  This scenario, for chaining.
     */
    Scenario& EnemyCondition(ConditionKind kind, int value = 1);

    /**
     * @brief   Starts the enemy with a modifier that lasts the whole round.
     * @details For cover, or a debuff from an earlier round, for example.
     * @param   [in] modifier - The modifier.
     * @return  This scenario, for chaining.
     */
    Scenario& EnemyModifier(const Modifier& modifier);

    /**
     * @brief   Starts a character with a counter set.
     * @details For example "prey" if they hunted earlier, or "spellstrike-spent". Adds the character to the
     *          party if needed.
     * @param   [in] character - The character.
     * @param   [in] counterName - Counter name.
     * @param   [in] value - Its value.
     * @return  This scenario, for chaining.
     */
    Scenario& Counter(const Character& character, const std::string& counterName, int value = 1);

    /**
     * @brief   Sets the run options.
     * @param   [in] settings - The settings.
     * @return  This scenario, for chaining.
     */
    Scenario& WithSettings(const Settings& settings);

    /**
     * @brief   A copy of this scenario under another name, to change with ReplaceTurn() and friends.
     * @param   [in] name - Name of the copy.
     * @return  The copy.
     */
    Scenario Variant(std::string name) const;

    /**
     * @brief   Builds the starting state and runs the round.
     * @return  The expected damage and healing, snapshots and final branches.
     * @throws  std::runtime_error if an action targets or grants an action to a character that isn't in the
     *          party.
     */
    RunResult Run() const;

    /**
     * @brief   The scenario's name.
     * @return  The name shown in reports.
     */
    const std::string& Name() const { return m_name; }

private:
    std::string m_name;                                             /**< Name shown in reports. */
    Encounter m_encounter;                                                /**< The enemy and the party. */
    std::vector<Turn> m_turns;                                      /**< The turns, in order. */
    std::vector<std::pair<ConditionKind, int>> m_enemyConditions;   /**< Conditions the enemy starts with. */
    std::vector<Modifier> m_enemyModifiers;                         /**< Modifiers the enemy starts with. */

    /**
     * @brief   Counters to start with, as (character, counter, value).
     */
    std::vector<std::tuple<std::string, std::string, int>> m_counters;
    Settings m_settings;  /**< Run options. */
};

/**
 * @brief   Prints the report of one run.
 * @details Damage and healing per character and action, skipped actions, the enemy's conditions before each
 *          turn, and branch counts.
 * @param   [in] result - The result of a run.
 * @param   [in, out] os - Where to print.
 */
void PrintReport(const RunResult& result, std::ostream& os);

/**
 * @brief   Runs every scenario, prints each report, then ranks them by expected damage.
 * @param   [in] scenarios - The scenarios to compare.
 * @param   [in, out] os - Where to print.
 */
void Compare(const std::vector<Scenario>& scenarios, std::ostream& os);

/**
 * @brief   Same as Compare(scenarios, os), printing to std::cout.
 * @param   [in] scenarios - The scenarios to compare.
 */
void Compare(const std::vector<Scenario>& scenarios);
