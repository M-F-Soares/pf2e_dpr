#pragma once

#include <cstdint>
#include <compare>
#include <string>
#include <vector>

// A roll or DC is described by a set of tags. A modifier applies when one of
// its selector alternatives is fully contained in the roll's tags.
//   Frightened -> {Check} or {DC}       (all checks and DCs)
//   Clumsy     -> {Dex}                 (Dex-based rolls/DCs: AC, Reflex, ranged attacks...)
//   Heroism    -> {AttackRoll} or {Save} or {Skill|Check} or {Perception|Check}
using Tags = std::uint32_t;

namespace Tag
{
enum : Tags
{
    Check = 1u << 0,
    DC = 1u << 1,
    AttackRoll = 1u << 2,
    Damage = 1u << 3,
    AC = 1u << 4,
    Save = 1u << 5,
    Fort = 1u << 6,
    Ref = 1u << 7,
    Will = 1u << 8,
    Perception = 1u << 9,
    Skill = 1u << 10,
    Spell = 1u << 11,
    Melee = 1u << 12,
    Ranged = 1u << 13,
    Str = 1u << 14,
    Dex = 1u << 15,
    Con = 1u << 16,
    Int = 1u << 17,
    Wis = 1u << 18,
    Cha = 1u << 19,
    ClassDC = 1u << 20,
    Healing = 1u << 21,
};
}

enum class Attribute
{
    None,
    Str,
    Dex,
    Con,
    Int,
    Wis,
    Cha
};

Tags attributeTag(Attribute a);

struct Selector
{
    std::vector<Tags> anyOf;

    bool matches(Tags query) const;
    auto operator<=>(const Selector&) const = default;
};

Selector select(std::initializer_list<Tags> alternatives);

enum class BonusType
{
    Circumstance,
    Item,
    Status,
    Untyped
};

const char* toString(BonusType t);

struct Modifier
{
    std::string source;          // same source never stacks with itself
    BonusType type = BonusType::Untyped;
    int value = 0;               // positive = bonus, negative = penalty
    Selector applies;
    int vsCreature = -1;         // only against this opposing creature (-1 = anyone)
    bool grantsOffGuard = false; // the holder counts as off-guard (for precision damage, etc.)

    auto operator<=>(const Modifier&) const = default;
};

// What the roll is, and who is on the other side of it.
struct RollQuery
{
    Tags tags = 0;
    int opponent = -1;
};

bool appliesTo(const Modifier& m, const RollQuery& q);

// PF2e stacking: per type (circumstance/item/status) only the highest bonus
// and the worst penalty count; untyped modifiers all stack.
class Stacker
{
public:
    void add(BonusType type, int value);
    void add(const Modifier& m) { add(m.type, m.value); }
    int total() const;

private:
    int m_bestBonus[3] = {0, 0, 0};
    int m_worstPenalty[3] = {0, 0, 0};
    int m_untyped = 0;
};
