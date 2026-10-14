/**
 * @file    test_import.cpp
 * @brief   Tests of the Foundry VTT spell converter (tools/), on small spells shaped like Foundry's data.
 */

#include <doctest.h>

#include "ContentLibrary.h"
#include "FoundryImport.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <utility>

namespace
{
using Json = nlohmann::ordered_json;  /**< A parsed JSON value that keeps its keys in order, as Foundry's files do. */

constexpr int kCritFail = static_cast<int>(Degree::CritFail);        /**< Action::outcomes index: critical failure. */
constexpr int kFail = static_cast<int>(Degree::Fail);                /**< Action::outcomes index: failure. */
constexpr int kSuccess = static_cast<int>(Degree::Success);          /**< Action::outcomes index: success. */
constexpr int kCritSuccess = static_cast<int>(Degree::CritSuccess);  /**< Action::outcomes index: critical success. */

/**
 * @brief   A rank 3 spell shaped like Foundry's data: 6d6 fire with a basic Reflex save, +2d6 per rank.
 * @return  The spell, for a test to change.
 */
Json FireSpell()
{
    return Json::parse(R"({
        "name": "Test Blast", "type": "spell",
        "system": {
            "level": {"value": 3}, "time": {"value": "2"},
            "traits": {"rarity": "common", "traditions": ["arcane"], "value": ["concentrate", "fire", "manipulate"]},
            "defense": {"save": {"basic": true, "statistic": "reflex"}},
            "damage": {"xYz1": {"applyMod": false, "category": null, "formula": "6d6", "kinds": ["damage"],
                                "materials": [], "type": "fire"}},
            "heightening": {"type": "interval", "interval": 1, "damage": {"xYz1": "2d6"}},
            "description": {"value": "<p>Fire.</p><hr /><p><strong>Heightened (+1)</strong> More fire.</p>"},
            "publication": {"title": "Test Book", "license": "ORC", "remaster": true},
            "rules": []
        }
    })");
}

/**
 * @brief   A rank 1 Will save spell with no damage, whose outcomes are listed by degree.
 * @param   [in] paragraphs - The degree paragraphs, HTML.
 * @return  The spell.
 */
Json SaveSpell(const std::string& paragraphs)
{
    Json spell = FireSpell();
    spell["name"] = "Test Scare";
    Json& system = spell["system"];
    system["level"]["value"] = 1;
    system["defense"] = {{"save", {{"basic", false}, {"statistic", "will"}}}};
    system["damage"] = Json::object();
    system.erase("heightening");
    system["description"]["value"] = "<p>Scary.</p><hr />" + paragraphs;
    return spell;
}

/** Spell effects by name, as the importer passes them. */
using SpellEffects = std::map<std::string, std::string>;

/**
 * @brief   A spell with no attack roll, save or damage, shaped like Foundry's data.
 * @param   [in] name - The spell's name.
 * @param   [in] rank - Its rank.
 * @param   [in] target - Its target text: "1 creature", "you and allies in the area", "" (none)...
 * @param   [in] area - Whether it has an area (an emanation).
 * @param   [in] description - Its description, HTML.
 * @return  The spell.
 */
Json NoCheckSpell(const std::string& name, int rank, const std::string& target, bool area,
                  const std::string& description)
{
    Json spell = FireSpell();
    spell["name"] = name;
    Json& system = spell["system"];
    system["level"]["value"] = rank;
    system["traits"]["value"] = {"concentrate", "manipulate"};
    system["defense"] = nullptr;
    system["damage"] = Json::object();
    system.erase("heightening");
    system["target"] = {{"value", target}};
    system["area"] = area ? Json{{"type", "emanation"}, {"value", 30}} : Json();
    system["description"]["value"] = description;
    return spell;
}

/**
 * @brief   A link to a spell effect, as Foundry's descriptions have them.
 * @param   [in] name - The effect's name.
 * @return  The HTML.
 */
std::string EffectLink(const std::string& name)
{
    return "<p>@UUID[Compendium.pf2e.spell-effects.Item." + name + "]</p>";
}

/**
 * @brief   A spell effect shaped like Foundry's data.
 * @param   [in] name - Its name.
 * @param   [in] rules - Its rule elements, as JSON text.
 * @return  The effects map, with only this effect.
 */
SpellEffects Effect(const std::string& name, const std::string& rules)
{
    Json effect = {{"name", name}, {"type", "effect"}, {"system", {{"rules", Json::parse(rules)}}}};
    return {{name, effect.dump()}};
}

/**
 * @brief   A link to a condition, as Foundry's descriptions have them.
 * @param   [in] name - The condition, e.g. "Frightened".
 * @param   [in] label - The text shown, e.g. "Frightened 1"; empty for none.
 * @return  The link.
 */
std::string ConditionLink(const std::string& name, const std::string& label = "")
{
    return "@UUID[Compendium.pf2e.conditionitems.Item." + name + "]" + (label.empty() ? "" : "{" + label + "}");
}

/**
 * @brief   Converts a spell, which must convert.
 * @param   [in] spell - The Foundry spell.
 * @param   [in] effects - The spell effects it may link to.
 * @return  A library with only the converted spell.
 */
ContentLibrary Import(const Json& spell, const SpellEffects& effects = {})
{
    SpellConversion conversion = ConvertFoundrySpell(spell.dump(), effects);
    INFO("skipped: " << conversion.skipReason << " (" << conversion.skipDetail << ")");
    REQUIRE_FALSE(conversion.definition.empty());
    ContentLibrary library;
    library.AddJson(conversion.definition, "import.json");
    return library;
}

/**
 * @brief   Why a spell was skipped.
 * @param   [in] spell - The Foundry spell.
 * @param   [in] effects - The spell effects it may link to.
 * @return  The skip reason; empty if it was converted.
 */
std::string SkipReason(const Json& spell, const SpellEffects& effects = {})
{
    return ConvertFoundrySpell(spell.dump(), effects).skipReason;
}

/**
 * @brief   The project's hand-written content, without a local import.
 * @return  The library.
 */
ContentLibrary HandWritten()
{
    ContentLibrary library;
    library.AddDirectory("data", "data/imported");
    return library;
}

/**
 * @brief   Checks that two buffs give the same modifiers and conditions.
 * @param   [in] mine - The converted buff.
 * @param   [in] theirs - The hand-written one.
 */
void CheckSameBuff(const Action& mine, const Action& theirs)
{
    CHECK(mine.cost == theirs.cost);
    const auto& ours = mine.outcomes[kSuccess].modifiers;
    const auto& expected = theirs.outcomes[kSuccess].modifiers;
    REQUIRE(ours.size() == expected.size());
    for (std::size_t index = 0; index < ours.size(); ++index)
    {
        CHECK(ours[index].to == expected[index].to);
        CHECK(ours[index].mod.type == expected[index].mod.type);
        CHECK(ours[index].mod.value == expected[index].mod.value);
        CHECK(ours[index].mod.source == expected[index].mod.source);
        CHECK(ours[index].mod.applies.anyOf == expected[index].mod.applies.anyOf);
    }
}

/**
 * @brief   Test caster: spell attack +15, spell DC 25.
 * @param   [in] level - Character level.
 * @return  The character.
 */
Character MakeCaster(int level = 5)
{
    Character caster;
    caster.name = "Caster";
    caster.level = level;
    caster.spellAttack = 15;
    caster.spellDC = 25;
    caster.keyAttribute = Attribute::Int;
    return caster;
}
} // namespace

TEST_CASE("import: a basic-save spell with interval heightening")
{
    ContentLibrary library = Import(FireSpell());
    Character caster = MakeCaster();
    ActionPtr blast = library.Use("Test Blast", caster);
    CHECK(blast->cost == 2);
    CHECK(blast->traits.count("fire") == 1);
    CHECK(blast->check.kind == CheckKind::Save);
    CHECK(blast->check.defense == Defense::Ref);
    CHECK(blast->check.modifier == 25);
    REQUIRE(blast->damage.size() == 1);
    CHECK(blast->damage.front().count == 6);
    CHECK(blast->damage.front().die == 6);
    CHECK(blast->damage.front().type == DamageType::Fire);
    CHECK(blast->outcomes[kCritSuccess].damage == DamageScale::None);
    CHECK(blast->outcomes[kSuccess].damage == DamageScale::Half);
    CHECK(blast->outcomes[kFail].damage == DamageScale::Normal);
    CHECK(blast->outcomes[kCritFail].damage == DamageScale::Double);
    CHECK(library.Use("Test Blast", caster, {.rank = 5})->damage.front().count == 10);

    SpellConversion conversion = ConvertFoundrySpell(FireSpell().dump());
    CHECK(Json::parse(conversion.definition).at("reference") == "Test Book");
    CHECK(conversion.warnings.empty());
}

TEST_CASE("import: an Electric Arc shaped like Foundry's matches the hand-written one")
{
    Json spell = FireSpell();
    spell["name"] = "Electric Arc";
    Json& system = spell["system"];
    system["level"]["value"] = 1;
    system["traits"]["value"] = {"cantrip", "concentrate", "electricity", "manipulate"};
    system["damage"] = {{"a1", {{"applyMod", false}, {"category", nullptr}, {"formula", "2d4"},
                                {"kinds", {"damage"}}, {"type", "electricity"}}}};
    system["heightening"] = {{"type", "interval"}, {"interval", 1}, {"damage", {{"a1", "1d4"}}}};
    ContentLibrary imported = Import(spell);
    ContentLibrary handWritten;
    handWritten.AddDirectory("data", "data/imported");

    for (int level : {1, 5, 11})
    {
        CAPTURE(level);
        Character caster = MakeCaster(level);
        ActionPtr mine = imported.Use("Electric Arc", caster);
        ActionPtr theirs = handWritten.Use("Electric Arc", caster);
        CHECK(mine->cost == theirs->cost);
        CHECK(mine->check.kind == theirs->check.kind);
        CHECK(mine->check.defense == theirs->check.defense);
        CHECK(mine->check.modifier == theirs->check.modifier);
        CHECK(mine->check.tags == theirs->check.tags);
        CHECK(mine->damageTags == theirs->damageTags);
        REQUIRE(mine->damage.size() == theirs->damage.size());
        for (std::size_t index = 0; index < mine->damage.size(); ++index)
        {
            CHECK(mine->damage[index].count == theirs->damage[index].count);
            CHECK(mine->damage[index].die == theirs->damage[index].die);
            CHECK(mine->damage[index].flat == theirs->damage[index].flat);
            CHECK(mine->damage[index].type == theirs->damage[index].type);
            CHECK(mine->damage[index].category == theirs->damage[index].category);
        }
        for (int degreeIndex = 0; degreeIndex < kDegrees; ++degreeIndex)
            CHECK(mine->outcomes[degreeIndex].damage == theirs->outcomes[degreeIndex].damage);
    }
}

TEST_CASE("import: attack spells roll against AC, or the defense Foundry names")
{
    Json spell = FireSpell();
    spell["system"]["traits"]["value"] = {"attack", "fire"};
    spell["system"]["defense"] = nullptr;
    ActionPtr ray = Import(spell).Use("Test Blast", MakeCaster());
    CHECK(ray->check.kind == CheckKind::Attack);
    CHECK(ray->check.defense == Defense::AC);
    CHECK(ray->check.modifier == 15);
    CHECK(ray->check.attackTrait);
    CHECK(ray->outcomes[kSuccess].damage == DamageScale::Normal);
    CHECK(ray->outcomes[kCritSuccess].damage == DamageScale::Double);

    spell["system"]["defense"] = {{"passive", {{"statistic", "fortitude-dc"}}}, {"save", nullptr}};
    CHECK(Import(spell).Use("Test Blast", MakeCaster())->check.defense == Defense::Fort);
}

TEST_CASE("import: outcomes listed by degree, with or without condition links")
{
    Json spell = SaveSpell(
        "<p><strong>Critical Success</strong> The target is unaffected.</p>"
        "<p><strong>Success</strong> The target is "
        "@UUID[Compendium.pf2e.conditionitems.Item.Frightened]{Frightened 1}.</p>"
        "<p><strong>Failure</strong> The target is slowed 1 and off-guard for 1 round.</p>"
        "<p><strong>Critical Failure</strong> The target is "
        "@UUID[Compendium.pf2e.conditionitems.Item.Frightened]{Frightened 3} and falls prone until the end of "
        "its next turn.</p>");
    ActionPtr scare = Import(spell).Use("Test Scare", MakeCaster());
    CHECK(scare->check.kind == CheckKind::Save);
    CHECK(scare->check.defense == Defense::Will);
    CHECK(scare->outcomes[kCritSuccess].conditions.empty());
    REQUIRE(scare->outcomes[kSuccess].conditions.size() == 1);
    CHECK(scare->outcomes[kSuccess].conditions[0].kind == ConditionKind::Frightened);
    CHECK(scare->outcomes[kSuccess].conditions[0].value == 1);
    REQUIRE(scare->outcomes[kFail].conditions.size() == 2);
    CHECK(scare->outcomes[kFail].conditions[0].kind == ConditionKind::Slowed);
    CHECK(scare->outcomes[kFail].conditions[1].kind == ConditionKind::OffGuard);
    REQUIRE(scare->outcomes[kCritFail].conditions.size() == 2);
    CHECK(scare->outcomes[kCritFail].conditions[0].value == 3);
    CHECK(scare->outcomes[kCritFail].conditions[1].kind == ConditionKind::Prone);

    // A save that isn't basic: the paragraphs say how much damage each degree deals.
    Json damaging = FireSpell();
    damaging["system"]["defense"]["save"]["basic"] = false;
    damaging["system"]["description"]["value"] =
        "<p><strong>Success</strong> The creature takes half damage.</p>"
        "<p><strong>Failure</strong> The creature takes full damage and is "
        "@UUID[Compendium.pf2e.conditionitems.Item.Sickened]{Sickened 1}.</p>";
    ActionPtr blast = Import(damaging).Use("Test Blast", MakeCaster());
    CHECK(blast->outcomes[kCritSuccess].damage == DamageScale::None);
    CHECK(blast->outcomes[kSuccess].damage == DamageScale::Half);
    CHECK(blast->outcomes[kFail].damage == DamageScale::Normal);
    CHECK(blast->outcomes[kFail].conditions.size() == 1);
    CHECK(blast->outcomes[kCritFail].damage == DamageScale::None);
}

TEST_CASE("import: fixed heightening gives the whole damage from that rank on")
{
    Json spell = FireSpell();
    Json& system = spell["system"];
    system["level"]["value"] = 7;
    system["damage"]["xYz1"]["formula"] = "6d8";
    system["damage"]["xYz1"]["type"] = "bludgeoning";
    system["damage"]["p2"] = {{"applyMod", false}, {"category", nullptr}, {"formula", "1d6"},
                              {"kinds", {"damage"}}, {"type", "fire"}};
    // Each level changes one roll; the other keeps its value from the levels below.
    system["heightening"] = Json::parse(R"({"type": "fixed", "levels": {
        "8": {"damage": {"xYz1": {"applyMod": false, "category": null, "formula": "7d8", "type": "bludgeoning"}}},
        "9": {"damage": {"p2": {"formula": "2d6"}}, "target": {"value": "2 creatures"}}}})");
    ContentLibrary library = Import(spell);
    Character caster = MakeCaster(17);
    auto dice = [&](int rank) {
        ActionPtr blast = library.Use("Test Blast", caster, {.rank = rank});
        return std::pair{blast->damage.at(0).count, blast->damage.at(1).count};
    };
    CHECK(dice(7) == std::pair{6, 1});
    CHECK(dice(8) == std::pair{7, 1});
    CHECK(dice(9) == std::pair{7, 2});
    CHECK(dice(10) == std::pair{7, 2});
}

TEST_CASE("import: healing spells, casting times and persistent damage")
{
    Json soothe = FireSpell();
    soothe["name"] = "Test Soothe";
    Json& system = soothe["system"];
    system["level"]["value"] = 1;
    system["defense"] = nullptr;
    system["damage"] = {{"h", {{"applyMod", false}, {"category", nullptr}, {"formula", "1d10+4"},
                               {"kinds", {"healing"}}, {"type", "vitality"}}}};
    system["heightening"] = {{"type", "interval"}, {"interval", 1}, {"damage", {{"h", "1d10+4"}}}};
    ContentLibrary library = Import(soothe);
    Character caster = MakeCaster();
    Character ally = MakeCaster();
    ally.name = "Ally";
    const HealSpec& heal = library.Use("Test Soothe", caster, {.rank = 3, .ally = &ally})->outcomes[kSuccess].heal;
    CHECK(heal.count == 3);
    CHECK(heal.die == 10);
    CHECK(heal.flat == 12);
    CHECK(heal.to == Who::Ally);

    Json reaction = FireSpell();
    reaction["system"]["time"]["value"] = "reaction";
    CHECK(Import(reaction).Use("Test Blast", caster)->reaction);
    Json free = FireSpell();
    free["system"]["time"]["value"] = "free";
    CHECK(Import(free).Use("Test Blast", caster)->cost == 0);

    Json persistent = FireSpell();
    persistent["system"]["damage"]["xYz1"]["category"] = "persistent";
    CHECK(Import(persistent).Use("Test Blast", caster)->damage.front().category == DamageCategory::Persistent);
}

TEST_CASE("import: spells that can't be converted faithfully are skipped, with the reason")
{
    auto changed = [](auto change) {
        Json spell = FireSpell();
        change(spell["system"]);
        return SkipReason(spell);
    };
    CHECK(changed([](Json& system) { system["time"]["value"] = "1 minute"; }) ==
          "casting time isn't 1 to 3 actions, free or a reaction");
    CHECK(changed([](Json& system) { system["time"]["value"] = "1 to 3"; }) == "takes a variable number of actions");
    CHECK(changed([](Json& system) {
              system["defense"] = nullptr;
              system["damage"] = Json::object();
          }) == "utility: no combat numbers in Foundry's data");
    CHECK(changed([](Json& system) { system["defense"] = nullptr; }) == "deals damage with no attack roll or save");
    CHECK(changed([](Json& system) { system["damage"]["xYz1"]["applyMod"] = true; }) ==
          "adds the spellcasting modifier to its damage");
    CHECK(changed([](Json& system) { system["damage"]["xYz1"]["kinds"] = {"damage", "healing"}; }) ==
          "damages or heals depending on the target");
    CHECK(changed([](Json& system) { system["damage"]["xYz1"]["category"] = "splash"; }) ==
          "damage category isn't modeled");
    CHECK(changed([](Json& system) { system["damage"]["xYz1"]["formula"] = "@item.rank"; }) ==
          "damage isn't plain dice");
    CHECK(changed([](Json& system) { system["traits"]["value"] = {"attack"}; }) ==
          "has both an attack roll and a save");
    CHECK(changed([](Json& system) { system["defense"]["save"]["basic"] = false; }) ==
          "the save isn't basic, and the outcomes aren't listed");
    CHECK(changed([](Json& system) {
              system["description"]["value"] = "<p><strong>Failure</strong> The target is pushed 10 feet.</p>";
          }) == "outcome text isn't understood");
    CHECK(changed([](Json& system) {
              system["defense"]["save"]["basic"] = false;
              system["description"]["value"] = "<p><strong>Failure</strong> The target is slowed 1.</p>";
          }) == "has damage, but the outcomes don't say how much");
    CHECK(ConvertFoundrySpell("{not json").skipReason == "data isn't in the expected format");

    // Conditions the engine doesn't model convert, but don't load.
    SpellConversion fleeing = ConvertFoundrySpell(
        SaveSpell("<p><strong>Failure</strong> The target is "
                  "@UUID[Compendium.pf2e.conditionitems.Item.Fleeing]{Fleeing}.</p>")
            .dump());
    ContentLibrary library;
    CHECK_THROWS_WITH_AS(library.AddJson(fleeing.definition, "import.json"),
                         doctest::Contains("unknown condition \"fleeing\""), ContentError);
}

TEST_CASE("import: what the conversion leaves out is listed as a warning")
{
    Json spell = FireSpell();
    spell["system"]["description"]["value"] =
        "<p>Fire. On a critical failure, the target is also "
        "@UUID[Compendium.pf2e.conditionitems.Item.Dazzled]{Dazzled}.</p>";
    spell["system"]["overlays"] = {{"o1", {{"name", "Bigger"}}}};
    SpellConversion conversion = ConvertFoundrySpell(spell.dump());
    REQUIRE(conversion.warnings.size() == 2);
    CHECK(conversion.warnings[0] == "has variants (Foundry overlays); only the base version was imported");
    CHECK(conversion.warnings[1] ==
          "the description has more effects (a condition or spell effect outside the outcomes)");
}

TEST_CASE("import: formulas from Foundry's rules")
{
    const std::string heroism = "ternary(gte(@item.level,9),3,ternary(gte(@item.level,6),2,1))";
    CHECK(EvaluateFoundryFormula(heroism, 3) == 1);
    CHECK(EvaluateFoundryFormula(heroism, 6) == 2);
    CHECK(EvaluateFoundryFormula(heroism, 9) == 3);
    CHECK(EvaluateFoundryFormula("ceil(1+(@item.level / 2))", 1) == 2);
    CHECK(EvaluateFoundryFormula("ceil(1+(@item.level / 2))", 3) == 3);
    CHECK(EvaluateFoundryFormula("ceil(1+(@item.level / 2))", 4) == 3);
    CHECK(EvaluateFoundryFormula("1+@item.level", 2) == 3);
    CHECK(EvaluateFoundryFormula("max(1, @item.rank - 3)", 2) == 1);
    CHECK(EvaluateFoundryFormula("(1 + 2) * -3", 1) == -9);
    CHECK(EvaluateFoundryFormula("floor(7 / 2)", 1) == 3);
    CHECK_FALSE(EvaluateFoundryFormula("@actor.level", 5));
    CHECK_FALSE(EvaluateFoundryFormula("{item|flags.system.rulesSelections.enlarge.damage}", 5));
    CHECK_FALSE(EvaluateFoundryFormula("roll(1d6)", 5));
    CHECK_FALSE(EvaluateFoundryFormula("1 +", 5));
    CHECK_FALSE(EvaluateFoundryFormula("1 / 0", 5));
}

TEST_CASE("import: buffs come from the spell effects they link to, and match the hand-written ones")
{
    ContentLibrary handWritten = HandWritten();
    Character caster = MakeCaster(20);
    Character ally = MakeCaster();
    ally.name = "Ally";

    Json heroism = NoCheckSpell("Heroism", 3, "1 creature", false,
                                "<p>Heroism.</p>" + EffectLink("Spell Effect: Heroism"));
    SpellEffects heroismEffect = Effect("Spell Effect: Heroism", R"json([{"key": "FlatModifier",
        "selector": ["attack", "saving-throw", "skill-check", "perception"], "type": "status",
        "value": "ternary(gte(@item.level,9),3,ternary(gte(@item.level,6),2,1))"}])json");
    ContentLibrary imported = Import(heroism, heroismEffect);
    for (int rank : {3, 5, 6, 8, 9, 10})
    {
        CAPTURE(rank);
        CheckSameBuff(*imported.Use("Heroism", caster, {.rank = rank, .ally = &ally}),
                      *handWritten.Use("Heroism", caster, {.rank = rank, .ally = &ally}));
    }

    // An area for you and your allies; the Aura rule only says who the effect reaches.
    Json bless = NoCheckSpell("Bless", 1, "you and allies in the area", true,
                              "<p>Bless.</p>" + EffectLink("Spell Effect: Bless"));
    ContentLibrary blessed = Import(bless, Effect("Spell Effect: Bless", R"json([
        {"key": "Aura", "radius": 15, "effects": [{"affects": "allies"}]},
        {"key": "FlatModifier", "selector": "attack-roll", "type": "status", "value": 1}])json"));
    CheckSameBuff(*blessed.Use("Bless", caster), *handWritten.Use("Bless", caster));

    // Parts that apply only in some cases are left out, with a warning each; RollOption is only a toggle.
    Json anthem = NoCheckSpell("Courageous Anthem", 1, "", true,
                               "<p>Anthem.</p>" + EffectLink("Spell Effect: Courageous Anthem"));
    anthem["system"]["time"]["value"] = "1";
    anthem["system"]["traits"]["value"] = {"cantrip", "composition"};
    SpellEffects anthemEffect = Effect("Spell Effect: Courageous Anthem", R"json([
        {"key": "FlatModifier", "selector": ["attack-roll", "damage"], "type": "status", "value": 1},
        {"key": "FlatModifier", "predicate": ["fear"], "selector": "saving-throw", "type": "status", "value": 1},
        {"key": "DamageDice", "predicate": ["discordant-voice"], "selector": "strike-damage", "diceNumber": 1},
        {"key": "RollOption", "option": "courageous-anthem"}])json");
    SpellConversion conversion = ConvertFoundrySpell(anthem.dump(), anthemEffect);
    CHECK(conversion.warnings == std::vector<std::string>{
                                     "left out: a conditional FlatModifier rule (saving-throw)",
                                     "left out: a conditional DamageDice rule (strike-damage)"});
    CheckSameBuff(*Import(anthem, anthemEffect).Use("Courageous Anthem", caster),
                  *handWritten.Use("Courageous Anthem", caster));
}

TEST_CASE("import: what a buff can't express is left out with a warning, or the buff is skipped")
{
    Json spell = NoCheckSpell("Test Buff", 2, "", true, "<p>Buff.</p>" + EffectLink("Spell Effect: Test Buff"));
    auto effect = [](const std::string& rules) { return Effect("Spell Effect: Test Buff", rules); };

    SpellEffects speedAndAttack = effect(R"json([
        {"key": "FlatModifier", "selector": "all-speeds", "type": "status", "value": 10},
        {"key": "FlatModifier", "selector": "attack-roll", "type": "status", "value": 1}])json");
    SpellConversion valiant = ConvertFoundrySpell(spell.dump(), speedAndAttack);
    CHECK(valiant.warnings == std::vector<std::string>{"left out: a bonus to all-speeds (not modeled)"});
    ActionPtr buff = Import(spell, speedAndAttack).Use("Test Buff", MakeCaster());
    REQUIRE(buff->outcomes[kSuccess].modifiers.size() == 1);
    CHECK(buff->outcomes[kSuccess].modifiers[0].to == Who::AllAllies);
    CHECK(buff->outcomes[kSuccess].modifiers[0].mod.applies.anyOf == std::vector<Tags>{Tag::AttackRoll});

    // Two modifiers from one spell get their own sources, so they load (the same source never stacks).
    ActionPtr twoParts = Import(spell, effect(R"json([
        {"key": "FlatModifier", "selector": "attack-roll", "type": "status", "value": 1},
        {"key": "FlatModifier", "selector": "ac", "type": "circumstance", "value": 1}])json"))
                             .Use("Test Buff", MakeCaster());
    REQUIRE(twoParts->outcomes[kSuccess].modifiers.size() == 2);
    CHECK(twoParts->outcomes[kSuccess].modifiers[1].mod.source == "Test Buff (2)");

    CHECK(SkipReason(spell, effect(R"json([{"key": "BattleForm"}])json")) ==
          "battle form (polymorph; not modeled)");
    CHECK(SkipReason(spell, effect(R"json([{"key": "ChoiceSet", "choices": []}])json")) ==
          "asks for a choice when cast");
    CHECK(SkipReason(spell, effect(R"json([{"key": "ActiveEffectLike"}])json")) ==
          "uses Foundry's ActiveEffectLike rule, which isn't converted");
    CHECK(SkipReason(spell, effect(R"json([{"key": "FlatModifier", "selector": "athletics", "value": 1}])json")) ==
          "only a bonus to a specific skill (one skill can't be targeted)");
    CHECK(SkipReason(spell, effect(R"json([{"key": "TempHP", "value": 5}])json")) ==
          "only defensive or movement effects (not modeled)");
    SpellEffects byLevel =
        effect(R"json([{"key": "FlatModifier", "selector": "damage", "value": "@actor.level"}])json");
    CHECK(SkipReason(spell, byLevel) == "a bonus depends on the caster or on a choice");
    CHECK(SkipReason(spell) == "links a spell effect that wasn't found");
    Json several = spell;
    several["system"]["target"]["value"] = "up to 3 creatures";
    several["system"]["area"] = nullptr;
    CHECK(SkipReason(several, effect(R"json([{"key": "FlatModifier", "selector": "damage", "value": 1}])json")) ==
          "targets something other than one ally, yourself or your allies");
}

TEST_CASE("import: conditions a spell gives with no roll")
{
    Character caster = MakeCaster();
    Character ally = MakeCaster();
    ally.name = "Ally";

    // From the spell effect: a granted condition.
    Json haste = NoCheckSpell("Test Haste", 3, "1 creature", false, "<p>Fast.</p>" + EffectLink("Spell Effect: Haste"));
    ActionPtr hasted =
        Import(haste, Effect("Spell Effect: Haste", R"json([{"key": "GrantItem",
            "uuid": "Compendium.pf2e.conditionitems.Item.Quickened"}])json"))
            .Use("Test Haste", caster, {.ally = &ally});
    REQUIRE(hasted->outcomes[kSuccess].conditions.size() == 1);
    CHECK(hasted->outcomes[kSuccess].conditions[0].kind == ConditionKind::Quickened);
    CHECK(hasted->outcomes[kSuccess].conditions[0].to == Who::Ally);

    // From a sentence; "can't reduce their frightened value" is an allowed ending, not another condition.
    Json dirge = NoCheckSpell("Test Dirge", 3, "", true,
                              "<p>Enemies within the area are " + ConditionLink("Frightened", "Frightened 1") +
                                  ". They can't reduce their frightened value below 1 while they remain in the "
                                  "area.</p>");
    ActionPtr doom = Import(dirge).Use("Test Dirge", caster);
    REQUIRE(doom->outcomes[kSuccess].conditions.size() == 1);
    CHECK(doom->outcomes[kSuccess].conditions[0].kind == ConditionKind::Frightened);
    CHECK(doom->outcomes[kSuccess].conditions[0].value == 1);
    CHECK(doom->outcomes[kSuccess].conditions[0].to == Who::Enemy);

    Json allegro = NoCheckSpell("Test Allegro", 7, "1 ally", false,
                                "<p>You perform rapidly. The ally becomes " + ConditionLink("Quickened") +
                                    " and can use the additional action to Strike, Stride, or Step.</p>");
    SpellConversion allegroConversion = ConvertFoundrySpell(allegro.dump());
    CHECK(allegroConversion.warnings ==
          std::vector<std::string>{"converted from a sentence in the description; the rest of it wasn't read"});
    ActionPtr quick = Import(allegro).Use("Test Allegro", caster, {.ally = &ally});
    REQUIRE(quick->outcomes[kSuccess].conditions.size() == 1);
    CHECK(quick->outcomes[kSuccess].conditions[0].kind == ConditionKind::Quickened);
    CHECK(quick->outcomes[kSuccess].conditions[0].to == Who::Ally);

    // A sentence that says anything more is not guessed at.
    const std::string notUnderstood = "mentions a condition the converter doesn't understand";
    CHECK(SkipReason(NoCheckSpell("Later", 3, "1 creature", false,
                                  "<p>The target is " + ConditionLink("Quickened") +
                                      " on its next turn and can use the action only to Step.</p>")) == notUnderstood);
    CHECK(SkipReason(NoCheckSpell("Not", 1, "", false,
                                  "<p>You are not " + ConditionLink("Off-Guard") + " to the attacker.</p>")) ==
          notUnderstood);
    CHECK(SkipReason(NoCheckSpell("Unless", 2, "", true,
                                  "<p>Creatures in the area are " + ConditionLink("Off-Guard") +
                                      " unless they succeed at a Reflex save.</p>")) == notUnderstood);
}

TEST_CASE("import: the reason a spell was left out says what kind of spell it is")
{
    Json summon = NoCheckSpell("Test Summon", 1, "", false, "<p>A creature appears.</p>");
    summon["system"]["traits"]["value"] = {"concentrate", "manipulate", "summon"};
    CHECK(SkipReason(summon) == "summons a creature or minion");
    Json song = NoCheckSpell("Test Song", 1, "", false, "<p>Attempt a @Check[performance|against:will] check.</p>");
    CHECK(SkipReason(song) == "rolls a check written only in the description");
    CHECK(SkipReason(NoCheckSpell("Test Aura", 1, "", true, "<p>Creatures take @Damage[1d4[fire]] damage.</p>")) ==
          "deals damage written only in the description (auras, ongoing or triggered damage)");
    CHECK(SkipReason(NoCheckSpell("Test Mind", 1, "1 creature", false,
                                  "<p><strong>Success</strong> The target is unaffected.</p>")) ==
          "lists outcomes, but Foundry's data has no attack roll or save for them");
    CHECK(SkipReason(NoCheckSpell("Test Light", 1, "1 object", false, "<p>The object glows.</p>")) ==
          "utility: no combat numbers in Foundry's data");
}

TEST_CASE("import: \"takes no damage\" is no damage")
{
    Json spell = FireSpell();
    spell["system"]["defense"]["save"]["basic"] = false;
    spell["system"]["description"]["value"] = "<p><strong>Critical Success</strong> The creature takes no damage.</p>"
                                              "<p><strong>Failure</strong> The creature takes full damage.</p>";
    ActionPtr blast = Import(spell).Use("Test Blast", MakeCaster());
    CHECK(blast->outcomes[kCritSuccess].damage == DamageScale::None);
    CHECK(blast->outcomes[kFail].damage == DamageScale::Normal);
}
