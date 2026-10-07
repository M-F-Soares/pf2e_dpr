#pragma once

#include "Check.h"
#include "Creature.h"
#include "State.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct Action;
using ActionPtr = std::shared_ptr<const Action>;

// Who an effect lands on, resolved when the action runs.
enum class Who
{
    Actor,      // the creature doing the action
    Ally,       // the action's ally target (Action::allyTarget)
    AllAllies,  // every party member
    Enemy,
};

struct DurationSpec
{
    Expiry when = Expiry::Never;
    Who of = Who::Actor;  // whose turn start/end ends it
};

inline DurationSpec forTheRound() { return {}; }
inline DurationSpec untilEndOfActorTurn() { return {Expiry::EndOfTurn, Who::Actor}; }
inline DurationSpec untilStartOfActorTurn() { return {Expiry::StartOfTurn, Who::Actor}; }

enum class CheckKind
{
    None,     // automatic: the Success outcome happens
    Attack,   // actor rolls vs the enemy's defense as a DC (Strike, Demoralize, Trip, spell attack)
    Save,     // enemy rolls a save vs the actor's DC; outcomes are the ENEMY's degree
    FixedDC,  // actor rolls vs a fixed DC (Aid, Battle Medicine)
};

struct CheckSpec
{
    CheckKind kind = CheckKind::None;
    int modifier = 0;   // Attack/FixedDC: the actor's total bonus. Save: the actor's DC.
    int itemBonus = 0;  // item bonus included in `modifier` (stacks properly with other item bonuses)
    Tags tags = 0;      // tags of the actor's roll (or of the actor's DC for saves)
    Defense defense = Defense::AC;
    int fixedDC = 0;
    bool attackTrait = false;  // counts for and suffers the multiple attack penalty
    int attacks = 1;           // attacks it counts as for the penalty, applied afterwards (Spellstrike: 2)
    bool agile = false;
    int untypedPenalty = 0;    // range increments, etc. (negative)
    bool fortune = false;
    bool targeted = true;      // concealed/hidden flat checks apply
    int incapacitationRank = 0;  // > 0: incapacitation trait with this spell rank (or half level)
};

struct ApplyCondition
{
    ConditionKind kind;
    int value = 1;
    DurationSpec until;
    Who to = Who::Enemy;
};

struct ApplyModifier
{
    Who to;
    Modifier mod;
    DurationSpec until;
    int uses = -1;
    bool onlyVsActor = false;  // e.g. Feint: off-guard only against the feinter
};

struct SetCounter
{
    Who on;
    std::string name;
    int value = 1;  // 0 clears the counter
};

struct HealSpec
{
    int count = 0;
    int die = 0;
    int flat = 0;
    Who to = Who::Ally;

    bool any() const { return count > 0 || flat > 0; }
    Dist dist() const { return Dist::dice(count, die) + flat; }
};

// An action an ally performs right away, outside their turn (Strike Hard!, ...).
struct Grant
{
    std::string ally;
    ActionPtr action;
};

struct EffectCtx;

// How a coupled spell (Spellstrike) gets its degree of success.
enum class Coupling
{
    SameRoll,  // spell attack spells: the Strike's result applies to the spell too
    OwnRoll,   // save spells: the target saves normally, unless the Strike critically failed
};

struct CoupledEffect
{
    ActionPtr action;
    Coupling roll = Coupling::SameRoll;
};

// What happens on one degree of success.
struct Outcome
{
    DamageScale damage = DamageScale::None;
    std::vector<ApplyCondition> conditions;
    std::vector<ApplyModifier> modifiers;
    std::vector<SetCounter> counters;
    HealSpec heal;
    std::vector<Grant> grants;
    std::function<void(EffectCtx&)> custom;  // escape hatch for anything else
};

struct Action
{
    std::string name;
    int cost = 1;           // actions spent (0 = free)
    bool reaction = false;  // uses the actor's reaction instead of actions
    CheckSpec check;
    std::array<Outcome, kDegrees> outcomes;  // indexed by Degree of the rolled check
    std::vector<DamageComponent> damage;     // base damage roll
    std::vector<DamageComponent> critDamage; // used instead of `damage` when doubled (fatal/deadly); may be empty
    Tags damageTags = 0;                     // for damage bonuses/penalties (Courageous Anthem, enfeebled)
    std::optional<Weapon> weapon;            // set for Strikes: character Features hook into these
    std::set<std::string> traits;            // "mental", "fear", ... (enemy immunities negate the action)
    std::string allyTarget;                  // name of the ally for Who::Ally effects
    std::function<bool(const BattleState&, int actor, int ally)> usable;  // optional: false -> no effect
    // Frequency-limited (Spellstrike "until recharged"): skipped while the actor has this
    // counter, and taking the action sets it. Clear the counter to recharge.
    std::string spentFlag;
    // Resolved together with this action; its damage joins this action's damage as one
    // instance for resistances and weaknesses. Its heal and grants are ignored.
    std::optional<CoupledEffect> coupled;
};

struct Encounter
{
    Monster enemy;
    std::vector<Character> party;  // party[i] is creature i + 1

    const Character& character(int creature) const { return party.at(creature - 1); }
    int indexOf(const std::string& name) const;
};

// Passed to features and custom hooks for one outcome branch.
struct EffectCtx
{
    const Encounter& enc;
    const Action& action;
    Degree degree;
    const BattleState& before;  // state when the check was rolled (read this for conditions)
    BattleState& state;         // state being built for this branch (write effects here)
    int actor;
    std::vector<DamageComponent>* damage;  // non-null while computing a damaging outcome

    const Character& actorCharacter() const { return enc.character(actor); }
    bool enemyOffGuard() const
    {
        return before.isOffGuardTo(kEnemy, actor, action.check.tags);
    }
};

// Read-only view for adaptive plans.
struct View
{
    const Encounter& enc;
    const BattleState& state;
    int actor;

    int enemyCondition(ConditionKind k) const { return state.conditionValue(kEnemy, k); }
    bool enemyOffGuardToMe(bool melee = true) const
    {
        return state.isOffGuardTo(kEnemy, actor, melee ? Tag::Melee : Tag::Ranged);
    }
    int actionsLeft() const { return state.actionsLeft(actor); }
    int counter(const std::string& name) const { return state.counter(actor, name); }
};

// One entry of a turn: either a fixed action or a choice made per branch.
struct Step
{
    ActionPtr action;
    std::function<ActionPtr(const View&)> chooser;

    Step(ActionPtr a) : action(std::move(a)) {}
    Step(std::function<ActionPtr(const View&)> c) : chooser(std::move(c)) {}
};

inline Step choose(std::function<ActionPtr(const View&)> chooser) { return Step(std::move(chooser)); }
