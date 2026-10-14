/**
 * @file    FoundryImport.cpp
 * @brief   Conversion of Foundry VTT spells into content definitions.
 */

#include "FoundryImport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
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
 * @brief   Lowercase copy of a text.
 * @param   [in] text - The text.
 * @return  The text, in lowercase.
 */
std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

/**
 * @brief   Joins texts with a separator.
 * @param   [in] parts - The texts.
 * @param   [in] separator - What goes between two of them.
 * @return  The joined text.
 */
std::string Join(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string joined;
    for (const std::string& part : parts)
        joined += (joined.empty() ? "" : separator) + part;
    return joined;
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
    html = std::regex_replace(Lowercase(html), spaces, " ");
    auto first = html.find_first_not_of(' ');
    return first == std::string::npos ? "" : html.substr(first, html.find_last_not_of(' ') - first + 1);
}

/**
 * @brief   A link to a condition: `@UUID[Compendium.pf2e.conditionitems.Item.Frightened]{Frightened 2}`.
 * @return  The pattern; group 1 is the condition's name, group 2 the label (with its value), if any.
 */
const std::regex& ConditionLink()
{
    static const std::regex pattern(R"(@UUID\[Compendium\.pf2e\.conditionitems\.Item\.([^\]]+)\](?:\{([^}]*)\})?)");
    return pattern;
}

/** The conditions the engine models (ConditionKind), as the rules write them. */
constexpr std::array kModeledConditions{"frightened", "sickened",  "clumsy",     "enfeebled",   "drained",  "stupefied",
                                        "off-guard",  "prone",     "grabbed",    "restrained",  "concealed", "hidden",
                                        "dazzled",    "slowed",    "stunned",    "quickened",   "blinded",  "confused",
                                        "paralyzed",  "unconscious", "fatigued", "encumbered"};

/** Conditions that only change what a creature does on its own turn, which one round of the party's turns doesn't
 *  simulate: an outcome that gives one is converted without it. */
constexpr std::array kEnemyTurnConditions{"fleeing", "fascinated", "deafened", "immobilized", "controlled", "doomed"};

/**
 * @brief   Names as regex alternatives: "frightened|sickened|...".
 * @tparam  Names - The array of names.
 * @param   [in] names - The names (with no regex characters in them).
 * @return  The alternatives.
 */
template <typename Names>
std::string Alternatives(const Names& names)
{
    return Join(std::vector<std::string>(names.begin(), names.end()), "|");
}

/**
 * @brief   A condition written without a link, in lowercase text: only the ones the engine models
 *          (ConditionKind), so nothing else matches.
 * @return  The pattern; group 1 is the condition, group 2 its value, if any.
 */
const std::regex& ConditionWord()
{
    static const std::regex pattern(R"(\b()" + Alternatives(kModeledConditions) + R"()(?: (\d+))?\b)");
    return pattern;
}

/**
 * @brief   A condition that only matters on the enemy's turn (kEnemyTurnConditions), in lowercase text.
 * @return  The pattern; group 1 is the condition.
 */
const std::regex& EnemyTurnConditionWord()
{
    static const std::regex pattern(R"(\b()" + Alternatives(kEnemyTurnConditions) + R"()(?: \d+)?\b)");
    return pattern;
}

/**
 * @brief   A condition the engine doesn't model that does matter to the party, in lowercase text: a spell
 *          whose outcome gives one is skipped.
 * @return  The pattern; group 1 is the condition.
 */
const std::regex& UnmodeledConditionWord()
{
    static const std::regex pattern(R"(\b(petrified|dying|wounded|invisible|undetected|unnoticed)\b)");
    return pattern;
}

/**
 * @brief   A condition effect from a condition link.
 * @param   [in] link - A match of ConditionLink().
 * @return  {"condition": name, "value": N}, with the value only if the label has one.
 */
Json LinkedCondition(const std::smatch& link)
{
    static const std::regex trailingNumber(R"((\d+)\s*$)");
    Json condition = {{"condition", Lowercase(link[1].str())}};
    std::smatch number;
    std::string label = link[2].str();
    if (std::regex_search(label, number, trailingNumber))
        condition["value"] = std::stoi(number[1].str());
    return condition;
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
 * @brief   Writes each condition link as the condition's name and value ("frightened 2"), so that linked and
 *          unlinked conditions read the same.
 * @param   [in] html - The text.
 * @return  The text with its condition links replaced.
 * @throws  Skip if a link names a condition the engine doesn't model, other than one that only matters on the
 *          enemy's turn.
 */
std::string UnlinkConditions(const std::string& html)
{
    std::string text;
    auto last = html.cbegin();
    for (std::sregex_iterator link(html.begin(), html.end(), ConditionLink()), end; link != end; ++link)
    {
        text.append(last, (*link)[0].first);
        last = (*link)[0].second;
        Json condition = LinkedCondition(*link);
        std::string name = condition.at("condition").get<std::string>();
        if (name == "flat-footed")
            name = "off-guard";  // its name before the remaster
        if (name == "persistent damage")
            SkipSpell("an outcome deals damage of its own, which isn't converted", "persistent damage");
        auto known = [&](const auto& names) { return std::find(names.begin(), names.end(), name) != names.end(); };
        if (!known(kModeledConditions) && !known(kEnemyTurnConditions))
            SkipSpell("unknown condition \"" + name + "\"");
        text += " " + name + (condition.contains("value") ? " " + condition.at("value").dump() : std::string()) + " ";
    }
    text.append(last, html.cend());
    return text;
}

/**
 * @brief   Splits plain text into sentences.
 * @param   [in] text - The text.
 * @return  The sentences that have a letter or digit, each without its final punctuation.
 */
std::vector<std::string> Sentences(const std::string& text)
{
    static const std::regex sentenceEnd(R"([.!?](?:\s+|$))");
    std::vector<std::string> sentences;
    auto keep = [&](std::string sentence) {
        if (std::any_of(sentence.begin(), sentence.end(), [](unsigned char c) { return std::isalnum(c); }))
            sentences.push_back(sentence.substr(sentence.find_first_not_of(' ')));
    };
    auto last = text.cbegin();
    for (std::sregex_iterator stop(text.begin(), text.end(), sentenceEnd), end; stop != end; ++stop)
    {
        keep(std::string(last, (*stop)[0].first));
        last = (*stop)[0].second;
    }
    keep(std::string(last, text.cend()));
    return sentences;
}

/**
 * @brief   Takes every match of a pattern out of a text.
 * @param   [in, out] text - The text; each match becomes a space.
 * @param   [in] pattern - The pattern.
 * @return  The matches, in order.
 */
std::vector<std::string> TakeOut(std::string& text, const std::regex& pattern)
{
    std::vector<std::string> matches;
    for (std::sregex_iterator match(text.begin(), text.end(), pattern), end; match != end; ++match)
        matches.push_back((*match)[0].str());
    if (!matches.empty())
        text = std::regex_replace(text, pattern, " ");
    return matches;
}

/**
 * @brief   Reads the bonuses and penalties to rolls in a sentence, like "a –2 status penalty to AC and saves".
 * @details A penalty to saves is also one to the DCs made from them, so "saves" becomes Fort, Ref and Will. Only
 *          rolls a selector can name are read; a penalty to anything else (a skill, Speed) stays in the text.
 * @param   [in, out] sentence - The sentence; each bonus or penalty read is taken out.
 * @return  Modifier effects, on the enemy.
 */
Json ReadRollModifiers(std::string& sentence)
{
    static const std::regex start(
        R"(\b(?:takes |gets |gains |has |suffers )?an? (–|-|\+)(\d+) (?:(status|circumstance|item) )?(penalty|bonus) )"
        R"((?:to|on) )");
    static const std::regex target(
        R"((?:all of its |all its |its |their |all |the )?)"
        R"((attack rolls and damage rolls|attack and damage rolls|spell attack rolls|attack rolls|attack roll|attacks|)"
        R"(damage rolls|damage roll|armor class|ac|fortitude saving throws|fortitude saves|reflex saving throws|)"
        R"(reflex saves|will saving throws|will saves|saving throws|saving throw|saves|perception checks|perception|)"
        R"(skill checks|checks and dcs|checks|dcs)\b)");
    static const std::regex joiner(R"(,? and |, )");
    static const std::map<std::string, std::vector<std::string>> selectors{
        {"attack rolls and damage rolls", {"AttackRoll", "Damage"}},
        {"attack and damage rolls", {"AttackRoll", "Damage"}},
        {"spell attack rolls", {"AttackRoll|Spell"}},
        {"attack rolls", {"AttackRoll"}},
        {"attack roll", {"AttackRoll"}},
        {"attacks", {"AttackRoll"}},
        {"damage rolls", {"Damage"}},
        {"damage roll", {"Damage"}},
        {"armor class", {"AC"}},
        {"ac", {"AC"}},
        {"fortitude saving throws", {"Fort"}},
        {"fortitude saves", {"Fort"}},
        {"reflex saving throws", {"Ref"}},
        {"reflex saves", {"Ref"}},
        {"will saving throws", {"Will"}},
        {"will saves", {"Will"}},
        {"saving throws", {"Fort", "Ref", "Will"}},
        {"saving throw", {"Fort", "Ref", "Will"}},
        {"saves", {"Fort", "Ref", "Will"}},
        {"perception checks", {"Perception"}},
        {"perception", {"Perception"}},
        {"skill checks", {"Skill|Check"}},
        {"checks and dcs", {"Check", "DC"}},
        {"checks", {"Check"}},
        {"dcs", {"DC"}},
    };
    const auto continuous = std::regex_constants::match_continuous;

    Json modifiers = Json::array();
    std::string kept;
    std::string rest = sentence;
    std::smatch found;
    while (std::regex_search(rest, found, start))
    {
        std::string after = found.suffix().str();
        std::vector<std::string> appliesTo;
        std::size_t at = 0;
        std::smatch part;
        while (std::regex_search(after.cbegin() + at, after.cend(), part, target, continuous))
        {
            for (const std::string& selector : selectors.at(part[1].str()))
                if (std::find(appliesTo.begin(), appliesTo.end(), selector) == appliesTo.end())
                    appliesTo.push_back(selector);
            at += part.length(0);
            // Another roll after "and" or a comma belongs to the same penalty.
            std::smatch join;
            if (!std::regex_search(after.cbegin() + at, after.cend(), join, joiner, continuous) ||
                !std::regex_search(after.cbegin() + at + join.length(0), after.cend(), part, target, continuous))
                break;
            at += join.length(0);
        }
        if (appliesTo.empty())
        {
            kept += found.prefix().str() + found.str();
            rest = after;
            continue;
        }
        int value = std::stoi(found[2].str()) * (found[1].str() == "+" ? 1 : -1);
        modifiers.push_back({{"modifier", found[3].matched ? found[3].str() : "untyped"},
                             {"value", value},
                             {"on", "enemy"},
                             {"appliesTo", appliesTo}});
        kept += found.prefix().str() + " ";
        rest = after.substr(at);
    }
    sentence = kept + rest;
    return modifiers;
}

/**
 * @brief   What one degree-of-success paragraph does.
 */
struct DegreeEffects
{
    std::optional<std::string> damage;      /**< "none", "half", "normal" or "double", if the paragraph says. */
    std::optional<std::string> persistent;  /**< The same for the persistent damage, if the paragraph says it apart. */
    Json effects = Json::array();           /**< Condition and modifier effects, in the order they're written. */
    std::string sameAs;                     /**< "As a failure, but…": the outcome this one adds to; else empty. */
    std::vector<std::string> warnings;      /**< What was dropped or read only in part, for someone to check. */
};

/**
 * @brief   Reads a degree-of-success paragraph, one sentence at a time.
 * @details From each sentence it reads how much damage the target takes (no, half, full or double, or the spell's
 *          own dice written out), conditions the engine models, bonuses and penalties to rolls, and "as a
 *          failure, but…". Durations, immunity, forced movement and the like are passed over, and conditions
 *          that only matter on the enemy's turn (fleeing, immobilized…) are dropped. A sentence with other words
 *          left is still applied, with a warning, unless those words change what it means (if, when, the first
 *          time, your choice, you…); a sentence with nothing read is ignored, with a warning.
 * @param   [in] html - The paragraph, after its heading.
 * @param   [in] heading - The heading ("Critical Success", ...), for messages.
 * @param   [in] entries - The spell's damage rolls, to recognize its own dice written out.
 * @return  What it does.
 * @throws  Skip if a sentence's meaning depends on something the converter can't read, the damage is described
 *          twice, or the paragraph gives something that isn't converted: damage of its own, a weakness, an
 *          affliction, another roll, or a condition the engine doesn't model.
 */
DegreeEffects ReadDegree(const std::string& html, const std::string& heading, const std::vector<DamageEntry>& entries)
{
    static const std::string damageType = "(?:(?:bludgeoning|piercing|slashing|fire|cold|electricity|acid|sonic|"
                                          "vitality|void|force|spirit|mental|poison|bleed|untyped) )?";
    static const std::regex diceDuration(R"(\bfor (?:\[\[/r [^\]]*\]\](?:\{[^}]*\})?|\d+d\d+ (?:rounds?|minutes?)))");
    static const std::regex affliction(R"(\bafflicted\b|\bstage \d)");
    static const std::array mentions{
        std::regex(R"(\b(?:for )?as long as (?:it|they|the target|the creature)(?:'s| is| are| remains?)? [a-z-]+)"
                   R"((?: \d+)?)"),
        std::regex(R"(\buntil (?:it|they|the target|the creature) (?:is|are) no longer [a-z-]+)"),
        std::regex(R"(\b(?:it |they |the target |the creature )?(?:can't|cannot) reduce (?:its|their) [a-z-]+ )"
                   R"((?:condition |value )?below \d+[^;]*)"),
        std::regex(R"(\bthe [a-z-]+ (?:condition's )?value can't be reduced below \d+[^;]*)"),
        std::regex(R"(\b(?:to )?(?:end|ends|ending|remove|removes|removing|reduce|reduces|reducing|decrease|)"
                   R"(decreases|decreasing) (?:the |its |their )?[a-z-]+ condition(?: (?:by|to) \d+)?)"),
        std::regex(R"(\bthe [a-z-]+ condition (?:ends|is removed)\b)"),
        std::regex(R"(\bwhile (?:it's|it is|they're|they are) (?:)" + Alternatives(kModeledConditions) + "|" +
                   Alternatives(kEnemyTurnConditions) + R"()(?: \d+)?\b)"),
    };
    static const std::regex sameAs(
        R"(\b(?:(?:the )?effects? (?:is|are) )?(?:the same )?as (?:a |an |the |on a |with a |for a )?)"
        R"((critical success|success|critical failure|failure)(?: effects?| results?| outcomes?)?\b)");
    static const std::regex inlineDamage(
        R"(\b(?:takes |is dealt |deals |take )?@damage\[([^\[\]]+)\[([a-z,]+)\]\](?:\{[^}]*\})?(?: damage)?)");
    static const std::regex writtenDamage(R"(\b(?:takes |is dealt |deals |take )?(\d+d\d+(?:[+-]\d+)?|\d+) )"
                                          R"((persistent )?([a-z]+) damage\b)");
    static const std::regex otherDice(R"(@damage\[|\b\d+d\d+\b)");
    static const std::regex persistentTrait(R"(,persistent,|^,|,$)");
    static const std::regex bothDamage(R"(\b(?:takes |is dealt |deals |you deal |take )?(no|half|full|double) )"
                                       R"((?:the )?initial and persistent damage\b)");
    static const std::regex mainDamage(R"(\b(?:takes |is dealt |deals |you deal |take )?(no|half|full|double) )"
                                       R"((?:the )?(?:initial )?)" + damageType + R"(damage\b)");
    static const std::regex persistentDamage(R"(\b(?:takes |is dealt |deals |take )?(no|half|full|double|the) )"
                                             R"((?:the )?persistent )" + damageType + R"(damage\b)");
    static const std::regex andPersistent(R"(\band persistent )" + damageType + R"(damage\b)");
    static const std::regex enemyTurnPhrase(R"(\b(?:can't|cannot) (?:use |take )?(?:actions|reactions|act|move))"
                                            R"((?: or (?:actions|reactions))?\b)");
    static const std::array fillers{
        // Durations: one round of turns doesn't need them.
        std::regex(R"(\bfor (?:\d+|one|two|three|four|five|six|ten) (?:rounds?|minutes?|hours?|days?|weeks?|)"
                   R"(months?|years?)\b)"),
        std::regex(R"(\buntil (?:the (?:start|end|beginning) of )?(?:your|its|their|the target's|the creature's|)"
                   R"(the caster's) (?:next )?turn(?: (?:begins|ends))?\b)"),
        std::regex(R"(\b(?:but )?recovers after (?:\d+|one) (?:rounds?|minutes?|hours?)\b)"),
        std::regex(R"(,? whichever comes first\b)"),
        std::regex(R"(\bfor the (?:spell's )?duration(?: of the spell)?\b)"),
        std::regex(R"(\b(?:for )?an unlimited duration\b)"),
        std::regex(R"(\b(?:the )?duration (?:is )?(?:unlimited|(?:\d+|one) (?:rounds?|minutes?|hours?))\b)"),
        std::regex(R"(\bpermanently\b)"),
        std::regex(R"(\b(?:or )?until (?:the spell ends|it escapes|they escape|the target escapes|)"
                   R"(the creature escapes|it breaks free)\b)"),
        // Immunity, and the spell ending.
        std::regex(R"(\b(?:after which (?:it's|it is|they're|they are) |is |becomes )?temporarily immune)"
                   R"((?: to (?:this|the|that|its) [a-z']+(?: [a-z']+)?)?(?: for (?:\d+|one) )"
                   R"((?:rounds?|minutes?|hours?|days?))?)"),
        std::regex(R"(\b(?:is )?immune to (?:this|the|that) (?:spell|effect)(?:'s effects)?\b)"),
        std::regex(R"(\bthe (?:spell|effect) ends\b)"),
        // Forced movement, Speed and escaping: one enemy, with no positions.
        std::regex(R"(\b(?:(?:and )?you can )?(?:is |are )?(?:pushed|pulled|knocked back|moved|launched|thrown|)"
                   R"(flung|dragged|shoved|drawn|push|pull|move|moves)(?: (?:it|them|the target|the creature))?)"
                   R"((?: up to)? (?:\d+|five|ten|fifteen|twenty|thirty|forty) feet(?: (?:directly )?(?:away from|)"
                   R"(toward|towards) (?:you|the [a-z]+)| closer to you| farther from you| into the air|)"
                   R"( in (?:a|the) direction of your choice| in a straight line)?)"),
        std::regex(R"(\b(?:but |and )?(?:it |they |the target |the creature )?can attempt to escape)"
                   R"((?: (?:with an escape dc equal to|against) your spell dc)?)"),
        std::regex(R"(\bwhile (?:it's |it is |they're |they are )?(?:in|within) the (?:cloud|area|fog|mist|zone|)"
                   R"(field|emanation|burst|aura)(?: of the spell)?\b)"),
        std::regex(R"(\b(?:takes? )?an? (?:–|-)\d+-foot (?:status |circumstance )?penalty to )"
                   R"((?:its |all its |their |all )?(?:speeds?|land speed)\b)"),
        std::regex(R"(\b(?:is |becomes )?difficult terrain\b)"),
        // What's left of a sentence that's understood.
        std::regex(R"(\b(?:the spell has no effect|no effect|nothing happens|unaffected)\b)"),
        std::regex(R"(\b(?:the|this|each) (?:target|targets|creature|creatures|enemy|enemies|foe|foes|attacker)\b)"),
        std::regex(R"(\binstead\b(?! of))"),
        std::regex(R"(\b(?:it's|it|is|are|becomes|become|gains|gets|falls|knocked|also|and|then|but|plus|with|)"
                   R"(in addition|as well as|additionally|both|takes|take|condition|conditions|the|a|an)\b)"),
        std::regex(R"([,;:!"()])"),
    };
    static const std::regex guard(
        R"(\b(?:if|unless|when|whenever|while|after|first|each time|every time|either|choice|chosen|choose|)"
        R"(choosing|depending|random|randomly|instead of|against|except|only|second|third|next|value|reduce|)"
        R"(reduces|reduced|remove|removes|you|your|ally|allies|other|another|not|no longer|can't|cannot)\b)");
    static const std::array degreeWords{std::pair{"critical success", "critSuccess"},
                                        std::pair{"success", "success"}, std::pair{"failure", "failure"},
                                        std::pair{"critical failure", "critFailure"}};

    auto scaleOf = [](const std::string& word) -> std::string {
        return word == "full" || word == "the" ? "normal" : word == "no" ? "none" : word;
    };
    auto quoted = [](const std::string& sentence) {
        std::size_t length = std::min<std::size_t>(sentence.size(), 140);
        while (length < sentence.size() && (static_cast<unsigned char>(sentence[length]) & 0xC0) == 0x80)
            --length;  // not inside a character (UTF-8)
        return "\"" + sentence.substr(0, length) + (length < sentence.size() ? "…" : "") + "\"";
    };

    DegreeEffects effects;
    std::vector<std::string> dropped;
    auto drop = [&](const std::string& what) {
        if (std::find(dropped.begin(), dropped.end(), what) == dropped.end())
            dropped.push_back(what);
    };
    auto setDamage = [&](std::optional<std::string>& slot, const std::string& scale) {
        if (slot)
            SkipSpell("outcome text isn't understood", heading + ": damage is described twice");
        slot = scale;
    };
    for (const std::string& sentence : Sentences(PlainText(UnlinkConditions(html))))
    {
        std::string rest = sentence;
        std::string detail = heading + ": " + sentence;
        TakeOut(rest, diceDuration);
        if (rest.find("@check[") != std::string::npos || rest.find("[[/r") != std::string::npos)
            SkipSpell("an outcome rolls another check or dice, which isn't converted", detail);
        if (rest.find("@template[") != std::string::npos)
            SkipSpell("an outcome has an area of its own, which isn't converted", detail);
        if (std::regex_search(rest, affliction))
            SkipSpell("inflicts an affliction, which isn't converted", detail);
        if (rest.find("weakness") != std::string::npos)
            SkipSpell("an outcome gives a weakness, which isn't converted", detail);
        if (std::smatch unmodeled; std::regex_search(rest, unmodeled, UnmodeledConditionWord()))
            SkipSpell("unknown condition \"" + unmodeled[1].str() + "\"");
        for (const std::regex& mention : mentions)
            TakeOut(rest, mention);

        bool read = false;
        if (std::smatch degree; std::regex_search(rest, degree, sameAs))
        {
            effects.sameAs = Translate(degree[1].str(), degreeWords);
            rest = degree.prefix().str() + " " + degree.suffix().str();
            read = true;
        }

        // The spell's own dice, written out: "takes 4d6 bludgeoning damage" is its full damage.
        auto ownDamage = [&](const std::string& formula, const std::string& type, bool persistent) {
            bool own = std::any_of(entries.begin(), entries.end(), [&](const DamageEntry& entry) {
                return !entry.healing && entry.formula == formula && entry.type == type &&
                       (entry.category == "persistent") == persistent;
            });
            if (!own)
                SkipSpell("an outcome deals damage of its own, which isn't converted", detail);
            setDamage(persistent ? effects.persistent : effects.damage, "normal");
            read = true;
        };
        for (std::smatch roll; std::regex_search(rest, roll, inlineDamage);)
        {
            std::string traits = "," + roll[2].str() + ",";
            bool persistent = traits.find(",persistent,") != std::string::npos;
            std::string type = std::regex_replace(traits, persistentTrait, "");
            ownDamage(roll[1].str(), type, persistent);
            rest = roll.prefix().str() + " " + roll.suffix().str();
        }
        for (std::smatch roll; std::regex_search(rest, roll, writtenDamage);)
        {
            ownDamage(roll[1].str(), roll[3].str(), roll[2].matched);
            rest = roll.prefix().str() + " " + roll.suffix().str();
        }
        if (std::regex_search(rest, otherDice))
            SkipSpell("an outcome deals damage of its own, which isn't converted", detail);

        for (std::smatch phrase; std::regex_search(rest, phrase, bothDamage);)
        {
            setDamage(effects.damage, scaleOf(phrase[1].str()));
            setDamage(effects.persistent, scaleOf(phrase[1].str()));
            rest = phrase.prefix().str() + " " + phrase.suffix().str();
            read = true;
        }
        for (std::smatch phrase; std::regex_search(rest, phrase, mainDamage);)
        {
            setDamage(effects.damage, scaleOf(phrase[1].str()));
            rest = phrase.prefix().str() + " " + phrase.suffix().str();
            read = true;
        }
        for (std::smatch phrase; std::regex_search(rest, phrase, persistentDamage);)
        {
            setDamage(effects.persistent, scaleOf(phrase[1].str()));
            rest = phrase.prefix().str() + " " + phrase.suffix().str();
            read = true;
        }
        for (std::smatch phrase; std::regex_search(rest, phrase, andPersistent);)
        {
            if (!effects.damage)
                SkipSpell("outcome text isn't understood", detail);
            setDamage(effects.persistent, *effects.damage);
            rest = phrase.prefix().str() + " " + phrase.suffix().str();
            read = true;
        }

        Json modifiers = ReadRollModifiers(rest);
        read = read || !modifiers.empty();
        for (Json& modifier : modifiers)
            effects.effects.push_back(modifier);

        for (std::sregex_iterator word(rest.begin(), rest.end(), ConditionWord()), end; word != end; ++word)
        {
            Json condition = {{"condition", (*word)[1].str()}};
            if ((*word)[2].matched)
                condition["value"] = std::stoi((*word)[2].str());
            effects.effects.push_back(condition);
            read = true;
        }
        TakeOut(rest, ConditionWord());
        for (std::sregex_iterator word(rest.begin(), rest.end(), EnemyTurnConditionWord()), end; word != end; ++word)
            drop((*word)[1].str());
        TakeOut(rest, EnemyTurnConditionWord());
        for (const std::string& phrase : TakeOut(rest, enemyTurnPhrase))
            drop("\"" + phrase + "\"");

        for (const std::regex& filler : fillers)
            rest = std::regex_replace(rest, filler, " ");
        if (!std::any_of(rest.begin(), rest.end(), [](unsigned char c) { return std::isalnum(c); }))
            continue;
        if (!read)
            effects.warnings.push_back(heading + ": ignored " + quoted(sentence));
        else if (std::regex_search(rest, guard))
            SkipSpell("an outcome depends on something the converter can't read", detail);
        else
            effects.warnings.push_back(heading + ": read only part of " + quoted(sentence));
    }
    if (!dropped.empty())
        effects.warnings.push_back(heading + ": dropped " + Join(dropped, ", ") +
                                   " (only matters on the enemy's turn)");

    // When all of the spell's damage is persistent, its persistent damage is all there is.
    bool allPersistent = !entries.empty() && std::all_of(entries.begin(), entries.end(), [](const DamageEntry& entry) {
        return entry.category == "persistent";
    });
    if (allPersistent && effects.persistent)
    {
        if (effects.damage && *effects.damage != *effects.persistent)
            SkipSpell("outcome text isn't understood", heading + ": damage is described twice");
        effects.damage = effects.persistent;
        effects.persistent.reset();
    }
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
 * @brief   Converts the degree-of-success paragraphs into outcomes.
 * @details "As a failure, but…" starts from that outcome's effects, and its damage unless it says its own. Each
 *          modifier after the first in an outcome gets its own source, since the same source never stacks.
 * @param   [in] paragraphs - The paragraphs' HTML, by outcome key.
 * @param   [in] entries - The spell's damage rolls.
 * @param   [in] spellName - The spell's name, which the modifiers' sources start with.
 * @param   [out] damageStated - Whether an outcome says how much damage it deals.
 * @param   [in, out] warnings - Gets what was dropped or read only in part.
 * @return  Our outcomes, by key; only those that do something.
 * @throws  Skip as ReadDegree() does, if an outcome builds on one that isn't listed, or if an outcome's persistent
 *          damage scales differently from the rest of its damage.
 */
Json ConvertOutcomes(const std::map<std::string, std::string>& paragraphs, const std::vector<DamageEntry>& entries,
                     const std::string& spellName, bool& damageStated, std::vector<std::string>& warnings)
{
    std::map<std::string, DegreeEffects> degrees;
    for (const auto& [heading, key] : kDegreeHeadings)
        if (auto paragraph = paragraphs.find(key); paragraph != paragraphs.end())
        {
            DegreeEffects effects = ReadDegree(paragraph->second, heading, entries);
            warnings.insert(warnings.end(), effects.warnings.begin(), effects.warnings.end());
            degrees.emplace(key, std::move(effects));
        }

    // "As a failure, but…", resolved once the outcome it builds on is (a few passes cover any order).
    for (std::size_t pass = 0; pass < kDegreeHeadings.size(); ++pass)
        for (auto& [key, effects] : degrees)
        {
            if (effects.sameAs.empty())
                continue;
            auto base = degrees.find(effects.sameAs);
            if (base == degrees.end() || base->first == key)
                SkipSpell("outcome text isn't understood",
                          "an outcome builds on the " + effects.sameAs + " outcome, which isn't listed");
            if (!base->second.sameAs.empty())
                continue;
            Json combined = base->second.effects;
            combined.insert(combined.end(), effects.effects.begin(), effects.effects.end());
            effects.effects = combined;
            if (!effects.damage)
            {
                effects.damage = base->second.damage;
                effects.persistent = base->second.persistent;
            }
            effects.sameAs.clear();
        }

    Json outcomes = Json::object();
    for (const auto& [heading, key] : kDegreeHeadings)
    {
        auto degree = degrees.find(key);
        if (degree == degrees.end())
            continue;
        const DegreeEffects& effects = degree->second;
        if (!effects.sameAs.empty())
            SkipSpell("outcome text isn't understood", "the outcomes build on each other in a circle");
        if (effects.persistent && effects.persistent != effects.damage)
            SkipSpell("persistent damage scales differently from the rest, which isn't converted",
                      std::string(heading) + ": " + effects.damage.value_or("no amount") + " damage, " +
                          *effects.persistent + " persistent damage");
        Json list = Json::array();
        int modifierCount = 0;
        for (Json effect : effects.effects)
        {
            if (effect.contains("modifier") && ++modifierCount > 1)
                effect["source"] = spellName + " (" + std::to_string(modifierCount) + ")";
            list.push_back(effect);
        }
        if (effects.damage)
        {
            list.push_back({{"damage", *effects.damage}});
            damageStated = true;
        }
        if (!list.empty())
            outcomes[key] = list;
    }
    return outcomes;
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
 * @brief   Thrown when a formula uses something the evaluator doesn't support.
 */
struct UnsupportedFormula
{
};

/**
 * @brief   A recursive-descent evaluator for the formulas in Foundry's rules.
 */
class FormulaEvaluator
{
public:
    /**
     * @brief   Prepares to evaluate a formula.
     * @param   [in] formula - The formula.
     * @param   [in] rank - The value of `@item.level` and `@item.rank`.
     */
    FormulaEvaluator(const std::string& formula, int rank) : m_text(formula), m_rank(rank) {}

    /**
     * @brief   Evaluates the whole formula.
     * @return  The value.
     * @throws  UnsupportedFormula if it uses anything unsupported, or isn't one complete expression.
     */
    double Evaluate()
    {
        double value = Sum();
        SkipSpaces();
        if (m_at != m_text.size())
            throw UnsupportedFormula{};
        return value;
    }

private:
    /**
     * @brief   Moves past spaces.
     */
    void SkipSpaces()
    {
        while (m_at < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_at])))
            ++m_at;
    }

    /**
     * @brief   Moves past a character if it's next.
     * @param   [in] wanted - The character.
     * @return  True if it was next.
     */
    bool Accept(char wanted)
    {
        SkipSpaces();
        if (m_at < m_text.size() && m_text[m_at] == wanted)
        {
            ++m_at;
            return true;
        }
        return false;
    }

    /**
     * @brief   Moves past a character that must be next.
     * @param   [in] wanted - The character.
     * @throws  UnsupportedFormula if it isn't next.
     */
    void Expect(char wanted)
    {
        if (!Accept(wanted))
            throw UnsupportedFormula{};
    }

    /**
     * @brief   Reads terms joined by + and -.
     * @return  Their value.
     */
    double Sum()
    {
        double value = Product();
        for (;;)
        {
            if (Accept('+'))
                value += Product();
            else if (Accept('-'))
                value -= Product();
            else
                return value;
        }
    }

    /**
     * @brief   Reads factors joined by * and /.
     * @return  Their value.
     * @throws  UnsupportedFormula on a division by zero.
     */
    double Product()
    {
        double value = Unary();
        for (;;)
        {
            if (Accept('*'))
                value *= Unary();
            else if (Accept('/'))
            {
                double divisor = Unary();
                if (divisor == 0)
                    throw UnsupportedFormula{};
                value /= divisor;
            }
            else
                return value;
        }
    }

    /**
     * @brief   Reads a factor, with an optional minus sign.
     * @return  Its value.
     */
    double Unary()
    {
        return Accept('-') ? -Unary() : Primary();
    }

    /**
     * @brief   Reads a number, the spell's rank, a function call, or an expression in parentheses.
     * @return  Its value.
     * @throws  UnsupportedFormula for anything else.
     */
    double Primary()
    {
        if (Accept('('))
        {
            double value = Sum();
            Expect(')');
            return value;
        }
        SkipSpaces();
        std::size_t start = m_at;
        auto isDigit = [&](std::size_t at) { return std::isdigit(static_cast<unsigned char>(m_text[at])) != 0; };
        if (m_at < m_text.size() && isDigit(m_at))
        {
            while (m_at < m_text.size() && (isDigit(m_at) || m_text[m_at] == '.'))
                ++m_at;
            return std::stod(m_text.substr(start, m_at - start));
        }
        while (m_at < m_text.size() &&
               (std::isalnum(static_cast<unsigned char>(m_text[m_at])) || m_text[m_at] == '@' || m_text[m_at] == '.'))
            ++m_at;
        std::string name = m_text.substr(start, m_at - start);
        if (name == "@item.level" || name == "@item.rank")
            return m_rank;
        if (name.empty() || name.front() == '@')
            throw UnsupportedFormula{};
        Expect('(');
        std::vector<double> arguments{Sum()};
        while (Accept(','))
            arguments.push_back(Sum());
        Expect(')');
        return Call(name, arguments);
    }

    /**
     * @brief   Applies one of the supported functions.
     * @param   [in] name - The function.
     * @param   [in] arguments - Its arguments.
     * @return  Its value; comparisons give 1 or 0.
     * @throws  UnsupportedFormula for any other function, or the wrong number of arguments.
     */
    static double Call(const std::string& name, const std::vector<double>& arguments)
    {
        std::size_t count = arguments.size();
        if (name == "ternary" && count == 3)
            return arguments[0] != 0 ? arguments[1] : arguments[2];
        if (count == 2)
        {
            double left = arguments[0];
            double right = arguments[1];
            if (name == "gte")
                return left >= right;
            if (name == "gt")
                return left > right;
            if (name == "lte")
                return left <= right;
            if (name == "lt")
                return left < right;
            if (name == "eq")
                return left == right;
        }
        if (name == "ceil" && count == 1)
            return std::ceil(arguments[0]);
        if (name == "floor" && count == 1)
            return std::floor(arguments[0]);
        if (name == "min")
            return *std::min_element(arguments.begin(), arguments.end());
        if (name == "max")
            return *std::max_element(arguments.begin(), arguments.end());
        throw UnsupportedFormula{};
    }

    const std::string& m_text;  /**< The formula. */
    std::size_t m_at = 0;       /**< Where reading is. */
    int m_rank;                 /**< The spell rank. */
};

/**
 * @brief   Who a buff goes to, from the spell's target and area.
 * @param   [in] system - The spell's "system" object.
 * @return  "ally" (one creature or ally), "actor" (no target or area: the caster), "allies" (an area for you
 *          and your allies); "" for anything else, such as several creatures, minions or objects.
 */
std::string Recipient(const Json& system)
{
    static const std::regex oneCreature(R"(1 (?:willing )?(?:living )?(?:creature|ally))");
    static const std::regex allies(R"(\ballies\b)");
    std::string target = Find(system, "target") ? PlainText(TextAt(system.at("target"), "value")) : "";
    bool area = Find(system, "area") != nullptr;
    if (std::regex_match(target, oneCreature))
        return "ally";
    if (target.empty() && !area)
        return "actor";
    if (target == "you")
        return "actor";
    if (area && (target.empty() || std::regex_search(target, allies)))
        return "allies";
    return "";
}

/**
 * @brief   Our "appliesTo" entries for one of Foundry's modifier selectors.
 * @param   [in] selector - Foundry's selector, e.g. "attack-roll".
 * @return  The tag combinations, as in the hand-written definitions; empty if we can't express it.
 */
std::vector<std::string> OurSelectors(const std::string& selector)
{
    static const std::map<std::string, std::vector<std::string>> table{
        {"attack", {"AttackRoll"}},
        {"attack-roll", {"AttackRoll"}},
        {"spell-attack-roll", {"AttackRoll|Spell"}},
        {"damage", {"Damage"}},
        {"strike-damage", {"Damage|Melee", "Damage|Ranged"}},
        {"melee-strike-damage", {"Damage|Melee"}},
        {"melee-damage", {"Damage|Melee"}},
        {"ranged-strike-damage", {"Damage|Ranged"}},
        {"spell-damage", {"Damage|Spell"}},
        {"skill-check", {"Skill|Check"}},
        {"perception", {"Perception|Check"}},
        {"saving-throw", {"Save"}},
        {"fortitude", {"Fort"}},
        {"reflex", {"Ref"}},
        {"will", {"Will"}},
        {"ac", {"AC"}},
    };
    auto found = table.find(selector);
    return found == table.end() ? std::vector<std::string>{} : found->second;
}

/**
 * @brief   Kinds of effect the converter leaves out, to name the reason when nothing is left.
 */
enum class LeftOut
{
    Conditional,  /**< Applies only in some cases (a predicate). */
    OneSkill,     /**< A bonus to one skill, which a selector can't target. */
    Defensive,    /**< Temporary HP, resistance, speed, senses and the like, which one round doesn't use. */
};

/**
 * @brief   A flat bonus or penalty kept from a spell effect, before its value is worked out for each rank.
 */
struct EffectModifier
{
    std::string type;                    /**< "status", "circumstance", "item" or "untyped". */
    std::vector<std::string> appliesTo;  /**< Our tag combinations. */
    Json value;                          /**< A number, or a formula on the spell's rank. */
};

/**
 * @brief   What a spell's linked spell effects do that can be converted, and what was left out.
 */
struct EffectContents
{
    std::vector<EffectModifier> modifiers;  /**< Unconditional flat bonuses and penalties we can express. */
    std::vector<std::string> conditions;    /**< Conditions the effects grant. */
    std::vector<std::string> leftOut;       /**< What was left out, for warnings. */
    std::set<LeftOut> leftOutKinds;         /**< The kinds of what was left out. */
};

/**
 * @brief   Reads the rules of one spell effect.
 * @param   [in] effect - The parsed spell effect.
 * @param   [in, out] contents - Gets its modifiers and conditions, and what was left out.
 * @throws  Skip if a rule can't be converted and can't be left out either.
 */
void ReadEffectRules(const Json& effect, EffectContents& contents)
{
    static const std::set<std::string> skills{
        "acrobatics", "arcana",   "athletics",   "crafting",    "deception", "diplomacy", "intimidation", "medicine",
        "nature",     "occultism", "performance", "religion",   "society",   "stealth",   "survival",     "thievery"};
    static const std::set<std::string> notModeled{"land-speed",  "all-speeds",   "speed",           "fly-speed",
                                                   "swim-speed",  "climb-speed",  "burrow-speed",    "initiative",
                                                   "healing-received"};
    static const std::map<std::string, std::string> defensiveRules{
        {"TempHP", "temporary Hit Points"}, {"Resistance", "a resistance"}, {"BaseSpeed", "a Speed"},
        {"Sense", "a sense"},               {"FastHealing", "fast healing"}, {"Immunity", "an immunity"}};
    const std::string conditionPrefix = "Compendium.pf2e.conditionitems.Item.";

    const Json* system = Find(effect, "system");
    const Json* rules = system ? Find(*system, "rules") : nullptr;
    if (!rules || !rules->is_array())
        return;
    for (const Json& rule : *rules)
    {
        std::string key = TextAt(rule, "key");
        const Json* predicate = Find(rule, "predicate");
        bool conditional = predicate && !predicate->empty();
        std::vector<std::string> selectors;
        if (const Json* selector = Find(rule, "selector"))
        {
            if (selector->is_string())
                selectors.push_back(selector->get<std::string>());
            else if (selector->is_array())
                for (const Json& item : *selector)
                    if (item.is_string())
                        selectors.push_back(item.get<std::string>());
        }
        std::string selectorText;
        for (const std::string& selector : selectors)
            selectorText += (selectorText.empty() ? "" : ", ") + selector;

        if (key == "Note" || key == "RollOption" || key == "Aura")
            continue;  // text, toggles, and aura targeting (the spell's area already says who it reaches)
        if (conditional && (key == "FlatModifier" || key == "DamageDice" || key == "AdjustModifier" ||
                            key == "GrantItem"))
        {
            contents.leftOut.push_back("a conditional " + key + " rule" +
                                       (selectorText.empty() ? "" : " (" + selectorText + ")"));
            contents.leftOutKinds.insert(LeftOut::Conditional);
            continue;
        }
        if (auto defensive = defensiveRules.find(key); defensive != defensiveRules.end())
        {
            contents.leftOut.push_back(defensive->second);
            contents.leftOutKinds.insert(LeftOut::Defensive);
            continue;
        }
        if (key == "BattleForm")
            SkipSpell("battle form (polymorph; not modeled)");
        if (key == "ChoiceSet")
            SkipSpell("asks for a choice when cast");
        if (key == "GrantItem")
        {
            std::string uuid = TextAt(rule, "uuid");
            if (uuid.rfind(conditionPrefix, 0) != 0)
                SkipSpell("grants an item or action, which isn't converted", uuid);
            contents.conditions.push_back(Lowercase(uuid.substr(conditionPrefix.size())));
            continue;
        }
        if (key != "FlatModifier")
            SkipSpell("uses Foundry's " + key + " rule, which isn't converted");

        if (Find(rule, "damageType") || Find(rule, "damageCategory"))
            SkipSpell("adds bonus damage of a specific type or category, which isn't converted");
        std::string type = TextAt(rule, "type");
        if (type.empty())
            type = "untyped";
        if (type != "status" && type != "circumstance" && type != "item" && type != "untyped")
            SkipSpell("uses a kind of bonus that isn't converted", type);
        EffectModifier modifier{type, {}, Find(rule, "value") ? rule.at("value") : Json()};
        for (const std::string& selector : selectors)
        {
            std::vector<std::string> ours = OurSelectors(selector);
            if (!ours.empty())
                modifier.appliesTo.insert(modifier.appliesTo.end(), ours.begin(), ours.end());
            else if (skills.count(selector) > 0)
            {
                contents.leftOut.push_back("a bonus to " + selector + " (one skill can't be targeted)");
                contents.leftOutKinds.insert(LeftOut::OneSkill);
            }
            else if (notModeled.count(selector) > 0)
            {
                contents.leftOut.push_back("a bonus to " + selector + " (not modeled)");
                contents.leftOutKinds.insert(LeftOut::Defensive);
            }
            else if (!selector.empty() && selector.front() == '{')
                SkipSpell("asks for a choice when cast");
            else
                SkipSpell("adds a bonus to something that isn't converted", selector);
        }
        if (!modifier.appliesTo.empty())
            contents.modifiers.push_back(modifier);
    }
}

/**
 * @brief   The effects a buff gives at one rank.
 * @param   [in] contents - What its spell effects do.
 * @param   [in] recipient - Who gets them: "ally", "actor" or "allies".
 * @param   [in] spellName - The spell's name, the source of its modifiers.
 * @param   [in] rank - The rank the spell is cast at.
 * @return  Our effects list.
 * @throws  Skip if a value isn't a whole number, or depends on something other than the rank.
 */
Json BuffEffects(const EffectContents& contents, const std::string& recipient, const std::string& spellName,
                 int rank)
{
    Json effects = Json::array();
    for (const std::string& condition : contents.conditions)
        effects.push_back({{"condition", condition}, {"on", recipient}});
    for (std::size_t index = 0; index < contents.modifiers.size(); ++index)
    {
        const EffectModifier& modifier = contents.modifiers[index];
        std::optional<double> value;
        if (modifier.value.is_number())
            value = modifier.value.get<double>();
        else if (modifier.value.is_string())
            value = EvaluateFoundryFormula(modifier.value.get<std::string>(), rank);
        if (!value)
            SkipSpell("a bonus depends on the caster or on a choice", modifier.value.dump());
        if (std::floor(*value) != *value)
            SkipSpell("a bonus isn't a whole number", modifier.value.dump());
        Json effect = {{"modifier", modifier.type},
                       {"value", static_cast<int>(*value)},
                       {"on", recipient},
                       {"appliesTo", modifier.appliesTo}};
        // The same source never stacks, so a second modifier from the same spell needs its own.
        if (index > 0)
            effect["source"] = spellName + " (" + std::to_string(index + 1) + ")";
        effects.push_back(effect);
    }
    return effects;
}

/**
 * @brief   Converts a spell from the spell effects it links to.
 * @details The effects at the spell's rank become "effects"; each rank whose values differ gets a
 *          "Heightened (Nth)" entry with the whole list, as in the hand-written Heroism.
 * @param   [in] effectNames - The linked effects' names.
 * @param   [in] spellEffects - Foundry's spell effects, by name.
 * @param   [in] system - The spell's "system" object.
 * @param   [in] baseRank - The spell's rank (1 for a cantrip).
 * @param   [in, out] definition - Gets "effects" and "heightened".
 * @param   [in, out] warnings - Gets what was left out.
 * @return  True if converted; false if the effects have no rules that apply (some keep only an aura there, and
 *          the description says what it does).
 * @throws  Skip if an effect is missing, a rule can't be converted, only rules that aren't converted are left,
 *          or the spell targets something other than one ally, yourself or your allies.
 */
bool ConvertSpellEffects(const std::vector<std::string>& effectNames,
                         const std::map<std::string, std::string>& spellEffects, const Json& system, int baseRank,
                         Json& definition, std::vector<std::string>& warnings)
{
    EffectContents contents;
    for (const std::string& name : effectNames)
    {
        auto found = spellEffects.find(name);
        if (found == spellEffects.end())
            SkipSpell("links a spell effect that wasn't found", name);
        ReadEffectRules(Json::parse(found->second), contents);
    }
    if (contents.modifiers.empty() && contents.conditions.empty())
    {
        std::string detail;
        for (const std::string& part : contents.leftOut)
            detail += (detail.empty() ? "" : "; ") + part;
        if (contents.leftOutKinds.size() > 1)
            SkipSpell("only effects that aren't converted (conditional, one skill, defensive or movement)", detail);
        if (contents.leftOutKinds.count(LeftOut::OneSkill) > 0)
            SkipSpell("only a bonus to a specific skill (one skill can't be targeted)", detail);
        if (contents.leftOutKinds.count(LeftOut::Defensive) > 0)
            SkipSpell("only defensive or movement effects (not modeled)", detail);
        if (contents.leftOutKinds.count(LeftOut::Conditional) > 0)
            SkipSpell("only bonuses that apply in some cases (not converted)", detail);
        return false;
    }
    std::string recipient = Recipient(system);
    if (recipient.empty())
        SkipSpell("targets something other than one ally, yourself or your allies",
                  Find(system, "target") ? TextAt(system.at("target"), "value") : "");

    std::string spellName = definition.at("name").get<std::string>();
    Json previous = BuffEffects(contents, recipient, spellName, baseRank);
    definition["effects"] = previous;
    Json heightened = Json::object();
    for (int rank = baseRank + 1; rank <= 10; ++rank)
    {
        Json effects = BuffEffects(contents, recipient, spellName, rank);
        if (effects != previous)
            heightened[std::to_string(rank)] = {{"effects", effects}};
        previous = effects;
    }
    if (!heightened.empty())
        definition["heightened"] = heightened;
    for (const std::string& part : contents.leftOut)
        warnings.push_back("left out: " + part);
    return true;
}

/**
 * @brief   Converts a spell from a sentence that gives a condition, like "It gains the quickened condition".
 * @details A sentence is understood only if it's entirely a subject (the target, it, the creature, the ally, you,
 *          or the enemies in the area), a verb (is, are, becomes, gains), the conditions, and optionally
 *          "condition" and a duration. Two endings are allowed: the use of a quickened action ("and can use the
 *          extra action only to Strike or Stride") and "can't reduce their frightened value below 1". Sentences
 *          without a condition are ignored, and a warning says so.
 * @param   [in] description - The spell's description, HTML.
 * @param   [in] system - The spell's "system" object.
 * @param   [in, out] definition - Gets "effects".
 * @param   [in, out] warnings - Gets a note that the rest of the description wasn't read.
 * @return  True if the spell was converted; false if no sentence gives a condition.
 * @throws  Skip if a sentence mentions a condition but isn't understood.
 */
bool ConvertConditionSentences(const std::string& description, const Json& system, Json& definition,
                               std::vector<std::string>& warnings)
{
    const char token = '\x01';  // stands for one condition in the text
    static const std::regex heightenedParagraph(R"(<p>\s*<strong>Heightened[\s\S]*?</p>)");
    static const std::regex sentenceEnd(R"([.!?](?:\s|$))");
    static const std::array endings{
        std::regex(R"((?:,? and )?(?:you )?can use the (?:extra|additional) action[^\x01]*$)"),
        std::regex(R"((?:,? and )?(?:they |it )?can't reduce (?:their|its) frightened value below \d+[^\x01]*$)"),
    };
    static const std::regex sentence(
        R"(^(the target|it|the creature|the ally|you|enemies (?:with)?in the area|each enemy (?:with)?in the area) )"
        R"((?:is|are|becomes|become|gains|gain)(?: the)? \x01(?:(?:,| and|, and) \x01)*(?: conditions?)?)"
        R"((?: (?:for (?:\d+|one|two|three) (?:rounds?|minutes?|hours?)|until the (?:start|end) of )"
        R"((?:your|its|the target's) next turn|for the (?:spell's )?duration))?$)");

    // Each condition link becomes a token; the conditions are kept in the same order.
    std::string html = std::regex_replace(description, heightenedParagraph, " ");
    std::vector<Json> linked;
    std::string tokened;
    auto last = html.cbegin();
    for (std::sregex_iterator link(html.begin(), html.end(), ConditionLink()), end; link != end; ++link)
    {
        tokened.append(last, (*link)[0].first);
        tokened += std::string(" ") + token + " ";
        last = (*link)[0].second;
        linked.push_back(LinkedCondition(*link));
    }
    tokened.append(last, html.cend());
    std::string text = PlainText(tokened);

    Json effects = Json::array();
    bool ignoredSentences = false;
    std::size_t nextLinked = 0;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::smatch stop;
        std::string remainder = text.substr(start);
        std::size_t length = std::regex_search(remainder, stop, sentenceEnd) ? stop.position(0) : remainder.size();
        std::string current = remainder.substr(0, length);
        start += length + (length < remainder.size() ? stop.length(0) : 0);

        std::vector<Json> conditions;
        for (char character : current)
            if (character == token)
                conditions.push_back(linked.at(nextLinked++));
        // The allowed endings go first: "can't reduce their frightened value" names a condition it doesn't give.
        for (const std::regex& ending : endings)
            current = std::regex_replace(current, ending, "");
        std::string unlinked;
        auto wordLast = current.cbegin();
        for (std::sregex_iterator word(current.begin(), current.end(), ConditionWord()), end; word != end; ++word)
        {
            unlinked.append(wordLast, (*word)[0].first);
            unlinked += token;
            wordLast = (*word)[0].second;
            Json condition = {{"condition", (*word)[1].str()}};
            if ((*word)[2].matched)
                condition["value"] = std::stoi((*word)[2].str());
            conditions.push_back(condition);
        }
        unlinked.append(wordLast, current.cend());
        if (conditions.empty())
        {
            ignoredSentences = ignoredSentences || !current.empty();
            continue;
        }
        unlinked = std::regex_replace(unlinked, std::regex(R"(^\s+|\s+$)"), "");
        unlinked = std::regex_replace(unlinked, std::regex(R"(\s+)"), " ");
        std::smatch parts;
        if (!std::regex_match(unlinked, parts, sentence) ||
            static_cast<std::size_t>(std::count(unlinked.begin(), unlinked.end(), token)) != conditions.size())
            SkipSpell("mentions a condition the converter doesn't understand", current.substr(0, 100));

        std::string subject = parts[1].str();
        std::string recipient = subject == "you"           ? "actor"
                                : subject == "the ally"    ? "ally"
                                : subject.find("enem") != std::string::npos ? "enemy"
                                                            : Recipient(system);
        if (recipient.empty() || recipient == "allies")
            SkipSpell("targets something other than one ally, yourself or your allies",
                      Find(system, "target") ? TextAt(system.at("target"), "value") : "");
        for (Json& condition : conditions)
        {
            condition["on"] = recipient;
            effects.push_back(condition);
        }
    }
    if (effects.empty())
        return false;
    definition["effects"] = effects;
    if (ignoredSentences)
        warnings.push_back("converted from a sentence in the description; the rest of it wasn't read");
    return true;
}

/**
 * @brief   Converts a spell with no attack roll, save, damage or healing, or skips it with a reason that says
 *          what kind of spell it is.
 * @details In order: summons, checks or damage written only in the description, linked spell effects, outcomes
 *          listed without a check, a sentence that gives a condition, and anything else is utility. A spell whose
 *          linked effects have no rules that apply is read from its sentences.
 * @param   [in] system - The spell's "system" object.
 * @param   [in] traits - The spell's traits.
 * @param   [in] description - The spell's description, HTML.
 * @param   [in] hasDegreeParagraphs - Whether the description lists outcomes by degree of success.
 * @param   [in] spellEffects - Foundry's spell effects, by name.
 * @param   [in, out] definition - Gets the effects.
 * @param   [in, out] warnings - Gets things to check by hand.
 * @throws  Skip if the spell can't be converted.
 */
void ConvertWithoutCheck(const Json& system, const std::vector<std::string>& traits, const std::string& description,
                         bool hasDegreeParagraphs, const std::map<std::string, std::string>& spellEffects,
                         Json& definition, std::vector<std::string>& warnings)
{
    static const std::regex effectLink(R"(@UUID\[Compendium\.pf2e\.spell-effects\.Item\.([^\]]+)\])");
    if (std::find(traits.begin(), traits.end(), "summon") != traits.end())
        SkipSpell("summons a creature or minion");
    if (description.find("@Check[") != std::string::npos)
        SkipSpell("rolls a check written only in the description");
    if (description.find("@Damage[") != std::string::npos)
        SkipSpell("deals damage written only in the description (auras, ongoing or triggered damage)");

    std::vector<std::string> effectNames;
    for (std::sregex_iterator link(description.begin(), description.end(), effectLink), end; link != end; ++link)
        if (std::find(effectNames.begin(), effectNames.end(), (*link)[1].str()) == effectNames.end())
            effectNames.push_back((*link)[1].str());
    int baseRank = definition.contains("rank") ? definition.at("rank").get<int>() : 1;
    if (!effectNames.empty() &&
        ConvertSpellEffects(effectNames, spellEffects, system, baseRank, definition, warnings))
        return;
    if (hasDegreeParagraphs)
        SkipSpell("lists outcomes, but Foundry's data has no attack roll or save for them");
    if (ConvertConditionSentences(description, system, definition, warnings))
        return;
    SkipSpell("utility: no combat numbers in Foundry's data");
}

/**
 * @brief   Converts a spell; skips by throwing.
 * @param   [in] spell - The parsed Foundry file.
 * @param   [in] spellEffects - Foundry's spell effects, by name.
 * @param   [out] warnings - Gets things to check by hand.
 * @return  Our definition.
 * @throws  Skip if the spell can't be converted faithfully.
 */
Json Convert(const Json& spell, const std::map<std::string, std::string>& spellEffects,
             std::vector<std::string>& warnings)
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
        ConvertWithoutCheck(system, traits, description, !paragraphs.empty(), spellEffects, definition, warnings);
        if (const Json* overlays = Find(system, "overlays"); overlays && !overlays->empty())
            warnings.push_back("has variants (Foundry overlays); only the base version was imported");
        return definition;
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

        bool damageStated = false;
        std::size_t warningCount = warnings.size();
        Json outcomes = ConvertOutcomes(paragraphs, entries, TextAt(spell, "name"), damageStated, warnings);
        if (entries.empty() && outcomes.empty())
            SkipSpell("outcome text isn't understood",
                      warnings.size() > warningCount ? warnings[warningCount] : "no outcome does anything");
        for (const auto& outcome : outcomes.items())
            for (const Json& effect : outcome.value())
                if (entries.empty() && effect.contains("damage") && effect.at("damage") != "none")
                    SkipSpell("an outcome deals damage, but Foundry's data has none (it depends on a weapon or a "
                              "choice)");
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
    if (rest.find("@Damage[") != std::string::npos)
        warnings.push_back("the description has more damage (an inline roll outside the outcomes)");
    if (const Json* rules = Find(system, "rules"); rules && !rules->empty())
        warnings.push_back("has Foundry rule elements, which weren't imported");
    return definition;
}

/**
 * @brief   The JSON merge patch (RFC 7386) that turns one object into another, as our variants are written.
 * @param   [in] from - The object to patch.
 * @param   [in] to - What it should become.
 * @return  The keys whose values differ, objects patched key by key, and null for keys to remove.
 */
Json MergePatchDiff(const Json& from, const Json& to)
{
    Json patch = Json::object();
    for (const auto& item : from.items())
        if (!to.contains(item.key()))
            patch[item.key()] = nullptr;
    for (const auto& item : to.items())
    {
        auto old = from.find(item.key());
        if (old == from.end())
            patch[item.key()] = item.value();
        else if (*old != item.value())
            patch[item.key()] = old->is_object() && item.value().is_object() ? MergePatchDiff(*old, item.value())
                                                                               : item.value();
    }
    return patch;
}

/**
 * @brief   A variant name made from text: lowercase words joined by hyphens ("vs. Living" -> "vs-living").
 * @param   [in] text - The text.
 * @return  The name.
 */
std::string VariantKey(const std::string& text)
{
    std::string key;
    for (unsigned char character : Lowercase(text))
        if (std::isalnum(character))
            key += static_cast<char>(character);
        else if (!key.empty() && key.back() != '-')
            key += '-';
    while (!key.empty() && key.back() == '-')
        key.pop_back();
    return key;
}

/**
 * @brief   Converts a spell whose Foundry overlays are versions of it: one per number of actions (Force Rain's 1,
 *          2 and 3 actions) or one per choice made when it's cast (Elemental Breath's elements).
 * @details Each overlay is applied to the spell as a JSON merge patch, as Foundry does, and the result converted
 *          like any spell. The versions that convert become variants, named "2-action" or after the choice
 *          ("metal"); a version that doesn't convert, or is cast over two turns, is left out with a warning. A
 *          version cast with "2 or 3" actions takes 2.
 * @param   [in] spell - The parsed Foundry file.
 * @param   [in] spellEffects - Foundry's spell effects, by name.
 * @param   [in, out] warnings - Gets things to check by hand, each prefixed with the variants it's about unless
 *          it's about all of them.
 * @return  The definition, with "variants"; nothing if the spell has no overlays, they aren't versions of either
 *          kind, or every version is cast over two turns (the spell is then converted as it is).
 * @throws  Skip if no version converts, with the first version's reason.
 */
std::optional<Json> ConvertOverlays(const Json& spell, const std::map<std::string, std::string>& spellEffects,
                                    std::vector<std::string>& warnings)
{
    static const std::regex fewestActions(R"((\d) (?:or|to) \d)");
    static const std::regex choice(R"(\(([^()]+)\)\s*$)");
    const Json* overlays = Find(spell.at("system"), "overlays");
    if (!overlays || !overlays->is_object() || overlays->empty())
        return std::nullopt;
    std::vector<const Json*> sorted;
    for (const auto& item : overlays->items())
        sorted.push_back(&item.value());
    std::stable_sort(sorted.begin(), sorted.end(), [](const Json* left, const Json* right) {
        auto order = [](const Json* overlay) {
            const Json* sort = Find(*overlay, "sort");
            return sort && sort->is_number() ? sort->get<double>() : 0.0;
        };
        return order(left) < order(right);
    });

    /** One version of the spell. */
    struct Version
    {
        std::string key;          /**< Its variant name. */
        int actions = 0;          /**< The actions it takes to cast, if its overlay sets them. */
        std::string choice;       /**< The choice in its overlay's name, if any ("vs. Living"). */
        std::string displayName;  /**< Its name in results. */
        Json spell;               /**< The spell with the overlay applied. */
    };
    std::string name = TextAt(spell, "name");
    std::vector<Version> versions;
    std::vector<std::string> leftOut;
    for (const Json* overlay : sorted)
    {
        const Json* patch = Find(*overlay, "system");
        if (!patch || !patch->is_object() || TextAt(*overlay, "overlayType") != "override")
            return std::nullopt;
        Version version;
        std::string label = TextAt(*overlay, "name");
        if (std::smatch named; std::regex_search(label, named, choice))
            version.choice = named[1].str();
        version.spell = spell;
        Json& system = version.spell.at("system");
        system.erase("overlays");
        system.merge_patch(*patch);
        std::string time = Find(*patch, "time") ? TextAt(patch->at("time"), "value") : "";
        if (!time.empty())
        {
            if (std::smatch range; std::regex_match(time, range, fewestActions))
            {
                time = range[1].str();
                system["time"]["value"] = time;
            }
            if (time != "1" && time != "2" && time != "3")
            {
                leftOut.push_back("left out the version cast in " + time);
                continue;
            }
            version.actions = std::stoi(time);
            version.key = time + "-action";
            version.displayName = name + " (" + time + "a)";
        }
        else if (!version.choice.empty())
        {
            version.key = VariantKey(version.choice);
            version.displayName = label;
        }
        else
            return std::nullopt;
        versions.push_back(version);
    }
    // A spell cast with a fixed number of actions whose overlays take more or fewer (Spiritual Torrent's 2 or 3) is
    // a version too, unless an overlay has its number of actions.
    std::string baseTime = TextAt(spell.at("system").at("time"), "value");
    bool timed =
        std::any_of(versions.begin(), versions.end(), [](const Version& version) { return version.actions > 0; });
    if (timed && (baseTime == "1" || baseTime == "2" || baseTime == "3") &&
        std::none_of(versions.begin(), versions.end(),
                     [&](const Version& version) { return version.actions == std::stoi(baseTime); }))
    {
        Version version{baseTime + "-action", std::stoi(baseTime), "", name + " (" + baseTime + "a)", spell};
        version.spell.at("system").erase("overlays");
        versions.insert(versions.begin(), version);
        std::stable_sort(versions.begin(), versions.end(),
                         [](const Version& left, const Version& right) { return left.actions < right.actions; });
    }

    // Two versions with the same number of actions are told apart by their choice ("2-action-vs-living").
    std::map<std::string, int> keyCount;
    for (const Version& version : versions)
        ++keyCount[version.key];
    for (Version& version : versions)
        if (version.actions > 0 && keyCount[version.key] > 1 && !version.choice.empty())
        {
            version.key += "-" + VariantKey(version.choice);
            version.displayName = name + " (" + std::to_string(version.actions) + "a, " + version.choice + ")";
        }

    Json base;
    Json variants = Json::object();
    std::optional<Skip> firstSkip;
    std::vector<std::pair<std::string, std::vector<std::string>>> versionWarnings;  // warning, then its variants
    for (const Version& version : versions)
    {
        if (variants.contains(version.key))
            SkipSpell("two of its versions have the same name", version.key);
        std::vector<std::string> notes;
        Json definition;
        try
        {
            definition = Convert(version.spell, spellEffects, notes);
        }
        catch (const Skip& skip)
        {
            if (!firstSkip)
                firstSkip = Skip{skip.reason, version.key + (skip.detail.empty() ? "" : ": " + skip.detail)};
            leftOut.push_back("left out the \"" + version.key + "\" version: " + skip.reason);
            continue;
        }
        if (base.is_null())
            base = definition;
        definition["displayName"] = version.displayName;
        variants[version.key] = MergePatchDiff(base, definition);
        for (const std::string& note : notes)
        {
            auto same = std::find_if(versionWarnings.begin(), versionWarnings.end(),
                                     [&](const auto& entry) { return entry.first == note; });
            if (same == versionWarnings.end())
                versionWarnings.emplace_back(note, std::vector<std::string>{version.key});
            else
                same->second.push_back(version.key);
        }
    }
    if (variants.empty() && firstSkip)
        throw *firstSkip;
    if (variants.empty())
        return std::nullopt;  // every version is cast over two turns: the spell is read as it is
    for (const auto& [note, keys] : versionWarnings)
        warnings.push_back(keys.size() == variants.size() ? note : Join(keys, ", ") + ": " + note);
    for (const std::string& note : leftOut)
        warnings.push_back(note);
    base["variants"] = variants;
    return base;
}
} // namespace

SpellConversion ConvertFoundrySpell(const std::string& foundryJson,
                                    const std::map<std::string, std::string>& spellEffects)
{
    SpellConversion conversion;
    try
    {
        Json spell = Json::parse(foundryJson);
        conversion.name = TextAt(spell, "name");
        std::optional<Json> versions = ConvertOverlays(spell, spellEffects, conversion.warnings);
        conversion.definition = (versions ? *versions : Convert(spell, spellEffects, conversion.warnings)).dump(4);
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

SpellTriage ReadSpellTriage(const std::string& tsv)
{
    SpellTriage triage;
    std::istringstream lines(tsv);
    std::string line;
    bool header = true;
    for (int number = 1; std::getline(lines, line); ++number)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string> fields;
        std::istringstream cells(line);
        for (std::string cell; std::getline(cells, cell, '\t');)
            fields.push_back(cell);
        auto fail = [&](const std::string& problem) {
            throw std::runtime_error("triage list, line " + std::to_string(number) + ": " + problem);
        };
        if (fields.size() != 3)
            fail("needs a name, a verdict and a group, separated by tabs");
        if (header)
        {
            header = false;
            if (fields != std::vector<std::string>{"name", "verdict", "group"})
                fail("the first line must be the header \"name, verdict, group\"");
            continue;
        }
        const std::string& name = fields[0];
        if (triage.contributing.count(name) > 0 || triage.leftOut.count(name) > 0)
            fail("\"" + name + "\" is listed twice");
        if (fields[1] == "contributes")
            triage.contributing.insert(name);
        else if (fields[1] == "left out")
            triage.leftOut.emplace(name, fields[2]);
        else
            fail("the verdict must be \"contributes\" or \"left out\", not \"" + fields[1] + "\"");
    }
    return triage;
}

std::optional<double> EvaluateFoundryFormula(const std::string& formula, int rank)
{
    try
    {
        return FormulaEvaluator(formula, rank).Evaluate();
    }
    catch (const UnsupportedFormula&)
    {
        return std::nullopt;
    }
}
