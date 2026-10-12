/**
 * @file    Templates.h
 * @brief   Turns content definitions into engine Actions and Features for one character.
 */

#pragma once

#include "Definitions.h"

#include <functional>
#include <optional>
#include <string>

/**
 * @brief   How a content action is used: the rank, the target ally, the variant, and what it's used with.
 * @details Set only what the action uses; anything else is an error. Designated initializers read well:
 *          `{.weapon = "Sword", .spell = fireRay}`.
 */
struct UseOptions
{
    int rank = 0;                     /**< Spell rank; 0 = its own rank (a cantrip: half the caster's level). */
    const Character* ally = nullptr;  /**< The ally it targets, for actions that affect one. */
    std::string variant;              /**< Variant ("2-action", "expert", ...); empty = the default one. */
    std::string weapon;               /**< Name of the actor's weapon, for Strikes and the like. */
    std::string allyWeapon;           /**< Name of the ally's weapon, for an action it lets the ally take. */
    ActionPtr spell;                  /**< The spell it carries (Spellstrike), already built for the actor. */
    std::optional<Skill> skill;       /**< The skill it's rolled with, for a check whose stat is "chosen". */
    int rangeIncrement = 1;           /**< Range increment of a weapon attack (-2 per increment past the first). */
};

/**
 * @brief   Builds another action by name, for actions that let an ally act (ContentLibrary::Use).
 */
using ActionResolver =
    std::function<ActionPtr(const std::string& name, const Character& actor, const UseOptions& options)>;

/**
 * @brief   The rank a spell is cast at.
 * @param   [in] definition - Any version of the spell (they all have the same base rank).
 * @param   [in] caster - The caster; a cantrip heightens to half their level, rounded up.
 * @param   [in] requestedRank - The rank asked for; 0 = the default.
 * @return  The rank; 0 for an action that isn't a spell.
 * @throws  ContentError if a rank is asked for something that isn't a spell, or the rank is out of range.
 */
int ResolveRank(const ActionDefinition& definition, const Character& caster, int requestedRank);

/**
 * @brief   Builds an action for one character.
 * @details Reads the check's numbers from @p actor (and the weapon or skill in @p options), adds the
 *          "Heightened (+N)" steps for @p rank, couples the carried spell, builds the actions it lets the ally
 *          take, and sets up the temporary immunity, if any.
 * @param   [in] definition - The version to build (already picked for the variant and rank).
 * @param   [in] actor - The character using it.
 * @param   [in] options - The ally, weapon, spell and skill it's used with.
 * @param   [in] rank - The rank from ResolveRank(); 0 for an action that isn't a spell.
 * @param   [in] resolve - Builds the actions it lets the ally take; may be empty if it lets them take none.
 * @return  The action.
 * @throws  ContentError if @p options lacks something the action needs, sets something it doesn't use, or
 *          has a spell it can't carry.
 * @throws  std::runtime_error if @p actor has no modifier for the skill the check uses, or no such weapon.
 */
ActionPtr BuildAction(const ActionDefinition& definition, const Character& actor, const UseOptions& options,
                      int rank, const ActionResolver& resolve = {});

/**
 * @brief   Builds a feature for one character.
 * @param   [in] definition - The feature.
 * @param   [in] owner - The character who has it; their level picks the number of dice.
 * @return  The feature, to add to Character::features.
 */
Feature BuildFeature(const FeatureDefinition& definition, const Character& owner);
