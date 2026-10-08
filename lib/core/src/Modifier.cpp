/**
 * @file    Modifier.cpp
 * @brief   Selector matching and PF2e bonus/penalty stacking.
 */

#include "Modifier.h"

#include <algorithm>

Tags AttributeTag(Attribute attribute)
{
    switch (attribute)
    {
    case Attribute::None: return 0;
    case Attribute::Str: return Tag::Str;
    case Attribute::Dex: return Tag::Dex;
    case Attribute::Con: return Tag::Con;
    case Attribute::Int: return Tag::Int;
    case Attribute::Wis: return Tag::Wis;
    case Attribute::Cha: return Tag::Cha;
    }
    return 0;
}

bool Selector::Matches(Tags query) const
{
    return std::any_of(anyOf.begin(), anyOf.end(),
                       [query](Tags alternative) { return (alternative & query) == alternative; });
}

Selector Select(std::initializer_list<Tags> alternatives)
{
    return Selector{std::vector<Tags>(alternatives)};
}

const char* ToString(BonusType type)
{
    switch (type)
    {
    case BonusType::Circumstance: return "circumstance";
    case BonusType::Item: return "item";
    case BonusType::Status: return "status";
    case BonusType::Untyped: return "untyped";
    }
    return "?";
}

bool AppliesTo(const Modifier& modifier, const RollQuery& query)
{
    if (modifier.vsCreatureIndex >= 0 && modifier.vsCreatureIndex != query.opponentIndex)
        return false;
    return modifier.applies.Matches(query.tags);
}

void Stacker::Add(BonusType type, int value)
{
    if (type == BonusType::Untyped)
    {
        m_untyped += value;
        return;
    }
    int slot = static_cast<int>(type);
    if (value > 0)
        m_bestBonus[slot] = std::max(m_bestBonus[slot], value);
    else
        m_worstPenalty[slot] = std::min(m_worstPenalty[slot], value);
}

int Stacker::Total() const
{
    int sum = m_untyped;
    for (int i = 0; i < 3; ++i)
        sum += m_bestBonus[i] + m_worstPenalty[i];
    return sum;
}
