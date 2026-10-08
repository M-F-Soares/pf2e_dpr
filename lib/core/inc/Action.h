/**
 * @file    Action.h
 * @brief   Data description of an action: what is rolled, and what each degree of success does.
 */

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

/**
 * @brief   Shared, immutable handle to an Action.
 * @details Factories return these so turns can reuse them.
 */
using ActionPtr = std::shared_ptr<const Action>;

/**
 * @brief   Who an effect lands on, resolved when the action runs.
 */
enum class Who
{
    Actor,      /**< The creature doing the action. */
    Ally,       /**< The action's ally target (Action::allyName). */
    AllAllies,  /**< Every party member. */
    Enemy,      /**< The enemy. */
};

/**
 * @brief   When an effect applied by an action ends, relative to the creatures involved.
 * @details Resolved into a Duration when the action runs.
 */
struct DurationSpec
{
    Expiry when = Expiry::Never;  /**< Start or end of turn, or never. */
    Who of = Who::Actor;          /**< Whose turn start/end ends it. */
};

/**
 * @brief   The effect lasts for the rest of the simulated round.
 * @return  The duration spec.
 */
inline DurationSpec ForTheRound() { return {}; }

/**
 * @brief   The effect ends at the end of the actor's turn.
 * @return  The duration spec.
 */
inline DurationSpec UntilEndOfActorTurn() { return {Expiry::EndOfTurn, Who::Actor}; }

/**
 * @brief   The effect ends at the start of the actor's next turn.
 * @return  The duration spec.
 */
inline DurationSpec UntilStartOfActorTurn() { return {Expiry::StartOfTurn, Who::Actor}; }

/**
 * @brief   What kind of check an action rolls.
 */
enum class CheckKind
{
    None,     /**< Automatic: the Success outcome happens. */
    Attack,   /**< Actor rolls vs the enemy's defense as a DC (Strike, Demoralize, Trip, spell attack). */
    Save,     /**< Enemy rolls a save vs the actor's DC; outcomes are the ENEMY's degree. */
    FixedDC,  /**< Actor rolls vs a fixed DC (Aid, Battle Medicine). */
};

/**
 * @brief   The check an action rolls, with the numbers it needs.
 */
struct CheckSpec
{
    CheckKind kind = CheckKind::None;   /**< What is rolled. */
    int modifier = 0;                   /**< Attack/FixedDC: the actor's total bonus. Save: the actor's DC. */
    int itemBonus = 0;                  /**< Item bonus included in `modifier` (stacks properly with other item bonuses). */
    Tags tags = 0;                      /**< Tags of the actor's roll (or of the actor's DC for saves). */
    Defense defense = Defense::AC;      /**< Attack: the enemy defense used as the DC. Save: the save the enemy rolls. */
    int fixedDC = 0;                    /**< FixedDC: the DC. */
    bool attackTrait = false;           /**< Counts for and suffers the multiple attack penalty. */
    int attacks = 1;                    /**< Attacks it counts as for the penalty, applied afterwards (Spellstrike: 2). */
    bool agile = false;                 /**< Agile: -4/-8 multiple attack penalty instead of -5/-10. */
    int untypedPenalty = 0;             /**< Range increments, etc. (negative). */
    bool fortune = false;               /**< The roll has fortune (for CheckKind::Save, the enemy's save does). */
    bool targeted = true;               /**< Concealed/hidden flat checks apply. */
    int incapacitationRank = 0;         /**< > 0: incapacitation trait with this spell rank (or half level). */
};

/**
 * @brief   A condition an outcome applies.
 */
struct ApplyCondition
{
    ConditionKind kind;    /**< The condition. */
    int value = 1;         /**< Its value. */
    DurationSpec until;    /**< When it ends. */
    Who to = Who::Enemy;   /**< Who gets it. */
};

/**
 * @brief   A modifier an outcome applies.
 */
struct ApplyModifier
{
    Who to;                     /**< Who gets it. */
    Modifier mod;               /**< The modifier. */
    DurationSpec until;         /**< When it ends. */
    int uses = -1;              /**< -1 = unlimited; otherwise removed after applying to this many rolls. */
    bool onlyVsActor = false;   /**< e.g. Feint: off-guard only against the feinter. */
};

/**
 * @brief   A counter an outcome sets or clears (see CreatureState::counters).
 */
struct SetCounter
{
    Who on;            /**< Whose counter. */
    std::string name;  /**< Counter name. */
    int value = 1;     /**< 0 clears the counter. */
};

/**
 * @brief   Healing an outcome rolls: `count`d`die` + `flat`, one roll for every target.
 */
struct HealSpec
{
    int count = 0;       /**< Number of dice. */
    int die = 0;         /**< Die size. */
    int flat = 0;        /**< Flat healing added to the dice. */
    Who to = Who::Ally;  /**< Who is healed. */

    /**
     * @brief   Whether this heals at all.
     * @return  True when there are dice or flat healing.
     */
    bool Any() const { return count > 0 || flat > 0; }

    /**
     * @brief   Exact distribution of the healing roll.
     * @return  The distribution of `count`d`die` + `flat`.
     */
    Dist Distribution() const { return Dist::Dice(count, die) + flat; }
};

/**
 * @brief   An action an ally performs right away, outside their turn (Strike Hard!, ...).
 */
struct Grant
{
    std::string allyName;  /**< Name of the party member who acts. */
    ActionPtr action;      /**< What they do. It's resolved as their action, but on the current turn. */
};

struct EffectCtx;

/**
 * @brief   How a coupled spell (Spellstrike) gets its degree of success.
 */
enum class Coupling
{
    SameRoll,  /**< Spell attack spells: the Strike's result applies to the spell too. */
    OwnRoll,   /**< Save spells: the target saves normally, unless the Strike critically failed. */
};

/**
 * @brief   A second action resolved together with the main one (see Action::coupled).
 */
struct CoupledEffect
{
    ActionPtr action;                    /**< The coupled action (Spellstrike: the spell). */
    Coupling roll = Coupling::SameRoll;  /**< Where its degree of success comes from. */
};

/**
 * @brief   What happens on one degree of success.
 */
struct Outcome
{
    DamageScale damage = DamageScale::None;  /**< How the action's damage is scaled; None deals no damage. */
    std::vector<ApplyCondition> conditions;  /**< Conditions applied. */
    std::vector<ApplyModifier> modifiers;    /**< Modifiers applied. */
    std::vector<SetCounter> counters;        /**< Counters set or cleared. */
    HealSpec heal;                           /**< Healing rolled. */
    std::vector<Grant> grants;               /**< Actions allies take right away. */
    std::function<void(EffectCtx&)> custom;  /**< Escape hatch for anything else. */
};

/**
 * @brief   An action, spell or activity, described as data.
 * @details The engine reads it to roll the check and apply the outcome of each degree. See the factories in
 *          Actions.h.
 */
struct Action
{
    std::string name;                           /**< Name shown in reports. */
    int cost = 1;                               /**< Actions spent (0 = free). */
    bool reaction = false;                      /**< Uses the actor's reaction instead of actions. */
    CheckSpec check;                            /**< What is rolled. */
    std::array<Outcome, kDegrees> outcomes;     /**< Indexed by Degree of the rolled check. */
    std::vector<DamageComponent> damage;        /**< Base damage roll. */
    std::vector<DamageComponent> critDamage;    /**< Used instead of `damage` when doubled (fatal/deadly); may be empty. */
    Tags damageTags = 0;                        /**< For damage bonuses/penalties (Courageous Anthem, enfeebled). */
    std::optional<Weapon> weapon;               /**< Set for Strikes: character Features hook into these. */
    std::set<std::string> traits;               /**< "mental", "fear", ... (enemy immunities negate the action). */
    std::string allyName;                       /**< Name of the ally for Who::Ally effects. */

    /**
     * @brief   Optional precondition; when it returns false, the action is spent but has no effect.
     * @details Called with the state before the roll, the actor's index and the ally's index (-1 if none).
     */
    std::function<bool(const BattleState&, int actorIndex, int allyIndex)> usable;

    /**
     * @brief   Counter that marks a frequency-limited action as spent (Spellstrike "until recharged").
     * @details The action is skipped while the actor has this counter, and taking it sets the counter. Clear
     *          the counter to recharge.
     */
    std::string spentFlag;

    /**
     * @brief   A second action resolved together with this one (Spellstrike's spell).
     * @details Its damage joins this action's damage as one instance for resistances and weaknesses. Its heal
     *          and grants are ignored.
     */
    std::optional<CoupledEffect> coupled;
};

/**
 * @brief   The creatures in a scenario: one enemy and the party.
 */
struct Encounter
{
    Monster enemy;                 /**< The enemy (creature kEnemyIndex). */
    std::vector<Character> party;  /**< party[i] is creature i + 1. */

    /**
     * @brief   A party member by creature index.
     * @param   [in] creatureIndex - Creature index, 1 or more.
     * @return  The character.
     * @throws  std::out_of_range if there is no such party member.
     */
    const Character& GetCharacter(int creatureIndex) const { return party.at(creatureIndex - 1); }

    /**
     * @brief   The creature index of a party member.
     * @param   [in] characterName - The character's name.
     * @return  The index (1 for the first party member).
     * @throws  std::runtime_error if no party member has that name.
     */
    int IndexOf(const std::string& characterName) const;
};

/**
 * @brief   Passed to features and custom hooks for one outcome branch.
 */
struct EffectCtx
{
    const Encounter& encounter;             /**< The creatures in the scenario. */
    const Action& action;                   /**< The action being resolved. */
    Degree degree;                          /**< The degree of success of this branch. */
    const BattleState& before;              /**< State when the check was rolled (read this for conditions). */
    BattleState& state;                     /**< State being built for this branch (write effects here). */
    int actorIndex;                         /**< Index of the creature doing the action. */
    std::vector<DamageComponent>* damage;   /**< Non-null while computing a damaging outcome. */

    /**
     * @brief   The character doing the action.
     * @return  The actor's character sheet.
     */
    const Character& ActorCharacter() const { return encounter.GetCharacter(actorIndex); }

    /**
     * @brief   Whether the enemy was off-guard to this action when it was rolled.
     * @return  True if the enemy counted as off-guard to the actor's attack.
     */
    bool EnemyOffGuard() const
    {
        return before.IsOffGuardTo(kEnemyIndex, actorIndex, action.check.tags);
    }
};

/**
 * @brief   Read-only view for adaptive plans.
 */
struct View
{
    const Encounter& encounter;     /**< The creatures in the scenario. */
    const BattleState& state;       /**< The state of the branch the choice is made in. */
    int actorIndex;                 /**< Index of the creature choosing. */

    /**
     * @brief   The value of a condition on the enemy.
     * @param   [in] kind - The condition.
     * @return  Its value, or 0 if the enemy doesn't have it.
     */
    int EnemyCondition(ConditionKind kind) const { return state.ConditionValue(kEnemyIndex, kind); }

    /**
     * @brief   Whether the enemy is off-guard to the actor's attacks.
     * @param   [in] melee - True to check melee attacks, false for ranged ones.
     * @return  True if the enemy counts as off-guard to the actor.
     */
    bool EnemyOffGuardToMe(bool melee = true) const
    {
        return state.IsOffGuardTo(kEnemyIndex, actorIndex, melee ? Tag::Melee : Tag::Ranged);
    }

    /**
     * @brief   Actions the actor has left this turn.
     * @return  The number of actions.
     */
    int ActionsLeft() const { return state.ActionsLeft(actorIndex); }

    /**
     * @brief   A counter on the actor.
     * @param   [in] counterName - Counter name.
     * @return  Its value, or 0 if it isn't set.
     */
    int Counter(const std::string& counterName) const { return state.Counter(actorIndex, counterName); }
};

/**
 * @brief   One entry of a turn: either a fixed action or a choice made per branch.
 */
struct Step
{
    ActionPtr action;                               /**< The fixed action, or null for a choice. */
    std::function<ActionPtr(const View&)> chooser;  /**< Picks the action per branch, or returns null to do nothing. */

    /**
     * @brief   A fixed action.
     * @details Implicit, so a turn can be written as a list of actions.
     * @param   [in] fixedAction - The action.
     */
    Step(ActionPtr fixedAction) : action(std::move(fixedAction)) {}

    /**
     * @brief   A choice made separately in each branch.
     * @details Implicit, like the other constructor.
     * @param   [in] choice - Returns the action to take given the branch, or null to do nothing.
     */
    Step(std::function<ActionPtr(const View&)> choice) : chooser(std::move(choice)) {}
};

/**
 * @brief   A step that picks its action separately in each branch (adaptive plans).
 * @code
 * Choose([&](const View& v) { return v.EnemyOffGuardToMe() ? Strike(rogue, "Rapier") : Feint(rogue); })
 * @endcode
 * @param   [in] chooser - Returns the action to take given the branch, or null to do nothing.
 * @return  The step.
 */
inline Step Choose(std::function<ActionPtr(const View&)> chooser) { return Step(std::move(chooser)); }
