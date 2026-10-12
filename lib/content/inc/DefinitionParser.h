/**
 * @file    DefinitionParser.h
 * @brief   Reads content definitions from JSON text.
 */

#pragma once

#include "Definitions.h"
#include "Hooks.h"

#include <string>

/**
 * @brief   Reads the definitions in one data file.
 * @details Strict: an unknown key or name, a value of the wrong type, or an inconsistent definition is an
 *          error. "extends" (an earlier action in the same file), variants and "Heightened (Nth)" entries are
 *          expanded into ActionEntry::versions here.
 * @param   [in] text - The file's contents: one definition object, or an array of them.
 * @param   [in] sourceName - The file's name, for error messages.
 * @param   [in] hooks - The hooks definitions may call.
 * @return  The actions and features the file defines.
 * @throws  ContentError if the text isn't valid JSON or a definition is invalid.
 */
DefinitionSet ParseDefinitions(const std::string& text, const std::string& sourceName, const HookRegistry& hooks);
