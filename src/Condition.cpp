#include "Condition.h"

const char* toString(ConditionKind k)
{
    switch (k)
    {
    case ConditionKind::Frightened: return "frightened";
    case ConditionKind::Sickened: return "sickened";
    case ConditionKind::Clumsy: return "clumsy";
    case ConditionKind::Enfeebled: return "enfeebled";
    case ConditionKind::Drained: return "drained";
    case ConditionKind::Stupefied: return "stupefied";
    case ConditionKind::OffGuard: return "off-guard";
    case ConditionKind::Prone: return "prone";
    case ConditionKind::Grabbed: return "grabbed";
    case ConditionKind::Restrained: return "restrained";
    case ConditionKind::Concealed: return "concealed";
    case ConditionKind::Hidden: return "hidden";
    case ConditionKind::Dazzled: return "dazzled";
    case ConditionKind::Slowed: return "slowed";
    case ConditionKind::Stunned: return "stunned";
    case ConditionKind::Quickened: return "quickened";
    }
    return "?";
}

namespace
{
Modifier statusPenalty(ConditionKind k, int value, Selector s)
{
    return Modifier{toString(k), BonusType::Status, -value, std::move(s)};
}

Modifier offGuard(const char* source)
{
    return Modifier{source, BonusType::Circumstance, -2, select({Tag::AC}), -1, true};
}
} // namespace

std::vector<Modifier> conditionModifiers(ConditionKind kind, int value)
{
    if (value <= 0)
        return {};
    switch (kind)
    {
    case ConditionKind::Frightened:
    case ConditionKind::Sickened:
        return {statusPenalty(kind, value, select({Tag::Check, Tag::DC}))};
    case ConditionKind::Clumsy:
        return {statusPenalty(kind, value, select({Tag::Dex}))};
    case ConditionKind::Enfeebled:
        return {statusPenalty(kind, value, select({Tag::Str}))};
    case ConditionKind::Drained:
        return {statusPenalty(kind, value, select({Tag::Con}))};
    case ConditionKind::Stupefied:
        return {statusPenalty(kind, value, select({Tag::Int, Tag::Wis, Tag::Cha}))};
    case ConditionKind::OffGuard:
    case ConditionKind::Grabbed:
    case ConditionKind::Restrained:
        return {offGuard("off-guard")};
    case ConditionKind::Prone:
        return {offGuard("off-guard"),
                Modifier{"prone", BonusType::Circumstance, -2, select({Tag::AttackRoll})}};
    default:
        return {};
    }
}

int conditionActionDelta(ConditionKind kind, int value)
{
    switch (kind)
    {
    case ConditionKind::Slowed:
    case ConditionKind::Stunned:
        return -value;
    case ConditionKind::Quickened:
        return value > 0 ? 1 : 0;
    default:
        return 0;
    }
}
