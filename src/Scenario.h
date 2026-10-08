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
 *   a.against(brute)
 *    .turn(ranger, {strike(ranger, "Longbow"), strike(ranger, "Longbow"), strike(ranger, "Longbow")})
 *    .turn(rogue,  {strike(rogue, "Rapier"), strike(rogue, "Rapier")});
 *   Scenario b = a.variant("Ranger strike + Demoralize")
 *                 .replaceTurn(ranger, {strike(ranger, "Longbow"), demoralize(ranger)});
 *   compare({a, b});
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
    Scenario& against(const Monster& enemy);

    /**
     * @brief   Adds a party member. turn() adds members automatically.
     * @details The order members are added in gives their creature indices. A character with the same name as
     *          an existing member isn't added again.
     * @param   [in] c - The character.
     * @return  This scenario, for chaining.
     */
    Scenario& member(const Character& c);

    /**
     * @brief   Adds a turn after the existing ones, and adds the actor to the party if needed.
     * @param   [in] actor - The character taking the turn.
     * @param   [in] steps - The actions, in order.
     * @return  This scenario, for chaining.
     */
    Scenario& turn(const Character& actor, std::vector<Step> steps);

    /**
     * @brief   Replaces the steps of the actor's first turn, or adds a turn if they have none.
     * @param   [in] actor - The character whose turn changes.
     * @param   [in] steps - The new actions, in order.
     * @return  This scenario, for chaining.
     */
    Scenario& replaceTurn(const Character& actor, std::vector<Step> steps);

    /**
     * @brief   Starts the enemy with a condition that lasts the whole round.
     * @details For a condition from an earlier round, for example.
     * @param   [in] kind - The condition.
     * @param   [in] value - Its value.
     * @return  This scenario, for chaining.
     */
    Scenario& enemyCondition(ConditionKind kind, int value = 1);

    /**
     * @brief   Starts the enemy with a modifier that lasts the whole round.
     * @details For cover, or a debuff from an earlier round, for example.
     * @param   [in] m - The modifier.
     * @return  This scenario, for chaining.
     */
    Scenario& enemyModifier(const Modifier& m);

    /**
     * @brief   Starts a character with a counter set.
     * @details For example "prey" if they hunted earlier, or "spellstrike-spent". Adds the character to the
     *          party if needed.
     * @param   [in] who - The character.
     * @param   [in] name - Counter name.
     * @param   [in] value - Its value.
     * @return  This scenario, for chaining.
     */
    Scenario& counter(const Character& who, const std::string& name, int value = 1);

    /**
     * @brief   Sets the run options.
     * @param   [in] s - The settings.
     * @return  This scenario, for chaining.
     */
    Scenario& settings(const Settings& s);

    /**
     * @brief   A copy of this scenario under another name, to change with replaceTurn() and friends.
     * @param   [in] name - Name of the copy.
     * @return  The copy.
     */
    Scenario variant(std::string name) const;

    /**
     * @brief   Builds the starting state and runs the round.
     * @return  The expected damage and healing, snapshots and final branches.
     * @throws  std::runtime_error if an action targets or grants an action to a character that isn't in the
     *          party.
     */
    RunResult run() const;

    /**
     * @brief   The scenario's name.
     * @return  The name shown in reports.
     */
    const std::string& name() const { return m_name; }

private:
    std::string m_name;  /**< Name shown in reports. */
    Encounter m_enc;     /**< The enemy and the party. */
    std::vector<Turn> m_turns;  /**< The turns, in order. */
    std::vector<std::pair<ConditionKind, int>> m_enemyConditions;  /**< Conditions the enemy starts with. */
    std::vector<Modifier> m_enemyModifiers;  /**< Modifiers the enemy starts with. */

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
 * @param   [in] r - The result of a run.
 * @param   [in, out] os - Where to print.
 */
void printReport(const RunResult& r, std::ostream& os);

/**
 * @brief   Runs every scenario, prints each report, then ranks them by expected damage.
 * @param   [in] scenarios - The scenarios to compare.
 * @param   [in, out] os - Where to print.
 */
void compare(const std::vector<Scenario>& scenarios, std::ostream& os);

/**
 * @brief   Same as compare(scenarios, os), printing to std::cout.
 * @param   [in] scenarios - The scenarios to compare.
 */
void compare(const std::vector<Scenario>& scenarios);
