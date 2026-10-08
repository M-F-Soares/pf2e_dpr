/**
 * @file    State.cpp
 * @brief   Queries and updates on the battle state of one branch.
 */

#include "State.h"

#include <algorithm>
#include <cstdlib>

std::string ModifierKey(const Modifier& modifier)
{
    return modifier.vsCreatureIndex < 0 ? modifier.source
                                        : modifier.source + "@" + std::to_string(modifier.vsCreatureIndex);
}

int BattleState::ModifierTotal(int creatureIndex, const RollQuery& query, const std::vector<Modifier>& extra,
                               std::vector<std::string>* used) const
{
    const CreatureState& state = creatures[creatureIndex];
    Stacker stack;
    for (const auto& modifier : extra)
        if (AppliesTo(modifier, query))
            stack.Add(modifier);
    for (const auto& [kind, condition] : state.conditions)
        for (const auto& modifier : ConditionModifiers(kind, condition.value))
            if (AppliesTo(modifier, query))
                stack.Add(modifier);
    for (const auto& [key, active] : state.modifiers)
    {
        if (!AppliesTo(active.mod, query))
            continue;
        stack.Add(active.mod);
        if (used && active.uses > 0)
            used->push_back(key);
    }
    return stack.Total();
}

bool BattleState::IsOffGuardTo(int creatureIndex, int attackerIndex, Tags attackTags) const
{
    RollQuery query{Tag::AC | Tag::DC | (attackTags & (Tag::Melee | Tag::Ranged)), attackerIndex};
    const CreatureState& state = creatures[creatureIndex];
    for (const auto& [kind, condition] : state.conditions)
        for (const auto& modifier : ConditionModifiers(kind, condition.value))
            if (modifier.grantsOffGuard && AppliesTo(modifier, query))
                return true;
    for (const auto& [key, active] : state.modifiers)
        if (active.mod.grantsOffGuard && AppliesTo(active.mod, query))
            return true;
    return false;
}

int BattleState::ConditionValue(int creatureIndex, ConditionKind kind) const
{
    const auto& conditions = creatures[creatureIndex].conditions;
    auto it = conditions.find(kind);
    return it == conditions.end() ? 0 : it->second.value;
}

int BattleState::Counter(int creatureIndex, const std::string& counterName) const
{
    const auto& counters = creatures[creatureIndex].counters;
    auto it = counters.find(counterName);
    return it == counters.end() ? 0 : it->second;
}

int BattleState::ActionsPerTurn(int creatureIndex) const
{
    int actions = 3;
    for (const auto& [kind, condition] : creatures[creatureIndex].conditions)
        actions += ConditionActionDelta(kind, condition.value);
    return std::max(actions, 0);
}

void BattleState::AddCondition(int creatureIndex, ConditionKind kind, int value, Duration until)
{
    auto& conditions = creatures[creatureIndex].conditions;
    auto it = conditions.find(kind);
    if (it == conditions.end() || it->second.value < value)
        conditions[kind] = ActiveCondition{value, until};
}

void BattleState::AddModifier(int creatureIndex, const ActiveModifier& modifier)
{
    auto& modifiers = creatures[creatureIndex].modifiers;
    std::string key = ModifierKey(modifier.mod);
    auto it = modifiers.find(key);
    if (it == modifiers.end() || std::abs(it->second.mod.value) <= std::abs(modifier.mod.value))
        modifiers[key] = modifier;
}

void BattleState::AddPersistent(int creatureIndex, const DamageComponent& component)
{
    auto& persistentByType = creatures[creatureIndex].persistent;
    auto it = persistentByType.find(component.type);
    if (it == persistentByType.end() || it->second.Mean() < component.Mean())
        persistentByType[component.type] = component;
}

void BattleState::ConsumeUses(int creatureIndex, const std::vector<std::string>& sources)
{
    auto& modifiers = creatures[creatureIndex].modifiers;
    for (const auto& key : sources)
    {
        auto it = modifiers.find(key);
        if (it != modifiers.end() && it->second.uses > 0 && --it->second.uses == 0)
            modifiers.erase(it);
    }
}

void BattleState::StartTurn(int creatureIndex)
{
    CreatureState& state = creatures[creatureIndex];
    state.attacksThisTurn = 0;
    state.actionsUsed = 0;
    state.reactionUsed = false;
    Expire(Expiry::StartOfTurn, creatureIndex);
}

void BattleState::EndTurn(int creatureIndex)
{
    creatures[creatureIndex].attacksThisTurn = 0;
    Expire(Expiry::EndOfTurn, creatureIndex);
}

void BattleState::Expire(Expiry when, int creatureIndex)
{
    Duration duration{when, creatureIndex};
    for (auto& state : creatures)
    {
        std::erase_if(state.conditions, [&](const auto& entry) { return entry.second.until == duration; });
        std::erase_if(state.modifiers, [&](const auto& entry) { return entry.second.until == duration; });
    }
}
