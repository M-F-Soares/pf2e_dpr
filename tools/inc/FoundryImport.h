/**
 * @file    FoundryImport.h
 * @brief   Converts spells from Foundry VTT's PF2e system data into content definitions.
 * @details Foundry keeps each spell as a JSON file (github.com/foundryvtt/pf2e, packs/pf2e/spells). Its damage,
 *          saves and heightening are structured; what each degree of success does is only in the description.
 *          A buff's bonuses are in a separate "spell effect" item (packs/pf2e/spell-effects) that the spell links
 *          to. The converter takes the structured parts, reads simple degree-of-success paragraphs, converts the
 *          simple rules of linked spell effects, and skips any spell it can't convert faithfully. No JSON types
 *          appear here.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief   The result of converting one Foundry spell.
 */
struct SpellConversion
{
    std::string name;                   /**< The spell's name. */
    std::string definition;             /**< Our definition, as JSON text; empty when the spell was skipped. */
    std::string skipReason;             /**< Why it was skipped (the same text for the same reason); else empty. */
    std::string skipDetail;             /**< The specifics, e.g. the casting time; may be empty. */
    std::vector<std::string> warnings;  /**< Things to check by hand in a converted spell. */
};

/**
 * @brief   Converts one spell from Foundry's data into a content definition.
 * @details Maps the name, rank (or cantrip), casting time, traits, attack roll or save, damage or healing, and
 *          both kinds of heightening. Degree-of-success paragraphs are read when every phrase in them is
 *          understood: condition links, "unaffected", and no, half, full or double damage.
 *
 *          A spell with no attack roll, save, damage or healing is converted from its linked spell effect
 *          (unconditional flat bonuses, and conditions it grants), or from a sentence that gives its target a
 *          condition ("It gains the quickened condition"); otherwise the skip reason says what kind of spell it
 *          is. The definition isn't validated here; load it with ParseDefinitions() to check it.
 * @param   [in] foundryJson - The contents of one file from Foundry's spell folder.
 * @param   [in] spellEffects - Foundry's spell effects, by name: the contents of each file in its spell effect
 *          folder. Empty to convert without them.
 * @return  The definition, or why the spell was skipped.
 */
SpellConversion ConvertFoundrySpell(const std::string& foundryJson,
                                    const std::map<std::string, std::string>& spellEffects = {});

/**
 * @brief   Evaluates a formula from Foundry's rules, such as "ternary(gte(@item.level,6),2,1)", for one rank.
 * @details Supports numbers, `@item.level` and `@item.rank` (the spell's rank), `+ - * /`, parentheses, and the
 *          functions ternary, gte, gt, lte, lt, eq, ceil, floor, min and max.
 * @param   [in] formula - The formula.
 * @param   [in] rank - The spell rank it's evaluated at.
 * @return  The value, or nothing if the formula uses anything else (such as `@actor.level`).
 */
std::optional<double> EvaluateFoundryFormula(const std::string& formula, int rank);
