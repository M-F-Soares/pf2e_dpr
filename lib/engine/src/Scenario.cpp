/**
 * @file    Scenario.cpp
 * @brief   Scenario building and running, and the text reports.
 */

#include "Scenario.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

Scenario& Scenario::Against(const Monster& enemy)
{
    m_encounter.enemy = enemy;
    return *this;
}

Scenario& Scenario::Member(const Character& character)
{
    for (const auto& existing : m_encounter.party)
        if (existing.name == character.name)
            return *this;
    m_encounter.party.push_back(character);
    return *this;
}

Scenario& Scenario::AddTurn(const Character& actor, std::vector<Step> steps)
{
    Member(actor);
    m_turns.push_back(Turn{actor.name, std::move(steps)});
    return *this;
}

Scenario& Scenario::ReplaceTurn(const Character& actor, std::vector<Step> steps)
{
    for (auto& existing : m_turns)
        if (existing.actorName == actor.name)
        {
            existing.steps = std::move(steps);
            return *this;
        }
    return AddTurn(actor, std::move(steps));
}

Scenario& Scenario::EnemyCondition(ConditionKind kind, int value)
{
    m_enemyConditions.emplace_back(kind, value);
    return *this;
}

Scenario& Scenario::EnemyModifier(const Modifier& modifier)
{
    m_enemyModifiers.push_back(modifier);
    return *this;
}

Scenario& Scenario::Counter(const Character& character, const std::string& counterName, int value)
{
    Member(character);
    m_counters.emplace_back(character.name, counterName, value);
    return *this;
}

Scenario& Scenario::WithSettings(const Settings& settings)
{
    m_settings = settings;
    return *this;
}

Scenario Scenario::Variant(std::string name) const
{
    Scenario copy = *this;
    copy.m_name = std::move(name);
    return copy;
}

RunResult Scenario::Run() const
{
    BattleState initial;
    initial.creatures.resize(m_encounter.party.size() + 1);
    for (std::size_t i = 0; i < m_encounter.party.size(); ++i)
        initial[static_cast<int>(i) + 1].hpMissing = m_encounter.party[i].hpMissing;
    for (const auto& [kind, value] : m_enemyConditions)
        initial.AddCondition(kEnemyIndex, kind, value, {});
    for (const auto& modifier : m_enemyModifiers)
        initial.AddModifier(kEnemyIndex, ActiveModifier{modifier, {}, -1});
    for (const auto& [characterName, counterName, value] : m_counters)
        initial[m_encounter.IndexOf(characterName)].counters[counterName] = value;

    Engine engine(m_encounter, m_settings);
    return engine.Run(m_name, initial, m_turns);
}

namespace
{
/**
 * @brief   Formats a number with a fixed number of decimals.
 * @param   [in] value - The number.
 * @param   [in] precision - Decimals to show.
 * @return  The formatted number.
 */
std::string Fixed(double value, int precision = 2)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}
} // namespace

void PrintReport(const RunResult& result, std::ostream& os)
{
    os << "=== " << result.name << " ===\n";
    os << "Expected damage: " << Fixed(result.ledger.TotalDamage()) << "\n";
    for (const auto& [key, value] : result.ledger.damage)
        os << "  " << std::left << std::setw(14) << key.actorName << std::setw(34) << key.actionName << std::right
           << std::setw(8) << Fixed(value) << "\n";

    if (!result.ledger.healRaw.empty())
    {
        os << "Expected healing: " << Fixed(result.ledger.TotalHealRaw()) << " raw, "
           << Fixed(result.ledger.TotalHealEffective()) << " effective\n";
        for (const auto& [key, value] : result.ledger.healRaw)
        {
            auto effective = result.ledger.healEffective.find(key);
            os << "  " << std::left << std::setw(14) << key.actorName << std::setw(34) << key.actionName << std::right
               << std::setw(8) << Fixed(value) << "  (effective "
               << Fixed(effective == result.ledger.healEffective.end() ? 0.0 : effective->second) << ")\n";
        }
    }

    for (const auto& [key, probability] : result.ledger.skipped)
        if (probability > 1e-9)
            os << "  note: " << key.actorName << " couldn't do " << key.actionName << " in "
               << Fixed(probability * 100, 1) << "% of branches (no actions or reaction left, or not recharged)\n";

    os << "Enemy state before each turn:\n";
    for (const Snapshot& snapshot : result.snapshots)
    {
        os << "  " << std::left << std::setw(14) << snapshot.actorName << std::right
           << "off-guard " << std::setw(5) << Fixed(snapshot.enemyOffGuardChance * 100, 1) << "%";
        for (const auto& [kind, chance] : snapshot.enemyConditionChance)
        {
            if (kind == ConditionKind::OffGuard)
                continue;
            os << "  " << ToString(kind) << " " << Fixed(chance * 100, 1) << "%";
            double mean = snapshot.enemyConditionMean.at(kind) / chance;
            if (chance > 0 && std::abs(mean - 1.0) > 1e-9)
                os << " (avg value " << Fixed(mean) << ")";
        }
        os << "\n";
    }
    os << "Branches: " << result.finalStates.size() << " final, " << result.peakBranches << " peak\n\n";
}

void Compare(const std::vector<Scenario>& scenarios, std::ostream& os)
{
    std::vector<RunResult> results;
    for (const auto& scenario : scenarios)
        results.push_back(scenario.Run());

    for (const auto& result : results)
        PrintReport(result, os);

    std::vector<const RunResult*> ranked;
    for (const auto& result : results)
        ranked.push_back(&result);
    std::sort(ranked.begin(), ranked.end(), [](const RunResult* left, const RunResult* right) {
        return left->ledger.TotalDamage() > right->ledger.TotalDamage();
    });

    os << "=== Ranking (expected damage this round) ===\n";
    double best = ranked.empty() ? 0.0 : ranked.front()->ledger.TotalDamage();
    for (const RunResult* result : ranked)
    {
        double damage = result->ledger.TotalDamage();
        os << "  " << std::left << std::setw(44) << result->name << std::right << std::setw(8) << Fixed(damage);
        if (result != ranked.front())
            os << "  (" << Fixed(damage - best) << ")";
        if (!result->ledger.healRaw.empty())
            os << "  heal " << Fixed(result->ledger.TotalHealEffective());
        os << "\n";
    }
}

void Compare(const std::vector<Scenario>& scenarios)
{
    Compare(scenarios, std::cout);
}
