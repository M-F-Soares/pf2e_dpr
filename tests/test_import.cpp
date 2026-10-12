/**
 * @file    test_import.cpp
 * @brief   Tests of the Foundry VTT spell converter (tools/), on small spells shaped like Foundry's data.
 */

#include "vendor/doctest.h"

#include "ContentLibrary.h"
#include "FoundryImport.h"

#include <nlohmann/json.hpp>

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

/**
 * @brief   Converts a spell, which must convert.
 * @param   [in] spell - The Foundry spell.
 * @return  A library with only the converted spell.
 */
ContentLibrary Import(const Json& spell)
{
    SpellConversion conversion = ConvertFoundrySpell(spell.dump());
    INFO("skipped: " << conversion.skipReason << " (" << conversion.skipDetail << ")");
    REQUIRE_FALSE(conversion.definition.empty());
    ContentLibrary library;
    library.AddJson(conversion.definition, "import.json");
    return library;
}

/**
 * @brief   Why a spell was skipped.
 * @param   [in] spell - The Foundry spell.
 * @return  The skip reason; empty if it was converted.
 */
std::string SkipReason(const Json& spell)
{
    return ConvertFoundrySpell(spell.dump()).skipReason;
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
          }) == "no attack roll, save, damage or healing (a buff or utility spell)");
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
