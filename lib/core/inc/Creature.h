/**
 * @file    Creature.h
 * @brief   Party members, the enemy, their weapons, and the defenses checks target.
 */

#pragma once

#include "Damage.h"
#include "Modifier.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
 * @brief   Skills, each tied to the attribute in SkillAttribute().
 */
enum class Skill
{
    Acrobatics,    /**< Acrobatics (Dex). */
    Arcana,        /**< Arcana (Int). */
    Athletics,     /**< Athletics (Str): Trip, Grapple. */
    Crafting,      /**< Crafting (Int). */
    Deception,     /**< Deception (Cha): Feint. */
    Diplomacy,     /**< Diplomacy (Cha). */
    Intimidation,  /**< Intimidation (Cha): Demoralize. */
    Medicine,      /**< Medicine (Wis): Battle Medicine. */
    Nature,        /**< Nature (Wis). */
    Occultism,     /**< Occultism (Int). */
    Performance,   /**< Performance (Cha). */
    Religion,      /**< Religion (Wis). */
    Society,       /**< Society (Int). */
    Stealth,       /**< Stealth (Dex). */
    Survival,      /**< Survival (Wis). */
    Thievery,      /**< Thievery (Dex). */
};

/**
 * @brief   The attribute a skill is based on (which conditions like clumsy affect it).
 * @param   [in] skill - The skill.
 * @return  Its attribute, or Attribute::None for an out-of-range value.
 */
Attribute SkillAttribute(Skill skill);

/**
 * @brief   A defense of the enemy that a check targets or that the enemy rolls.
 */
enum class Defense
{
    AC,          /**< Armor Class. */
    Fort,        /**< Fortitude. */
    Ref,         /**< Reflex. */
    Will,        /**< Will. */
    Perception,  /**< Perception. */
};

/**
 * @brief   Tags for the defender rolling a save/Perception check, and for the defense used as a DC.
 * @param   [in] defense - The defense being rolled.
 * @return  Save/Perception, Check, the defense and its attribute (AC gets its DC tags).
 */
Tags DefenseRollTags(Defense defense);

/**
 * @brief   Tags for a defense used as a DC (AC, Will DC, ...).
 * @param   [in] defense - The defense.
 * @return  DC, the defense and its attribute.
 */
Tags DefenseDCTags(Defense defense);

struct EffectCtx;

/**
 * @brief   A passive class feature that changes Strike damage (Sneak Attack, Precision edge...).
 * @details It runs once per damaging outcome and may append damage components or set counters.
 */
struct Feature
{
    std::string name;  /**< Name of the feature. */

    /**
     * @brief   Called for each damaging outcome of a Strike.
     * @details EffectCtx::damage points to the damage being built, and EffectCtx::state can be changed (e.g.
     *          to mark a once-per-round use).
     */
    std::function<void(EffectCtx&)> onStrikeDamage;
};

/**
 * @brief   A weapon on a character sheet.
 * @details Numbers are the totals from the character sheet.
 */
struct Weapon
{
    std::string name;                           /**< Name used to pick the weapon, e.g. in Strike(). */
    int attack = 0;                             /**< Total attack bonus, potency rune included. */
    int potency = 0;                            /**< Item bonus inside `attack` (so it doesn't stack with other item bonuses). */
    int dice = 1;                               /**< 1 + striking rune. */
    int die = 6;                                /**< Weapon damage die size. */
    DamageType type = DamageType::Slashing;     /**< Weapon damage type. */
    int flat = 0;                               /**< Attribute + weapon specialization + other always-on flat damage. */
    bool ranged = false;                        /**< Ranged weapon (its attacks are tagged Ranged instead of Melee). */
    bool agile = false;                         /**< Agile trait: -4/-8 multiple attack penalty instead of -5/-10. */
    bool finesse = false;                       /**< Finesse trait (Sneak Attack works with it). */
    Attribute attackAttribute = Attribute::Str; /**< Attribute of the attack roll (clumsy, enfeebled). */
    bool strengthToDamage = true;               /**< Tags the damage roll as Str-based (enfeebled). */
    int deadly = 0;                             /**< Deadly die size (0 = none). */
    int fatal = 0;                              /**< Fatal die size (0 = none). */
    std::vector<DamageComponent> onHit;         /**< Property runes, e.g. flaming 1d6 fire. */
    std::vector<DamageComponent> onCrit;        /**< Crit-only riders, e.g. flaming 1d10 persistent fire. */
    std::set<std::string> traits;               /**< Materials/traits that bypass resistances ("silver", "cold-iron"). */
};

/**
 * @brief   A party member, with the totals from their character sheet.
 */
struct Character
{
    std::string name;                           /**< Unique name in the party; identifies the character in turns and reports. */
    int level = 1;                              /**< Character level. */
    int maxHP = 10;                             /**< Maximum Hit Points (for reference; the engine only tracks missing HP). */
    int hpMissing = 0;                          /**< Damage already taken when the round starts (for effective healing). */
    int spellAttack = 0;                        /**< Spell attack modifier. */
    int spellDC = 10;                           /**< Spell DC. */
    int classDC = 10;                           /**< Class DC (for reference; no built-in action uses it). */
    Attribute keyAttribute = Attribute::None;   /**< Spellcasting / class DC attribute. */
    std::map<Skill, int> skills;                /**< Skill modifiers; a skill that's missing can't be used. */
    std::vector<Weapon> weapons;                /**< Weapons, picked by name. */
    std::vector<Feature> features;              /**< Passive features applied to this character's Strikes. */

    /**
     * @brief   The modifier for a skill.
     * @param   [in] skill - The skill.
     * @return  The total modifier.
     * @throws  std::runtime_error if no modifier is set for @p skill.
     */
    int SkillModifier(Skill skill) const;

    /**
     * @brief   A weapon by name.
     * @param   [in] weaponName - The weapon's Weapon::name.
     * @return  The weapon.
     * @throws  std::runtime_error if the character has no weapon with that name.
     */
    const Weapon& GetWeapon(const std::string& weaponName) const;
};

/**
 * @brief   The enemy, with the numbers from its stat block.
 */
struct Monster
{
    std::string name;               /**< Name shown in reports. */
    int level = 1;                  /**< Creature level (incapacitation, drained HP loss). */
    int ac = 10;                    /**< Armor Class. */
    int fort = 0;                   /**< Fortitude modifier. */
    int ref = 0;                    /**< Reflex modifier. */
    int will = 0;                   /**< Will modifier. */
    int perception = 0;             /**< Perception modifier. */
    int hp = 0;                     /**< Hit Points (for reference; not used by the engine). */
    std::set<std::string> traits;   /**< Creature traits (for reference; not used by the engine). */
    DamageDefenses defenses;        /**< Immunities may also name traits ("mental", "fear") or conditions ("frightened"). */

    /**
     * @brief   The value of a defense.
     * @param   [in] defense - The defense.
     * @return  AC, or the save/Perception modifier.
     */
    int DefenseValue(Defense defense) const;

    /**
     * @brief   A defense as a DC.
     * @param   [in] defense - The defense.
     * @return  AC, or 10 + modifier.
     */
    int DefenseDC(Defense defense) const;
};
