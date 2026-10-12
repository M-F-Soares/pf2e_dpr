/**
 * @file    Hooks.cpp
 * @brief   The hook registry and the built-in hooks.
 */

#include "Hooks.h"

#include "Definitions.h"

HookRegistry HookRegistry::BuiltIn()
{
    HookRegistry hooks;
    hooks.Register("uses-reaction", [](EffectCtx& ctx) { ctx.state[ctx.actorIndex].reactionUsed = true; });
    return hooks;
}

void HookRegistry::Register(const std::string& hookName, Hook hook)
{
    m_hooks[hookName] = std::move(hook);
}

bool HookRegistry::Has(const std::string& hookName) const
{
    return m_hooks.count(hookName) > 0;
}

const Hook& HookRegistry::Get(const std::string& hookName) const
{
    auto found = m_hooks.find(hookName);
    if (found == m_hooks.end())
        throw ContentError("no hook named \"" + hookName + "\"");
    return found->second;
}

std::vector<std::string> HookRegistry::Names() const
{
    std::vector<std::string> names;
    for (const auto& [hookName, hook] : m_hooks)
        names.push_back(hookName);
    return names;
}
