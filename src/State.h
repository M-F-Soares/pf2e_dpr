#pragma once

#include "Condition.h"
#include "Damage.h"
#include "Modifier.h"

#include <compare>
#include <map>
#include <string>
#include <vector>

// Creature indices inside a BattleState: 0 is the enemy, 1..N the party in scenario order.
constexpr int kEnemy = 0;

enum class Expiry
{
    Never,       // lasts for the rest of the simulated round
    EndOfTurn,   // ends at the end of `creature`'s turn
    StartOfTurn, // ends at the start of `creature`'s turn
};

struct Duration
{
    Expiry when = Expiry::Never;
    int creature = -1;
    auto operator<=>(const Duration&) const = default;
};

struct ActiveCondition
{
    int value = 0;
    Duration until;
    auto operator<=>(const ActiveCondition&) const = default;
};

struct ActiveModifier
{
    Modifier mod;
    Duration until;
    int uses = -1;  // -1 = unlimited; otherwise removed after this many rolls it applied to
    auto operator<=>(const ActiveModifier&) const = default;
};

struct CreatureState
{
    std::map<ConditionKind, ActiveCondition> conditions;
    std::map<std::string, ActiveModifier> modifiers;  // keyed by source (+ opponent)
    std::map<std::string, int> counters;              // free-form flags for features/actions
    std::map<DamageType, DamageComponent> persistent;
    int attacksThisTurn = 0;  // for the multiple attack penalty
    int actionsUsed = 0;
    bool reactionUsed = false;
    int hpMissing = 0;

    auto operator<=>(const CreatureState&) const = default;
};

// Everything that can change later rolls. Damage dealt is NOT part of the
// state (it goes to the ledger), which keeps identical branches mergeable.
struct BattleState
{
    std::vector<CreatureState> creatures;

    CreatureState& operator[](int i) { return creatures[i]; }
    const CreatureState& operator[](int i) const { return creatures[i]; }

    // Sum of all applicable modifiers (conditions + active effects + `extra`) after
    // stacking rules. Sources with limited uses that applied are appended to `used`.
    int modifierTotal(int creature, const RollQuery& q, const std::vector<Modifier>& extra = {},
                      std::vector<std::string>* used = nullptr) const;

    bool isOffGuardTo(int creature, int attacker, Tags attackTags) const;
    int conditionValue(int creature, ConditionKind kind) const;
    int counter(int creature, const std::string& name) const;
    int actionsPerTurn(int creature) const;
    int actionsLeft(int creature) const { return actionsPerTurn(creature) - creatures[creature].actionsUsed; }

    // Same condition doesn't stack: the higher value wins.
    void addCondition(int creature, ConditionKind kind, int value, Duration until);
    // Same source doesn't stack: the stronger one wins.
    void addModifier(int creature, const ActiveModifier& m);
    // Same persistent damage type doesn't stack: the higher amount wins.
    void addPersistent(int creature, const DamageComponent& c);
    void consumeUses(int creature, const std::vector<std::string>& sources);

    void startTurn(int creature);
    void endTurn(int creature);

    auto operator<=>(const BattleState&) const = default;

private:
    void expire(Expiry when, int creature);
};

std::string modifierKey(const Modifier& m);
