/**
 * @file    ImportFoundry.cpp
 * @brief   The importer program (`make import`): converts Foundry VTT's spells into content definitions.
 * @details Usage: `import-foundry <foundry-clone> <data-dir> <output-dir> [source-revision]`. Writes
 *          `<output-dir>/spells.json` and `<output-dir>/import-report.md`, and prints a summary. Spells already
 *          defined by hand in `<data-dir>` are left out, so hand-written definitions win.
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
    std::vector<std::pair<std::string, Json>> imported;           /**< Converted definitions, by name. */
    std::map<std::string, std::vector<std::string>> warnings;     /**< Things to check, by spell name. */
    std::map<std::string, std::vector<SkippedSpell>> skipped;     /**< Spells left out, by reason. */
};

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
 * @brief   The skip reasons, most common first.
 * @param   [in] result - The import.
 * @return  (reason, spells) pairs.
 */
std::vector<std::pair<std::string, std::vector<SkippedSpell>>> ReasonsByCount(const ImportResult& result)
{
    std::vector<std::pair<std::string, std::vector<SkippedSpell>>> reasons(result.skipped.begin(),
                                                                          result.skipped.end());
    std::stable_sort(reasons.begin(), reasons.end(),
                     [](const auto& left, const auto& right) { return left.second.size() > right.second.size(); });
    return reasons;
}

/**
 * @brief   The import report, in Markdown.
 * @param   [in] result - The import.
 * @param   [in] revision - Foundry's commit the spells came from.
 * @return  The report: counts, skip reasons by count, warnings, and every spell left out with its reason.
 */
std::string Report(const ImportResult& result, const std::string& revision)
{
    std::size_t skippedCount = result.spellCount - result.imported.size();
    std::ostringstream report;
    report << "# Foundry VTT spell import\n\n"
           << "Spells from github.com/foundryvtt/pf2e at commit `" << revision << "`, made by `make import`. "
           << "Don't edit this folder; change the importer or write the definition by hand in data/, and import "
           << "again.\n\n"
           << "Imported **" << result.imported.size() << "** of " << result.spellCount << " spells ("
           << result.warnings.size() << " with something to check). Left out: " << skippedCount << ".\n\n";

    report << "## Left out, by reason\n\n| Spells | Reason |\n|---:|---|\n";
    auto reasons = ReasonsByCount(result);
    for (const auto& [reason, spells] : reasons)
        report << "| " << spells.size() << " | " << TableCell(reason) << " |\n";

    report << "\n## Imported, with something to check\n\n";
    for (const auto& [name, notes] : result.warnings)
        for (const std::string& note : notes)
            report << "- **" << name << "**: " << note << "\n";

    report << "\n## Imported\n\n";
    for (const auto& [name, definition] : result.imported)
        report << "- " << name << "\n";

    report << "\n## Left out\n";
    for (const auto& [reason, spells] : reasons)
    {
        report << "\n### " << reason << " (" << spells.size() << ")\n\n";
        for (const SkippedSpell& spell : spells)
            report << "- " << spell.name << (spell.detail.empty() ? "" : ": " + spell.detail) << "\n";
    }
    return report.str();
}

/**
 * @brief   Imports Foundry's spells.
 * @param   [in] clone - A clone of github.com/foundryvtt/pf2e (only the spell folder is needed).
 * @param   [in] dataFolder - The content folder; its hand-written definitions win over imported ones.
 * @param   [in] outputFolder - Where to write spells.json and the report (usually inside @p dataFolder).
 * @param   [in] revision - Foundry's commit, for the report.
 * @throws  std::runtime_error or ContentError if a folder or file can't be read or written, or the
 *          hand-written content doesn't load.
 */
void Import(const fs::path& clone, const fs::path& dataFolder, const fs::path& outputFolder,
            const std::string& revision)
{
    fs::path spellFolder = SpellFolder(clone);
    ContentLibrary content;
    content.AddDirectory(dataFolder.string(), outputFolder.string());
    std::vector<std::string> names = content.Names();
    std::set<std::string> handWritten(names.begin(), names.end());

    HookRegistry hooks = HookRegistry::BuiltIn();
    ImportResult result;
    std::set<std::string> importedNames;
    for (const fs::path& file : JsonFiles(spellFolder))
    {
        ++result.spellCount;
        std::string sourceName = file.lexically_relative(clone).generic_string();
        SpellConversion conversion = ConvertFoundrySpell(ReadFile(file));
        std::string name = conversion.name.empty() ? sourceName : conversion.name;
        auto skip = [&](const std::string& reason, const std::string& detail) {
            result.skipped[reason].push_back(SkippedSpell{name, detail});
        };
        if (conversion.definition.empty())
        {
            skip(conversion.skipReason, conversion.skipDetail);
            continue;
        }
        if (handWritten.count(name) > 0)
        {
            skip("defined by hand in " + dataFolder.generic_string() + "/", "");
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

    std::sort(result.imported.begin(), result.imported.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
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
              << " have something to check.\nMost common reasons for leaving a spell out:\n";
    auto reasons = ReasonsByCount(result);
    for (std::size_t index = 0; index < reasons.size() && index < 5; ++index)
        std::cout << "  " << reasons[index].second.size() << "  " << reasons[index].first << "\n";
    std::cout << "Full report: " << reportFile.generic_string() << "\n";
}
} // namespace

/**
 * @brief   Runs the importer.
 * @param   [in] argc - Number of command-line arguments.
 * @param   [in] argv - The Foundry clone, the content folder, the output folder, and optionally the revision.
 * @return  0, 1 on an error, or 2 for wrong arguments.
 */
int main(int argc, char* argv[])
{
    if (argc < 4 || argc > 5)
    {
        std::cerr << "usage: import-foundry <foundry-clone> <data-dir> <output-dir> [source-revision]\n";
        return 2;
    }
    try
    {
        Import(argv[1], argv[2], argv[3], argc > 4 ? argv[4] : "unknown");
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
