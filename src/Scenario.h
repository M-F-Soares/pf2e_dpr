#pragma once

#include "Engine.h"

#include <iosfwd>
#include <tuple>
#include <string>
#include <vector>

// A full party round against one enemy, described with a fluent builder:
//
//   Scenario a("Ranger 3 strikes");
//   a.against(brute)
//    .turn(ranger, {strike(ranger, "Longbow"), strike(ranger, "Longbow"), strike(ranger, "Longbow")})
//    .turn(rogue,  {strike(rogue, "Rapier"), strike(rogue, "Rapier")});
//   Scenario b = a.variant("Ranger strike + Demoralize")
//                 .replaceTurn(ranger, {strike(ranger, "Longbow"), demoralize(ranger)});
//   compare({a, b});
class Scenario
{
public:
    explicit Scenario(std::string name) : m_name(std::move(name)) {}

    Scenario& against(const Monster& enemy);
    Scenario& member(const Character& c);  // turn() adds members automatically
    Scenario& turn(const Character& actor, std::vector<Step> steps);
    Scenario& replaceTurn(const Character& actor, std::vector<Step> steps);

    // Starting state of the enemy (cover, conditions from earlier rounds, ...).
    Scenario& enemyCondition(ConditionKind kind, int value = 1);
    Scenario& enemyModifier(const Modifier& m);
    Scenario& counter(const Character& who, const std::string& name, int value = 1);
    Scenario& settings(const Settings& s);

    Scenario variant(std::string name) const;

    RunResult run() const;
    const std::string& name() const { return m_name; }

private:
    std::string m_name;
    Encounter m_enc;
    std::vector<Turn> m_turns;
    std::vector<std::pair<ConditionKind, int>> m_enemyConditions;
    std::vector<Modifier> m_enemyModifiers;
    std::vector<std::tuple<std::string, std::string, int>> m_counters;
    Settings m_settings;
};

void printReport(const RunResult& r, std::ostream& os);
void compare(const std::vector<Scenario>& scenarios, std::ostream& os);
void compare(const std::vector<Scenario>& scenarios);
