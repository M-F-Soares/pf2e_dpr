/**
 * @file    ExampleParty.h
 * @brief   Sample characters and an enemy for the example and for experimenting.
 * @details Sample level 5 characters and enemy, with typical sheet numbers. Replace with your own party: only
 *          the totals matter (attack bonus, DCs, skills).
 */

#pragma once

#include "Creature.h"

/**
 * @brief   Level 5 ranger with a composite longbow (deadly d10) and Precision edge.
 * @return  The character.
 */
Character ExampleRanger();

/**
 * @brief   Level 5 bard (Charisma caster) with a rapier, and Intimidation, Deception and Performance.
 * @return  The character.
 */
Character ExampleBard();

/**
 * @brief   Level 5 thief rogue with a rapier and 2d6 Sneak Attack.
 * @return  The character.
 */
Character ExampleRogue();

/**
 * @brief   Level 5 cleric (Wisdom caster) with a mace, Medicine and Religion.
 * @return  The character.
 */
Character ExampleCleric();

/**
 * @brief   Level 5 brute with no immunities, resistances or weaknesses.
 * @return  The monster.
 */
Monster ExampleBrute();

/**
 * @brief   "Seis": level 15, fatal d12 blade with a flaming rune.
 * @return  The character.
 */
Character Seis();
