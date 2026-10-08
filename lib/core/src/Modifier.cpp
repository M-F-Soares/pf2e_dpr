/**
 * @file    Modifier.cpp
 * @brief   Selector matching and PF2e bonus/penalty stacking.
 */

#include "Modifier.h"

#include <algorithm>

Tags attributeTag(Attribute a)
{
    switch (a)
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

bool Selector::matches(Tags query) const
{
    return std::any_of(anyOf.begin(), anyOf.end(), [query](Tags alt) { return (alt & query) == alt; });
}

Selector select(std::initializer_list<Tags> alternatives)
{
    return Selector{std::vector<Tags>(alternatives)};
}

const char* toString(BonusType t)
{
    switch (t)
    {
    case BonusType::Circumstance: return "circumstance";
    case BonusType::Item: return "item";
    case BonusType::Status: return "status";
    case BonusType::Untyped: return "untyped";
    }
    return "?";
}

bool appliesTo(const Modifier& m, const RollQuery& q)
{
    if (m.vsCreature >= 0 && m.vsCreature != q.opponent)
        return false;
    return m.applies.matches(q.tags);
}

void Stacker::add(BonusType type, int value)
{
    if (type == BonusType::Untyped)
    {
        m_untyped += value;
        return;
    }
    int i = static_cast<int>(type);
    if (value > 0)
        m_bestBonus[i] = std::max(m_bestBonus[i], value);
    else
        m_worstPenalty[i] = std::min(m_worstPenalty[i], value);
}

int Stacker::total() const
{
    int sum = m_untyped;
    for (int i = 0; i < 3; ++i)
        sum += m_bestBonus[i] + m_worstPenalty[i];
    return sum;
}
