#include "State.h"

#include <algorithm>
#include <cstdlib>

std::string modifierKey(const Modifier& m)
{
    return m.vsCreature < 0 ? m.source : m.source + "@" + std::to_string(m.vsCreature);
}

int BattleState::modifierTotal(int creature, const RollQuery& q, const std::vector<Modifier>& extra,
                               std::vector<std::string>* used) const
{
    const CreatureState& c = creatures[creature];
    Stacker stack;
    for (const auto& m : extra)
        if (appliesTo(m, q))
            stack.add(m);
    for (const auto& [kind, cond] : c.conditions)
        for (const auto& m : conditionModifiers(kind, cond.value))
            if (appliesTo(m, q))
                stack.add(m);
    for (const auto& [key, active] : c.modifiers)
    {
        if (!appliesTo(active.mod, q))
            continue;
        stack.add(active.mod);
        if (used && active.uses > 0)
            used->push_back(key);
    }
    return stack.total();
}

bool BattleState::isOffGuardTo(int creature, int attacker, Tags attackTags) const
{
    RollQuery q{Tag::AC | Tag::DC | (attackTags & (Tag::Melee | Tag::Ranged)), attacker};
    const CreatureState& c = creatures[creature];
    for (const auto& [kind, cond] : c.conditions)
        for (const auto& m : conditionModifiers(kind, cond.value))
            if (m.grantsOffGuard && appliesTo(m, q))
                return true;
    for (const auto& [key, active] : c.modifiers)
        if (active.mod.grantsOffGuard && appliesTo(active.mod, q))
            return true;
    return false;
}

int BattleState::conditionValue(int creature, ConditionKind kind) const
{
    const auto& conds = creatures[creature].conditions;
    auto it = conds.find(kind);
    return it == conds.end() ? 0 : it->second.value;
}

int BattleState::counter(int creature, const std::string& name) const
{
    const auto& counters = creatures[creature].counters;
    auto it = counters.find(name);
    return it == counters.end() ? 0 : it->second;
}

int BattleState::actionsPerTurn(int creature) const
{
    int actions = 3;
    for (const auto& [kind, cond] : creatures[creature].conditions)
        actions += conditionActionDelta(kind, cond.value);
    return std::max(actions, 0);
}

void BattleState::addCondition(int creature, ConditionKind kind, int value, Duration until)
{
    auto& conds = creatures[creature].conditions;
    auto it = conds.find(kind);
    if (it == conds.end() || it->second.value < value)
        conds[kind] = ActiveCondition{value, until};
}

void BattleState::addModifier(int creature, const ActiveModifier& m)
{
    auto& mods = creatures[creature].modifiers;
    std::string key = modifierKey(m.mod);
    auto it = mods.find(key);
    if (it == mods.end() || std::abs(it->second.mod.value) <= std::abs(m.mod.value))
        mods[key] = m;
}

void BattleState::addPersistent(int creature, const DamageComponent& c)
{
    auto& p = creatures[creature].persistent;
    auto it = p.find(c.type);
    if (it == p.end() || it->second.mean() < c.mean())
        p[c.type] = c;
}

void BattleState::consumeUses(int creature, const std::vector<std::string>& sources)
{
    auto& mods = creatures[creature].modifiers;
    for (const auto& key : sources)
    {
        auto it = mods.find(key);
        if (it != mods.end() && it->second.uses > 0 && --it->second.uses == 0)
            mods.erase(it);
    }
}

void BattleState::startTurn(int creature)
{
    CreatureState& c = creatures[creature];
    c.attacksThisTurn = 0;
    c.actionsUsed = 0;
    c.reactionUsed = false;
    expire(Expiry::StartOfTurn, creature);
}

void BattleState::endTurn(int creature)
{
    creatures[creature].attacksThisTurn = 0;
    expire(Expiry::EndOfTurn, creature);
}

void BattleState::expire(Expiry when, int creature)
{
    Duration d{when, creature};
    for (auto& c : creatures)
    {
        std::erase_if(c.conditions, [&](const auto& kv) { return kv.second.until == d; });
        std::erase_if(c.modifiers, [&](const auto& kv) { return kv.second.until == d; });
    }
}
