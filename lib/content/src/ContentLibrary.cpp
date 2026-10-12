/**
 * @file    ContentLibrary.cpp
 * @brief   Loading data files and looking up content by name.
 */

#include "ContentLibrary.h"

#include "DefinitionParser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
/**
 * @brief   Lists names for a message.
 * @param   [in] names - The names.
 * @return  The names separated by commas.
 */
std::string JoinNames(const std::vector<std::string>& names)
{
    std::string text;
    for (const std::string& name : names)
        text += (text.empty() ? "" : ", ") + name;
    return text;
}

/**
 * @brief   Whether a path is inside a folder (or is the folder).
 * @param   [in] path - The path.
 * @param   [in] folder - The folder.
 * @return  True if @p path is @p folder or inside it.
 */
bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& folder)
{
    std::filesystem::path relative =
        std::filesystem::weakly_canonical(path).lexically_relative(std::filesystem::weakly_canonical(folder));
    return !relative.empty() && *relative.begin() != "..";
}
} // namespace

ContentLibrary::ContentLibrary(HookRegistry hooks) : m_hooks(std::move(hooks)) {}

ContentLibrary ContentLibrary::Load(const std::string& directory, HookRegistry hooks)
{
    ContentLibrary library(std::move(hooks));
    library.AddDirectory(directory);
    return library;
}

void ContentLibrary::AddDirectory(const std::string& directory, const std::string& excludedFolder)
{
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::is_directory(directory, error))
        throw ContentError("content directory \"" + directory +
                           "\" not found (run from the project root, or pass the directory's path)");
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == ".json" &&
            (excludedFolder.empty() || !IsWithin(entry.path(), excludedFolder)))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files)
    {
        std::ifstream stream(file);
        if (!stream)
            throw ContentError("can't read \"" + file.generic_string() + "\"");
        std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        AddJson(text, file.generic_string());
    }
}

void ContentLibrary::AddJson(const std::string& text, const std::string& sourceName)
{
    DefinitionSet set = ParseDefinitions(text, sourceName, m_hooks);
    for (const ActionEntry& entry : set.actions)
        CheckNewName(entry.name, sourceName);
    for (const FeatureDefinition& feature : set.features)
        CheckNewName(feature.name, sourceName);
    for (ActionEntry& entry : set.actions)
    {
        std::string name = entry.name;
        m_actions.emplace(name, std::move(entry));
    }
    for (FeatureDefinition& feature : set.features)
    {
        std::string name = feature.name;
        m_features.emplace(name, std::move(feature));
    }
}

ActionPtr ContentLibrary::Use(const std::string& name, const Character& actor, const UseOptions& options) const
{
    auto found = m_actions.find(name);
    if (found == m_actions.end())
    {
        if (m_features.count(name) > 0)
            throw ContentError("\"" + name + "\" is a feature; use GetFeature()");
        throw ContentError("no action named \"" + name + "\"");
    }
    const ActionEntry& entry = found->second;
    const std::string& variantName = options.variant.empty() ? entry.defaultVariant : options.variant;
    auto variant = entry.versions.find(variantName);
    if (variant == entry.versions.end())
    {
        std::vector<std::string> variantNames;
        for (const auto& [knownName, versions] : entry.versions)
            variantNames.push_back(knownName);
        if (variantNames.size() == 1 && variantNames.front().empty())
            throw ContentError(name + ": has no variants, but \"" + options.variant + "\" was asked for");
        if (options.variant.empty())
            throw ContentError(name + ": needs a variant (UseOptions::variant): " + JoinNames(variantNames));
        throw ContentError(name + ": has no variant \"" + options.variant + "\" (it has: " +
                           JoinNames(variantNames) + ")");
    }

    const std::map<int, ActionDefinition>& versions = variant->second;
    int rank = ResolveRank(versions.begin()->second, actor, options.rank);
    const ActionDefinition& definition = std::prev(versions.upper_bound(rank))->second;
    ActionResolver resolve = [this](const std::string& grantedName, const Character& grantedActor,
                                    const UseOptions& grantedOptions) {
        return Use(grantedName, grantedActor, grantedOptions);
    };
    return BuildAction(definition, actor, options, rank, resolve);
}

Feature ContentLibrary::GetFeature(const std::string& name, const Character& owner) const
{
    auto found = m_features.find(name);
    if (found == m_features.end())
    {
        if (m_actions.count(name) > 0)
            throw ContentError("\"" + name + "\" is an action; use Use()");
        throw ContentError("no feature named \"" + name + "\"");
    }
    return BuildFeature(found->second, owner);
}

std::vector<std::string> ContentLibrary::Names() const
{
    std::vector<std::string> names;
    for (const auto& [name, entry] : m_actions)
        names.push_back(name);
    for (const auto& [name, feature] : m_features)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

void ContentLibrary::CheckNewName(const std::string& name, const std::string& sourceName) const
{
    std::string previous;
    if (auto action = m_actions.find(name); action != m_actions.end())
        previous = action->second.sourceName;
    else if (auto feature = m_features.find(name); feature != m_features.end())
        previous = feature->second.sourceName;
    else
        return;
    throw ContentError(sourceName + ": " + name + ": already defined in " + previous);
}
