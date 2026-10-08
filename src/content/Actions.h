/**
 * @file    Actions.h
 * @brief   Ready-made actions, spells and class features (Remaster rules).
 * @details Each factory reads the numbers it needs from the Character, so a scenario line reads like the
 *          table: strike(rogue, "Rapier"), demoralize(fighter)...
 */

#pragma once

#include "../Action.h"

#include <string>
#include <vector>

/**
 * @brief   Options for strike().
 */
struct StrikeOptions
{
    std::string name;                         /**< Defaults to "Strike (<weapon>)". */
    int cost = 1;                             /**< Actions spent. */
    bool reaction = false;                    /**< Uses the reaction instead of actions (Strike Hard!). */
    int rangeIncrement = 1;                   /**< -2 untyped per increment beyond the first. */
    bool fortune = false;                     /**< Roll the attack with fortune. */
    std::vector<DamageComponent> extraOnHit;  /**< e.g. Spellstrike spell damage (doubled on crit). */
};

/**
 * @brief   A Strike with one of the character's weapons.
 * @details Rolls the weapon's attack bonus against AC, with the multiple attack penalty. A hit deals the
 *          weapon's damage and property runes. A critical hit doubles that (with fatal's larger dice), then
 *          adds the fatal and deadly extra dice and crit-only riders, which aren't doubled.
 * @param   [in] c - The attacker.
 * @param   [in] weapon - Name of one of @p c's weapons.
 * @param   [in] opts - Name, cost, range and other options.
 * @return  The action.
 * @throws  std::runtime_error if @p c has no weapon with that name.
 */
ActionPtr strike(const Character& c, const std::string& weapon, StrikeOptions opts = {});

/**
 * @name    Skill actions
 * @{
 */

/**
 * @brief   Demoralize: Intimidation against the enemy's Will DC.
 * @details Success: frightened 1; critical success: frightened 2. Whatever the result, the enemy is then
 *          immune to this character's Demoralize for the round.
 * @param   [in] c - The character.
 * @return  The action.
 * @throws  std::runtime_error if @p c has no Intimidation modifier.
 */
ActionPtr demoralize(const Character& c);

/**
 * @brief   Feint: Deception against the enemy's Perception DC.
 * @details Success: the enemy is off-guard to the character's next melee attack this turn; critical success:
 *          to all their melee attacks for the rest of the round.
 * @param   [in] c - The character.
 * @return  The action.
 * @throws  std::runtime_error if @p c has no Deception modifier.
 */
ActionPtr feint(const Character& c);

/**
 * @brief   Trip: Athletics against the enemy's Reflex DC, with the attack trait.
 * @details Success: prone; critical success: prone and 1d6 bludgeoning; critical failure: the character falls
 *          prone.
 * @param   [in] c - The character.
 * @return  The action.
 * @throws  std::runtime_error if @p c has no Athletics modifier.
 */
ActionPtr trip(const Character& c);

/**
 * @brief   Grapple: Athletics against the enemy's Fortitude DC, with the attack trait.
 * @details Success: grabbed; critical success: restrained.
 * @param   [in] c - The character.
 * @return  The action.
 * @throws  std::runtime_error if @p c has no Athletics modifier.
 */
ActionPtr grapple(const Character& c);

/**
 * @brief   Aid: a DC 15 check that gives the ally a bonus to their next matching roll this round.
 * @details Circumstance bonus of +1 on a success and @p critBonus on a critical success; -1 penalty on a
 *          critical failure. Simplified: the preparation and the reaction both happen on the aider's turn,
 *          and the reaction is used up.
 * @param   [in] aider - The character aiding.
 * @param   [in] ally - The character being aided.
 * @param   [in] modifier - The aider's modifier for the check.
 * @param   [in] critBonus - Bonus on a critical success (2, or more with master or legendary proficiency).
 * @param   [in] aided - Which of the ally's rolls the bonus applies to.
 * @return  The action.
 */
ActionPtr aid(const Character& aider, const Character& ally, int modifier, int critBonus = 2,
              Selector aided = select({Tag::AttackRoll}));

/**
 * @brief   Battle Medicine: Medicine against @p dc.
 * @details Success heals 2d8 + @p bonusHealing; critical success 4d8 + @p bonusHealing. The patient is then
 *          immune to Battle Medicine for the round.
 * @param   [in] medic - The character using Medicine.
 * @param   [in] patient - The character being healed.
 * @param   [in] dc - The check's DC (higher proficiency allows a higher DC with bonus healing).
 * @param   [in] bonusHealing - Flat healing added on a success.
 * @return  The action.
 * @throws  std::runtime_error if @p medic has no Medicine modifier.
 */
ActionPtr battleMedicine(const Character& medic, const Character& patient, int dc = 15, int bonusHealing = 0);

/** @} */

/**
 * @name    Class actions
 * @{
 */

/**
 * @brief   Hunt Prey: marks the enemy as the ranger's prey (the "prey" counter, read by precisionEdge()).
 * @param   [in] ranger - The ranger.
 * @return  The action.
 */
ActionPtr huntPrey(const Character& ranger);

/**
 * @brief   Strike Hard! (commander tactic, 2 actions): the ally immediately Strikes with their reaction.
 * @details The Strike has no multiple attack penalty, since it isn't the ally's turn.
 * @param   [in] commander - The commander.
 * @param   [in] ally - The ally who Strikes.
 * @param   [in] allyWeapon - Name of the weapon the ally Strikes with.
 * @return  The action.
 * @throws  std::runtime_error if @p ally has no weapon with that name.
 */
ActionPtr strikeHard(const Character& commander, const Character& ally, const std::string& allyWeapon);

/**
 * @brief   Magus: a melee Strike carrying a 1- or 2-action spell (from spellAttackSpell or basicSaveSpell).
 * @details A spell attack spell uses the Strike's result; a save spell's target saves normally unless the
 *          Strike critically fails. Damage is combined with the Strike's. Counts as two attacks for the
 *          multiple attack penalty, and can't be used again until recharged.
 * @param   [in] magus - The magus.
 * @param   [in] weapon - Name of the weapon to Strike with.
 * @param   [in] spell - The spell, from spellAttackSpell() or basicSaveSpell().
 * @return  The action (2 actions).
 * @throws  std::invalid_argument if @p spell is null, has neither an attack roll nor a save, or doesn't take
 *          1 or 2 actions.
 * @throws  std::runtime_error if @p magus has no weapon with that name.
 */
ActionPtr spellstrike(const Character& magus, const std::string& weapon, const ActionPtr& spell);

/**
 * @brief   Overwhelming Spellstrike (magus feat 12): a Spellstrike whose spell ignores some resistance.
 * @details The spell ignores the target's acid, cold, electricity, fire and sonic resistance up to the
 *          magus's level (persistent damage included).
 * @param   [in] magus - The magus; their level is the resistance ignored.
 * @param   [in] weapon - Name of the weapon to Strike with.
 * @param   [in] spell - The spell, from spellAttackSpell() or basicSaveSpell().
 * @return  The action (2 actions).
 * @throws  std::invalid_argument for the same spells spellstrike() rejects.
 * @throws  std::runtime_error if @p magus has no weapon with that name.
 */
ActionPtr overwhelmingSpellstrike(const Character& magus, const std::string& weapon, const ActionPtr& spell);

/**
 * @brief   Recharges Spellstrike (1 action), so it can be used again.
 * @param   [in] magus - The magus.
 * @return  The action.
 */
ActionPtr rechargeSpellstrike(const Character& magus);

/** @} */

/**
 * @name    Spells
 * @{
 */

/**
 * @brief   Sure Strike (1 action): the caster's next attack roll this round has fortune.
 * @param   [in] caster - The caster.
 * @return  The action.
 */
ActionPtr sureStrike(const Character& caster);

/**
 * @brief   Heroism (2 actions): status bonus to the ally's checks for the round.
 * @details Applies to attack rolls, saves, skill checks and Perception.
 * @param   [in] caster - The caster.
 * @param   [in] ally - The target.
 * @param   [in] rank - Spell rank: +1 below rank 6, +2 from rank 6, +3 from rank 9.
 * @return  The action.
 */
ActionPtr heroism(const Character& caster, const Character& ally, int rank = 3);

/**
 * @brief   Bless (2 actions): +1 status bonus to attack rolls for the whole party for the round.
 * @param   [in] caster - The caster.
 * @return  The action.
 */
ActionPtr bless(const Character& caster);

/**
 * @brief   Courageous Anthem (1 action): +1 status bonus to the party's attack and damage rolls.
 * @details Lasts for the round.
 * @param   [in] bard - The bard.
 * @return  The action.
 */
ActionPtr courageousAnthem(const Character& bard);

/**
 * @brief   Fear (2 actions): Will save against the caster's spell DC.
 * @details The enemy is frightened 1 on a success, 2 on a failure, and 3 on a critical failure.
 * @param   [in] caster - The caster.
 * @param   [in] rank - Spell rank. Heightening only adds targets, so it doesn't change the result.
 * @return  The action.
 */
ActionPtr fear(const Character& caster, int rank = 1);

/**
 * @brief   Heal (1 to 3 actions).
 * @details 1 action: rank d8 to the ally. 2 actions: rank d8 + 8 x rank to the ally. 3 actions: rank d8 to
 *          every party member.
 * @param   [in] caster - The caster.
 * @param   [in] ally - The target (unused with 3 actions).
 * @param   [in] rank - Spell rank.
 * @param   [in] actions - Actions spent casting it, 1 to 3.
 * @return  The action.
 */
ActionPtr heal(const Character& caster, const Character& ally, int rank, int actions);

/**
 * @brief   A spell with a basic save against the caster's spell DC.
 * @details Damage by the enemy's degree of success: none on a critical success, half on a success, full on a
 *          failure, double on a critical failure.
 * @param   [in] caster - The caster.
 * @param   [in] name - Spell name.
 * @param   [in] cost - Actions spent casting it.
 * @param   [in] save - The save the enemy rolls.
 * @param   [in] damage - The damage roll.
 * @param   [in] traits - The spell's traits (the enemy may be immune to one).
 * @param   [in] incapacitationRank - Spell rank if it has the incapacitation trait, else 0.
 * @return  The action.
 */
ActionPtr basicSaveSpell(const Character& caster, const std::string& name, int cost, Defense save,
                         std::vector<DamageComponent> damage, std::set<std::string> traits = {},
                         int incapacitationRank = 0);

/**
 * @brief   A ranged spell attack against AC, with the attack trait.
 * @details Full damage on a hit, double on a critical hit.
 * @param   [in] caster - The caster.
 * @param   [in] name - Spell name.
 * @param   [in] cost - Actions spent casting it.
 * @param   [in] damage - The damage roll.
 * @param   [in] traits - The spell's traits ("attack" is added).
 * @return  The action.
 */
ActionPtr spellAttackSpell(const Character& caster, const std::string& name, int cost,
                           std::vector<DamageComponent> damage, std::set<std::string> traits = {});

/** @} */

/**
 * @name    Class features (add to Character::features)
 * @{
 */

/**
 * @brief   Sneak Attack: extra precision damage against an off-guard enemy.
 * @details Applies to Strikes with an agile, finesse or ranged weapon when the enemy is off-guard to the
 *          attacker.
 * @param   [in] dice - Number of d6s.
 * @return  The feature.
 */
Feature sneakAttack(int dice);

/**
 * @brief   Precision edge.
 * @details First hit each round vs. hunted prey: +Nd8 precision.
 * @param   [in] dice - Number of d8s.
 * @return  The feature.
 */
Feature precisionEdge(int dice = 1);

/** @} */
