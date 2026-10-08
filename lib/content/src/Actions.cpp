/**
 * @file    Actions.cpp
 * @brief   Factories for the ready-made actions, spells and class features.
 */

#include "Actions.h"

#include <algorithm>
#include <stdexcept>

namespace
{
constexpr int kCritFail = static_cast<int>(Degree::CritFail);        /**< Action::outcomes index: critical failure. */
constexpr int kFail = static_cast<int>(Degree::Fail);                /**< Action::outcomes index: failure. */
constexpr int kSuccess = static_cast<int>(Degree::Success);          /**< Action::outcomes index: success. */
constexpr int kCritSuccess = static_cast<int>(Degree::CritSuccess);  /**< Action::outcomes index: critical success. */

/**
 * @brief   A new, empty action.
 * @param   [in] name - Action name.
 * @param   [in] cost - Actions spent.
 * @return  The action, still mutable.
 */
std::shared_ptr<Action> Make(std::string name, int cost)
{
    auto action = std::make_shared<Action>();
    action->name = std::move(name);
    action->cost = cost;
    return action;
}

/**
 * @brief   Makes an action a skill check against one of the enemy's DCs.
 * @param   [in, out] action - The action.
 * @param   [in] character - The character rolling.
 * @param   [in] skill - The skill.
 * @param   [in] defense - The enemy defense used as the DC.
 * @throws  std::runtime_error if @p character has no modifier for @p skill.
 */
void SkillCheck(Action& action, const Character& character, Skill skill, Defense defense)
{
    action.check.kind = CheckKind::Attack;
    action.check.modifier = character.SkillModifier(skill);
    action.check.tags = Tag::Skill | Tag::Check | AttributeTag(SkillAttribute(skill));
    action.check.defense = defense;
}

/**
 * @brief   The damage types whose resistance Overwhelming Spellstrike ignores.
 * @param   [in] type - The damage type.
 * @return  True for acid, cold, electricity, fire and sonic.
 */
bool OverwhelmingAffects(DamageType type)
{
    return type == DamageType::Acid || type == DamageType::Cold || type == DamageType::Electricity ||
           type == DamageType::Fire || type == DamageType::Sonic;
}

/**
 * @brief   Tags of a character's spell DC.
 * @param   [in] caster - The caster.
 * @return  DC, Spell and the caster's key attribute.
 */
Tags SpellDCTags(const Character& caster)
{
    return Tag::DC | Tag::Spell | AttributeTag(caster.keyAttribute);
}

/**
 * @brief   A status bonus for the round.
 * @param   [in] to - Who gets it.
 * @param   [in] source - Name of the effect.
 * @param   [in] value - The bonus.
 * @param   [in] selector - The rolls it applies to.
 * @return  The modifier to apply.
 */
ApplyModifier AllyBonus(Who to, const std::string& source, int value, Selector selector)
{
    return ApplyModifier{to, Modifier{source, BonusType::Status, value, std::move(selector)}, ForTheRound()};
}
} // namespace

ActionPtr Strike(const Character& character, const std::string& weaponName, StrikeOptions opts)
{
    const Weapon& weapon = character.GetWeapon(weaponName);
    auto action = Make(opts.name.empty() ? "Strike (" + weapon.name + ")" : opts.name, opts.cost);
    action->reaction = opts.reaction;

    Tags range = weapon.ranged ? Tag::Ranged : Tag::Melee;
    CheckSpec& check = action->check;
    check.kind = CheckKind::Attack;
    check.modifier = weapon.attack;
    check.itemBonus = weapon.potency;
    check.tags = Tag::AttackRoll | Tag::Check | range | AttributeTag(weapon.attackAttribute);
    check.defense = Defense::AC;
    check.attackTrait = true;
    check.agile = weapon.agile;
    check.untypedPenalty = -2 * (opts.rangeIncrement - 1);
    check.fortune = opts.fortune;

    action->damage = {Damage(weapon.dice, weapon.die, weapon.flat, weapon.type)};
    action->damage.insert(action->damage.end(), weapon.onHit.begin(), weapon.onHit.end());
    action->damage.insert(action->damage.end(), opts.extraOnHit.begin(), opts.extraOnHit.end());

    // Fatal: weapon dice become the fatal size on a crit, plus one extra (undoubled) die.
    action->critDamage = action->damage;
    if (weapon.fatal)
    {
        action->critDamage.front().die = weapon.fatal;
        action->critDamage.push_back(DamageComponent{1, weapon.fatal, 0, weapon.type, DamageCategory::Normal, false});
    }
    // Deadly: 1 extra die, 2 with greater striking, 3 with major striking (not doubled).
    if (weapon.deadly)
        action->critDamage.push_back(DamageComponent{std::max(1, weapon.dice - 1), weapon.deadly, 0, weapon.type,
                                                     DamageCategory::Normal, false});
    for (DamageComponent rider : weapon.onCrit)
    {
        rider.doublesOnCrit = false;
        action->critDamage.push_back(rider);
    }

    action->damageTags = Tag::Damage | range | (weapon.strengthToDamage ? Tags{Tag::Str} : Tags{0});
    action->weapon = weapon;
    action->traits = {"attack"};
    action->outcomes[kSuccess].damage = DamageScale::Normal;
    action->outcomes[kCritSuccess].damage = DamageScale::Double;
    return action;
}

ActionPtr Demoralize(const Character& character)
{
    auto action = Make("Demoralize", 1);
    SkillCheck(*action, character, Skill::Intimidation, Defense::Will);
    action->traits = {"auditory", "concentrate", "emotion", "fear", "mental"};
    action->outcomes[kSuccess].conditions = {{ConditionKind::Frightened, 1}};
    action->outcomes[kCritSuccess].conditions = {{ConditionKind::Frightened, 2}};
    // The target is then temporarily immune to this character's Demoralize.
    std::string immunity = "demoralize-immune:" + character.name;
    for (auto& outcome : action->outcomes)
        outcome.counters = {{Who::Enemy, immunity}};
    action->usable = [immunity](const BattleState& state, int, int) {
        return state.Counter(kEnemyIndex, immunity) == 0;
    };
    return action;
}

ActionPtr Feint(const Character& character)
{
    auto action = Make("Feint", 1);
    SkillCheck(*action, character, Skill::Deception, Defense::Perception);
    action->traits = {"mental"};
    Modifier offGuard{"feint", BonusType::Circumstance, -2, Select({Tag::AC | Tag::Melee}), -1, true};
    // Success: off-guard against your next melee attack this turn.
    action->outcomes[kSuccess].modifiers = {ApplyModifier{Who::Enemy, offGuard, UntilEndOfActorTurn(), 1, true}};
    // Critical success: off-guard against your melee attacks until the end of your next turn.
    action->outcomes[kCritSuccess].modifiers = {ApplyModifier{Who::Enemy, offGuard, ForTheRound(), -1, true}};
    return action;
}

ActionPtr Trip(const Character& character)
{
    auto action = Make("Trip", 1);
    SkillCheck(*action, character, Skill::Athletics, Defense::Ref);
    action->check.attackTrait = true;
    action->traits = {"attack"};
    action->damage = {Damage(1, 6, 0, DamageType::Bludgeoning)};
    action->outcomes[kSuccess].conditions = {{ConditionKind::Prone, 1}};
    action->outcomes[kCritSuccess].conditions = {{ConditionKind::Prone, 1}};
    action->outcomes[kCritSuccess].damage = DamageScale::Normal;
    action->outcomes[kCritFail].conditions = {{ConditionKind::Prone, 1, ForTheRound(), Who::Actor}};
    return action;
}

ActionPtr Grapple(const Character& character)
{
    auto action = Make("Grapple", 1);
    SkillCheck(*action, character, Skill::Athletics, Defense::Fort);
    action->check.attackTrait = true;
    action->traits = {"attack"};
    action->outcomes[kSuccess].conditions = {{ConditionKind::Grabbed, 1}};
    action->outcomes[kCritSuccess].conditions = {{ConditionKind::Restrained, 1}};
    return action;
}

ActionPtr Aid(const Character&, const Character& ally, int modifier, int critBonus, Selector aided)
{
    // Simplified: the preparation action and the reaction happen on the aider's turn,
    // and the bonus applies to the ally's next matching check.
    auto action = Make("Aid " + ally.name, 1);
    action->allyName = ally.name;
    action->check.kind = CheckKind::FixedDC;
    action->check.modifier = modifier;
    action->check.tags = Tag::Check;
    action->check.fixedDC = 15;
    action->check.targeted = false;
    auto bonus = [&](int value) {
        return std::vector<ApplyModifier>{
            ApplyModifier{Who::Ally, Modifier{"aid", BonusType::Circumstance, value, aided}, ForTheRound(), 1}};
    };
    action->outcomes[kCritSuccess].modifiers = bonus(critBonus);
    action->outcomes[kSuccess].modifiers = bonus(1);
    action->outcomes[kCritFail].modifiers = bonus(-1);
    for (auto& outcome : action->outcomes)
        outcome.custom = [](EffectCtx& ctx) { ctx.state[ctx.actorIndex].reactionUsed = true; };
    return action;
}

ActionPtr BattleMedicine(const Character& medic, const Character& patient, int dc, int bonusHealing)
{
    auto action = Make("Battle Medicine -> " + patient.name, 1);
    action->allyName = patient.name;
    action->check.kind = CheckKind::FixedDC;
    action->check.modifier = medic.SkillModifier(Skill::Medicine);
    action->check.tags = Tag::Skill | Tag::Check | Tag::Wis;
    action->check.fixedDC = dc;
    action->check.targeted = false;
    action->outcomes[kSuccess].heal = HealSpec{2, 8, bonusHealing};
    action->outcomes[kCritSuccess].heal = HealSpec{4, 8, bonusHealing};
    for (auto& outcome : action->outcomes)
        outcome.counters = {{Who::Ally, "immune:battle-medicine"}};
    action->usable = [](const BattleState& state, int, int allyIndex) {
        return state.Counter(allyIndex, "immune:battle-medicine") == 0;
    };
    return action;
}

ActionPtr HuntPrey(const Character&)
{
    auto action = Make("Hunt Prey", 1);
    action->outcomes[kSuccess].counters = {{Who::Actor, "prey"}};
    return action;
}

ActionPtr StrikeHard(const Character&, const Character& ally, const std::string& allyWeaponName)
{
    auto action = Make("Strike Hard! -> " + ally.name, 2);
    action->allyName = ally.name;
    StrikeOptions opts;
    opts.name = "Strike (" + allyWeaponName + ", Strike Hard!)";
    opts.reaction = true;
    action->outcomes[kSuccess].grants = {Grant{ally.name, Strike(ally, allyWeaponName, opts)}};
    return action;
}

ActionPtr Spellstrike(const Character& magus, const std::string& weaponName, const ActionPtr& spell)
{
    bool attackSpell = spell && spell->check.kind == CheckKind::Attack;
    bool saveSpell = spell && spell->check.kind == CheckKind::Save;
    if (!attackSpell && !saveSpell)
        throw std::invalid_argument("Spellstrike needs a spell with a spell attack roll or a saving throw");
    if (spell->cost < 1 || spell->cost > 2)
        throw std::invalid_argument("Spellstrike needs a spell that takes 1 or 2 actions to cast");

    StrikeOptions opts;
    opts.name = "Spellstrike (" + spell->name + ")";
    opts.cost = 2;
    auto action = std::make_shared<Action>(*Strike(magus, weaponName, opts));
    action->check.attacks = 2;
    action->spentFlag = "spellstrike-spent";

    // The spell goes where the Strike goes, so it has no concealment flat check of its own.
    auto coupled = std::make_shared<Action>(*spell);
    coupled->check.targeted = false;
    action->coupled = CoupledEffect{coupled, attackSpell ? Coupling::SameRoll : Coupling::OwnRoll};
    return action;
}

ActionPtr OverwhelmingSpellstrike(const Character& magus, const std::string& weaponName, const ActionPtr& spell)
{
    if (!spell)
        throw std::invalid_argument("Spellstrike needs a spell with a spell attack roll or a saving throw");
    // Only the spell's damage ignores resistance; immunities are unaffected.
    auto piercing = std::make_shared<Action>(*spell);
    for (auto* list : {&piercing->damage, &piercing->critDamage})
        for (DamageComponent& component : *list)
            if (OverwhelmingAffects(component.type))
                component.ignoredResistance = magus.level;

    auto action = std::make_shared<Action>(*Spellstrike(magus, weaponName, piercing));
    action->name = "Overwhelming Spellstrike (" + spell->name + ")";
    return action;
}

ActionPtr RechargeSpellstrike(const Character&)
{
    auto action = Make("Recharge Spellstrike", 1);
    action->outcomes[kSuccess].counters = {{Who::Actor, "spellstrike-spent", 0}};
    return action;
}

ActionPtr SureStrike(const Character&)
{
    auto action = Make("Sure Strike", 1);
    action->outcomes[kSuccess].counters = {{Who::Actor, "fortune:attack"}};
    return action;
}

ActionPtr Heroism(const Character&, const Character& ally, int rank)
{
    auto action = Make("Heroism -> " + ally.name, 2);
    action->allyName = ally.name;
    int value = rank >= 9 ? 3 : rank >= 6 ? 2 : 1;
    action->outcomes[kSuccess].modifiers = {AllyBonus(Who::Ally, "heroism", value,
                                                      Select({Tag::AttackRoll, Tag::Save, Tag::Skill | Tag::Check,
                                                              Tag::Perception | Tag::Check}))};
    return action;
}

ActionPtr Bless(const Character&)
{
    auto action = Make("Bless", 2);
    action->outcomes[kSuccess].modifiers = {AllyBonus(Who::AllAllies, "bless", 1, Select({Tag::AttackRoll}))};
    return action;
}

ActionPtr CourageousAnthem(const Character&)
{
    auto action = Make("Courageous Anthem", 1);
    action->outcomes[kSuccess].modifiers = {
        AllyBonus(Who::AllAllies, "courageous-anthem", 1, Select({Tag::AttackRoll, Tag::Damage}))};
    return action;
}

ActionPtr Fear(const Character& caster, int rank)
{
    auto action = Make("Fear", 2);
    action->check.kind = CheckKind::Save;
    action->check.modifier = caster.spellDC;
    action->check.tags = SpellDCTags(caster);
    action->check.defense = Defense::Will;
    action->traits = {"concentrate", "emotion", "fear", "manipulate", "mental"};
    (void)rank;  // heightening only adds targets
    action->outcomes[kSuccess].conditions = {{ConditionKind::Frightened, 1}};
    action->outcomes[kFail].conditions = {{ConditionKind::Frightened, 2}};
    action->outcomes[kCritFail].conditions = {{ConditionKind::Frightened, 3}};
    return action;
}

ActionPtr Heal(const Character&, const Character& ally, int rank, int actions)
{
    auto action = Make("Heal (" + std::to_string(actions) + "a) -> " +
                           (actions == 3 ? std::string("party") : ally.name),
                       actions);
    action->allyName = ally.name;
    HealSpec healing{rank, 8, actions == 2 ? 8 * rank : 0, actions == 3 ? Who::AllAllies : Who::Ally};
    action->outcomes[kSuccess].heal = healing;
    return action;
}

ActionPtr BasicSaveSpell(const Character& caster, const std::string& name, int cost, Defense save,
                         std::vector<DamageComponent> damage, std::set<std::string> traits, int incapacitationRank)
{
    auto action = Make(name, cost);
    action->check.kind = CheckKind::Save;
    action->check.modifier = caster.spellDC;
    action->check.tags = SpellDCTags(caster);
    action->check.defense = save;
    action->check.incapacitationRank = incapacitationRank;
    action->damage = std::move(damage);
    action->damageTags = Tag::Damage | Tag::Spell;
    action->traits = std::move(traits);
    // Indexed by the ENEMY's degree of success.
    action->outcomes[kCritSuccess].damage = DamageScale::None;
    action->outcomes[kSuccess].damage = DamageScale::Half;
    action->outcomes[kFail].damage = DamageScale::Normal;
    action->outcomes[kCritFail].damage = DamageScale::Double;
    return action;
}

ActionPtr SpellAttackSpell(const Character& caster, const std::string& name, int cost,
                           std::vector<DamageComponent> damage, std::set<std::string> traits)
{
    auto action = Make(name, cost);
    action->check.kind = CheckKind::Attack;
    action->check.modifier = caster.spellAttack;
    action->check.tags = Tag::AttackRoll | Tag::Check | Tag::Spell | Tag::Ranged | AttributeTag(caster.keyAttribute);
    action->check.attackTrait = true;
    action->damage = std::move(damage);
    action->damageTags = Tag::Damage | Tag::Spell;
    action->traits = std::move(traits);
    action->traits.insert("attack");
    action->outcomes[kSuccess].damage = DamageScale::Normal;
    action->outcomes[kCritSuccess].damage = DamageScale::Double;
    return action;
}

Feature SneakAttack(int dice)
{
    return Feature{"Sneak Attack", [dice](EffectCtx& ctx) {
                       const Weapon& weapon = *ctx.action.weapon;
                       if ((weapon.ranged || weapon.agile || weapon.finesse) && ctx.EnemyOffGuard())
                           ctx.damage->push_back(Precision(dice, 6, weapon.type));
                   }};
}

Feature PrecisionEdge(int dice)
{
    return Feature{"Precision", [dice](EffectCtx& ctx) {
                       if (ctx.before.Counter(ctx.actorIndex, "prey") == 0 ||
                           ctx.before.Counter(ctx.actorIndex, "precision-used") > 0)
                           return;
                       ctx.damage->push_back(Precision(dice, 8, ctx.action.weapon->type));
                       ctx.state[ctx.actorIndex].counters["precision-used"] = 1;
                   }};
}
