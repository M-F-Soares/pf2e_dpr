/**
 * @file    FoundryImport.cpp
 * @brief   Conversion of Foundry VTT spells into content definitions.
 */

#include "FoundryImport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <regex>
#include <utility>

namespace
{
using Json = nlohmann::ordered_json;  /**< A parsed JSON value that keeps its keys in order. */

/**
 * @brief   Thrown inside the converter to skip a spell.
 */
struct Skip
{
    std::string reason;  /**< Why, the same text for the same reason. */
    std::string detail;  /**< The specifics. */
};

/**
 * @brief   Skips the spell being converted.
 * @param   [in] reason - Why, the same text for the same reason.
 * @param   [in] detail - The specifics, e.g. the casting time.
 * @throws  Skip always.
 */
[[noreturn]] void SkipSpell(const std::string& reason, const std::string& detail = "")
{
    throw Skip{reason, detail};
}

/** The degrees of success, by the heading Foundry's descriptions use and the key our outcomes use. */
constexpr std::array kDegreeHeadings{std::pair{"Critical Success", "critSuccess"}, std::pair{"Success", "success"},
                                     std::pair{"Failure", "failure"}, std::pair{"Critical Failure", "critFailure"}};

/** The defenses an attack spell can target, by Foundry's statistic name and our defense name. */
constexpr std::array kPassiveDefenses{std::pair{"ac", "AC"}, std::pair{"fortitude-dc", "Fort"},
                                      std::pair{"reflex-dc", "Ref"}, std::pair{"will-dc", "Will"},
                                      std::pair{"perception-dc", "Perception"}};

/** The saves, by Foundry's statistic name and our defense name. */
constexpr std::array kSaves{std::pair{"fortitude", "Fort"}, std::pair{"reflex", "Ref"}, std::pair{"will", "Will"}};

/**
 * @brief   A value in an object, if it's there and not null.
 * @param   [in] object - The object (anything else has no values).
 * @param   [in] key - The key.
 * @return  The value, or null if it's missing or null.
 */
const Json* Find(const Json& object, const char* key)
{
    if (!object.is_object())
        return nullptr;
    auto found = object.find(key);
    return found == object.end() || found->is_null() ? nullptr : &*found;
}

/**
 * @brief   A text value in an object.
 * @param   [in] object - The object.
 * @param   [in] key - The key.
 * @return  The text, or "" if it's missing or not text.
 */
std::string TextAt(const Json& object, const char* key)
{
    const Json* value = Find(object, key);
    return value && value->is_string() ? value->get<std::string>() : "";
}

/**
 * @brief   Looks a word up in a table of (Foundry name, our name) pairs.
 * @tparam  Table - The table.
 * @param   [in] word - Foundry's name.
 * @param   [in] table - The pairs.
 * @return  Our name, or "" if the word isn't in the table.
 */
template <typename Table>
std::string Translate(const std::string& word, const Table& table)
{
    for (const auto& [foundryName, ourName] : table)
        if (word == foundryName)
            return ourName;
    return "";
}

/**
 * @brief   Whether text is plain dice we can read: "2d6", "1d4+1" or a flat "5".
 * @param   [in] formula - The text.
 * @return  True if our definitions accept it as dice.
 */
bool PlainDice(const std::string& formula)
{
    static const std::regex dicePattern(R"(\d{1,3}d\d{1,3}(?:[+-]\d{1,4})?|\d{1,4})");
    return std::regex_match(formula, dicePattern);
}

/**
 * @brief   Turns description HTML into lowercase plain text.
 * @details Links like `@UUID[...]{Frightened 2}` become their label (or the linked item's name); tags are
 *          dropped and the common entities decoded. Other inline syntax (rolls, checks) is left as it is, so
 *          that text which uses it isn't mistaken for something understood.
 * @param   [in] html - The HTML.
 * @return  The text, lowercase, with single spaces.
 */
std::string PlainText(std::string html)
{
    static const std::regex labeledLink(R"(@UUID\[[^\]]*\]\{([^}]*)\})");
    static const std::regex bareLink(R"(@UUID\[[^\]]*\.([^.\]]+)\])");
    static const std::regex tag(R"(<[^>]*>)");
    static const std::regex spaces(R"(\s+)");
    html = std::regex_replace(html, labeledLink, "$1");
    html = std::regex_replace(html, bareLink, "$1");
    html = std::regex_replace(html, tag, " ");
    for (const auto& [entity, character] : {std::pair{"&nbsp;", " "}, std::pair{"&amp;", "&"},
                                            std::pair{"&lt;", "<"}, std::pair{"&gt;", ">"},
                                            std::pair{"&quot;", "\""}, std::pair{"&#39;", "'"}})
        for (std::size_t at = html.find(entity); at != std::string::npos; at = html.find(entity, at))
            html.replace(at, std::string(entity).size(), character);
    std::transform(html.begin(), html.end(), html.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    html = std::regex_replace(html, spaces, " ");
    auto first = html.find_first_not_of(' ');
    return first == std::string::npos ? "" : html.substr(first, html.find_last_not_of(' ') - first + 1);
}

/**
 * @brief   One damage (or healing) roll of a spell.
 */
struct DamageEntry
{
    std::string id;        /**< Foundry's id for it; heightening names rolls by id. */
    std::string formula;   /**< Dice, e.g. "2d6". */
    std::string type;      /**< Damage type, e.g. "fire"; empty for healing. */
    std::string category;  /**< "", "persistent" or "precision". */
    bool healing = false;  /**< Heals instead of dealing damage. */
};

/**
 * @brief   Reads (or updates) one roll from Foundry's damage data.
 * @param   [in] value - The roll's object.
 * @param   [in, out] entry - Gets the fields @p value has.
 * @throws  Skip if the roll adds the spellcasting modifier, isn't plain dice, both damages and heals, or has
 *          a category we don't model.
 */
void ReadDamageEntry(const Json& value, DamageEntry& entry)
{
    if (const Json* applyMod = Find(value, "applyMod"); applyMod && applyMod->is_boolean() && applyMod->get<bool>())
        SkipSpell("adds the spellcasting modifier to its damage");
    if (Find(value, "formula"))
        entry.formula = TextAt(value, "formula");
    if (!PlainDice(entry.formula))
        SkipSpell("damage isn't plain dice", entry.formula);
    if (Find(value, "type"))
        entry.type = TextAt(value, "type");
    if (value.contains("category"))
        entry.category = TextAt(value, "category");  // null or "" is normal damage
    if (!entry.category.empty() && entry.category != "persistent" && entry.category != "precision")
        SkipSpell("damage category isn't modeled", entry.category);
    if (const Json* kinds = Find(value, "kinds"); kinds && kinds->is_array())
    {
        bool damages = std::find(kinds->begin(), kinds->end(), "damage") != kinds->end();
        entry.healing = std::find(kinds->begin(), kinds->end(), "healing") != kinds->end();
        if (damages && entry.healing)
            SkipSpell("damages or heals depending on the target");
    }
}

/**
 * @brief   Reads a spell's damage and healing rolls.
 * @param   [in] system - The spell's "system" object.
 * @return  The rolls, in Foundry's order.
 * @throws  Skip if a roll can't be converted, or the spell both damages and heals.
 */
std::vector<DamageEntry> ReadDamage(const Json& system)
{
    std::vector<DamageEntry> entries;
    if (const Json* damage = Find(system, "damage"); damage && damage->is_object())
        for (const auto& item : damage->items())
        {
            DamageEntry entry{item.key()};
            ReadDamageEntry(item.value(), entry);
            entries.push_back(entry);
        }
    auto heals = [](const DamageEntry& entry) { return entry.healing; };
    bool anyHealing = std::any_of(entries.begin(), entries.end(), heals);
    bool anyDamage = !std::all_of(entries.begin(), entries.end(), heals);
    if (anyHealing && anyDamage)
        SkipSpell("damages or heals depending on the target");
    if (anyHealing && entries.size() > 1)
        SkipSpell("heals with more than one roll");
    return entries;
}

/**
 * @brief   Our damage list for some rolls.
 * @param   [in] entries - The rolls (not healing).
 * @return  A list of {"dice", "type", "category"}.
 */
Json DamageList(const std::vector<DamageEntry>& entries)
{
    Json list = Json::array();
    for (const DamageEntry& entry : entries)
    {
        Json damage = {{"dice", entry.formula}, {"type", entry.type}};
        if (!entry.category.empty())
            damage["category"] = entry.category;
        list.push_back(damage);
    }
    return list;
}

/**
 * @brief   What one degree-of-success paragraph does.
 */
struct DegreeEffects
{
    std::optional<std::string> damage;  /**< "none", "half", "normal" or "double", if the paragraph says. */
    Json conditions = Json::array();    /**< Condition effects, e.g. {"condition": "frightened", "value": 2}. */
};

/**
 * @brief   Reads a degree-of-success paragraph, which must be made only of phrases the converter understands.
 * @param   [in] html - The paragraph, after its heading.
 * @param   [in] heading - The heading ("Critical Success", ...), for messages.
 * @return  What it does.
 * @throws  Skip if any part of it isn't understood.
 */
DegreeEffects ReadDegree(const std::string& html, const std::string& heading)
{
    static const std::regex conditionLink(
        R"(@UUID\[Compendium\.pf2e\.conditionitems\.Item\.([^\]]+)\](?:\{([^}]*)\})?)");
    static const std::regex trailingNumber(R"((\d+)\s*$)");
    // Conditions written without a link; only the ones the engine models (ConditionKind), so nothing else matches.
    static const std::regex conditionWord(R"(\b(frightened|sickened|clumsy|enfeebled|drained|stupefied|off-guard|)"
                                          R"(prone|grabbed|restrained|concealed|hidden|dazzled|slowed|stunned|)"
                                          R"(quickened)(?: (\d+))?\b)");
    static const std::regex damagePhrase(R"(\b(?:takes|is dealt|deals|you deal) (no|half|full|double) damage\b)");
    static const std::array fillers{
        std::regex(R"(\bfor (?:\d+|one|two|three|four|five|ten) (?:rounds?|minutes?|hours?)\b)"),
        std::regex(R"(\buntil the (?:start|end) of (?:your|its|the target's) next turn\b)"),
        std::regex(R"(\bfor the (?:spell's )?duration\b)"),
        std::regex(R"(\b(?:the spell has no effect|no effect|nothing happens|unaffected)\b)"),
        std::regex(R"(\b(?:the|this) (?:target|targets|creature|creatures|enemy|foe|attacker)\b)"),
        std::regex(R"(\b(?:it|is|are|becomes|become|gains|gets|falls|knocked|also|and|then|instead)\b)"),
        std::regex(R"([.,;:!])"),
    };

    DegreeEffects effects;
    std::string rest;
    auto last = html.cbegin();
    for (std::sregex_iterator link(html.begin(), html.end(), conditionLink), end; link != end; ++link)
    {
        const std::smatch& match = *link;
        rest.append(last, match[0].first);
        rest += " ";
        last = match[0].second;
        std::string name = match[1].str();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        Json condition = {{"condition", name}};
        std::smatch number;
        std::string label = match[2].str();
        if (std::regex_search(label, number, trailingNumber))
            condition["value"] = std::stoi(number[1].str());
        effects.conditions.push_back(condition);
    }
    rest.append(last, html.cend());

    std::string text = PlainText(rest);
    std::string withoutConditions;
    auto unlinkedLast = text.cbegin();
    for (std::sregex_iterator word(text.begin(), text.end(), conditionWord), end; word != end; ++word)
    {
        const std::smatch& match = *word;
        withoutConditions.append(unlinkedLast, match[0].first);
        withoutConditions += " ";
        unlinkedLast = match[0].second;
        Json condition = {{"condition", match[1].str()}};
        if (match[2].matched)
            condition["value"] = std::stoi(match[2].str());
        effects.conditions.push_back(condition);
    }
    withoutConditions.append(unlinkedLast, text.cend());
    text = withoutConditions;

    std::smatch damage;
    if (std::regex_search(text, damage, damagePhrase))
    {
        std::string scale = damage[1].str();
        effects.damage = scale == "full" ? "normal" : scale;
        text = damage.prefix().str() + " " + damage.suffix().str();
        if (std::regex_search(text, damagePhrase))
            SkipSpell("outcome text isn't understood", heading + ": damage is described twice");
    }
    std::string leftover = text;
    for (const std::regex& filler : fillers)
        leftover = std::regex_replace(leftover, filler, " ");
    if (std::any_of(leftover.begin(), leftover.end(), [](unsigned char character) { return std::isalnum(character); }))
        SkipSpell("outcome text isn't understood", heading + ": " + PlainText(html).substr(0, 100));
    return effects;
}

/**
 * @brief   Splits a description into its degree-of-success paragraphs and the rest.
 * @param   [in] html - The description.
 * @param   [out] rest - The description without the degree-of-success and Heightened paragraphs.
 * @return  The paragraphs' HTML (after the heading), by our outcome key.
 * @throws  Skip if a degree is listed twice.
 */
std::map<std::string, std::string> DegreeParagraphs(const std::string& html, std::string& rest)
{
    static const std::regex degreeParagraph(
        R"(<p>\s*<strong>(Critical Success|Success|Failure|Critical Failure)</strong>([\s\S]*?)</p>)");
    static const std::regex heightenedParagraph(R"(<p>\s*<strong>Heightened[\s\S]*?</p>)");
    std::map<std::string, std::string> paragraphs;
    for (std::sregex_iterator paragraph(html.begin(), html.end(), degreeParagraph), end; paragraph != end;
         ++paragraph)
    {
        std::string key = Translate((*paragraph)[1].str(), kDegreeHeadings);
        if (!paragraphs.emplace(key, (*paragraph)[2].str()).second)
            SkipSpell("outcomes are listed more than once", (*paragraph)[1].str());
    }
    rest = std::regex_replace(std::regex_replace(html, degreeParagraph, " "), heightenedParagraph, " ");
    return paragraphs;
}

/**
 * @brief   Sets the cost from Foundry's casting time.
 * @param   [in] system - The spell's "system" object.
 * @param   [in, out] definition - Gets "cost" or "reaction".
 * @throws  Skip if the casting time isn't 1 to 3 actions, free, or a reaction.
 */
void ConvertCastingTime(const Json& system, Json& definition)
{
    static const std::regex variable(R"(\d+ (?:to|or) \d+.*)");
    std::string time = Find(system, "time") ? TextAt(system.at("time"), "value") : "";
    if (time == "1" || time == "2" || time == "3")
        definition["cost"] = std::stoi(time);
    else if (time == "free")
        definition["cost"] = 0;
    else if (time == "reaction")
        definition["reaction"] = true;
    else if (std::regex_match(time, variable))
        SkipSpell("takes a variable number of actions", time);
    else
        SkipSpell("casting time isn't 1 to 3 actions, free or a reaction", time);
}

/**
 * @brief   Sets the heightening from Foundry's "interval" or "fixed" heightening.
 * @details An interval step adds dice to the roll it names. A fixed level gives the whole damage (or healing)
 *          from that rank on; other changes in it (targets, area, range) don't matter against one enemy.
 * @param   [in] system - The spell's "system" object.
 * @param   [in] entries - The spell's rolls.
 * @param   [in, out] definition - Gets "heightened".
 * @throws  Skip if the heightening names a roll the spell doesn't have, or a roll can't be converted.
 */
void ConvertHeightening(const Json& system, const std::vector<DamageEntry>& entries, Json& definition)
{
    const Json* heightening = Find(system, "heightening");
    if (!heightening)
        return;
    std::string kind = TextAt(*heightening, "type");
    auto findEntry = [&](const std::vector<DamageEntry>& list, const std::string& id) {
        auto found = std::find_if(list.begin(), list.end(), [&](const DamageEntry& entry) { return entry.id == id; });
        if (found == list.end())
            SkipSpell("heightening names damage the spell doesn't have", id);
        return found;
    };

    if (kind == "interval")
    {
        const Json* steps = Find(*heightening, "damage");
        const Json* interval = Find(*heightening, "interval");
        if (!steps || !steps->is_object() || steps->empty())
            return;  // only targets or area change
        if (!interval || !interval->is_number_integer())
            SkipSpell("heightening has no interval");
        Json heightened = {{"every", interval->get<int>()}};
        Json damage = Json::array();
        for (const auto& step : steps->items())
        {
            auto entry = findEntry(entries, step.key());
            std::string formula = step.value().is_string() ? step.value().get<std::string>() : "";
            if (!PlainDice(formula))
                SkipSpell("damage isn't plain dice", formula);
            if (entry->healing)
                heightened["heal"] = formula;
            else
                damage.push_back(DamageList({DamageEntry{entry->id, formula, entry->type, entry->category}}).at(0));
        }
        if (!damage.empty())
            heightened["damage"] = damage;
        definition["heightened"] = heightened;
    }
    else if (kind == "fixed")
    {
        const Json* levels = Find(*heightening, "levels");
        if (!levels || !levels->is_object())
            return;
        std::vector<std::pair<int, const Json*>> ranks;
        for (const auto& level : levels->items())
            if (Find(level.value(), "damage"))
                ranks.emplace_back(std::stoi(level.key()), &level.value());
        std::sort(ranks.begin(), ranks.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
        std::vector<DamageEntry> current = entries;
        Json heightened = Json::object();
        for (const auto& [rank, level] : ranks)
        {
            for (const auto& change : level->at("damage").items())
            {
                auto existing = std::find_if(current.begin(), current.end(),
                                             [&](const DamageEntry& entry) { return entry.id == change.key(); });
                if (existing == current.end())
                    existing = current.insert(current.end(), DamageEntry{change.key()});
                ReadDamageEntry(change.value(), *existing);
            }
            bool healing = !current.empty() && current.front().healing;
            heightened[std::to_string(rank)] =
                healing ? Json{{"effects", Json::array({{{"heal", current.front().formula}}})}}
                        : Json{{"damage", DamageList(current)}};
        }
        if (!heightened.empty())
            definition["heightened"] = heightened;
    }
}

/**
 * @brief   Converts a spell; skips by throwing.
 * @param   [in] spell - The parsed Foundry file.
 * @param   [out] warnings - Gets things to check by hand.
 * @return  Our definition.
 * @throws  Skip if the spell can't be converted faithfully.
 */
Json Convert(const Json& spell, std::vector<std::string>& warnings)
{
    if (TextAt(spell, "type") != "spell")
        SkipSpell("isn't a spell", TextAt(spell, "type"));
    const Json& system = spell.at("system");

    Json definition = {{"name", TextAt(spell, "name")}};
    if (const Json* publication = Find(system, "publication"); publication && !TextAt(*publication, "title").empty())
        definition["reference"] = TextAt(*publication, "title");
    ConvertCastingTime(system, definition);

    std::vector<std::string> traits;
    if (const Json* traitData = Find(system, "traits"); traitData && Find(*traitData, "value"))
        for (const Json& trait : traitData->at("value"))
            if (trait.is_string())
                traits.push_back(trait.get<std::string>());
    auto hasTrait = [&](const char* trait) { return std::find(traits.begin(), traits.end(), trait) != traits.end(); };
    if (hasTrait("cantrip"))
        definition["cantrip"] = true;
    else
        definition["rank"] = system.at("level").at("value").get<int>();
    if (!traits.empty())
        definition["traits"] = traits;

    // The check: a spell attack roll, or a save.
    const Json* defense = Find(system, "defense");
    const Json* save = defense ? Find(*defense, "save") : nullptr;
    const Json* passive = defense ? Find(*defense, "passive") : nullptr;
    bool attack = hasTrait("attack");
    bool basicSave = false;
    if (attack && save)
        SkipSpell("has both an attack roll and a save");
    if (attack)
    {
        std::string statistic = passive ? TextAt(*passive, "statistic") : "ac";
        std::string against = Translate(statistic, kPassiveDefenses);
        if (against.empty())
            SkipSpell("attacks a defense that isn't modeled", statistic);
        definition["check"] = {{"stat", "spell"}, {"against", against}};
    }
    else if (save)
    {
        std::string statistic = TextAt(*save, "statistic");
        std::string saveName = Translate(statistic, kSaves);
        if (saveName.empty())
            SkipSpell("save isn't Fortitude, Reflex or Will", statistic);
        definition["check"] = {{"stat", "spell"}, {"save", saveName}};
        basicSave = Find(*save, "basic") && save->at("basic").is_boolean() && save->at("basic").get<bool>();
    }
    bool hasCheck = attack || save;

    std::vector<DamageEntry> entries = ReadDamage(system);
    bool healing = !entries.empty() && entries.front().healing;
    std::string description = Find(system, "description") ? TextAt(system.at("description"), "value") : "";
    std::string rest;
    std::map<std::string, std::string> paragraphs = DegreeParagraphs(description, rest);

    if (healing)
    {
        if (hasCheck)
            SkipSpell("heals with an attack roll or a save");
        definition["effects"] = Json::array({{{"heal", entries.front().formula}}});
    }
    else if (!hasCheck)
    {
        if (!entries.empty())
            SkipSpell("deals damage with no attack roll or save");
        SkipSpell("no attack roll, save, damage or healing (a buff or utility spell)");
    }
    else
    {
        if (!entries.empty())
            definition["damage"] = DamageList(entries);
        if (attack && !entries.empty())
            definition["preset"] = "attack";
        else if (basicSave && !entries.empty())
            definition["preset"] = "basicSave";
        else if (save && !basicSave && paragraphs.empty())
            SkipSpell("the save isn't basic, and the outcomes aren't listed");
        else if (paragraphs.empty())
            SkipSpell("has an attack roll or a save, but its effects aren't listed");

        Json outcomes = Json::object();
        bool damageStated = false;
        for (const auto& [heading, key] : kDegreeHeadings)
        {
            auto paragraph = paragraphs.find(key);
            if (paragraph == paragraphs.end())
                continue;
            DegreeEffects effects = ReadDegree(paragraph->second, heading);
            Json list = effects.conditions;
            if (effects.damage)
            {
                list.push_back({{"damage", *effects.damage}});
                damageStated = true;
            }
            if (!list.empty())
                outcomes[key] = list;
        }
        if (!entries.empty() && !definition.contains("preset") && !damageStated)
            SkipSpell("has damage, but the outcomes don't say how much");
        if (!outcomes.empty())
            definition["outcomes"] = outcomes;
    }
    ConvertHeightening(system, entries, definition);

    // Things the conversion leaves out, for someone to check.
    if (const Json* overlays = Find(system, "overlays"); overlays && !overlays->empty())
        warnings.push_back("has variants (Foundry overlays); only the base version was imported");
    if (rest.find("@UUID[Compendium.pf2e.conditionitems.") != std::string::npos ||
        rest.find("@UUID[Compendium.pf2e.spell-effects.") != std::string::npos)
        warnings.push_back("the description has more effects (a condition or spell effect outside the outcomes)");
    if (const Json* rules = Find(system, "rules"); rules && !rules->empty())
        warnings.push_back("has Foundry rule elements, which weren't imported");
    return definition;
}
} // namespace

SpellConversion ConvertFoundrySpell(const std::string& foundryJson)
{
    SpellConversion conversion;
    try
    {
        Json spell = Json::parse(foundryJson);
        conversion.name = TextAt(spell, "name");
        conversion.definition = Convert(spell, conversion.warnings).dump(4);
    }
    catch (const Skip& skip)
    {
        conversion.skipReason = skip.reason;
        conversion.skipDetail = skip.detail;
        conversion.warnings.clear();
    }
    catch (const Json::exception& error)
    {
        conversion.skipReason = "data isn't in the expected format";
        conversion.skipDetail = error.what();
        conversion.warnings.clear();
    }
    return conversion;
}
