/**
 * @file    ContentLibrary.h
 * @brief   Actions, spells and features loaded from data files, looked up by name.
 */

#pragma once

#include "Definitions.h"
#include "Hooks.h"
#include "Templates.h"

#include <map>
#include <string>
#include <vector>

/**
 * @brief   Content loaded from data files: actions, spells and features, looked up by name.
 * @details Every `.json` file under a directory holds one definition, or a list of them; README.md describes the
 *          format. Each Use() builds a fresh action with the numbers of the character using it.
 * @code
 *          ContentLibrary content = ContentLibrary::Load("data");
 *          scenario.AddTurn(bard, {content.Use("Courageous Anthem", bard),
 *                                  content.Use("Heroism", bard, {.ally = &rogue}),
 *                                  content.Use("Strike", bard, {.weapon = "Rapier"})});
 *          rogue.features = {content.GetFeature("Sneak Attack", rogue)};
 * @endcode
 */
class ContentLibrary
{
public:
    /**
     * @brief   Creates an empty library.
     * @param   [in] hooks - The hooks definitions may call.
     */
    explicit ContentLibrary(HookRegistry hooks = HookRegistry::BuiltIn());

    /**
     * @brief   Loads every definition under a directory.
     * @param   [in] directory - The directory, e.g. "data".
     * @param   [in] hooks - The hooks definitions may call.
     * @return  The library.
     * @throws  ContentError if the directory is missing, or a definition is invalid or defined twice.
     */
    static ContentLibrary Load(const std::string& directory, HookRegistry hooks = HookRegistry::BuiltIn());

    /**
     * @brief   Adds every definition under a directory: the `.json` files in it and its subdirectories.
     * @param   [in] directory - The directory.
     * @param   [in] excludedFolder - A folder inside it to leave out, e.g. "data/imported" for only the
     *          hand-written content; empty to leave nothing out.
     * @throws  ContentError if the directory is missing, or a definition is invalid or already defined.
     */
    void AddDirectory(const std::string& directory, const std::string& excludedFolder = "");

    /**
     * @brief   Adds the definitions in a JSON text.
     * @param   [in] text - One definition object, or a list of them.
     * @param   [in] sourceName - Where the text comes from, for error messages.
     * @throws  ContentError if a definition is invalid or already defined.
     */
    void AddJson(const std::string& text, const std::string& sourceName);

    /**
     * @brief   An action for a character to use in a scenario.
     * @param   [in] name - The action's name, e.g. "Demoralize".
     * @param   [in] actor - The character using it; the check reads their numbers.
     * @param   [in] options - Rank, target ally, variant, and the weapon, spell or skill it's used with.
     * @return  The action.
     * @throws  ContentError if there's no such action, the variant or rank doesn't exist, @p options lacks
     *          something the action needs or sets something it doesn't use, or the spell can't be carried.
     * @throws  std::runtime_error if @p actor has no modifier for the skill the check uses, or no such weapon.
     */
    ActionPtr Use(const std::string& name, const Character& actor, const UseOptions& options = {}) const;

    /**
     * @brief   A feature for a character to have (add it to Character::features).
     * @param   [in] name - The feature's name, e.g. "Sneak Attack".
     * @param   [in] owner - The character; their level picks the number of dice.
     * @return  The feature.
     * @throws  ContentError if there's no such feature.
     */
    Feature GetFeature(const std::string& name, const Character& owner) const;

    /**
     * @brief   Everything the library defines.
     * @return  The names of its actions and features, sorted.
     */
    std::vector<std::string> Names() const;

private:
    /**
     * @brief   Fails if a name is already defined.
     * @param   [in] name - The new definition's name.
     * @param   [in] sourceName - Where the new definition comes from.
     * @throws  ContentError if an action or feature already has that name.
     */
    void CheckNewName(const std::string& name, const std::string& sourceName) const;

    HookRegistry m_hooks;                                 /**< Hooks definitions may call. */
    std::map<std::string, ActionEntry> m_actions;         /**< Actions by name. */
    std::map<std::string, FeatureDefinition> m_features;  /**< Features by name. */
};
