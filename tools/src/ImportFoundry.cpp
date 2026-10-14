/**
 * @file    ImportFoundry.cpp
 * @brief   The importer program (`make import`): converts Foundry VTT's spells into content definitions.
 * @details Usage: `import-foundry <foundry-clone> <data-dir> <output-dir> [source-revision] [triage-list]`.
 *          Writes `<output-dir>/spells.json` and `<output-dir>/import-report.md`, and prints a summary. Spells
 *          already defined by hand in `<data-dir>` are left out, so hand-written definitions win, and so are the
 *          spells the triage list leaves out. Buffs need the clone's spell effect folder too.
 */

#include "ContentLibrary.h"
#include "DefinitionParser.h"
#include "FoundryImport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;  /**< A parsed JSON value that keeps its keys in order. */

/**
 * @brief   Reads a whole file.
 * @param   [in] path - The file.
 * @return  Its contents.
 * @throws  std::runtime_error if it can't be read.
 */
std::string ReadFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("can't read \"" + path.generic_string() + "\"");
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

/**
 * @brief   Writes a whole file, replacing it.
 * @param   [in] path - The file.
 * @param   [in] text - Its new contents.
 * @throws  std::runtime_error if it can't be written.
 */
void WriteFile(const fs::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!(stream << text))
        throw std::runtime_error("can't write \"" + path.generic_string() + "\"");
}

/**
 * @brief   The .json files under a folder, sorted, without Foundry's `_folders.json` files.
 * @param   [in] folder - The folder.
 * @return  The files.
 */
std::vector<fs::path> JsonFiles(const fs::path& folder)
{
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(folder))
        if (entry.is_regular_file() && entry.path().extension() == ".json" &&
            entry.path().filename() != "_folders.json")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}

/**
 * @brief   The folder of spells in a clone of Foundry's repository.
 * @param   [in] clone - The clone.
 * @return  `packs/pf2e/spells`, or `packs/spells` in older versions.
 * @throws  std::runtime_error if neither exists.
 */
fs::path SpellFolder(const fs::path& clone)
{
    for (const char* candidate : {"packs/pf2e/spells", "packs/spells"})
        if (fs::is_directory(clone / candidate))
            return clone / candidate;
    throw std::runtime_error("no spell folder in \"" + clone.generic_string() +
                             "\" (looked for packs/pf2e/spells and packs/spells)");
}

/**
 * @brief   Foundry's spell effects, which hold the bonuses of buff spells.
 * @param   [in] clone - The clone.
 * @return  Each effect's file contents, by the effect's name; empty if the clone has no spell effect folder
 *          (`packs/pf2e/spell-effects`, or `packs/spell-effects` in older versions).
 * @throws  std::runtime_error if a file can't be read.
 */
std::map<std::string, std::string> SpellEffects(const fs::path& clone)
{
    std::map<std::string, std::string> effects;
    for (const char* candidate : {"packs/pf2e/spell-effects", "packs/spell-effects"})
    {
        if (!fs::is_directory(clone / candidate))
            continue;
        for (const fs::path& file : JsonFiles(clone / candidate))
        {
            std::string text = ReadFile(file);
            Json effect = Json::parse(text, nullptr, false);
            if (effect.is_object() && effect.contains("name") && effect.at("name").is_string())
                effects.emplace(effect.at("name").get<std::string>(), std::move(text));
        }
        break;
    }
    return effects;
}

/**
 * @brief   A spell that was left out, and the specifics of why.
 */
struct SkippedSpell
{
    std::string name;    /**< The spell's name. */
    std::string detail;  /**< The specifics; may be empty. */
};

/**
 * @brief   Everything one import found.
 */
struct ImportResult
{
    std::size_t spellCount = 0;                                   /**< Spells read from Foundry. */
    std::size_t effectCount = 0;                                  /**< Spell effects read from Foundry. */
    std::vector<std::pair<std::string, Json>> imported;           /**< Converted definitions, by name. */
    std::map<std::string, std::vector<std::string>> warnings;     /**< Things to check, by spell name. */
    std::map<std::string, std::vector<SkippedSpell>> skipped;     /**< Spells still to import, by reason. */
    std::vector<std::string> handWritten;                         /**< Spells defined by hand in the data folder. */
    std::map<std::string, std::vector<std::string>> triaged;      /**< Spells the triage leaves out, by group. */
    std::optional<SpellTriage> triage;                            /**< The triage list, if one was given. */
    std::vector<std::string> triageNotFound;                      /**< Triaged spells Foundry's data doesn't have. */
};

/**
 * @brief   How the spells the triage found worth importing fared.
 */
struct TriageProgress
{
    std::size_t total = 0;       /**< Spells worth importing. */
    std::size_t imported = 0;    /**< Imported. */
    std::size_t handWritten = 0; /**< Defined by hand. */
    std::size_t remaining = 0;   /**< Still to import. */
};

/**
 * @brief   Counts how the spells the triage found worth importing fared.
 * @param   [in] result - The import, with a triage.
 * @return  The counts.
 */
TriageProgress Progress(const ImportResult& result)
{
    TriageProgress progress;
    const std::set<std::string>& contributing = result.triage->contributing;
    progress.total = contributing.size();
    for (const auto& [name, definition] : result.imported)
        progress.imported += contributing.count(name);
    for (const std::string& name : result.handWritten)
        progress.handWritten += contributing.count(name);
    for (const auto& [reason, spells] : result.skipped)
        for (const SkippedSpell& spell : spells)
            progress.remaining += contributing.count(spell.name);
    return progress;
}

/**
 * @brief   The reason in a ParseDefinitions() error, without the file and definition it names.
 * @param   [in] message - The error message, `<source>: <name> (heightened N): <reason>`.
 * @param   [in] sourceName - The source it names.
 * @param   [in] name - The definition it names.
 * @return  The reason, the same text for the same problem.
 */
std::string ValidationReason(const std::string& message, const std::string& sourceName, const std::string& name)
{
    static const std::regex where(R"(^(?: \((?:variant|heightened) [^)]*\))*: )");
    std::string reason = message;
    std::string prefix = sourceName + ": " + name;
    if (reason.rfind(prefix, 0) == 0)
        reason.erase(0, prefix.size());
    return std::regex_replace(reason, where, "", std::regex_constants::format_first_only);
}

/**
 * @brief   Escapes text for a Markdown table cell.
 * @param   [in] text - The text.
 * @return  The text with `|` escaped.
 */
std::string TableCell(std::string text)
{
    for (std::size_t at = text.find('|'); at != std::string::npos; at = text.find('|', at + 2))
        text.replace(at, 1, "\\|");
    return text;
}

/**
 * @brief   Lists by key, most entries first.
 * @tparam  Entry - What the lists hold.
 * @param   [in] lists - The lists, by key (a reason or a group).
 * @return  (key, list) pairs.
 */
template <typename Entry>
std::vector<std::pair<std::string, std::vector<Entry>>> ByCount(const std::map<std::string, std::vector<Entry>>& lists)
{
    std::vector<std::pair<std::string, std::vector<Entry>>> sorted(lists.begin(), lists.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto& left, const auto& right) { return left.second.size() > right.second.size(); });
    return sorted;
}

/**
 * @brief   The import report, in Markdown.
 * @param   [in] result - The import.
 * @param   [in] revision - Foundry's commit the spells came from.
 * @return  The report: counts, progress on the triage, reasons and groups by count, warnings, and every spell
 *          left out with its reason or group.
 */
std::string Report(const ImportResult& result, const std::string& revision)
{
    std::size_t remainingCount = 0;
    for (const auto& [reason, spells] : result.skipped)
        remainingCount += spells.size();
    std::size_t triagedCount = 0;
    for (const auto& [group, names] : result.triaged)
        triagedCount += names.size();

    std::ostringstream report;
    report << "# Foundry VTT spell import\n\n"
           << "Spells from github.com/foundryvtt/pf2e at commit `" << revision << "`, made by `make import`. "
           << "Don't edit this folder; change the importer or write the definition by hand in data/, and import "
           << "again.\n\n"
           << "Imported **" << result.imported.size() << "** of " << result.spellCount << " spells ("
           << result.warnings.size() << " with something to check). Written by hand in data/: "
           << result.handWritten.size() << ". Left out by the triage: " << triagedCount
           << ". Still to import: " << remainingCount << ".\n\n"
           << (result.effectCount > 0
                   ? "Buffs were read from " + std::to_string(result.effectCount) + " spell effects.\n\n"
                   : std::string("No spell effects were found, so buffs were left out.\n\n"));
    if (result.triage)
    {
        TriageProgress progress = Progress(result);
        report << "Of the " << progress.total << " spells the triage (tools/data/spell-triage.tsv) found worth "
               << "importing, **" << progress.imported << "** are imported and " << progress.handWritten
               << " are written by hand; " << progress.remaining << " are still to import.\n\n";
        if (!result.triageNotFound.empty())
        {
            report << "The triage lists " << result.triageNotFound.size()
                   << " spells that aren't in Foundry's data (renamed or removed):";
            for (std::size_t index = 0; index < result.triageNotFound.size(); ++index)
                report << (index == 0 ? " " : ", ") << result.triageNotFound[index];
            report << ".\n\n";
        }
    }
    else
        report << "No triage list was given, so no spell was left out on purpose.\n\n";

    report << "## Still to import, by reason\n\n| Spells | Reason |\n|---:|---|\n";
    auto reasons = ByCount(result.skipped);
    for (const auto& [reason, spells] : reasons)
        report << "| " << spells.size() << " | " << TableCell(reason) << " |\n";

    auto groups = ByCount(result.triaged);
    if (!groups.empty())
    {
        report << "\n## Left out by the triage, by group\n\n| Spells | Group |\n|---:|---|\n";
        for (const auto& [group, names] : groups)
            report << "| " << names.size() << " | " << TableCell(group) << " |\n";
    }

    report << "\n## Imported, with something to check\n\n";
    for (const auto& [name, notes] : result.warnings)
        for (const std::string& note : notes)
            report << "- **" << name << "**: " << note << "\n";

    report << "\n## Imported\n\n";
    for (const auto& [name, definition] : result.imported)
        report << "- " << name << "\n";

    report << "\n## Written by hand\n\n";
    for (const std::string& name : result.handWritten)
        report << "- " << name << "\n";

    report << "\n## Still to import\n";
    for (const auto& [reason, spells] : reasons)
    {
        report << "\n### " << reason << " (" << spells.size() << ")\n\n";
        for (const SkippedSpell& spell : spells)
            report << "- " << spell.name << (spell.detail.empty() ? "" : ": " + spell.detail) << "\n";
    }

    if (!groups.empty())
    {
        report << "\n## Left out by the triage\n";
        for (const auto& [group, names] : groups)
        {
            report << "\n### " << group << " (" << names.size() << ")\n\n";
            for (const std::string& name : names)
                report << "- " << name << "\n";
        }
    }
    return report.str();
}

/**
 * @brief   Imports Foundry's spells.
 * @param   [in] clone - A clone of github.com/foundryvtt/pf2e (only the spell and spell effect folders are needed).
 * @param   [in] dataFolder - The content folder; its hand-written definitions win over imported ones.
 * @param   [in] outputFolder - Where to write spells.json and the report (usually inside @p dataFolder).
 * @param   [in] revision - Foundry's commit, for the report.
 * @param   [in] triageFile - The triage list; empty to import every spell that converts.
 * @throws  std::runtime_error or ContentError if a folder or file can't be read or written, the triage list is
 *          malformed, or the hand-written content doesn't load.
 */
void Import(const fs::path& clone, const fs::path& dataFolder, const fs::path& outputFolder,
            const std::string& revision, const fs::path& triageFile)
{
    fs::path spellFolder = SpellFolder(clone);
    std::map<std::string, std::string> spellEffects = SpellEffects(clone);
    ContentLibrary content;
    content.AddDirectory(dataFolder.string(), outputFolder.string());
    std::vector<std::string> names = content.Names();
    std::set<std::string> handWritten(names.begin(), names.end());

    HookRegistry hooks = HookRegistry::BuiltIn();
    ImportResult result;
    result.effectCount = spellEffects.size();
    if (!triageFile.empty())
        result.triage = ReadSpellTriage(ReadFile(triageFile));
    std::set<std::string> foundryNames;
    std::set<std::string> importedNames;
    for (const fs::path& file : JsonFiles(spellFolder))
    {
        ++result.spellCount;
        std::string sourceName = file.lexically_relative(clone).generic_string();
        SpellConversion conversion = ConvertFoundrySpell(ReadFile(file), spellEffects);
        std::string name = conversion.name.empty() ? sourceName : conversion.name;
        foundryNames.insert(name);
        auto skip = [&](const std::string& reason, const std::string& detail) {
            result.skipped[reason].push_back(SkippedSpell{name, detail});
        };
        if (result.triage)
            if (auto leftOut = result.triage->leftOut.find(name); leftOut != result.triage->leftOut.end())
            {
                result.triaged[leftOut->second].push_back(name);
                continue;
            }
        if (handWritten.count(name) > 0)
        {
            result.handWritten.push_back(name);
            continue;
        }
        if (conversion.definition.empty())
        {
            skip(conversion.skipReason, conversion.skipDetail);
            continue;
        }
        if (importedNames.count(name) > 0)
        {
            skip("another spell has the same name", sourceName);
            continue;
        }
        try
        {
            ParseDefinitions(conversion.definition, sourceName, hooks);
        }
        catch (const ContentError& error)
        {
            skip(ValidationReason(error.what(), sourceName, name), "");
            continue;
        }
        importedNames.insert(name);
        result.imported.emplace_back(name, Json::parse(conversion.definition));
        if (!conversion.warnings.empty())
            result.warnings[name] = conversion.warnings;
    }
    if (result.triage)
    {
        for (const std::string& name : result.triage->contributing)
            if (foundryNames.count(name) == 0)
                result.triageNotFound.push_back(name);
        for (const auto& [name, group] : result.triage->leftOut)
            if (foundryNames.count(name) == 0)
                result.triageNotFound.push_back(name);
        std::sort(result.triageNotFound.begin(), result.triageNotFound.end());
    }

    std::sort(result.imported.begin(), result.imported.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    std::sort(result.handWritten.begin(), result.handWritten.end());
    Json spells = Json::array();
    for (const auto& [name, definition] : result.imported)
        spells.push_back(definition);
    std::string spellsText = spells.dump(4) + "\n";
    fs::path spellsFile = outputFolder / "spells.json";
    content.AddJson(spellsText, spellsFile.generic_string());  // the whole file must load with the rest

    fs::create_directories(outputFolder);
    WriteFile(spellsFile, spellsText);
    fs::path reportFile = outputFolder / "import-report.md";
    WriteFile(reportFile, Report(result, revision));

    std::cout << "Imported " << result.imported.size() << " of " << result.spellCount << " spells from Foundry ("
              << revision << ") into " << spellsFile.generic_string() << "; " << result.warnings.size()
              << " have something to check.\n";
    if (spellEffects.empty())
        std::cout << "No spell effects were found in the clone, so buffs were left out.\n";
    if (result.triage)
    {
        TriageProgress progress = Progress(result);
        std::cout << "Of the " << progress.total << " spells worth importing (" << triageFile.generic_string()
                  << "): " << progress.imported << " imported, " << progress.handWritten << " written by hand, "
                  << progress.remaining << " still to import.\n";
    }
    std::cout << "Most common reasons a spell is still to import:\n";
    auto reasons = ByCount(result.skipped);
    for (std::size_t index = 0; index < reasons.size() && index < 5; ++index)
        std::cout << "  " << reasons[index].second.size() << "  " << reasons[index].first << "\n";
    std::cout << "Full report: " << reportFile.generic_string() << "\n";
}
} // namespace

/**
 * @brief   Runs the importer.
 * @param   [in] argc - Number of command-line arguments.
 * @param   [in] argv - The Foundry clone, the content folder, the output folder, and optionally the revision and
 *          the triage list.
 * @return  0, 1 on an error, or 2 for wrong arguments.
 */
int main(int argc, char* argv[])
{
    if (argc < 4 || argc > 6)
    {
        std::cerr << "usage: import-foundry <foundry-clone> <data-dir> <output-dir> [source-revision] "
                     "[triage-list]\n";
        return 2;
    }
    try
    {
        Import(argv[1], argv[2], argv[3], argc > 4 ? argv[4] : "unknown", argc > 5 ? argv[5] : "");
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
