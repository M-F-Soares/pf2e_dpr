/**
 * @file    Modifier.h
 * @brief   Roll tags, typed bonuses and penalties, and the PF2e stacking rules.
 */

#pragma once

#include <cstdint>
#include <compare>
#include <string>
#include <vector>

/**
 * @brief   Bit set of Tag values that describes a roll or a DC.
 * @details A roll or DC is described by a set of tags. A modifier applies when one of its selector
 *          alternatives is fully contained in the roll's tags.
 * @code
 *   Frightened -> {Check} or {DC}       (all checks and DCs)
 *   Clumsy     -> {Dex}                 (Dex-based rolls/DCs: AC, Reflex, ranged attacks...)
 *   Heroism    -> {AttackRoll} or {Save} or {Skill|Check} or {Perception|Check}
 * @endcode
 */
using Tags = std::uint32_t;

/**
 * @brief   The individual bits that make up Tags.
 */
namespace Tag
{
/**
 * @brief   One bit per property a roll or DC can have.
 */
enum : Tags
{
    Check = 1u << 0,       /**< A check the creature rolls (attack roll, save, skill check, Perception). */
    DC = 1u << 1,          /**< One of the creature's DCs, AC included. */
    AttackRoll = 1u << 2,  /**< An attack roll (Strike or spell attack). */
    Damage = 1u << 3,      /**< A damage roll. */
    AC = 1u << 4,          /**< Armor Class. */
    Save = 1u << 5,        /**< A saving throw. */
    Fort = 1u << 6,        /**< Fortitude save or DC. */
    Ref = 1u << 7,         /**< Reflex save or DC. */
    Will = 1u << 8,        /**< Will save or DC. */
    Perception = 1u << 9,  /**< Perception check or DC. */
    Skill = 1u << 10,      /**< A skill check. */
    Spell = 1u << 11,      /**< A spell attack roll, spell DC or spell damage roll. */
    Melee = 1u << 12,      /**< A melee attack, or the AC it targets. */
    Ranged = 1u << 13,     /**< A ranged attack, or the AC it targets. */
    Str = 1u << 14,        /**< Strength-based (enfeebled applies). */
    Dex = 1u << 15,        /**< Dexterity-based (clumsy applies). */
    Con = 1u << 16,        /**< Constitution-based (drained applies). */
    Int = 1u << 17,        /**< Intelligence-based (stupefied applies). */
    Wis = 1u << 18,        /**< Wisdom-based (stupefied applies). */
    Cha = 1u << 19,        /**< Charisma-based (stupefied applies). */
    ClassDC = 1u << 20,    /**< A class DC. */
    Healing = 1u << 21,    /**< A healing roll. */
};
}

/**
 * @brief   The attribute a roll or DC is based on.
 */
enum class Attribute
{
    None,  /**< Not based on an attribute. */
    Str,   /**< Strength. */
    Dex,   /**< Dexterity. */
    Con,   /**< Constitution. */
    Int,   /**< Intelligence. */
    Wis,   /**< Wisdom. */
    Cha    /**< Charisma. */
};

/**
 * @brief   Tag bit for rolls and DCs based on an attribute.
 * @param   [in] attribute - The attribute.
 * @return  The matching Tag bit, or 0 for Attribute::None.
 */
Tags AttributeTag(Attribute attribute);

/**
 * @brief   Which rolls a modifier applies to: any one of several tag combinations.
 */
struct Selector
{
    /**
     * @brief   The alternatives.
     * @details A roll matches when it has every bit of at least one of them, so an alternative of 0 matches
     *          every roll and an empty list matches none.
     */
    std::vector<Tags> anyOf;

    /**
     * @brief   Whether one of the alternatives is fully contained in @p query.
     * @param   [in] query - Tags of the roll or DC.
     * @return  True when every bit of some alternative is set in @p query.
     */
    bool Matches(Tags query) const;

    /**
     * @brief   Member-wise ordering, so states that hold modifiers can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const Selector&) const = default;
};

/**
 * @brief   Builds a Selector from its alternatives.
 * @param   [in] alternatives - Tag combinations, e.g. `Select({Tag::AttackRoll, Tag::Skill | Tag::Check})`.
 * @return  A selector that matches a roll with all the bits of any one alternative.
 */
Selector Select(std::initializer_list<Tags> alternatives);

/**
 * @brief   Type of a bonus or penalty, which decides how it stacks (see Stacker).
 */
enum class BonusType
{
    Circumstance,  /**< Circumstance bonus or penalty. */
    Item,          /**< Item bonus or penalty. */
    Status,        /**< Status bonus or penalty. */
    Untyped        /**< Untyped: always stacks. */
};

/**
 * @brief   Name of a bonus type for reports ("circumstance", "item", "status", "untyped").
 * @param   [in] type - The bonus type.
 * @return  A static string; "?" for an out-of-range value.
 */
const char* ToString(BonusType type);

/**
 * @brief   A typed bonus or penalty, and the rolls it applies to.
 */
struct Modifier
{
    std::string source;                   /**< Name of the effect; the same source never stacks with itself. */
    BonusType type = BonusType::Untyped;  /**< How it stacks with other modifiers. */
    int value = 0;                        /**< Positive = bonus, negative = penalty. */
    Selector applies;                     /**< The rolls and DCs it applies to. */
    int vsCreatureIndex = -1;             /**< Only against this opposing creature index (-1 = anyone). */
    bool grantsOffGuard = false;          /**< The holder counts as off-guard (for precision damage, etc.). */

    /**
     * @brief   Member-wise ordering, so states that hold modifiers can be compared and merged.
     * @return  The comparison of the first member that differs.
     */
    auto operator<=>(const Modifier&) const = default;
};

/**
 * @brief   What the roll is, and who is on the other side of it.
 */
struct RollQuery
{
    Tags tags = 0;           /**< Tags of the roll or DC. */
    int opponentIndex = -1;  /**< Index of the creature on the other side of the roll (-1 = none). */
};

/**
 * @brief   Whether a modifier applies to a roll.
 * @param   [in] modifier - The modifier.
 * @param   [in] query - The roll.
 * @return  True when @p modifier's selector matches the roll's tags and, if @p modifier only works against one
 *          creature, that creature is the roll's opponent.
 */
bool AppliesTo(const Modifier& modifier, const RollQuery& query);

/**
 * @brief   Totals bonuses and penalties with the PF2e stacking rules.
 * @details Per type (circumstance/item/status) only the highest bonus and the worst penalty count; untyped
 *          modifiers all stack.
 */
class Stacker
{
public:
    /**
     * @brief   Adds one bonus or penalty.
     * @param   [in] type - How it stacks.
     * @param   [in] value - Positive for a bonus, negative for a penalty.
     */
    void Add(BonusType type, int value);

    /**
     * @brief   Adds a modifier's type and value.
     * @details Its selector isn't checked.
     * @param   [in] modifier - The modifier.
     */
    void Add(const Modifier& modifier) { Add(modifier.type, modifier.value); }

    /**
     * @brief   Net total of everything added so far.
     * @return  The untyped sum, plus the best bonus and worst penalty of each type.
     */
    int Total() const;

private:
    int m_bestBonus[3] = {0, 0, 0};     /**< Highest bonus of each typed BonusType, indexed by its value. */
    int m_worstPenalty[3] = {0, 0, 0};  /**< Lowest penalty of each typed BonusType, indexed by its value. */
    int m_untyped = 0;                  /**< Sum of the untyped modifiers. */
};
