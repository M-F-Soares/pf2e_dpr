/**
 * @file    Definitions.h
 * @brief   Typed content definitions, as read from the data files.
 * @details The parser turns each JSON definition into these structs, and the templates turn them into engine
 *          Actions and Features for one character. No JSON types appear here.
 */

#pragma once

#include "Action.h"

#include <array>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief   A content definition that is invalid, unknown, or used the wrong way.
 * @details Messages name the file and the definition, e.g.
 *          `data/spells/fear.json: Fear: unknown condition "frightend"`.
 */
class ContentError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief   Which of the actor's numbers a check uses.
 */
enum class StatSource
{
    None,     /**< No check. */
    Spell,    /**< Spell attack modifier (rolled against a defense) or spell DC (the enemy saves). */
    ClassDC,  /**< Class DC (the enemy saves). */
    Skill,    /**< A skill modifier (CheckDefinition::skill). */
    Weapon,   /**< The attack bonus of the weapon it's used with (UseOptions::weapon), against AC. */
    Chosen,   /**< A weapon's attack bonus or a skill, whichever it's used with (UseOptions::weapon or skill). */
};

/**
 * @brief   The check an action rolls, before it's tied to a character.
 */
struct CheckDefinition
{
    CheckKind kind = CheckKind::None;    /**< None, Attack (against a defense), Save or FixedDC. */
    StatSource stat = StatSource::None;  /**< Which of the actor's numbers it uses. */
    Skill skill = Skill::Acrobatics;     /**< The skill, when `stat` is StatSource::Skill. */
    Defense defense = Defense::AC;       /**< Attack: the defense used as the DC. Save: the save rolled. */
    int dc = 0;                          /**< FixedDC: the DC. */
};

/**
 * @brief   What each "Heightened (+N)" step adds.
 */
struct Heightening
{
    int every = 0;                        /**< Spell ranks per step; 0 = no steps. */
    std::vector<DamageComponent> damage;  /**< Damage added per step, merged into matching damage. */
    HealSpec heal;                        /**< Dice and flat healing added per step to every heal. */
};

/**
 * @brief   "Then the target is temporarily immune": the action can't affect the same target again.
 */
struct TemporaryImmunity
{
    Who on = Who::Enemy;    /**< Who becomes immune: Who::Enemy or Who::Ally. */
    bool perActor = false;  /**< Immune only to this character's use of the action, not to anyone's. */
};

/**
 * @brief   Resistance that a carried spell's damage ignores (Overwhelming Spellstrike).
 */
struct ResistanceBypass
{
    std::set<DamageType> types;  /**< Damage types whose resistance is ignored. */
    std::optional<int> amount;   /**< How much is ignored; empty = the actor's level. */
};

/**
 * @brief   An action that carries a spell: the spell resolves with the action's roll (Spellstrike).
 */
struct SpellCarry
{
    bool attackSpells = false;                        /**< Takes spells with a spell attack roll (same roll). */
    bool saveSpells = false;                          /**< Takes spells with a save (the target saves on its own). */
    std::set<int> costs;                              /**< Casting times it takes, in actions. */
    std::optional<ResistanceBypass> ignoreResistance; /**< Resistance the spell's damage ignores. */
};

/**
 * @brief   An action an outcome lets the ally take right away (Strike Hard!).
 */
struct GrantDefinition
{
    std::string actionName;   /**< The action, looked up by name and built with UseOptions::allyWeapon. */
    bool reaction = false;    /**< The ally uses their reaction for it. */
    std::string displayName;  /**< Name shown in reports, with {weapon}; empty = the action's own. */
};

/**
 * @brief   One action, spell or activity, at one variant and one "Heightened (Nth)" bracket.
 */
struct ActionDefinition
{
    std::string name;  /**< Name to look it up by. */
    /**
     * @brief   Name shown in reports; `{ally}`, `{weapon}` and `{spell}` are replaced by the names it's used with.
     * @details Empty: the name, followed by " -> <ally>" when the action is used on an ally.
     */
    std::string displayName;
    int cost = 1;                               /**< Actions spent (0 = free). */
    bool reaction = false;                      /**< Uses the reaction instead of actions. */
    std::set<std::string> traits;               /**< Traits ("attack" gives the multiple attack penalty). */
    int rank = 0;                               /**< Base spell rank; 0 for things that aren't spells. */
    bool cantrip = false;                       /**< Heightens to half the caster's level, rounded up. */
    CheckDefinition check;                      /**< What is rolled. */
    int attacks = 1;                            /**< Attacks it counts as for the multiple attack penalty. */
    std::string spentFlag;                      /**< Counter that marks it as used until cleared ("spends"). */
    std::vector<DamageComponent> damage;        /**< Base damage, before heightening. */
    bool weaponDamage = false;                  /**< Deals the weapon's damage instead (the "strike" preset). */
    std::array<Outcome, kDegrees> outcomes;     /**< What each degree does, with hooks already resolved. */
    /**
     * @brief   Per degree, the indexes in Outcome::modifiers of the modifiers whose "appliesTo" is "chosen".
     * @details They apply to the ally's attack rolls when the action is used with a weapon, and to the
     *          ally's skill checks when it's used with a skill.
     */
    std::array<std::vector<std::size_t>, kDegrees> chosenModifiers;
    std::array<std::vector<GrantDefinition>, kDegrees> grants;  /**< Actions each degree lets the ally take. */
    std::optional<SpellCarry> carriesSpell;     /**< Set when it carries a spell (UseOptions::spell). */
    Heightening heightening;                    /**< What each heightening step adds. */
    std::optional<TemporaryImmunity> immunity;  /**< Set when the target becomes temporarily immune. */
};

/**
 * @brief   Every version of one action: by variant, then by the lowest rank each version applies from.
 */
struct ActionEntry
{
    std::string name;            /**< Name to look it up by. */
    std::string sourceName;      /**< File it was read from, for error messages. */
    std::string defaultVariant;  /**< Variant used when none is asked for; empty if one must be asked for. */
    /**
     * @brief   Versions keyed by variant name ("" when there are no variants), then by the lowest spell rank
     *          they apply from: the base rank, then each "Heightened (Nth)" rank.
     */
    std::map<std::string, std::map<int, ActionDefinition>> versions;
};

/**
 * @brief   Extra damage a feature adds to a Strike when its conditions hold (Sneak Attack, Precision edge).
 */
struct StrikeDamageRider
{
    std::map<int, int> diceByLevel;                       /**< Number of dice from each character level on. */
    int die = 6;                                          /**< Die size. */
    DamageCategory category = DamageCategory::Precision;  /**< How the extra damage is dealt. */
    bool enemyOffGuard = false;                           /**< Only if the enemy is off-guard to the attacker. */
    std::vector<std::string> weaponTraits;                /**< Only with a weapon that has one of these. */
    std::string actorCounter;                             /**< Only if the attacker has this counter. */
    std::string oncePerRound;                             /**< Counter that limits it to once per round. */
};

/**
 * @brief   A passive feature (class feature or feat) defined in data.
 */
struct FeatureDefinition
{
    std::string name;                /**< Name to look it up by. */
    std::string sourceName;          /**< File it was read from, for error messages. */
    StrikeDamageRider strikeDamage;  /**< What it adds to Strikes. */
};

/**
 * @brief   Everything defined in one data file.
 */
struct DefinitionSet
{
    std::vector<ActionEntry> actions;         /**< Actions, spells and activities. */
    std::vector<FeatureDefinition> features;  /**< Passive features. */
};
