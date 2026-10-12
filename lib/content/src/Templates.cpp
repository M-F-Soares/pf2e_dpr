/**
 * @file    Templates.cpp
 * @brief   Builds engine Actions and Features from content definitions.
 */

#include "Templates.h"

#include <algorithm>
#include <utility>

namespace
{
/**
 * @brief   Whether any effect of an action lands on its ally target.
 * @param   [in] definition - The action.
 * @return  True if a condition, modifier, counter, heal or temporary immunity is on Who::Ally, or it lets the
 *          ally take an action.
 */
bool TargetsAlly(const ActionDefinition& definition)
{
    if (definition.immunity && definition.immunity->on == Who::Ally)
        return true;
    for (const Outcome& outcome : definition.outcomes)
    {
        for (const ApplyCondition& condition : outcome.conditions)
            if (condition.to == Who::Ally)
                return true;
        for (const ApplyModifier& modifier : outcome.modifiers)
            if (modifier.to == Who::Ally)
                return true;
        for (const SetCounter& counter : outcome.counters)
            if (counter.on == Who::Ally)
                return true;
        if (outcome.heal.Any() && outcome.heal.to == Who::Ally)
            return true;
    }
    return std::any_of(definition.grants.begin(), definition.grants.end(),
                       [](const std::vector<GrantDefinition>& grants) { return !grants.empty(); });
}

/**
 * @brief   Replaces every occurrence of a placeholder.
 * @param   [in] text - The text.
 * @param   [in] placeholder - The placeholder, like "{ally}".
 * @param   [in] value - What to put in its place.
 * @return  The text with the placeholder filled in.
 */
std::string Fill(std::string text, const std::string& placeholder, const std::string& value)
{
    for (std::size_t at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder, at))
    {
        text.replace(at, placeholder.size(), value);
        at += value.size();
    }
    return text;
}

/**
 * @brief   The name an action shows in reports.
 * @param   [in] definition - The action.
 * @param   [in] options - The ally, weapon and spell it's used with.
 * @return  The display name with {ally}, {weapon} and {spell} filled in; by default the name, plus
 *          " -> <ally>" with an ally.
 */
std::string DisplayName(const ActionDefinition& definition, const UseOptions& options)
{
    if (definition.displayName.empty())
        return options.ally ? definition.name + " -> " + options.ally->name : definition.name;
    std::string text = Fill(definition.displayName, "{weapon}", options.weapon);
    text = Fill(text, "{spell}", options.spell ? options.spell->name : "");
    return Fill(text, "{ally}", options.ally ? options.ally->name : "");
}

/**
 * @brief   Whether a display name has a placeholder.
 * @param   [in] definition - The action.
 * @param   [in] placeholder - The placeholder, like "{weapon}".
 * @return  True if its display name contains @p placeholder.
 */
bool Mentions(const ActionDefinition& definition, const char* placeholder)
{
    return definition.displayName.find(placeholder) != std::string::npos;
}

/**
 * @brief   Whether a weapon has a trait, including the ones the Weapon struct stores as flags.
 * @param   [in] weapon - The weapon.
 * @param   [in] trait - The trait ("agile", "finesse", "ranged", "melee", or any name in Weapon::traits).
 * @return  True if the weapon has it.
 */
bool HasTrait(const Weapon& weapon, const std::string& trait)
{
    if (trait == "agile")
        return weapon.agile;
    if (trait == "finesse")
        return weapon.finesse;
    if (trait == "ranged")
        return weapon.ranged;
    if (trait == "melee")
        return !weapon.ranged;
    return weapon.traits.count(trait) > 0;
}

/**
 * @brief   The base damage plus the "Heightened (+N)" steps.
 * @details A step's dice join the damage of the same type, category and die (a flat step joins any die);
 *          otherwise they're added as a new damage component.
 * @param   [in] definition - The action.
 * @param   [in] steps - How many steps apply.
 * @return  The damage.
 */
std::vector<DamageComponent> HeightenedDamage(const ActionDefinition& definition, int steps)
{
    std::vector<DamageComponent> damage = definition.damage;
    if (steps <= 0)
        return damage;
    for (const DamageComponent& step : definition.heightening.damage)
    {
        auto same = std::find_if(damage.begin(), damage.end(), [&](const DamageComponent& component) {
            return component.type == step.type && component.category == step.category &&
                   (step.die == 0 || component.die == step.die);
        });
        if (same != damage.end())
        {
            same->count += steps * step.count;
            same->flat += steps * step.flat;
        }
        else
            damage.push_back(
                DamageComponent{steps * step.count, step.die, steps * step.flat, step.type, step.category});
    }
    return damage;
}

/**
 * @brief   The tags of attack rolls with a weapon.
 * @param   [in] weapon - The weapon.
 * @return  AttackRoll, Check, Melee or Ranged, and the attack's attribute.
 */
Tags WeaponAttackTags(const Weapon& weapon)
{
    return Tag::AttackRoll | Tag::Check | (weapon.ranged ? Tag::Ranged : Tag::Melee) |
           AttributeTag(weapon.attackAttribute);
}

/**
 * @brief   Fills in a check with the actor's numbers.
 * @param   [in] definition - The check.
 * @param   [in] actor - The character rolling it, or whose DC it is.
 * @param   [in] weapon - The weapon it's used with, or null.
 * @param   [in] options - The skill and range increment it's used with.
 * @param   [in, out] check - Gets the kind, modifier or DC, tags and defense.
 * @throws  std::runtime_error if @p actor has no modifier for the skill.
 */
void ResolveCheck(const CheckDefinition& definition, const Character& actor, const Weapon* weapon,
                  const UseOptions& options, CheckSpec& check)
{
    check.kind = definition.kind;
    check.defense = definition.defense;
    Tags keyAttribute = AttributeTag(actor.keyAttribute);
    switch (definition.stat)
    {
    case StatSource::None:
        break;
    case StatSource::Skill:
        check.modifier = actor.SkillModifier(definition.skill);
        check.tags = Tag::Skill | Tag::Check | AttributeTag(SkillAttribute(definition.skill));
        break;
    case StatSource::Weapon:
        check.modifier = weapon->attack;
        check.itemBonus = weapon->potency;
        check.tags = WeaponAttackTags(*weapon);
        check.agile = weapon->agile;
        check.untypedPenalty = -2 * (options.rangeIncrement - 1);
        break;
    case StatSource::Chosen:
        if (weapon)
        {
            check.modifier = weapon->attack;
            check.itemBonus = weapon->potency;
            check.tags = WeaponAttackTags(*weapon);
        }
        else
        {
            check.modifier = actor.SkillModifier(*options.skill);
            check.tags = Tag::Skill | Tag::Check | AttributeTag(SkillAttribute(*options.skill));
        }
        break;
    case StatSource::Spell:
        if (definition.kind == CheckKind::Attack)
        {
            check.modifier = actor.spellAttack;
            check.tags = Tag::AttackRoll | Tag::Check | Tag::Spell | Tag::Ranged | keyAttribute;
        }
        else
        {
            check.modifier = actor.spellDC;
            check.tags = Tag::DC | Tag::Spell | keyAttribute;
        }
        break;
    case StatSource::ClassDC:
        check.modifier = actor.classDC;
        check.tags = Tag::DC | Tag::ClassDC | keyAttribute;
        break;
    }
    if (definition.kind == CheckKind::FixedDC)
    {
        check.fixedDC = definition.dc;
        check.targeted = false;
    }
}

/**
 * @brief   Gives a Strike its weapon's damage.
 * @details A hit deals the weapon's damage and property runes. A critical hit doubles that (with fatal's larger
 *          dice), then adds the fatal and deadly extra dice and crit-only riders, which aren't doubled.
 * @param   [in] weapon - The weapon.
 * @param   [in, out] action - Gets the damage, critical damage, damage tags and weapon.
 */
void WeaponDamage(const Weapon& weapon, Action& action)
{
    action.damage = {Damage(weapon.dice, weapon.die, weapon.flat, weapon.type)};
    action.damage.insert(action.damage.end(), weapon.onHit.begin(), weapon.onHit.end());

    // Fatal: weapon dice become the fatal size on a crit, plus one extra (undoubled) die.
    action.critDamage = action.damage;
    if (weapon.fatal)
    {
        action.critDamage.front().die = weapon.fatal;
        action.critDamage.push_back(DamageComponent{1, weapon.fatal, 0, weapon.type, DamageCategory::Normal, false});
    }
    // Deadly: 1 extra die, 2 with greater striking, 3 with major striking (not doubled).
    if (weapon.deadly)
        action.critDamage.push_back(DamageComponent{std::max(1, weapon.dice - 1), weapon.deadly, 0, weapon.type,
                                                    DamageCategory::Normal, false});
    for (DamageComponent rider : weapon.onCrit)
    {
        rider.doublesOnCrit = false;
        action.critDamage.push_back(rider);
    }

    action.damageTags = Tag::Damage | (weapon.ranged ? Tag::Ranged : Tag::Melee) |
                        (weapon.strengthToDamage ? Tags{Tag::Str} : Tags{0});
    action.weapon = weapon;
}

/**
 * @brief   Couples a carried spell to an action: it resolves with the action's roll, and its damage joins the
 *          action's as one instance.
 * @param   [in] carry - What spells the action takes.
 * @param   [in] spell - The spell.
 * @param   [in] actor - The character; their level is the resistance ignored, unless a number is given.
 * @param   [in] actionName - The action's name, for messages.
 * @param   [in, out] action - Gets the coupled spell.
 * @throws  ContentError if the action can't carry @p spell.
 */
void CarrySpell(const SpellCarry& carry, const Action& spell, const Character& actor, const std::string& actionName,
                Action& action)
{
    // A spell's check is tagged Spell; Demoralize also rolls against a defense, but isn't a spell.
    bool isSpell = (spell.check.tags & Tag::Spell) != 0;
    bool attackSpell = isSpell && spell.check.kind == CheckKind::Attack;
    bool saveSpell = isSpell && spell.check.kind == CheckKind::Save;
    std::string spellName = "\"" + spell.name + "\"";
    if (!(attackSpell && carry.attackSpells) && !(saveSpell && carry.saveSpells))
    {
        std::string kinds = carry.attackSpells && carry.saveSpells ? "a spell attack roll or a saving throw"
                            : carry.attackSpells                 ? "a spell attack roll"
                                                                 : "a saving throw";
        throw ContentError(actionName + ": can't carry " + spellName + ": it needs a spell with " + kinds);
    }
    if (carry.costs.count(spell.cost) == 0 || spell.reaction)
    {
        std::string costs;
        for (int cost : carry.costs)
            costs += (costs.empty() ? "" : " or ") + std::to_string(cost);
        throw ContentError(actionName + ": can't carry " + spellName + ": it needs a spell that takes " + costs +
                           (carry.costs.size() == 1 && *carry.costs.begin() == 1 ? " action" : " actions") +
                           " to cast");
    }

    // The spell goes where the action goes, so it has no concealment flat check of its own.
    auto coupled = std::make_shared<Action>(spell);
    coupled->check.targeted = false;
    if (carry.ignoreResistance)
    {
        // Only the spell's damage ignores resistance; immunities are unaffected.
        const ResistanceBypass& bypass = *carry.ignoreResistance;
        for (auto* list : {&coupled->damage, &coupled->critDamage})
            for (DamageComponent& component : *list)
                if (bypass.types.count(component.type) > 0)
                    component.ignoredResistance = bypass.amount.value_or(actor.level);
    }
    action.coupled = CoupledEffect{coupled, attackSpell ? Coupling::SameRoll : Coupling::OwnRoll};
}

/**
 * @brief   Checks that the options give what the action needs, and nothing it doesn't use.
 * @param   [in] definition - The action.
 * @param   [in] options - The options it's used with.
 * @throws  ContentError on the first option that's missing or not used.
 */
void CheckOptions(const ActionDefinition& definition, const UseOptions& options)
{
    const std::string& name = definition.name;
    StatSource stat = definition.check.stat;
    bool usesWeapon = stat == StatSource::Weapon || stat == StatSource::Chosen || definition.weaponDamage ||
                      Mentions(definition, "{weapon}");
    bool usesSpell = definition.carriesSpell || Mentions(definition, "{spell}");
    bool grants = std::any_of(definition.grants.begin(), definition.grants.end(),
                              [](const std::vector<GrantDefinition>& list) { return !list.empty(); });

    if (stat == StatSource::Chosen)
    {
        if (options.weapon.empty() == !options.skill.has_value())
            throw ContentError(name + ": needs a weapon or a skill (UseOptions::weapon or skill), not " +
                               (options.weapon.empty() ? "neither" : "both"));
    }
    else if (usesWeapon && options.weapon.empty())
        throw ContentError(name + ": needs a weapon (UseOptions::weapon)");
    if (usesSpell && !options.spell)
        throw ContentError(name + ": needs a spell (UseOptions::spell)");
    if ((TargetsAlly(definition) || Mentions(definition, "{ally}")) && !options.ally)
        throw ContentError(name + ": needs an ally to target (UseOptions::ally)");

    auto unused = [&](const char* option) {
        throw ContentError(name + ": doesn't use UseOptions::" + std::string(option) + ", but it's set");
    };
    if (!usesWeapon && !options.weapon.empty())
        unused("weapon");
    if (stat != StatSource::Chosen && options.skill)
        unused("skill");
    if (!usesSpell && options.spell)
        unused("spell");
    if (!grants && !options.allyWeapon.empty())
        unused("allyWeapon");
    if (options.rangeIncrement != 1 && stat != StatSource::Weapon)
        unused("rangeIncrement");
    if (options.rangeIncrement < 1)
        throw ContentError(name + ": UseOptions::rangeIncrement must be at least 1");
}
} // namespace

int ResolveRank(const ActionDefinition& definition, const Character& caster, int requestedRank)
{
    if (definition.rank == 0)
    {
        if (requestedRank != 0)
            throw ContentError(definition.name + ": isn't a spell, so it has no rank");
        return 0;
    }
    int rank = requestedRank;
    if (rank == 0)
        rank = definition.cantrip ? std::max(1, (caster.level + 1) / 2) : definition.rank;
    if (rank < definition.rank || rank > 10)
        throw ContentError(definition.name + ": can't be cast at rank " + std::to_string(rank) + " (its rank is " +
                           std::to_string(definition.rank) + ", and the highest is 10)");
    return rank;
}

ActionPtr BuildAction(const ActionDefinition& definition, const Character& actor, const UseOptions& options,
                      int rank, const ActionResolver& resolve)
{
    CheckOptions(definition, options);
    const Weapon* weapon = options.weapon.empty() ? nullptr : &actor.GetWeapon(options.weapon);

    auto action = std::make_shared<Action>();
    action->name = DisplayName(definition, options);
    action->cost = definition.cost;
    action->reaction = definition.reaction;
    action->traits = definition.traits;
    ResolveCheck(definition.check, actor, weapon, options, action->check);
    action->check.attackTrait = definition.traits.count("attack") > 0;
    action->check.attacks = definition.attacks;
    if (definition.traits.count("incapacitation") > 0)
        action->check.incapacitationRank = rank > 0 ? rank : (actor.level + 1) / 2;
    action->spentFlag = definition.spentFlag;

    const Heightening& heightening = definition.heightening;
    int steps = heightening.every > 0 ? (rank - definition.rank) / heightening.every : 0;
    if (definition.weaponDamage)
        WeaponDamage(*weapon, *action);
    else
    {
        action->damage = HeightenedDamage(definition, steps);
        // Bonuses to damage rolls apply to every damage roll (Courageous Anthem on Trip); spells' also get spell
        // bonuses.
        bool spell = definition.rank > 0 || definition.check.stat == StatSource::Spell;
        action->damageTags = Tag::Damage | (spell ? Tags{Tag::Spell} : Tags{0});
    }
    action->outcomes = definition.outcomes;
    if (steps > 0 && heightening.heal.Any())
        for (Outcome& outcome : action->outcomes)
            if (outcome.heal.Any())
            {
                outcome.heal.count += steps * heightening.heal.count;
                outcome.heal.flat += steps * heightening.heal.flat;
            }

    if (definition.immunity)
    {
        const TemporaryImmunity& immunity = *definition.immunity;
        std::string counterName = "immune:" + definition.name + (immunity.perActor ? ":" + actor.name : "");
        for (Outcome& outcome : action->outcomes)
            outcome.counters.push_back(SetCounter{immunity.on, counterName});
        bool onEnemy = immunity.on == Who::Enemy;
        action->usable = [counterName, onEnemy](const BattleState& state, int, int allyIndex) {
            return state.Counter(onEnemy ? kEnemyIndex : allyIndex, counterName) == 0;
        };
    }
    // Modifiers that apply to the same kind of roll the actor made (Aid).
    Selector chosen = weapon ? Select({Tag::AttackRoll}) : Select({Tag::Skill | Tag::Check});
    for (int degreeIndex = 0; degreeIndex < kDegrees; ++degreeIndex)
    {
        for (std::size_t modifierIndex : definition.chosenModifiers[degreeIndex])
            action->outcomes[degreeIndex].modifiers[modifierIndex].mod.applies = chosen;
        for (const GrantDefinition& grant : definition.grants[degreeIndex])
        {
            if (!resolve)
                throw ContentError(definition.name + ": lets the ally take an action, so it needs a resolver "
                                                     "(use ContentLibrary::Use)");
            UseOptions allyOptions;
            allyOptions.weapon = options.allyWeapon;
            Action granted;
            try
            {
                granted = *resolve(grant.actionName, *options.ally, allyOptions);
            }
            catch (const ContentError& error)
            {
                throw ContentError(definition.name + ": the ally's \"" + grant.actionName +
                                   "\" (its weapon is UseOptions::allyWeapon): " + error.what());
            }
            if (grant.reaction)
                granted.reaction = true;
            if (!grant.displayName.empty())
                granted.name = Fill(grant.displayName, "{weapon}", options.allyWeapon);
            action->outcomes[degreeIndex].grants.push_back(
                Grant{options.ally->name, std::make_shared<Action>(std::move(granted))});
        }
    }

    if (definition.carriesSpell)
        CarrySpell(*definition.carriesSpell, *options.spell, actor, definition.name, *action);
    if (options.ally)
        action->allyName = options.ally->name;
    return action;
}

Feature BuildFeature(const FeatureDefinition& definition, const Character& owner)
{
    const StrikeDamageRider& rider = definition.strikeDamage;
    int dice = 0;
    for (const auto& [level, count] : rider.diceByLevel)
        if (owner.level >= level)
            dice = count;
    return Feature{definition.name, [rider, dice](EffectCtx& ctx) {
                       const Weapon& weapon = *ctx.action.weapon;
                       if (dice <= 0)
                           return;
                       if (!rider.weaponTraits.empty() &&
                           std::none_of(rider.weaponTraits.begin(), rider.weaponTraits.end(),
                                        [&](const std::string& trait) { return HasTrait(weapon, trait); }))
                           return;
                       if (rider.enemyOffGuard && !ctx.EnemyOffGuard())
                           return;
                       if (!rider.actorCounter.empty() && ctx.before.Counter(ctx.actorIndex, rider.actorCounter) == 0)
                           return;
                       if (!rider.oncePerRound.empty() && ctx.before.Counter(ctx.actorIndex, rider.oncePerRound) > 0)
                           return;
                       ctx.damage->push_back(DamageComponent{dice, rider.die, 0, weapon.type, rider.category});
                       if (!rider.oncePerRound.empty())
                           ctx.state[ctx.actorIndex].counters[rider.oncePerRound] = 1;
                   }};
}
