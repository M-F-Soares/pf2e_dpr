#pragma once

#include "../Action.h"

#include <string>
#include <vector>

// Ready-made actions, spells and class features (Remaster rules).
// Each factory reads the numbers it needs from the Character, so a scenario
// line reads like the table: strike(rogue, "Rapier"), demoralize(fighter)...

struct StrikeOptions
{
    std::string name;                         // defaults to "Strike (<weapon>)"
    int cost = 1;
    bool reaction = false;
    int rangeIncrement = 1;                   // -2 untyped per increment beyond the first
    bool fortune = false;
    std::vector<DamageComponent> extraOnHit;  // e.g. Spellstrike spell damage (doubled on crit)
};

ActionPtr strike(const Character& c, const std::string& weapon, StrikeOptions opts = {});

// Skill actions
ActionPtr demoralize(const Character& c);
ActionPtr feint(const Character& c);
ActionPtr trip(const Character& c);
ActionPtr grapple(const Character& c);
ActionPtr aid(const Character& aider, const Character& ally, int modifier, int critBonus = 2,
              Selector aided = select({Tag::AttackRoll}));
ActionPtr battleMedicine(const Character& medic, const Character& patient, int dc = 15, int bonusHealing = 0);

// Class actions
ActionPtr huntPrey(const Character& ranger);
ActionPtr strikeHard(const Character& commander, const Character& ally, const std::string& allyWeapon);
// Magus: a melee Strike carrying a 1- or 2-action spell (from spellAttackSpell or basicSaveSpell).
// A spell attack spell uses the Strike's result; a save spell's target saves normally unless the
// Strike critically fails. Damage is combined with the Strike's. Counts as two attacks for the
// multiple attack penalty, and can't be used again until recharged.
ActionPtr spellstrike(const Character& magus, const std::string& weapon, const ActionPtr& spell);
// Overwhelming Spellstrike (magus feat 12): a Spellstrike whose spell ignores the target's acid,
// cold, electricity, fire and sonic resistance up to the magus's level (persistent damage included).
ActionPtr overwhelmingSpellstrike(const Character& magus, const std::string& weapon, const ActionPtr& spell);
ActionPtr rechargeSpellstrike(const Character& magus);

// Spells
ActionPtr sureStrike(const Character& caster);
ActionPtr heroism(const Character& caster, const Character& ally, int rank = 3);
ActionPtr bless(const Character& caster);
ActionPtr courageousAnthem(const Character& bard);
ActionPtr fear(const Character& caster, int rank = 1);
ActionPtr heal(const Character& caster, const Character& ally, int rank, int actions);
ActionPtr basicSaveSpell(const Character& caster, const std::string& name, int cost, Defense save,
                         std::vector<DamageComponent> damage, std::set<std::string> traits = {},
                         int incapacitationRank = 0);
ActionPtr spellAttackSpell(const Character& caster, const std::string& name, int cost,
                           std::vector<DamageComponent> damage, std::set<std::string> traits = {});

// Class features (add to Character::features)
Feature sneakAttack(int dice);
Feature precisionEdge(int dice = 1);  // first hit each round vs. hunted prey: +Nd8 precision
