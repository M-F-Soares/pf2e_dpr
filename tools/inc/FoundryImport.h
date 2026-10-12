/**
 * @file    FoundryImport.h
 * @brief   Converts spells from Foundry VTT's PF2e system data into content definitions.
 * @details Foundry keeps each spell as a JSON file (github.com/foundryvtt/pf2e, packs/pf2e/spells). Its damage,
 *          saves and heightening are structured; what each degree of success does is only in the description.
 *          The converter takes the structured parts, reads simple degree-of-success paragraphs, and skips any
 *          spell it can't convert faithfully. No JSON types appear here.
 */

#pragma once

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
 *          understood: condition links, "unaffected", and no, half, full or double damage. The definition isn't
 *          validated here; load it with ParseDefinitions() to check it.
 * @param   [in] foundryJson - The contents of one file from Foundry's spell folder.
 * @return  The definition, or why the spell was skipped.
 */
SpellConversion ConvertFoundrySpell(const std::string& foundryJson);
