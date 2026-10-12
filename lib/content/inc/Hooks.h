/**
 * @file    Hooks.h
 * @brief   Named C++ effects that data definitions can call.
 * @details A definition uses one with an effect like `{"hook": "uses-reaction"}`. Hooks are for the rare
 *          effect that the data format can't describe; register new ones before loading the data that uses
 *          them.
 */

#pragma once

#include "Action.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

/**
 * @brief   A named C++ effect, run on an outcome like Outcome::custom.
 */
using Hook = std::function<void(EffectCtx&)>;

/**
 * @brief   The hooks data definitions can call, by name.
 */
class HookRegistry
{
public:
    /**
     * @brief   The hooks every content library knows.
     * @details "uses-reaction": the actor's reaction is used up (Aid).
     * @return  The registry.
     */
    static HookRegistry BuiltIn();

    /**
     * @brief   Adds a hook, or replaces the one with the same name.
     * @param   [in] hookName - Name that definitions use.
     * @param   [in] hook - The effect.
     */
    void Register(const std::string& hookName, Hook hook);

    /**
     * @brief   Whether a hook exists.
     * @param   [in] hookName - The hook's name.
     * @return  True if it's registered.
     */
    bool Has(const std::string& hookName) const;

    /**
     * @brief   A hook by name.
     * @param   [in] hookName - The hook's name.
     * @return  The effect.
     * @throws  ContentError if no hook has that name.
     */
    const Hook& Get(const std::string& hookName) const;

    /**
     * @brief   The registered names, for error messages.
     * @return  The names, sorted.
     */
    std::vector<std::string> Names() const;

private:
    std::map<std::string, Hook> m_hooks;  /**< Hooks by name. */
};
