/**
 * @file    Engine.cpp
 * @brief   Branching resolution of turns, actions, checks and outcomes.
 */

#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
/**
 * @brief   Sum of the values of a ledger map.
 * @param   [in] lines - The map.
 * @return  The total.
 */
double Sum(const std::map<LedgerKey, double>& lines)
{
    double total = 0.0;
    for (const auto& [key, value] : lines)
        total += value;
    return total;
}

/**
 * @brief   Matches every roll; for modifiers that come from the roll itself (item bonus, MAP...).
 */
const Selector kEverything = Select({0});

/**
 * @brief   Fortune state of a roll, using up the roller's pending fortune and misfortune counters.
 * @details Pending fortune/misfortune counters ("fortune:attack", "misfortune:save", "fortune:check"...) on
 *          the roller are consumed by its next matching roll.
 * @param   [in] before - The state when the roll is made.
 * @param   [in, out] after - Loses the counters that were used.
 * @param   [in] rollerIndex - Index of the creature rolling.
 * @param   [in] rollKind - "attack", "save", "skill" or "check"; "check" counters match every roll.
 * @param   [in] actionFortune - Whether the action itself grants fortune.
 * @return  The roll's fortune state.
 */
Fortune TakeFortune(const BattleState& before, BattleState& after, int rollerIndex, const std::string& rollKind,
                    bool actionFortune)
{
    bool fortune = actionFortune;
    bool misfortune = false;
    for (const std::string& kind : {rollKind, std::string("check")})
    {
        for (auto [prefix, flag] : {std::pair<const char*, bool*>{"fortune:", &fortune}, {"misfortune:", &misfortune}})
        {
            std::string key = prefix + kind;
            if (before.Counter(rollerIndex, key) > 0)
            {
                *flag = true;
                after[rollerIndex].counters.erase(key);
            }
        }
    }
    return CombineFortune(fortune, misfortune);
}

/**
 * @brief   The multiple attack penalty.
 * @param   [in] attacksMade - Attacks already made this turn.
 * @param   [in] agile - Whether the attack is agile.
 * @return  0, then -5 (agile -4), then -10 (agile -8) from the third attack on.
 */
int MultipleAttackPenalty(int attacksMade, bool agile)
{
    if (attacksMade == 0)
        return 0;
    if (attacksMade == 1)
        return agile ? -4 : -5;
    return agile ? -8 : -10;
}
} // namespace

double Ledger::TotalDamage() const { return Sum(damage); }
double Ledger::TotalHealRaw() const { return Sum(healRaw); }
double Ledger::TotalHealEffective() const { return Sum(healEffective); }

double Ledger::DamageBy(const std::string& actorName) const
{
    double total = 0.0;
    for (const auto& [key, value] : damage)
        if (key.actorName == actorName)
            total += value;
    return total;
}

RunResult Engine::Run(const std::string& scenarioName, const BattleState& initial, const std::vector<Turn>& turns)
{
    RunResult result;
    result.name = scenarioName;
    Branches current{{initial, 1.0}};

    for (const Turn& turn : turns)
    {
        int actorIndex = m_encounter.IndexOf(turn.actorName);
        result.snapshots.push_back(TakeSnapshot(current, turn.actorName, actorIndex));
        for (auto& [state, probability] : current)
            state.StartTurn(actorIndex);
        current = Merge(std::move(current));

        for (const Step& step : turn.steps)
        {
            Branches next;
            for (const auto& [state, probability] : current)
            {
                ActionPtr action = step.action ? step.action : step.chooser(View{m_encounter, state, actorIndex});
                if (!action)
                {
                    next.emplace_back(state, probability);
                    continue;
                }
                Branches out = Execute(*action, state, probability, actorIndex, actorIndex, result.ledger);
                next.insert(next.end(), std::make_move_iterator(out.begin()), std::make_move_iterator(out.end()));
            }
            result.peakBranches = std::max(result.peakBranches, next.size());
            current = Merge(std::move(next));
        }

        for (auto& [state, probability] : current)
            state.EndTurn(actorIndex);
        current = Merge(std::move(current));
    }

    EnemyEndOfTurn(current, result.ledger);
    result.finalStates = std::move(current);
    return result;
}

Branches Engine::Execute(const Action& action, const BattleState& state, double probability, int actorIndex,
                         int turnOwnerIndex, Ledger& ledger) const
{
    LedgerKey key{m_encounter.GetCharacter(actorIndex).name, action.name};
    if (!action.spentFlag.empty() && state.Counter(actorIndex, action.spentFlag) > 0)
    {
        ledger.skipped[key] += probability;
        return {{state, probability}};
    }

    BattleState before = state;
    if (action.reaction)
    {
        if (before[actorIndex].reactionUsed)
        {
            ledger.skipped[key] += probability;
            return {{state, probability}};
        }
        before[actorIndex].reactionUsed = true;
    }
    else
    {
        if (before.ActionsLeft(actorIndex) < action.cost)
        {
            ledger.skipped[key] += probability;
            return {{state, probability}};
        }
        before[actorIndex].actionsUsed += action.cost;
    }
    if (!action.spentFlag.empty())
        before[actorIndex].counters[action.spentFlag] = 1;

    int allyIndex = action.allyName.empty() ? -1 : m_encounter.IndexOf(action.allyName);
    BattleState after = before;
    DegreeDist dist = Roll(action, before, after, actorIndex, turnOwnerIndex, allyIndex);
    double noEffect = 1.0 - (dist[0] + dist[1] + dist[2] + dist[3]);

    Branches result;
    if (noEffect > 1e-12)
        result.emplace_back(after, probability * noEffect);

    std::set<std::string> attackTraits = action.traits;
    if (action.weapon)
        attackTraits.insert(action.weapon->traits.begin(), action.weapon->traits.end());
    if (action.coupled)
        attackTraits.insert(action.coupled->action->traits.begin(), action.coupled->action->traits.end());

    for (int degreeIndex = 0; degreeIndex < kDegrees; ++degreeIndex)
    {
        if (dist[degreeIndex] <= 0.0)
            continue;
        Degree degree = static_cast<Degree>(degreeIndex);
        OutcomeBranch base{after, probability * dist[degreeIndex], {}};
        ApplyOutcome(action, degree, base, before, actorIndex, allyIndex, key, ledger);

        std::vector<OutcomeBranch> parts;
        if (action.coupled)
            parts = ResolveCoupled(action, degree, std::move(base), before, actorIndex, turnOwnerIndex, allyIndex, key,
                                   ledger);
        else
            parts.push_back(std::move(base));

        const Outcome& outcome = action.outcomes[degreeIndex];
        for (OutcomeBranch& branch : parts)
        {
            if (!branch.rolls.empty())
                ledger.damage[key] +=
                    branch.probability * ExpectedDamage(branch.rolls, m_encounter.enemy.defenses, attackTraits);
            for (const DamageRoll& damageRoll : branch.rolls)
                AddPersistent(damageRoll, branch.state);

            Branches outs = Heal(outcome.heal, std::move(branch.state), branch.probability, actorIndex, allyIndex, key,
                                 ledger);
            for (const Grant& grant : outcome.grants)
            {
                int granteeIndex = m_encounter.IndexOf(grant.allyName);
                Branches next;
                for (const auto& [branchState, branchProbability] : outs)
                {
                    Branches granted =
                        Execute(*grant.action, branchState, branchProbability, granteeIndex, turnOwnerIndex, ledger);
                    next.insert(next.end(), std::make_move_iterator(granted.begin()),
                                std::make_move_iterator(granted.end()));
                }
                outs = Merge(std::move(next));
            }
            result.insert(result.end(), std::make_move_iterator(outs.begin()), std::make_move_iterator(outs.end()));
        }
    }
    return result;
}

void Engine::ApplyOutcome(const Action& action, Degree degree, OutcomeBranch& branch, const BattleState& before,
                          int actorIndex, int allyIndex, const LedgerKey& key, Ledger& ledger) const
{
    const Outcome& outcome = action.outcomes[static_cast<int>(degree)];
    const Monster& enemy = m_encounter.enemy;
    BattleState& state = branch.state;
    EffectCtx ctx{m_encounter, action, degree, before, state, actorIndex, nullptr};

    if (outcome.damage != DamageScale::None)
        branch.rolls.push_back(DamageRoll{DamageComponents(action, outcome.damage, ctx), outcome.damage});

    for (const ApplyCondition& condition : outcome.conditions)
        for (int targetIndex : Resolve(condition.to, actorIndex, allyIndex))
        {
            if (targetIndex == kEnemyIndex && enemy.defenses.ImmuneTo(ToString(condition.kind)))
                continue;
            int previous = state.ConditionValue(targetIndex, condition.kind);
            state.AddCondition(targetIndex, condition.kind, condition.value,
                               Resolve(condition.until, actorIndex, allyIndex));

            // Drained: the enemy loses level (min 1) x the increase in HP. It isn't damage,
            // so it skips immunity/weakness/resistance, but it's credited to the actor.
            int increase = state.ConditionValue(targetIndex, condition.kind) - previous;
            if (targetIndex == kEnemyIndex && condition.kind == ConditionKind::Drained && increase > 0)
                ledger.damage[LedgerKey{key.actorName, key.actionName + " (drained HP loss)"}] +=
                    branch.probability * std::max(1, enemy.level) * increase;
        }

    for (const ApplyModifier& modifier : outcome.modifiers)
    {
        Modifier mod = modifier.mod;
        if (modifier.onlyVsActor)
            mod.vsCreatureIndex = actorIndex;
        for (int targetIndex : Resolve(modifier.to, actorIndex, allyIndex))
            state.AddModifier(targetIndex,
                              ActiveModifier{mod, Resolve(modifier.until, actorIndex, allyIndex), modifier.uses});
    }

    for (const SetCounter& counter : outcome.counters)
        for (int targetIndex : Resolve(counter.on, actorIndex, allyIndex))
        {
            if (counter.value == 0)
                state[targetIndex].counters.erase(counter.name);
            else
                state[targetIndex].counters[counter.name] = counter.value;
        }

    if (outcome.custom)
        outcome.custom(ctx);
}

std::vector<Engine::OutcomeBranch> Engine::ResolveCoupled(const Action& action, Degree degree, OutcomeBranch base,
                                                          const BattleState& before, int actorIndex, int turnOwnerIndex,
                                                          int allyIndex, const LedgerKey& key, Ledger& ledger) const
{
    const Action& spell = *action.coupled->action;
    if (action.coupled->roll == Coupling::SameRoll)
    {
        // Spell attack spells take the Strike's result.
        if (!ImmuneToTraits(spell))
        {
            int rank = spell.check.incapacitationRank;
            bool incapacitated = rank > 0 && m_encounter.enemy.level > 2 * rank;
            Degree spellDegree = (incapacitated && degree == Degree::CritSuccess) ? Degree::Success : degree;
            ApplyOutcome(spell, spellDegree, base, before, actorIndex, allyIndex, key, ledger);
        }
        return {std::move(base)};
    }

    // Save spells: the target saves normally, unless the Strike critically failed (spell disrupted).
    if (degree == Degree::CritFail)
        return {std::move(base)};

    DegreeDist dist = Roll(spell, before, base.state, actorIndex, turnOwnerIndex, allyIndex);
    std::vector<OutcomeBranch> out;
    double noEffect = 1.0 - (dist[0] + dist[1] + dist[2] + dist[3]);
    if (noEffect > 1e-12)
        out.push_back(OutcomeBranch{base.state, base.probability * noEffect, base.rolls});
    for (int degreeIndex = 0; degreeIndex < kDegrees; ++degreeIndex)
    {
        if (dist[degreeIndex] <= 0.0)
            continue;
        OutcomeBranch branch{base.state, base.probability * dist[degreeIndex], base.rolls};
        ApplyOutcome(spell, static_cast<Degree>(degreeIndex), branch, before, actorIndex, allyIndex, key, ledger);
        out.push_back(std::move(branch));
    }
    return out;
}

std::vector<DamageComponent> Engine::DamageComponents(const Action& action, DamageScale scale, EffectCtx& ctx) const
{
    bool doubled = scale == DamageScale::Double;
    std::vector<DamageComponent> components =
        (doubled && !action.critDamage.empty()) ? action.critDamage : action.damage;
    ctx.damage = &components;
    if (action.weapon)
        for (const Feature& feature : m_encounter.GetCharacter(ctx.actorIndex).features)
            if (feature.onStrikeDamage)
                feature.onStrikeDamage(ctx);
    ctx.damage = nullptr;

    if (action.damageTags != 0)
    {
        int bonus = ctx.before.ModifierTotal(ctx.actorIndex, RollQuery{action.damageTags, kEnemyIndex});
        auto main = std::find_if(components.begin(), components.end(), [](const DamageComponent& component) {
            return component.category == DamageCategory::Normal;
        });
        if (bonus != 0 && main != components.end())
            main->flat += bonus;
    }
    return components;
}

void Engine::AddPersistent(const DamageRoll& roll, BattleState& state) const
{
    for (DamageComponent component : roll.components)
    {
        if (component.category != DamageCategory::Persistent ||
            m_encounter.enemy.defenses.ImmuneTo(ToString(component.type)))
            continue;
        if (roll.scale == DamageScale::Double && component.doublesOnCrit)
        {
            component.count *= 2;  // same mean as doubling the roll
            component.flat *= 2;
        }
        else if (roll.scale == DamageScale::Half)
        {
            component.count = (component.count + 1) / 2;
            component.flat /= 2;
        }
        component.category = DamageCategory::Normal;
        state.AddPersistent(kEnemyIndex, component);
    }
}

Branches Engine::Heal(const HealSpec& healSpec, BattleState state, double probability, int actorIndex, int allyIndex,
                      const LedgerKey& key, Ledger& ledger) const
{
    if (!healSpec.Any())
        return {{std::move(state), probability}};

    std::vector<int> targetIndices = Resolve(healSpec.to, actorIndex, allyIndex);
    Dist healing = healSpec.Distribution();
    ledger.healRaw[key] += probability * healing.Mean() * targetIndices.size();
    // One roll heals every target; branch on the amount so later heals see the right missing HP.
    Branches outs;
    for (auto [amount, amountProbability] : healing.Values())
    {
        BattleState healed = state;
        int effective = 0;
        for (int targetIndex : targetIndices)
        {
            int amountHealed = std::clamp(amount, 0, healed[targetIndex].hpMissing);
            healed[targetIndex].hpMissing -= amountHealed;
            effective += amountHealed;
        }
        ledger.healEffective[key] += probability * amountProbability * effective;
        outs.emplace_back(std::move(healed), probability * amountProbability);
    }
    return Merge(std::move(outs));
}

bool Engine::ImmuneToTraits(const Action& action) const
{
    return std::any_of(action.traits.begin(), action.traits.end(),
                       [&](const std::string& trait) { return m_encounter.enemy.defenses.ImmuneTo(trait); });
}

DegreeDist Engine::Roll(const Action& action, const BattleState& before, BattleState& after, int actorIndex,
                        int turnOwnerIndex, int allyIndex) const
{
    const CheckSpec& check = action.check;
    const Monster& enemy = m_encounter.enemy;
    DegreeDist none{};

    // Multiple attack penalty only counts on your own turn.
    bool countsForMAP = check.attackTrait && actorIndex == turnOwnerIndex;
    if (countsForMAP)
        after[actorIndex].attacksThisTurn += check.attacks;

    bool vsEnemy = check.kind == CheckKind::Attack || check.kind == CheckKind::Save;
    if (vsEnemy && ImmuneToTraits(action))
        return none;
    if (action.usable && !action.usable(before, actorIndex, allyIndex))
        return none;

    if (check.kind == CheckKind::None)
        return DegreeDist{0.0, 0.0, 1.0, 0.0};

    std::vector<Modifier> actorExtra;
    if (check.itemBonus != 0)
        actorExtra.push_back(Modifier{"item", BonusType::Item, check.itemBonus, kEverything});
    if (check.untypedPenalty != 0)
        actorExtra.push_back(Modifier{"penalty", BonusType::Untyped, check.untypedPenalty, kEverything});
    if (countsForMAP)
        if (int map = MultipleAttackPenalty(before[actorIndex].attacksThisTurn, check.agile); map != 0)
            actorExtra.push_back(Modifier{"MAP", BonusType::Untyped, map, kEverything});

    int opponentIndex = vsEnemy ? kEnemyIndex : allyIndex;
    std::vector<std::string> actorUsed, enemyUsed;
    int actorSide = check.modifier - check.itemBonus +
                    before.ModifierTotal(actorIndex, RollQuery{check.tags, opponentIndex}, actorExtra, &actorUsed);

    DegreeDist dist{};
    if (check.kind == CheckKind::Attack)
    {
        Tags dcTags = DefenseDCTags(check.defense) | (check.tags & (Tag::Melee | Tag::Ranged));
        int dc = enemy.DefenseDC(check.defense) +
                 before.ModifierTotal(kEnemyIndex, RollQuery{dcTags, actorIndex}, {}, &enemyUsed);
        std::string kind = check.attackTrait ? "attack" : ((check.tags & Tag::Skill) ? "skill" : "check");
        dist = ResolveCheck(actorSide, dc, TakeFortune(before, after, actorIndex, kind, check.fortune));
        bool critImmune = check.attackTrait && enemy.defenses.ImmuneTo("critical-hits");
        bool incapacitated = check.incapacitationRank > 0 && enemy.level > 2 * check.incapacitationRank;
        if (critImmune || incapacitated)
            dist = RemapDegree(dist, Degree::CritSuccess, Degree::Success);
    }
    else if (check.kind == CheckKind::Save)
    {
        int save = enemy.DefenseValue(check.defense) +
                   before.ModifierTotal(kEnemyIndex, RollQuery{DefenseRollTags(check.defense), actorIndex}, {},
                                        &enemyUsed);
        dist = ResolveCheck(save, actorSide, TakeFortune(before, after, kEnemyIndex, "save", check.fortune));
        if (check.incapacitationRank > 0 && enemy.level > 2 * check.incapacitationRank)
            dist = ShiftDegrees(dist, +1);
    }
    else // FixedDC
    {
        std::string kind = (check.tags & Tag::Skill) ? "skill" : "check";
        dist = ResolveCheck(actorSide, check.fixedDC, TakeFortune(before, after, actorIndex, kind, check.fortune));
    }

    after.ConsumeUses(actorIndex, actorUsed);
    if (vsEnemy)
        after.ConsumeUses(kEnemyIndex, enemyUsed);

    // Concealed / hidden targets (or a dazzled or blinded actor): failing the flat check wastes the action.
    if (vsEnemy && check.targeted)
    {
        int flatDC = 0;
        if (before.ConditionValue(kEnemyIndex, ConditionKind::Hidden) > 0 ||
            before.ConditionValue(actorIndex, ConditionKind::Blinded) > 0)
            flatDC = 11;
        else if (before.ConditionValue(kEnemyIndex, ConditionKind::Concealed) > 0 ||
                 before.ConditionValue(actorIndex, ConditionKind::Dazzled) > 0)
            flatDC = 5;
        if (flatDC > 0)
        {
            double pass = FlatCheckPassChance(flatDC);
            for (double& probability : dist)
                probability *= pass;
        }
    }
    return dist;
}

std::vector<int> Engine::Resolve(Who who, int actorIndex, int allyIndex) const
{
    switch (who)
    {
    case Who::Actor: return {actorIndex};
    case Who::Enemy: return {kEnemyIndex};
    case Who::Ally:
        if (allyIndex < 0)
            throw std::runtime_error("action targets an ally but has no allyTarget");
        return {allyIndex};
    case Who::AllAllies:
    {
        std::vector<int> partyIndices;
        for (std::size_t creatureIndex = 1; creatureIndex <= m_encounter.party.size(); ++creatureIndex)
            partyIndices.push_back(static_cast<int>(creatureIndex));
        return partyIndices;
    }
    }
    return {};
}

Duration Engine::Resolve(const DurationSpec& spec, int actorIndex, int allyIndex) const
{
    if (spec.when == Expiry::Never)
        return {};
    int creatureIndex = actorIndex;
    if (spec.of == Who::Enemy)
        creatureIndex = kEnemyIndex;
    else if (spec.of == Who::Ally && allyIndex >= 0)
        creatureIndex = allyIndex;
    return Duration{spec.when, creatureIndex};
}

Branches Engine::Merge(Branches branches) const
{
    if (!m_settings.mergeStates)
        return branches;
    std::map<BattleState, double> merged;
    for (auto& [state, probability] : branches)
        merged[std::move(state)] += probability;
    return Branches(std::make_move_iterator(merged.begin()), std::make_move_iterator(merged.end()));
}

Snapshot Engine::TakeSnapshot(const Branches& branches, const std::string& actorName, int actorIndex) const
{
    Snapshot result;
    result.actorName = actorName;
    result.branches = branches.size();
    for (const auto& [state, probability] : branches)
    {
        for (const auto& [kind, condition] : state[kEnemyIndex].conditions)
        {
            result.enemyConditionChance[kind] += probability;
            result.enemyConditionMean[kind] += probability * condition.value;
        }
        if (state.IsOffGuardTo(kEnemyIndex, actorIndex, Tag::Melee))
            result.enemyOffGuardChance += probability;
    }
    return result;
}

void Engine::EnemyEndOfTurn(Branches& branches, Ledger& ledger) const
{
    // Each tick after the first only happens if the previous DC 15 flat check failed (70%).
    double ticks = 0.0;
    for (int tick = 0; tick < m_settings.persistentTicks; ++tick)
        ticks += std::pow(1.0 - FlatCheckPassChance(15), tick);

    for (auto& [state, probability] : branches)
    {
        for (const auto& [type, component] : state[kEnemyIndex].persistent)
        {
            double damage = ExpectedDamage({component}, DamageScale::Normal, m_encounter.enemy.defenses);
            ledger.damage[LedgerKey{"(persistent)", ToString(type)}] += probability * damage * ticks;
        }
        state.EndTurn(kEnemyIndex);
    }
    branches = Merge(std::move(branches));
}
