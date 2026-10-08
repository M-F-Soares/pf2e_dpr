/**
 * @file    Actions.cpp
 * @brief   Factories for the ready-made actions, spells and class features.
 */

#include "Actions.h"

#include <algorithm>
#include <stdexcept>

namespace
{
constexpr int CF = static_cast<int>(Degree::CritFail);    /**< Action::outcomes index of a critical failure. */
constexpr int F = static_cast<int>(Degree::Fail);         /**< Action::outcomes index of a failure. */
constexpr int S = static_cast<int>(Degree::Success);      /**< Action::outcomes index of a success. */
constexpr int CS = static_cast<int>(Degree::CritSuccess); /**< Action::outcomes index of a critical success. */

/**
 * @brief   A new, empty action.
 * @param   [in] name - Action name.
 * @param   [in] cost - Actions spent.
 * @return  The action, still mutable.
 */
std::shared_ptr<Action> make(std::string name, int cost)
{
    auto a = std::make_shared<Action>();
    a->name = std::move(name);
    a->cost = cost;
    return a;
}

/**
 * @brief   Makes an action a skill check against one of the enemy's DCs.
 * @param   [in, out] a - The action.
 * @param   [in] c - The character rolling.
 * @param   [in] skill - The skill.
 * @param   [in] vs - The enemy defense used as the DC.
 * @throws  std::runtime_error if @p c has no modifier for @p skill.
 */
void skillCheck(Action& a, const Character& c, Skill skill, Defense vs)
{
    a.check.kind = CheckKind::Attack;
    a.check.modifier = c.skill(skill);
    a.check.tags = Tag::Skill | Tag::Check | attributeTag(skillAttribute(skill));
    a.check.defense = vs;
}

/**
 * @brief   The damage types whose resistance Overwhelming Spellstrike ignores.
 * @param   [in] t - The damage type.
 * @return  True for acid, cold, electricity, fire and sonic.
 */
bool overwhelmingAffects(DamageType t)
{
    return t == DamageType::Acid || t == DamageType::Cold || t == DamageType::Electricity ||
           t == DamageType::Fire || t == DamageType::Sonic;
}

/**
 * @brief   Tags of a character's spell DC.
 * @param   [in] c - The caster.
 * @return  DC, Spell and the caster's key attribute.
 */
Tags spellDCTags(const Character& c)
{
    return Tag::DC | Tag::Spell | attributeTag(c.keyAttribute);
}

/**
 * @brief   A status bonus for the round.
 * @param   [in] to - Who gets it.
 * @param   [in] source - Name of the effect.
 * @param   [in] value - The bonus.
 * @param   [in] s - The rolls it applies to.
 * @return  The modifier to apply.
 */
ApplyModifier allyBonus(Who to, const std::string& source, int value, Selector s)
{
    return ApplyModifier{to, Modifier{source, BonusType::Status, value, std::move(s)}, forTheRound()};
}
} // namespace

ActionPtr strike(const Character& c, const std::string& weaponName, StrikeOptions opts)
{
    const Weapon& w = c.weapon(weaponName);
    auto a = make(opts.name.empty() ? "Strike (" + w.name + ")" : opts.name, opts.cost);
    a->reaction = opts.reaction;

    Tags range = w.ranged ? Tag::Ranged : Tag::Melee;
    CheckSpec& k = a->check;
    k.kind = CheckKind::Attack;
    k.modifier = w.attack;
    k.itemBonus = w.potency;
    k.tags = Tag::AttackRoll | Tag::Check | range | attributeTag(w.attackAttribute);
    k.defense = Defense::AC;
    k.attackTrait = true;
    k.agile = w.agile;
    k.untypedPenalty = -2 * (opts.rangeIncrement - 1);
    k.fortune = opts.fortune;

    a->damage = {dmg(w.dice, w.die, w.flat, w.type)};
    a->damage.insert(a->damage.end(), w.onHit.begin(), w.onHit.end());
    a->damage.insert(a->damage.end(), opts.extraOnHit.begin(), opts.extraOnHit.end());

    // Fatal: weapon dice become the fatal size on a crit, plus one extra (undoubled) die.
    a->critDamage = a->damage;
    if (w.fatal)
    {
        a->critDamage.front().die = w.fatal;
        a->critDamage.push_back(DamageComponent{1, w.fatal, 0, w.type, DamageCategory::Normal, false});
    }
    // Deadly: 1 extra die, 2 with greater striking, 3 with major striking (not doubled).
    if (w.deadly)
        a->critDamage.push_back(DamageComponent{std::max(1, w.dice - 1), w.deadly, 0, w.type,
                                                DamageCategory::Normal, false});
    for (DamageComponent rider : w.onCrit)
    {
        rider.doublesOnCrit = false;
        a->critDamage.push_back(rider);
    }

    a->damageTags = Tag::Damage | range | (w.strengthToDamage ? Tags{Tag::Str} : Tags{0});
    a->weapon = w;
    a->traits = {"attack"};
    a->outcomes[S].damage = DamageScale::Normal;
    a->outcomes[CS].damage = DamageScale::Double;
    return a;
}

ActionPtr demoralize(const Character& c)
{
    auto a = make("Demoralize", 1);
    skillCheck(*a, c, Skill::Intimidation, Defense::Will);
    a->traits = {"auditory", "concentrate", "emotion", "fear", "mental"};
    a->outcomes[S].conditions = {{ConditionKind::Frightened, 1}};
    a->outcomes[CS].conditions = {{ConditionKind::Frightened, 2}};
    // The target is then temporarily immune to this character's Demoralize.
    std::string immunity = "demoralize-immune:" + c.name;
    for (auto& o : a->outcomes)
        o.counters = {{Who::Enemy, immunity}};
    a->usable = [immunity](const BattleState& s, int, int) { return s.counter(kEnemy, immunity) == 0; };
    return a;
}

ActionPtr feint(const Character& c)
{
    auto a = make("Feint", 1);
    skillCheck(*a, c, Skill::Deception, Defense::Perception);
    a->traits = {"mental"};
    Modifier offGuard{"feint", BonusType::Circumstance, -2, select({Tag::AC | Tag::Melee}), -1, true};
    // Success: off-guard against your next melee attack this turn.
    a->outcomes[S].modifiers = {ApplyModifier{Who::Enemy, offGuard, untilEndOfActorTurn(), 1, true}};
    // Critical success: off-guard against your melee attacks until the end of your next turn.
    a->outcomes[CS].modifiers = {ApplyModifier{Who::Enemy, offGuard, forTheRound(), -1, true}};
    return a;
}

ActionPtr trip(const Character& c)
{
    auto a = make("Trip", 1);
    skillCheck(*a, c, Skill::Athletics, Defense::Ref);
    a->check.attackTrait = true;
    a->traits = {"attack"};
    a->damage = {dmg(1, 6, 0, DamageType::Bludgeoning)};
    a->outcomes[S].conditions = {{ConditionKind::Prone, 1}};
    a->outcomes[CS].conditions = {{ConditionKind::Prone, 1}};
    a->outcomes[CS].damage = DamageScale::Normal;
    a->outcomes[CF].conditions = {{ConditionKind::Prone, 1, forTheRound(), Who::Actor}};
    return a;
}

ActionPtr grapple(const Character& c)
{
    auto a = make("Grapple", 1);
    skillCheck(*a, c, Skill::Athletics, Defense::Fort);
    a->check.attackTrait = true;
    a->traits = {"attack"};
    a->outcomes[S].conditions = {{ConditionKind::Grabbed, 1}};
    a->outcomes[CS].conditions = {{ConditionKind::Restrained, 1}};
    return a;
}

ActionPtr aid(const Character&, const Character& ally, int modifier, int critBonus, Selector aided)
{
    // Simplified: the preparation action and the reaction happen on the aider's turn,
    // and the bonus applies to the ally's next matching check.
    auto a = make("Aid " + ally.name, 1);
    a->allyTarget = ally.name;
    a->check.kind = CheckKind::FixedDC;
    a->check.modifier = modifier;
    a->check.tags = Tag::Check;
    a->check.fixedDC = 15;
    a->check.targeted = false;
    auto bonus = [&](int value) {
        return std::vector<ApplyModifier>{
            ApplyModifier{Who::Ally, Modifier{"aid", BonusType::Circumstance, value, aided}, forTheRound(), 1}};
    };
    a->outcomes[CS].modifiers = bonus(critBonus);
    a->outcomes[S].modifiers = bonus(1);
    a->outcomes[CF].modifiers = bonus(-1);
    for (auto& o : a->outcomes)
        o.custom = [](EffectCtx& ctx) { ctx.state[ctx.actor].reactionUsed = true; };
    return a;
}

ActionPtr battleMedicine(const Character& medic, const Character& patient, int dc, int bonusHealing)
{
    auto a = make("Battle Medicine -> " + patient.name, 1);
    a->allyTarget = patient.name;
    a->check.kind = CheckKind::FixedDC;
    a->check.modifier = medic.skill(Skill::Medicine);
    a->check.tags = Tag::Skill | Tag::Check | Tag::Wis;
    a->check.fixedDC = dc;
    a->check.targeted = false;
    a->outcomes[S].heal = HealSpec{2, 8, bonusHealing};
    a->outcomes[CS].heal = HealSpec{4, 8, bonusHealing};
    for (auto& o : a->outcomes)
        o.counters = {{Who::Ally, "immune:battle-medicine"}};
    a->usable = [](const BattleState& s, int, int ally) { return s.counter(ally, "immune:battle-medicine") == 0; };
    return a;
}

ActionPtr huntPrey(const Character&)
{
    auto a = make("Hunt Prey", 1);
    a->outcomes[S].counters = {{Who::Actor, "prey"}};
    return a;
}

ActionPtr strikeHard(const Character&, const Character& ally, const std::string& allyWeapon)
{
    auto a = make("Strike Hard! -> " + ally.name, 2);
    a->allyTarget = ally.name;
    StrikeOptions opts;
    opts.name = "Strike (" + allyWeapon + ", Strike Hard!)";
    opts.reaction = true;
    a->outcomes[S].grants = {Grant{ally.name, strike(ally, allyWeapon, opts)}};
    return a;
}

ActionPtr spellstrike(const Character& magus, const std::string& weapon, const ActionPtr& spell)
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
    auto a = std::make_shared<Action>(*strike(magus, weapon, opts));
    a->check.attacks = 2;
    a->spentFlag = "spellstrike-spent";

    // The spell goes where the Strike goes, so it has no concealment flat check of its own.
    auto coupled = std::make_shared<Action>(*spell);
    coupled->check.targeted = false;
    a->coupled = CoupledEffect{coupled, attackSpell ? Coupling::SameRoll : Coupling::OwnRoll};
    return a;
}

ActionPtr overwhelmingSpellstrike(const Character& magus, const std::string& weapon, const ActionPtr& spell)
{
    if (!spell)
        throw std::invalid_argument("Spellstrike needs a spell with a spell attack roll or a saving throw");
    // Only the spell's damage ignores resistance; immunities are unaffected.
    auto piercing = std::make_shared<Action>(*spell);
    for (auto* list : {&piercing->damage, &piercing->critDamage})
        for (DamageComponent& c : *list)
            if (overwhelmingAffects(c.type))
                c.ignoredResistance = magus.level;

    auto a = std::make_shared<Action>(*spellstrike(magus, weapon, piercing));
    a->name = "Overwhelming Spellstrike (" + spell->name + ")";
    return a;
}

ActionPtr rechargeSpellstrike(const Character&)
{
    auto a = make("Recharge Spellstrike", 1);
    a->outcomes[S].counters = {{Who::Actor, "spellstrike-spent", 0}};
    return a;
}

ActionPtr sureStrike(const Character&)
{
    auto a = make("Sure Strike", 1);
    a->outcomes[S].counters = {{Who::Actor, "fortune:attack"}};
    return a;
}

ActionPtr heroism(const Character&, const Character& ally, int rank)
{
    auto a = make("Heroism -> " + ally.name, 2);
    a->allyTarget = ally.name;
    int value = rank >= 9 ? 3 : rank >= 6 ? 2 : 1;
    a->outcomes[S].modifiers = {allyBonus(Who::Ally, "heroism", value,
                                          select({Tag::AttackRoll, Tag::Save, Tag::Skill | Tag::Check,
                                                  Tag::Perception | Tag::Check}))};
    return a;
}

ActionPtr bless(const Character&)
{
    auto a = make("Bless", 2);
    a->outcomes[S].modifiers = {allyBonus(Who::AllAllies, "bless", 1, select({Tag::AttackRoll}))};
    return a;
}

ActionPtr courageousAnthem(const Character&)
{
    auto a = make("Courageous Anthem", 1);
    a->outcomes[S].modifiers = {
        allyBonus(Who::AllAllies, "courageous-anthem", 1, select({Tag::AttackRoll, Tag::Damage}))};
    return a;
}

ActionPtr fear(const Character& caster, int rank)
{
    auto a = make("Fear", 2);
    a->check.kind = CheckKind::Save;
    a->check.modifier = caster.spellDC;
    a->check.tags = spellDCTags(caster);
    a->check.defense = Defense::Will;
    a->traits = {"concentrate", "emotion", "fear", "manipulate", "mental"};
    (void)rank;  // heightening only adds targets
    a->outcomes[S].conditions = {{ConditionKind::Frightened, 1}};
    a->outcomes[F].conditions = {{ConditionKind::Frightened, 2}};
    a->outcomes[CF].conditions = {{ConditionKind::Frightened, 3}};
    return a;
}

ActionPtr heal(const Character&, const Character& ally, int rank, int actions)
{
    auto a = make("Heal (" + std::to_string(actions) + "a) -> " + (actions == 3 ? std::string("party") : ally.name),
                  actions);
    a->allyTarget = ally.name;
    HealSpec h{rank, 8, actions == 2 ? 8 * rank : 0, actions == 3 ? Who::AllAllies : Who::Ally};
    a->outcomes[S].heal = h;
    return a;
}

ActionPtr basicSaveSpell(const Character& caster, const std::string& name, int cost, Defense save,
                         std::vector<DamageComponent> damage, std::set<std::string> traits, int incapacitationRank)
{
    auto a = make(name, cost);
    a->check.kind = CheckKind::Save;
    a->check.modifier = caster.spellDC;
    a->check.tags = spellDCTags(caster);
    a->check.defense = save;
    a->check.incapacitationRank = incapacitationRank;
    a->damage = std::move(damage);
    a->damageTags = Tag::Damage | Tag::Spell;
    a->traits = std::move(traits);
    // Indexed by the ENEMY's degree of success.
    a->outcomes[CS].damage = DamageScale::None;
    a->outcomes[S].damage = DamageScale::Half;
    a->outcomes[F].damage = DamageScale::Normal;
    a->outcomes[CF].damage = DamageScale::Double;
    return a;
}

ActionPtr spellAttackSpell(const Character& caster, const std::string& name, int cost,
                           std::vector<DamageComponent> damage, std::set<std::string> traits)
{
    auto a = make(name, cost);
    a->check.kind = CheckKind::Attack;
    a->check.modifier = caster.spellAttack;
    a->check.tags = Tag::AttackRoll | Tag::Check | Tag::Spell | Tag::Ranged | attributeTag(caster.keyAttribute);
    a->check.attackTrait = true;
    a->damage = std::move(damage);
    a->damageTags = Tag::Damage | Tag::Spell;
    a->traits = std::move(traits);
    a->traits.insert("attack");
    a->outcomes[S].damage = DamageScale::Normal;
    a->outcomes[CS].damage = DamageScale::Double;
    return a;
}

Feature sneakAttack(int dice)
{
    return Feature{"Sneak Attack", [dice](EffectCtx& ctx) {
                       const Weapon& w = *ctx.action.weapon;
                       if ((w.ranged || w.agile || w.finesse) && ctx.enemyOffGuard())
                           ctx.damage->push_back(precision(dice, 6, w.type));
                   }};
}

Feature precisionEdge(int dice)
{
    return Feature{"Precision", [dice](EffectCtx& ctx) {
                       if (ctx.before.counter(ctx.actor, "prey") == 0 ||
                           ctx.before.counter(ctx.actor, "precision-used") > 0)
                           return;
                       ctx.damage->push_back(precision(dice, 8, ctx.action.weapon->type));
                       ctx.state[ctx.actor].counters["precision-used"] = 1;
                   }};
}
