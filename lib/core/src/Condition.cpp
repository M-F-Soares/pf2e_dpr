/**
 * @file    Condition.cpp
 * @brief   The modifiers and action changes of each condition.
 */

#include "Condition.h"

const char* ToString(ConditionKind kind)
{
    switch (kind)
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
    case ConditionKind::Blinded: return "blinded";
    case ConditionKind::Confused: return "confused";
    case ConditionKind::Paralyzed: return "paralyzed";
    case ConditionKind::Unconscious: return "unconscious";
    case ConditionKind::Fatigued: return "fatigued";
    case ConditionKind::Encumbered: return "encumbered";
    }
    return "?";
}

namespace
{
/**
 * @brief   A status penalty named after the condition.
 * @param   [in] kind - The condition (its name is the modifier's source).
 * @param   [in] value - The condition value; the penalty is -value.
 * @param   [in] selector - The rolls it applies to.
 * @return  The modifier.
 */
Modifier StatusPenalty(ConditionKind kind, int value, Selector selector)
{
    return Modifier{ToString(kind), BonusType::Status, -value, std::move(selector)};
}

/**
 * @brief   The -2 circumstance penalty to AC that also makes the holder count as off-guard.
 * @param   [in] source - Name of the modifier.
 * @return  The modifier.
 */
Modifier OffGuard(const char* source)
{
    return Modifier{source, BonusType::Circumstance, -2, Select({Tag::AC}), -1, true};
}
} // namespace

std::vector<Modifier> ConditionModifiers(ConditionKind kind, int value)
{
    if (value <= 0)
        return {};
    switch (kind)
    {
    case ConditionKind::Frightened:
    case ConditionKind::Sickened:
        return {StatusPenalty(kind, value, Select({Tag::Check, Tag::DC}))};
    case ConditionKind::Clumsy:
        return {StatusPenalty(kind, value, Select({Tag::Dex}))};
    case ConditionKind::Enfeebled:
        return {StatusPenalty(kind, value, Select({Tag::Str}))};
    case ConditionKind::Drained:
        return {StatusPenalty(kind, value, Select({Tag::Con}))};
    case ConditionKind::Stupefied:
        return {StatusPenalty(kind, value, Select({Tag::Int, Tag::Wis, Tag::Cha}))};
    case ConditionKind::OffGuard:
    case ConditionKind::Grabbed:
    case ConditionKind::Restrained:
    case ConditionKind::Blinded:
    case ConditionKind::Confused:
    case ConditionKind::Paralyzed:
        return {OffGuard("off-guard")};
    case ConditionKind::Prone:
        return {OffGuard("off-guard"),
                Modifier{"prone", BonusType::Circumstance, -2, Select({Tag::AttackRoll})}};
    case ConditionKind::Unconscious:
        return {OffGuard("off-guard"), StatusPenalty(kind, 4, Select({Tag::AC, Tag::Perception, Tag::Ref}))};
    case ConditionKind::Fatigued:
        // A penalty to a save is also one to the DC made from it.
        return {StatusPenalty(kind, 1, Select({Tag::AC, Tag::Fort, Tag::Ref, Tag::Will}))};
    case ConditionKind::Encumbered:
        return {StatusPenalty(kind, 1, Select({Tag::Dex}))};
    default:
        return {};
    }
}

int ConditionActionDelta(ConditionKind kind, int value)
{
    switch (kind)
    {
    case ConditionKind::Slowed:
    case ConditionKind::Stunned:
        return -value;
    case ConditionKind::Quickened:
        return value > 0 ? 1 : 0;
    case ConditionKind::Paralyzed:
    case ConditionKind::Unconscious:
        return -3;
    default:
        return 0;
    }
}
