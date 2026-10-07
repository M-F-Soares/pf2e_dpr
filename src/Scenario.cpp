#include "Scenario.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

Scenario& Scenario::against(const Monster& enemy)
{
    m_enc.enemy = enemy;
    return *this;
}

Scenario& Scenario::member(const Character& c)
{
    for (const auto& m : m_enc.party)
        if (m.name == c.name)
            return *this;
    m_enc.party.push_back(c);
    return *this;
}

Scenario& Scenario::turn(const Character& actor, std::vector<Step> steps)
{
    member(actor);
    m_turns.push_back(Turn{actor.name, std::move(steps)});
    return *this;
}

Scenario& Scenario::replaceTurn(const Character& actor, std::vector<Step> steps)
{
    for (auto& t : m_turns)
        if (t.actor == actor.name)
        {
            t.steps = std::move(steps);
            return *this;
        }
    return turn(actor, std::move(steps));
}

Scenario& Scenario::enemyCondition(ConditionKind kind, int value)
{
    m_enemyConditions.emplace_back(kind, value);
    return *this;
}

Scenario& Scenario::enemyModifier(const Modifier& m)
{
    m_enemyModifiers.push_back(m);
    return *this;
}

Scenario& Scenario::counter(const Character& who, const std::string& name, int value)
{
    member(who);
    m_counters.emplace_back(who.name, name, value);
    return *this;
}

Scenario& Scenario::settings(const Settings& s)
{
    m_settings = s;
    return *this;
}

Scenario Scenario::variant(std::string name) const
{
    Scenario copy = *this;
    copy.m_name = std::move(name);
    return copy;
}

RunResult Scenario::run() const
{
    BattleState initial;
    initial.creatures.resize(m_enc.party.size() + 1);
    for (std::size_t i = 0; i < m_enc.party.size(); ++i)
        initial[static_cast<int>(i) + 1].hpMissing = m_enc.party[i].hpMissing;
    for (const auto& [kind, value] : m_enemyConditions)
        initial.addCondition(kEnemy, kind, value, {});
    for (const auto& m : m_enemyModifiers)
        initial.addModifier(kEnemy, ActiveModifier{m, {}, -1});
    for (const auto& [who, name, value] : m_counters)
        initial[m_enc.indexOf(who)].counters[name] = value;

    Engine engine(m_enc, m_settings);
    return engine.run(m_name, initial, m_turns);
}

namespace
{
std::string fixed(double v, int precision = 2)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    return ss.str();
}
} // namespace

void printReport(const RunResult& r, std::ostream& os)
{
    os << "=== " << r.name << " ===\n";
    os << "Expected damage: " << fixed(r.ledger.totalDamage()) << "\n";
    for (const auto& [k, v] : r.ledger.damage)
        os << "  " << std::left << std::setw(14) << k.who << std::setw(34) << k.what << std::right << std::setw(8)
           << fixed(v) << "\n";

    if (!r.ledger.healRaw.empty())
    {
        os << "Expected healing: " << fixed(r.ledger.totalHealRaw()) << " raw, "
           << fixed(r.ledger.totalHealEffective()) << " effective\n";
        for (const auto& [k, v] : r.ledger.healRaw)
        {
            auto eff = r.ledger.healEffective.find(k);
            os << "  " << std::left << std::setw(14) << k.who << std::setw(34) << k.what << std::right << std::setw(8)
               << fixed(v) << "  (effective " << fixed(eff == r.ledger.healEffective.end() ? 0.0 : eff->second)
               << ")\n";
        }
    }

    for (const auto& [k, p] : r.ledger.skipped)
        if (p > 1e-9)
            os << "  note: " << k.who << " couldn't do " << k.what << " in " << fixed(p * 100, 1)
               << "% of branches (no actions or reaction left, or not recharged)\n";

    os << "Enemy state before each turn:\n";
    for (const Snapshot& s : r.snapshots)
    {
        os << "  " << std::left << std::setw(14) << s.beforeTurnOf << std::right
           << "off-guard " << std::setw(5) << fixed(s.enemyOffGuardChance * 100, 1) << "%";
        for (const auto& [kind, chance] : s.enemyConditionChance)
        {
            if (kind == ConditionKind::OffGuard)
                continue;
            os << "  " << toString(kind) << " " << fixed(chance * 100, 1) << "%";
            double mean = s.enemyConditionMean.at(kind) / chance;
            if (chance > 0 && std::abs(mean - 1.0) > 1e-9)
                os << " (avg value " << fixed(mean) << ")";
        }
        os << "\n";
    }
    os << "Branches: " << r.finalStates.size() << " final, " << r.peakBranches << " peak\n\n";
}

void compare(const std::vector<Scenario>& scenarios, std::ostream& os)
{
    std::vector<RunResult> results;
    for (const auto& s : scenarios)
        results.push_back(s.run());

    for (const auto& r : results)
        printReport(r, os);

    std::vector<const RunResult*> ranked;
    for (const auto& r : results)
        ranked.push_back(&r);
    std::sort(ranked.begin(), ranked.end(), [](const RunResult* a, const RunResult* b) {
        return a->ledger.totalDamage() > b->ledger.totalDamage();
    });

    os << "=== Ranking (expected damage this round) ===\n";
    double best = ranked.empty() ? 0.0 : ranked.front()->ledger.totalDamage();
    for (const RunResult* r : ranked)
    {
        double dmg = r->ledger.totalDamage();
        os << "  " << std::left << std::setw(44) << r->name << std::right << std::setw(8) << fixed(dmg);
        if (r != ranked.front())
            os << "  (" << fixed(dmg - best) << ")";
        if (!r->ledger.healRaw.empty())
            os << "  heal " << fixed(r->ledger.totalHealEffective());
        os << "\n";
    }
}

void compare(const std::vector<Scenario>& scenarios)
{
    compare(scenarios, std::cout);
}
