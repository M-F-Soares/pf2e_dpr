/**
 * @file    DefinitionParser.cpp
 * @brief   Strict JSON parsing of content definitions, with variants and heightening expanded.
 * @details The only file that uses the JSON library.
 */

#include "DefinitionParser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <map>
#include <regex>
#include <set>
#include <string_view>
#include <utility>

namespace
{
using Json = nlohmann::json;  /**< A parsed JSON value. */

/**
 * @brief   Where a value comes from, for error messages.
 */
struct Origin
{
    std::string sourceName;      /**< File name. */
    std::string definitionName;  /**< Definition being read; empty before its name is known. */
    std::string variantName;     /**< Variant being read; empty when there are none. */
    int heightenedRank = 0;      /**< "Heightened (Nth)" version being read; 0 for the base version. */

    /**
     * @brief   Throws an error that names the file, the definition, and the variant or rank being read.
     * @param   [in] message - What is wrong.
     * @throws  ContentError always.
     */
    [[noreturn]] void Fail(const std::string& message) const
    {
        std::string where = sourceName;
        if (!definitionName.empty())
            where += ": " + definitionName;
        if (!variantName.empty())
            where += " (variant " + variantName + ")";
        if (heightenedRank > 0)
            where += " (heightened " + std::to_string(heightenedRank) + ")";
        throw ContentError(where + ": " + message);
    }
};

/** Every condition, for name lookups. */
constexpr std::array kConditionKinds{
    ConditionKind::Frightened, ConditionKind::Sickened,  ConditionKind::Clumsy,     ConditionKind::Enfeebled,
    ConditionKind::Drained,    ConditionKind::Stupefied, ConditionKind::OffGuard,   ConditionKind::Prone,
    ConditionKind::Grabbed,    ConditionKind::Restrained, ConditionKind::Concealed, ConditionKind::Hidden,
    ConditionKind::Dazzled,    ConditionKind::Slowed,    ConditionKind::Stunned,    ConditionKind::Quickened,
    ConditionKind::Blinded,    ConditionKind::Confused,  ConditionKind::Paralyzed,  ConditionKind::Unconscious,
    ConditionKind::Fatigued,   ConditionKind::Encumbered};

/** The damage types an attack can deal (Physical and All only name resistances). */
constexpr std::array kDamageTypes{
    DamageType::Bludgeoning, DamageType::Piercing, DamageType::Slashing, DamageType::Fire,
    DamageType::Cold,        DamageType::Electricity, DamageType::Acid,  DamageType::Sonic,
    DamageType::Vitality,    DamageType::Void,     DamageType::Force,    DamageType::Spirit,
    DamageType::Mental,      DamageType::Poison,   DamageType::Bleed,    DamageType::Untyped};

/** Every skill, for name lookups. */
constexpr std::array kSkills{
    Skill::Acrobatics,  Skill::Arcana,    Skill::Athletics,   Skill::Crafting, Skill::Deception, Skill::Diplomacy,
    Skill::Intimidation, Skill::Medicine, Skill::Nature,      Skill::Occultism, Skill::Performance,
    Skill::Religion,    Skill::Society,   Skill::Stealth,     Skill::Survival, Skill::Thievery};

/** Every defense, for name lookups. */
constexpr std::array kDefenses{Defense::AC, Defense::Fort, Defense::Ref, Defense::Will, Defense::Perception};

/** Every bonus type, for name lookups. */
constexpr std::array kBonusTypes{BonusType::Circumstance, BonusType::Item, BonusType::Status, BonusType::Untyped};

/** Who an effect lands on, by the word definitions use. */
constexpr std::array kTargets{std::pair{"actor", Who::Actor}, std::pair{"ally", Who::Ally},
                              std::pair{"allies", Who::AllAllies}, std::pair{"enemy", Who::Enemy}};

/** How a damage component is dealt, by the word definitions use. */
constexpr std::array kDamageCategories{std::pair{"normal", DamageCategory::Normal},
                                       std::pair{"precision", DamageCategory::Precision},
                                       std::pair{"persistent", DamageCategory::Persistent}};

/** How an outcome scales the damage, by the word definitions use. */
constexpr std::array kDamageScales{std::pair{"none", DamageScale::None}, std::pair{"half", DamageScale::Half},
                                   std::pair{"normal", DamageScale::Normal}, std::pair{"double", DamageScale::Double}};

/** The degrees of success, by the key definitions use in "outcomes". */
constexpr std::array kDegreeKeys{std::pair{"critFailure", Degree::CritFail}, std::pair{"failure", Degree::Fail},
                                 std::pair{"success", Degree::Success},
                                 std::pair{"critSuccess", Degree::CritSuccess}};

/** The roll and DC tags, by name, for modifier selectors like "Skill|Check". */
const std::array<std::pair<const char*, Tags>, 22> kTagNames{{
    {"Check", Tag::Check},   {"DC", Tag::DC},        {"AttackRoll", Tag::AttackRoll}, {"Damage", Tag::Damage},
    {"AC", Tag::AC},         {"Save", Tag::Save},    {"Fort", Tag::Fort},             {"Ref", Tag::Ref},
    {"Will", Tag::Will},     {"Perception", Tag::Perception}, {"Skill", Tag::Skill},  {"Spell", Tag::Spell},
    {"Melee", Tag::Melee},   {"Ranged", Tag::Ranged}, {"Str", Tag::Str},              {"Dex", Tag::Dex},
    {"Con", Tag::Con},       {"Int", Tag::Int},      {"Wis", Tag::Wis},               {"Cha", Tag::Cha},
    {"ClassDC", Tag::ClassDC}, {"Healing", Tag::Healing}}};

/**
 * @brief   Quotes a name for a message.
 * @param   [in] text - The name.
 * @return  The name in double quotes.
 */
std::string Quote(const std::string& text)
{
    return "\"" + text + "\"";
}

/**
 * @brief   Fails on any key of an object that isn't allowed there.
 * @param   [in] object - The object.
 * @param   [in] allowed - The keys it may have.
 * @param   [in] where - What the object is, for the message ("check", "a condition effect", ...).
 * @param   [in] origin - Where it comes from.
 * @throws  ContentError on the first unknown key.
 */
void CheckKeys(const Json& object, std::initializer_list<const char*> allowed, const std::string& where,
               const Origin& origin)
{
    for (const auto& item : object.items())
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* key) { return item.key() == key; }))
            origin.Fail("unknown key " + Quote(item.key()) + " in " + where);
}

/**
 * @brief   A value that must be text.
 * @param   [in] value - The value.
 * @param   [in] what - What it is, for the message.
 * @param   [in] origin - Where it comes from.
 * @return  The text.
 * @throws  ContentError if it isn't text.
 */
std::string Text(const Json& value, const std::string& what, const Origin& origin)
{
    if (!value.is_string())
        origin.Fail(what + " must be text");
    return value.get<std::string>();
}

/**
 * @brief   An optional whole number in an object.
 * @param   [in] object - The object.
 * @param   [in] key - The key.
 * @param   [in] fallback - Value when the key is missing.
 * @param   [in] origin - Where it comes from.
 * @return  The number, or @p fallback.
 * @throws  ContentError if the value isn't a whole number.
 */
int IntAt(const Json& object, const char* key, int fallback, const Origin& origin)
{
    if (!object.contains(key))
        return fallback;
    const Json& value = object.at(key);
    if (!value.is_number_integer())
        origin.Fail(Quote(key) + " must be a whole number");
    return value.get<int>();
}

/**
 * @brief   An optional true/false value in an object.
 * @param   [in] object - The object.
 * @param   [in] key - The key.
 * @param   [in] origin - Where it comes from.
 * @return  The value, or false when the key is missing.
 * @throws  ContentError if the value isn't true or false.
 */
bool BoolAt(const Json& object, const char* key, const Origin& origin)
{
    if (!object.contains(key))
        return false;
    const Json& value = object.at(key);
    if (!value.is_boolean())
        origin.Fail(Quote(key) + " must be true or false");
    return value.get<bool>();
}

/**
 * @brief   A value that must be a list.
 * @param   [in] value - The value.
 * @param   [in] what - What it is, for the message.
 * @param   [in] origin - Where it comes from.
 * @return  The list.
 * @throws  ContentError if it isn't a list.
 */
const Json& List(const Json& value, std::string_view what, const Origin& origin)
{
    if (!value.is_array())
        origin.Fail(std::string(what) + " must be a list");
    return value;
}

/**
 * @brief   A value that must be an object.
 * @param   [in] value - The value.
 * @param   [in] what - What it is, for the message.
 * @param   [in] origin - Where it comes from.
 * @return  The object.
 * @throws  ContentError if it isn't an object.
 */
const Json& Object(const Json& value, std::string_view what, const Origin& origin)
{
    if (!value.is_object())
        origin.Fail(std::string(what) + " must be an object");
    return value;
}

/**
 * @brief   An enum value by the name ToString() gives it.
 * @tparam  Enum - The enum.
 * @tparam  Count - How many values it has.
 * @param   [in] name - The name.
 * @param   [in] values - Every value of the enum.
 * @param   [in] what - What it is, for the message.
 * @param   [in] origin - Where it comes from.
 * @return  The value.
 * @throws  ContentError if no value has that name.
 */
template <typename Enum, std::size_t Count>
Enum ParseName(const std::string& name, const std::array<Enum, Count>& values, const char* what,
               const Origin& origin)
{
    for (Enum value : values)
        if (name == ToString(value))
            return value;
    origin.Fail("unknown " + std::string(what) + " " + Quote(name));
}

/**
 * @brief   A value by the word definitions use for it.
 * @tparam  Table - A list of (word, value) pairs.
 * @param   [in] word - The word.
 * @param   [in] table - The words and their values.
 * @param   [in] what - What it is, for the message.
 * @param   [in] origin - Where it comes from.
 * @return  The value.
 * @throws  ContentError if the word isn't in the table.
 */
template <typename Table>
auto ParseWord(const std::string& word, const Table& table, const char* what, const Origin& origin)
{
    for (const auto& [name, value] : table)
        if (word == name)
            return value;
    std::string known;
    for (const auto& entry : table)
        known += (known.empty() ? "" : ", ") + std::string(entry.first);
    origin.Fail("unknown " + std::string(what) + " " + Quote(word) + " (expected one of: " + known + ")");
}

/**
 * @brief   Dice text, read: "2d6+4" is 2 dice, die 6, flat 4.
 */
struct DiceText
{
    int count = 0;  /**< Number of dice. */
    int die = 0;    /**< Die size. */
    int flat = 0;   /**< Flat amount. */
};

/**
 * @brief   Reads dice text like "2d6+4", "1d8", "2d6-1" or a flat "10".
 * @param   [in] value - The text.
 * @param   [in] origin - Where it comes from.
 * @return  The dice.
 * @throws  ContentError if the text isn't dice.
 */
DiceText ParseDice(const Json& value, const Origin& origin)
{
    static const std::regex dicePattern(R"((\d{1,3})d(\d{1,3})(?:([+-])(\d{1,4}))?)");
    static const std::regex flatPattern(R"(\d{1,4})");
    std::string text = Text(value, "dice", origin);
    std::smatch match;
    DiceText dice;
    if (std::regex_match(text, match, dicePattern))
    {
        dice.count = std::stoi(match[1].str());
        dice.die = std::stoi(match[2].str());
        if (match[3].matched)
            dice.flat = (match[3].str() == "-" ? -1 : 1) * std::stoi(match[4].str());
        if (dice.count < 1 || dice.die < 1)
            origin.Fail("bad dice " + Quote(text) + " (need at least one die with at least one side)");
    }
    else if (std::regex_match(text, flatPattern))
        dice.flat = std::stoi(text);
    else
        origin.Fail("bad dice " + Quote(text) + " (expected text like \"2d6+4\", \"1d8\" or \"10\")");
    return dice;
}

/**
 * @brief   Reads one damage entry: {"dice": "2d6", "type": "fire", "category": "persistent"}.
 * @param   [in] entry - The entry.
 * @param   [in] like - Type and category to use when the entry leaves them out (heightening steps).
 * @param   [in] origin - Where it comes from.
 * @return  The damage component.
 * @throws  ContentError if the entry is invalid, or has no type and there's nothing to take it from.
 */
DamageComponent ParseDamage(const Json& entry, const DamageComponent* like, const Origin& origin)
{
    Object(entry, "each damage entry", origin);
    CheckKeys(entry, {"dice", "type", "category"}, "damage", origin);
    if (!entry.contains("dice"))
        origin.Fail("damage needs \"dice\"");
    DiceText dice = ParseDice(entry.at("dice"), origin);
    DamageComponent component;
    component.count = dice.count;
    component.die = dice.die;
    component.flat = dice.flat;
    if (entry.contains("type"))
        component.type = ParseName(Text(entry.at("type"), "a damage type", origin), kDamageTypes, "damage type",
                                   origin);
    else if (like)
        component.type = like->type;
    else
        origin.Fail("damage needs a \"type\"");
    if (entry.contains("category"))
        component.category = ParseWord(Text(entry.at("category"), "a damage category", origin), kDamageCategories,
                                       "damage category", origin);
    else if (like)
        component.category = like->category;
    return component;
}

/**
 * @brief   Reads a modifier selector: a list of alternatives like "AttackRoll" or "Skill|Check".
 * @param   [in] value - The list.
 * @param   [in] origin - Where it comes from.
 * @return  The selector.
 * @throws  ContentError if the list is empty or names an unknown tag.
 */
Selector ParseSelector(const Json& value, const Origin& origin)
{
    Selector selector;
    for (const Json& alternative : List(value, "\"appliesTo\"", origin))
    {
        std::string text = Text(alternative, "each \"appliesTo\" entry", origin);
        Tags tags = 0;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t end = std::min(text.find('|', start), text.size());
            tags |= ParseWord(text.substr(start, end - start), kTagNames, "tag", origin);
            start = end + 1;
        }
        selector.anyOf.push_back(tags);
    }
    if (selector.anyOf.empty())
        origin.Fail("\"appliesTo\" needs at least one entry");
    return selector;
}

/**
 * @brief   Reads who an effect lands on.
 * @param   [in] effect - The effect.
 * @param   [in] fallback - Target when the effect has no "on".
 * @param   [in] origin - Where it comes from.
 * @return  The target.
 * @throws  ContentError if "on" isn't a known target.
 */
Who ParseTarget(const Json& effect, Who fallback, const Origin& origin)
{
    if (!effect.contains("on"))
        return fallback;
    return ParseWord(Text(effect.at("on"), "\"on\"", origin), kTargets, "target", origin);
}

/**
 * @brief   Reads when an effect ends.
 * @param   [in] effect - The effect.
 * @param   [in] origin - Where it comes from.
 * @return  The duration; for the rest of the round when the effect has no "until".
 * @throws  ContentError if "until" isn't a known duration.
 */
DurationSpec ParseUntil(const Json& effect, const Origin& origin)
{
    if (!effect.contains("until"))
        return ForTheRound();
    const std::array durations{std::pair{"round", ForTheRound()}, std::pair{"endOfActorTurn", UntilEndOfActorTurn()},
                               std::pair{"startOfActorTurn", UntilStartOfActorTurn()}};
    return ParseWord(Text(effect.at("until"), "\"until\"", origin), durations, "duration", origin);
}

/**
 * @brief   Reads a "displayName" and checks its placeholders.
 * @param   [in] value - The name.
 * @param   [in] placeholders - The placeholders it may contain, like "{ally}".
 * @param   [in] origin - Where it comes from.
 * @return  The name.
 * @throws  ContentError if it isn't text, or has another placeholder.
 */
std::string ParseDisplayName(const Json& value, std::initializer_list<const char*> placeholders,
                             const Origin& origin)
{
    std::string displayName = Text(value, "\"displayName\"", origin);
    for (std::size_t brace = displayName.find('{'); brace != std::string::npos;
         brace = displayName.find('{', brace + 1))
        if (std::none_of(placeholders.begin(), placeholders.end(), [&](const char* placeholder) {
                return displayName.compare(brace, std::string(placeholder).size(), placeholder) == 0;
            }))
        {
            std::string known;
            for (const char* placeholder : placeholders)
                known += (known.empty() ? "" : ", ") + std::string(placeholder);
            origin.Fail("\"displayName\" can only contain the placeholders " + known);
        }
    return displayName;
}

/**
 * @brief   Reads one effect into an outcome of an action.
 * @param   [in] effect - The effect, with exactly one of condition, modifier, counter, heal, hook, damage or
 *          grant.
 * @param   [in] hooks - The hooks the effect may call.
 * @param   [in, out] action - Gets the effect in its outcome, grants or chosen modifiers for @p degreeIndex. Its
 *          name and check must already be read.
 * @param   [in] degreeIndex - The degree of success the effect is for.
 * @param   [in, out] outcomeHooks - Gets the hook, for a hook effect.
 * @param   [in] origin - Where it comes from.
 * @throws  ContentError if the effect is invalid.
 */
void ParseEffect(const Json& effect, const HookRegistry& hooks, ActionDefinition& action, int degreeIndex,
                 std::vector<Hook>& outcomeHooks, const Origin& origin)
{
    Object(effect, "each effect", origin);
    Outcome& outcome = action.outcomes[degreeIndex];
    const std::array kinds{"condition", "modifier", "counter", "heal", "hook", "damage", "grant"};
    if (std::count_if(kinds.begin(), kinds.end(), [&](const char* kind) { return effect.contains(kind); }) != 1)
        origin.Fail("each effect needs exactly one of \"condition\", \"modifier\", \"counter\", \"heal\", \"hook\", "
                    "\"damage\" or \"grant\"");

    if (effect.contains("condition"))
    {
        CheckKeys(effect, {"condition", "value", "on", "until"}, "a condition effect", origin);
        ApplyCondition condition{
            ParseName(Text(effect.at("condition"), "a condition", origin), kConditionKinds, "condition", origin)};
        condition.value = IntAt(effect, "value", 1, origin);
        if (condition.value < 1)
            origin.Fail("a condition's \"value\" must be at least 1");
        condition.until = ParseUntil(effect, origin);
        condition.to = ParseTarget(effect, Who::Enemy, origin);
        outcome.conditions.push_back(condition);
    }
    else if (effect.contains("modifier"))
    {
        CheckKeys(effect, {"modifier", "value", "appliesTo", "on", "until", "source", "vsActor", "offGuard", "uses"},
                  "a modifier effect", origin);
        for (const char* key : {"value", "appliesTo", "on"})
            if (!effect.contains(key))
                origin.Fail("a modifier effect needs " + Quote(key));
        Modifier modifier;
        modifier.source = effect.contains("source") ? Text(effect.at("source"), "\"source\"", origin) : action.name;
        modifier.type = ParseName(Text(effect.at("modifier"), "a bonus type", origin), kBonusTypes, "bonus type",
                                  origin);
        modifier.value = IntAt(effect, "value", 0, origin);
        if (effect.at("appliesTo") == "chosen")
        {
            // Filled in when the action is used: attack rolls with a weapon, skill checks with a skill.
            if (action.check.stat != StatSource::Chosen)
                origin.Fail("\"appliesTo\": \"chosen\" needs a check whose \"stat\" is \"chosen\"");
            action.chosenModifiers[degreeIndex].push_back(outcome.modifiers.size());
        }
        else
            modifier.applies = ParseSelector(effect.at("appliesTo"), origin);
        modifier.grantsOffGuard = BoolAt(effect, "offGuard", origin);
        for (const ApplyModifier& existing : outcome.modifiers)
            if (existing.mod.source == modifier.source)
                origin.Fail("two modifiers in one outcome share the source " + Quote(modifier.source) +
                            "; give each its own \"source\"");
        ApplyModifier apply{ParseTarget(effect, Who::Enemy, origin), modifier, ParseUntil(effect, origin)};
        apply.uses = IntAt(effect, "uses", -1, origin);
        if (apply.uses == 0 || apply.uses < -1)
            origin.Fail("\"uses\" must be at least 1 (leave it out for unlimited uses)");
        apply.onlyVsActor = BoolAt(effect, "vsActor", origin);
        outcome.modifiers.push_back(apply);
    }
    else if (effect.contains("counter"))
    {
        CheckKeys(effect, {"counter", "value", "on"}, "a counter effect", origin);
        SetCounter counter{ParseTarget(effect, Who::Actor, origin), Text(effect.at("counter"), "a counter", origin)};
        counter.value = IntAt(effect, "value", 1, origin);
        if (counter.value < 0)
            origin.Fail("a counter's \"value\" can't be negative (0 clears it)");
        outcome.counters.push_back(counter);
    }
    else if (effect.contains("heal"))
    {
        CheckKeys(effect, {"heal", "on"}, "a heal effect", origin);
        if (outcome.heal.Any())
            origin.Fail("an outcome can only heal once");
        DiceText dice = ParseDice(effect.at("heal"), origin);
        outcome.heal = HealSpec{dice.count, dice.die, dice.flat, ParseTarget(effect, Who::Ally, origin)};
    }
    else if (effect.contains("hook"))
    {
        CheckKeys(effect, {"hook"}, "a hook effect", origin);
        std::string hookName = Text(effect.at("hook"), "a hook name", origin);
        if (!hooks.Has(hookName))
        {
            std::string known;
            for (const std::string& name : hooks.Names())
                known += (known.empty() ? "" : ", ") + name;
            origin.Fail("no hook named " + Quote(hookName) + " (registered: " + known + ")");
        }
        outcomeHooks.push_back(hooks.Get(hookName));
    }
    else if (effect.contains("damage"))
    {
        CheckKeys(effect, {"damage"}, "a damage effect", origin);
        outcome.damage = ParseWord(Text(effect.at("damage"), "a damage scale", origin), kDamageScales,
                                   "damage scale", origin);
    }
    else
    {
        CheckKeys(effect, {"grant", "reaction", "displayName"}, "a grant effect", origin);
        GrantDefinition grant{Text(effect.at("grant"), "the action to grant", origin)};
        grant.reaction = BoolAt(effect, "reaction", origin);
        if (effect.contains("displayName"))
            grant.displayName = ParseDisplayName(effect.at("displayName"), {"{weapon}"}, origin);
        action.grants[degreeIndex].push_back(grant);
    }
}

/**
 * @brief   Reads a check: {"stat": "skill:Intimidation", "against": "Will"}, {"stat": "spell", "save": "Ref"},
 *          {"stat": "skill:Medicine", "dc": 15}, {"stat": "weapon"} (a Strike) or {"stat": "chosen", "dc": 15}.
 * @param   [in] value - The check object.
 * @param   [in] origin - Where it comes from.
 * @return  The check.
 * @throws  ContentError if the check is invalid.
 */
CheckDefinition ParseCheck(const Json& value, const Origin& origin)
{
    const Json& check = Object(value, "\"check\"", origin);
    CheckKeys(check, {"stat", "against", "save", "dc"}, "check", origin);
    if (!check.contains("stat"))
        origin.Fail("a check needs a \"stat\"");

    CheckDefinition definition;
    std::string stat = Text(check.at("stat"), "\"stat\"", origin);
    if (stat == "spell")
        definition.stat = StatSource::Spell;
    else if (stat == "classDC")
        definition.stat = StatSource::ClassDC;
    else if (stat == "weapon")
        definition.stat = StatSource::Weapon;
    else if (stat == "chosen")
        definition.stat = StatSource::Chosen;
    else if (stat.rfind("skill:", 0) == 0)
    {
        definition.stat = StatSource::Skill;
        definition.skill = ParseName(stat.substr(6), kSkills, "skill", origin);
    }
    else
        origin.Fail("unknown stat " + Quote(stat) +
                    " (expected \"spell\", \"classDC\", \"skill:<Skill>\", \"weapon\" or \"chosen\")");

    if (definition.stat == StatSource::Weapon)
    {
        // A Strike: the weapon's attack bonus against AC.
        if (check.contains("against") || check.contains("save") || check.contains("dc"))
            origin.Fail("a weapon attack is always against AC; leave out \"against\", \"save\" and \"dc\"");
        definition.kind = CheckKind::Attack;
        definition.defense = Defense::AC;
        return definition;
    }
    if (check.contains("against") + check.contains("save") + check.contains("dc") != 1)
        origin.Fail("a check needs exactly one of \"against\" (roll against a defense), \"save\" (the enemy saves) "
                    "or \"dc\" (a fixed DC)");

    if (check.contains("against"))
    {
        definition.kind = CheckKind::Attack;
        definition.defense = ParseName(Text(check.at("against"), "\"against\"", origin), kDefenses, "defense", origin);
        if (definition.stat == StatSource::ClassDC)
            origin.Fail("a class DC can't roll against a defense; use \"save\"");
    }
    else if (check.contains("save"))
    {
        definition.kind = CheckKind::Save;
        definition.defense = ParseName(Text(check.at("save"), "\"save\"", origin), kDefenses, "defense", origin);
        if (definition.defense == Defense::AC || definition.defense == Defense::Perception)
            origin.Fail("\"save\" must be Fort, Ref or Will");
        if (definition.stat == StatSource::Skill || definition.stat == StatSource::Chosen)
            origin.Fail("a save is against a spell DC or class DC; use \"stat\": \"spell\" or \"classDC\"");
    }
    else
    {
        definition.kind = CheckKind::FixedDC;
        definition.dc = IntAt(check, "dc", 0, origin);
        if (definition.dc < 1)
            origin.Fail("\"dc\" must be at least 1");
        if (definition.stat != StatSource::Skill && definition.stat != StatSource::Chosen)
            origin.Fail("a fixed DC check needs a skill (or \"chosen\") as its \"stat\"");
    }
    return definition;
}

/**
 * @brief   Applies a damage preset: "basicSave", "attack" or "strike".
 * @param   [in] value - The preset's name.
 * @param   [in, out] action - Gets the damage scale of each degree.
 * @param   [in] origin - Where it comes from.
 * @throws  ContentError if the preset is unknown or doesn't fit the check.
 */
void ApplyPreset(const Json& value, ActionDefinition& action, const Origin& origin)
{
    std::string preset = Text(value, "\"preset\"", origin);
    auto& outcomes = action.outcomes;
    if (preset == "basicSave")
    {
        if (action.check.kind != CheckKind::Save)
            origin.Fail("the \"basicSave\" preset needs a check with \"save\"");
        outcomes[static_cast<int>(Degree::CritSuccess)].damage = DamageScale::None;
        outcomes[static_cast<int>(Degree::Success)].damage = DamageScale::Half;
        outcomes[static_cast<int>(Degree::Fail)].damage = DamageScale::Normal;
        outcomes[static_cast<int>(Degree::CritFail)].damage = DamageScale::Double;
    }
    else if (preset == "attack" || preset == "strike")
    {
        if (preset == "attack" && action.check.kind != CheckKind::Attack)
            origin.Fail("the \"attack\" preset needs a check with \"against\"");
        if (preset == "strike" && action.check.stat != StatSource::Weapon)
            origin.Fail("the \"strike\" preset deals the weapon's damage, so it needs \"stat\": \"weapon\"");
        action.weaponDamage = preset == "strike";
        outcomes[static_cast<int>(Degree::Success)].damage = DamageScale::Normal;
        outcomes[static_cast<int>(Degree::CritSuccess)].damage = DamageScale::Double;
    }
    else
        origin.Fail("unknown preset " + Quote(preset) + " (expected \"basicSave\", \"attack\" or \"strike\")");
}

/**
 * @brief   Reads "outcomes" (effects by degree, and "always") and "effects" (for actions without a check).
 * @param   [in] definition - The definition object.
 * @param   [in] hooks - The hooks effects may call.
 * @param   [in, out] action - Gets the effects; its check must already be read.
 * @param   [in] origin - Where it comes from.
 * @throws  ContentError if an effect is invalid, or "effects" is used on an action with a check.
 */
void ParseOutcomes(const Json& definition, const HookRegistry& hooks, ActionDefinition& action,
                   const Origin& origin)
{
    std::array<std::vector<Hook>, kDegrees> outcomeHooks;
    auto parseList = [&](const Json& list, const std::string& what, Degree degree) {
        int degreeIndex = static_cast<int>(degree);
        for (const Json& effect : List(list, what, origin))
            ParseEffect(effect, hooks, action, degreeIndex, outcomeHooks[degreeIndex], origin);
    };

    if (definition.contains("outcomes"))
    {
        const Json& outcomes = Object(definition.at("outcomes"), "\"outcomes\"", origin);
        CheckKeys(outcomes, {"critFailure", "failure", "success", "critSuccess", "always"}, "outcomes", origin);
        for (const auto& [key, degree] : kDegreeKeys)
            if (outcomes.contains(key))
                parseList(outcomes.at(key), Quote(key), degree);
        if (outcomes.contains("always"))
            for (const auto& [key, degree] : kDegreeKeys)
                parseList(outcomes.at("always"), "\"always\"", degree);
    }
    if (definition.contains("effects"))
    {
        if (action.check.kind != CheckKind::None)
            origin.Fail("\"effects\" is for actions without a check; use \"outcomes\"");
        parseList(definition.at("effects"), "\"effects\"", Degree::Success);
    }

    for (int degreeIndex = 0; degreeIndex < kDegrees; ++degreeIndex)
        if (!outcomeHooks[degreeIndex].empty())
            action.outcomes[degreeIndex].custom = [list = outcomeHooks[degreeIndex]](EffectCtx& ctx) {
                for (const Hook& hook : list)
                    hook(ctx);
            };
}

/**
 * @brief   Reads the "Heightened (+N)" steps: {"every": 1, "damage": [...], "heal": "1d8"}.
 * @param   [in] value - The "heightened" object, without its "Heightened (Nth)" entries.
 * @param   [in, out] action - Gets the steps; its rank, damage and outcomes must already be read.
 * @param   [in] origin - Where it comes from.
 * @throws  ContentError if the steps are invalid or don't match the action.
 */
void ParseHeightening(const Json& value, ActionDefinition& action, const Origin& origin)
{
    const Json& heightened = Object(value, "\"heightened\"", origin);
    CheckKeys(heightened, {"every", "damage", "heal"}, "heightened (use a rank like \"6\" for Heightened (6th))",
              origin);
    if (action.rank == 0)
        origin.Fail("only spells can be heightened; give it a \"rank\" or make it a \"cantrip\"");
    Heightening& heightening = action.heightening;
    heightening.every = IntAt(heightened, "every", 0, origin);
    if (heightening.every < 1)
        origin.Fail("\"heightened\" needs \"every\": how many ranks each step takes");
    if (!heightened.contains("damage") && !heightened.contains("heal"))
        origin.Fail("\"heightened\" needs \"damage\" or \"heal\" to add each step");

    if (heightened.contains("damage"))
    {
        if (action.damage.empty())
            origin.Fail("\"heightened\" adds damage, but there's no \"damage\"");
        for (const Json& entry : List(heightened.at("damage"), "heightened \"damage\"", origin))
            heightening.damage.push_back(ParseDamage(entry, &action.damage.front(), origin));
    }
    if (heightened.contains("heal"))
    {
        DiceText dice = ParseDice(heightened.at("heal"), origin);
        heightening.heal = HealSpec{dice.count, dice.die, dice.flat};
        bool matches = std::any_of(action.outcomes.begin(), action.outcomes.end(), [&](const Outcome& outcome) {
            return outcome.heal.Any() && (dice.count == 0 || outcome.heal.die == dice.die);
        });
        if (!matches)
            origin.Fail("\"heightened\" adds healing, but no outcome heals with the same die");
    }
}

/**
 * @brief   Reads "carriesSpell": {"checks": ["attack", "save"], "costs": [1, 2], "ignoreResistance": {...}}.
 * @param   [in] value - The "carriesSpell" object.
 * @param   [in] origin - Where it comes from.
 * @return  The spell it carries.
 * @throws  ContentError if it is invalid.
 */
SpellCarry ParseSpellCarry(const Json& value, const Origin& origin)
{
    const Json& carry = Object(value, "\"carriesSpell\"", origin);
    CheckKeys(carry, {"checks", "costs", "ignoreResistance"}, "carriesSpell", origin);
    if (!carry.contains("checks") || !carry.contains("costs"))
        origin.Fail("\"carriesSpell\" needs \"checks\" (\"attack\", \"save\") and \"costs\" (casting times it takes)");
    SpellCarry parsed;
    for (const Json& check : List(carry.at("checks"), "\"checks\"", origin))
    {
        const std::array checks{std::pair{"attack", &SpellCarry::attackSpells},
                                std::pair{"save", &SpellCarry::saveSpells}};
        parsed.*ParseWord(Text(check, "each check", origin), checks, "spell check", origin) = true;
    }
    for (const Json& cost : List(carry.at("costs"), "\"costs\"", origin))
    {
        if (!cost.is_number_integer() || cost.get<int>() < 1 || cost.get<int>() > 3)
            origin.Fail("each of \"costs\" must be 1 to 3 actions");
        parsed.costs.insert(cost.get<int>());
    }
    if (parsed.costs.empty() || !(parsed.attackSpells || parsed.saveSpells))
        origin.Fail("\"carriesSpell\" needs at least one check and one cost");

    if (carry.contains("ignoreResistance"))
    {
        const Json& bypass = Object(carry.at("ignoreResistance"), "\"ignoreResistance\"", origin);
        CheckKeys(bypass, {"types", "amount"}, "ignoreResistance", origin);
        if (!bypass.contains("types") || !bypass.contains("amount"))
            origin.Fail("\"ignoreResistance\" needs \"types\" and \"amount\" (a number, or \"level\")");
        ResistanceBypass& ignored = parsed.ignoreResistance.emplace();
        for (const Json& type : List(bypass.at("types"), "\"types\"", origin))
            ignored.types.insert(ParseName(Text(type, "a damage type", origin), kDamageTypes, "damage type", origin));
        const Json& amount = bypass.at("amount");
        if (amount.is_number_integer() && amount.get<int>() >= 1)
            ignored.amount = amount.get<int>();
        else if (amount != "level")
            origin.Fail("\"amount\" must be at least 1, or \"level\" (the actor's level)");
    }
    return parsed;
}

/**
 * @brief   Reads one version of an action (variants and "Heightened (Nth)" entries already applied).
 * @param   [in] definition - The definition object.
 * @param   [in] hooks - The hooks effects may call.
 * @param   [in] origin - Where it comes from.
 * @return  The definition.
 * @throws  ContentError if it is invalid.
 */
ActionDefinition ParseAction(const Json& definition, const HookRegistry& hooks, const Origin& origin)
{
    CheckKeys(definition,
              {"name", "displayName", "reference", "cost", "reaction", "traits", "rank", "cantrip", "check", "attacks",
               "spends", "damage", "preset", "outcomes", "effects", "carriesSpell", "temporaryImmunity", "heightened"},
              "the definition", origin);
    ActionDefinition action;
    action.name = Text(definition.at("name"), "\"name\"", origin);
    if (definition.contains("displayName"))
        action.displayName = ParseDisplayName(definition.at("displayName"), {"{ally}", "{weapon}", "{spell}"}, origin);
    if (definition.contains("reference"))
        Text(definition.at("reference"), "\"reference\"", origin);

    action.cost = IntAt(definition, "cost", 1, origin);
    if (action.cost < 0 || action.cost > 3)
        origin.Fail("\"cost\" must be 0 to 3 actions");
    action.reaction = BoolAt(definition, "reaction", origin);
    if (definition.contains("traits"))
        for (const Json& trait : List(definition.at("traits"), "\"traits\"", origin))
            action.traits.insert(Text(trait, "each trait", origin));

    action.cantrip = BoolAt(definition, "cantrip", origin);
    action.rank = IntAt(definition, "rank", 0, origin);
    if (action.cantrip && definition.contains("rank"))
        origin.Fail("a cantrip has no fixed \"rank\"; it heightens to half the caster's level");
    if (action.cantrip)
        action.rank = 1;
    else if (definition.contains("rank") && (action.rank < 1 || action.rank > 10))
        origin.Fail("\"rank\" must be 1 to 10");

    if (definition.contains("check"))
        action.check = ParseCheck(definition.at("check"), origin);
    if (action.check.kind == CheckKind::Attack &&
        (action.check.stat == StatSource::Spell || action.check.stat == StatSource::Weapon))
        action.traits.insert("attack");  // spell attack rolls and Strikes have the attack trait
    action.attacks = IntAt(definition, "attacks", 1, origin);
    if (action.attacks < 1)
        origin.Fail("\"attacks\" must be at least 1");
    if (action.attacks > 1 && action.traits.count("attack") == 0)
        origin.Fail("\"attacks\" is for actions with the attack trait");
    if (definition.contains("spends"))
        action.spentFlag = Text(definition.at("spends"), "\"spends\"", origin);

    if (definition.contains("damage"))
        for (const Json& entry : List(definition.at("damage"), "\"damage\"", origin))
            action.damage.push_back(ParseDamage(entry, nullptr, origin));
    if (definition.contains("preset"))
        ApplyPreset(definition.at("preset"), action, origin);
    if (action.weaponDamage && !action.damage.empty())
        origin.Fail("the \"strike\" preset deals the weapon's damage; leave out \"damage\"");
    ParseOutcomes(definition, hooks, action, origin);
    if (definition.contains("carriesSpell"))
    {
        if (action.check.kind != CheckKind::Attack)
            origin.Fail("\"carriesSpell\" needs a check with an attack roll (its result decides the spell's)");
        action.carriesSpell = ParseSpellCarry(definition.at("carriesSpell"), origin);
    }

    if (definition.contains("temporaryImmunity"))
    {
        const Json& immunity = Object(definition.at("temporaryImmunity"), "\"temporaryImmunity\"", origin);
        CheckKeys(immunity, {"on", "perActor"}, "temporaryImmunity", origin);
        TemporaryImmunity parsed{ParseTarget(immunity, Who::Enemy, origin), BoolAt(immunity, "perActor", origin)};
        if (parsed.on != Who::Enemy && parsed.on != Who::Ally)
            origin.Fail("a temporary immunity is \"on\" the \"enemy\" or an \"ally\"");
        action.immunity = parsed;
    }
    if (definition.contains("heightened"))
        ParseHeightening(definition.at("heightened"), action, origin);

    bool dealsDamage = std::any_of(action.outcomes.begin(), action.outcomes.end(),
                                   [](const Outcome& outcome) { return outcome.damage != DamageScale::None; });
    if (!action.damage.empty() && !dealsDamage)
        origin.Fail("has \"damage\", but no outcome deals it (add a \"preset\" or a {\"damage\": ...} effect)");
    if (action.damage.empty() && !action.weaponDamage && dealsDamage)
        origin.Fail("an outcome deals damage, but there's no \"damage\"");
    return action;
}

/**
 * @brief   Reads a feature: {"name": ..., "strikeDamage": {...}}.
 * @param   [in] definition - The definition object.
 * @param   [in] origin - Where it comes from.
 * @return  The feature.
 * @throws  ContentError if it is invalid.
 */
FeatureDefinition ParseFeature(const Json& definition, const Origin& origin)
{
    CheckKeys(definition, {"name", "reference", "strikeDamage"}, "a feature", origin);
    FeatureDefinition feature;
    feature.name = Text(definition.at("name"), "\"name\"", origin);
    feature.sourceName = origin.sourceName;
    if (definition.contains("reference"))
        Text(definition.at("reference"), "\"reference\"", origin);

    const Json& rider = Object(definition.at("strikeDamage"), "\"strikeDamage\"", origin);
    CheckKeys(rider, {"dice", "die", "category", "when", "oncePerRound"}, "strikeDamage", origin);
    StrikeDamageRider& strikeDamage = feature.strikeDamage;
    if (!rider.contains("dice") || !rider.contains("die"))
        origin.Fail("\"strikeDamage\" needs \"dice\" (a number, or one per level) and \"die\"");
    const Json& dice = rider.at("dice");
    if (dice.is_number_integer())
        strikeDamage.diceByLevel[1] = dice.get<int>();
    else
        for (const auto& item : Object(dice, "\"dice\"", origin).items())
        {
            if (item.key().empty() || !std::all_of(item.key().begin(), item.key().end(), ::isdigit))
                origin.Fail("\"dice\" levels must be numbers, like {\"1\": 1, \"5\": 2}");
            if (!item.value().is_number_integer())
                origin.Fail("\"dice\" counts must be whole numbers");
            strikeDamage.diceByLevel[std::stoi(item.key())] = item.value().get<int>();
        }
    strikeDamage.die = IntAt(rider, "die", 6, origin);
    if (strikeDamage.die < 1)
        origin.Fail("\"die\" must be at least 1");
    if (rider.contains("category"))
        strikeDamage.category = ParseWord(Text(rider.at("category"), "\"category\"", origin), kDamageCategories,
                                          "damage category", origin);
    if (rider.contains("oncePerRound"))
        strikeDamage.oncePerRound = Text(rider.at("oncePerRound"), "\"oncePerRound\"", origin);

    if (rider.contains("when"))
    {
        const Json& when = Object(rider.at("when"), "\"when\"", origin);
        CheckKeys(when, {"enemyOffGuard", "weaponTraits", "actorCounter"}, "when", origin);
        strikeDamage.enemyOffGuard = BoolAt(when, "enemyOffGuard", origin);
        if (when.contains("weaponTraits"))
            for (const Json& trait : List(when.at("weaponTraits"), "\"weaponTraits\"", origin))
                strikeDamage.weaponTraits.push_back(Text(trait, "each weapon trait", origin));
        if (when.contains("actorCounter"))
            strikeDamage.actorCounter = Text(when.at("actorCounter"), "\"actorCounter\"", origin);
    }
    return feature;
}

/**
 * @brief   Reads an action with all its variants and "Heightened (Nth)" versions.
 * @details Each variant is the definition with the variant's keys merged in (JSON merge patch: objects merge,
 *          everything else is replaced). Each "Heightened (Nth)" entry is merged in the same way on top of the
 *          ones below it, so a rank 9 version also has the rank 6 changes.
 * @param   [in] definition - The definition object.
 * @param   [in] hooks - The hooks effects may call.
 * @param   [in] origin - Where it comes from.
 * @return  The action and all its versions.
 * @throws  ContentError if any version is invalid.
 */
ActionEntry ExpandAction(const Json& definition, const HookRegistry& hooks, const Origin& origin)
{
    Json base = definition;
    ActionEntry entry;
    entry.name = origin.definitionName;
    entry.sourceName = origin.sourceName;

    Json variants = Json::object();
    if (base.contains("variants"))
    {
        variants = Object(base.at("variants"), "\"variants\"", origin);
        if (variants.empty())
            origin.Fail("\"variants\" needs at least one variant");
        base.erase("variants");
    }
    if (base.contains("defaultVariant"))
    {
        entry.defaultVariant = Text(base.at("defaultVariant"), "\"defaultVariant\"", origin);
        if (!variants.contains(entry.defaultVariant))
            origin.Fail("the default variant " + Quote(entry.defaultVariant) + " isn't one of the variants");
        base.erase("defaultVariant");
    }

    std::vector<std::pair<std::string, Json>> variantList;
    if (variants.empty())
        variantList.emplace_back("", Json::object());
    for (const auto& item : variants.items())
        variantList.emplace_back(item.key(), item.value());

    for (const auto& [variantName, patch] : variantList)
    {
        Origin variantOrigin = origin;
        variantOrigin.variantName = variantName;
        if (!variants.empty() && variantName.empty())
            origin.Fail("a variant needs a name");
        Object(patch, "a variant", variantOrigin);
        for (const char* key : {"name", "variants", "defaultVariant"})
            if (patch.contains(key))
                variantOrigin.Fail(Quote(key) + " can't change in a variant");
        Json version = base;
        version.merge_patch(patch);

        std::map<int, Json> rankPatches;
        if (version.contains("heightened") && version.at("heightened").is_object())
        {
            Json& heightened = version.at("heightened");
            std::vector<std::string> rankKeys;
            for (const auto& item : heightened.items())
                if (!item.key().empty() && std::all_of(item.key().begin(), item.key().end(), ::isdigit))
                    rankKeys.push_back(item.key());
            for (const std::string& key : rankKeys)
            {
                rankPatches[std::stoi(key)] = heightened.at(key);
                heightened.erase(key);
            }
            if (heightened.empty())
                version.erase("heightened");
        }

        ActionDefinition baseVersion = ParseAction(version, hooks, variantOrigin);
        if (!rankPatches.empty() && baseVersion.rank == 0)
            variantOrigin.Fail("only spells can be heightened; give it a \"rank\" or make it a \"cantrip\"");
        std::map<int, ActionDefinition>& versions = entry.versions[variantName];
        versions[baseVersion.rank] = baseVersion;
        for (const auto& [rank, rankPatch] : rankPatches)
        {
            Origin rankOrigin = variantOrigin;
            rankOrigin.heightenedRank = rank;
            if (rank <= baseVersion.rank || rank > 10)
                rankOrigin.Fail("the rank must be above the spell's own rank, and at most 10");
            Object(rankPatch, "a heightened entry", rankOrigin);
            for (const char* key : {"name", "rank", "cantrip", "heightened"})
                if (rankPatch.contains(key))
                    rankOrigin.Fail(Quote(key) + " can't change when heightened");
            version.merge_patch(rankPatch);
            versions[rank] = ParseAction(version, hooks, rankOrigin);
        }
    }
    return entry;
}
} // namespace

DefinitionSet ParseDefinitions(const std::string& text, const std::string& sourceName, const HookRegistry& hooks)
{
    Origin origin{sourceName};
    Json root;
    try
    {
        root = Json::parse(text);
    }
    catch (const Json::parse_error& error)
    {
        origin.Fail(std::string("invalid JSON: ") + error.what());
    }
    if (!root.is_object() && !root.is_array())
        origin.Fail("expected a definition object, or a list of them");
    Json definitions = root.is_array() ? root : Json::array({root});

    DefinitionSet set;
    std::set<std::string> namesInFile;
    std::map<std::string, Json> actionsInFile;  // as written, after "extends", for later entries to extend
    for (const Json& written : definitions)
    {
        Object(written, "each definition", origin);
        if (!written.contains("name") || !written.at("name").is_string() ||
            written.at("name").get<std::string>().empty())
            origin.Fail("each definition needs a \"name\"");
        Origin named = origin;
        named.definitionName = written.at("name").get<std::string>();
        if (!namesInFile.insert(named.definitionName).second)
            named.Fail("defined twice in this file");

        Json definition = written;
        if (written.contains("extends"))
        {
            // Start from the earlier entry and merge this one in, as variants do.
            std::string baseName = Text(written.at("extends"), "\"extends\"", named);
            auto base = actionsInFile.find(baseName);
            if (base == actionsInFile.end())
                named.Fail("\"extends\" " + Quote(baseName) + ", which isn't an action defined earlier in this file");
            definition = base->second;
            Json patch = written;
            patch.erase("extends");
            definition.merge_patch(patch);
        }
        if (definition.contains("strikeDamage"))
            set.features.push_back(ParseFeature(definition, named));
        else
        {
            set.actions.push_back(ExpandAction(definition, hooks, named));
            actionsInFile[named.definitionName] = definition;
        }
    }
    return set;
}
