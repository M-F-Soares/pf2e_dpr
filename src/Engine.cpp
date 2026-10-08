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
 * @param   [in] m - The map.
 * @return  The total.
 */
double sum(const std::map<LedgerKey, double>& m)
{
    double total = 0.0;
    for (const auto& [k, v] : m)
        total += v;
    return total;
}

/**
 * @brief   Matches every roll; for modifiers that come from the roll itself (item bonus, MAP...).
 */
const Selector kEverything = select({0});

/**
 * @brief   Fortune state of a roll, using up the roller's pending fortune and misfortune counters.
 * @details Pending fortune/misfortune counters ("fortune:attack", "misfortune:save", "fortune:check"...) on
 *          the roller are consumed by its next matching roll.
 * @param   [in] before - The state when the roll is made.
 * @param   [in, out] after - Loses the counters that were used.
 * @param   [in] roller - Index of the creature rolling.
 * @param   [in] rollKind - "attack", "save", "skill" or "check"; "check" counters match every roll.
 * @param   [in] actionFortune - Whether the action itself grants fortune.
 * @return  The roll's fortune state.
 */
Fortune takeFortune(const BattleState& before, BattleState& after, int roller, const std::string& rollKind,
                    bool actionFortune)
{
    bool fortune = actionFortune;
    bool misfortune = false;
    for (const std::string& kind : {rollKind, std::string("check")})
    {
        for (auto [prefix, flag] : {std::pair<const char*, bool*>{"fortune:", &fortune}, {"misfortune:", &misfortune}})
        {
            std::string key = prefix + kind;
            if (before.counter(roller, key) > 0)
            {
                *flag = true;
                after[roller].counters.erase(key);
            }
        }
    }
    return combineFortune(fortune, misfortune);
}

/**
 * @brief   The multiple attack penalty.
 * @param   [in] attacksMade - Attacks already made this turn.
 * @param   [in] agile - Whether the attack is agile.
 * @return  0, then -5 (agile -4), then -10 (agile -8) from the third attack on.
 */
int multipleAttackPenalty(int attacksMade, bool agile)
{
    if (attacksMade == 0)
        return 0;
    if (attacksMade == 1)
        return agile ? -4 : -5;
    return agile ? -8 : -10;
}
} // namespace

int Encounter::indexOf(const std::string& name) const
{
    for (std::size_t i = 0; i < party.size(); ++i)
        if (party[i].name == name)
            return static_cast<int>(i) + 1;
    throw std::runtime_error("no party member named " + name);
}

double Ledger::totalDamage() const { return sum(damage); }
double Ledger::totalHealRaw() const { return sum(healRaw); }
double Ledger::totalHealEffective() const { return sum(healEffective); }

double Ledger::damageBy(const std::string& who) const
{
    double total = 0.0;
    for (const auto& [k, v] : damage)
        if (k.who == who)
            total += v;
    return total;
}

RunResult Engine::run(const std::string& name, const BattleState& initial, const std::vector<Turn>& turns)
{
    RunResult result;
    result.name = name;
    Branches current{{initial, 1.0}};

    for (const Turn& turn : turns)
    {
        int actor = m_enc.indexOf(turn.actor);
        result.snapshots.push_back(snapshot(current, turn.actor, actor));
        for (auto& [s, p] : current)
            s.startTurn(actor);
        current = merge(std::move(current));

        for (const Step& step : turn.steps)
        {
            Branches next;
            for (const auto& [s, p] : current)
            {
                ActionPtr a = step.action ? step.action : step.chooser(View{m_enc, s, actor});
                if (!a)
                {
                    next.emplace_back(s, p);
                    continue;
                }
                Branches out = execute(*a, s, p, actor, actor, result.ledger);
                next.insert(next.end(), std::make_move_iterator(out.begin()), std::make_move_iterator(out.end()));
            }
            result.peakBranches = std::max(result.peakBranches, next.size());
            current = merge(std::move(next));
        }

        for (auto& [s, p] : current)
            s.endTurn(actor);
        current = merge(std::move(current));
    }

    enemyEndOfTurn(current, result.ledger);
    result.finalStates = std::move(current);
    return result;
}

Branches Engine::execute(const Action& a, const BattleState& state, double p, int actor, int turnOwner,
                         Ledger& ledger) const
{
    LedgerKey key{m_enc.character(actor).name, a.name};
    if (!a.spentFlag.empty() && state.counter(actor, a.spentFlag) > 0)
    {
        ledger.skipped[key] += p;
        return {{state, p}};
    }

    BattleState before = state;
    if (a.reaction)
    {
        if (before[actor].reactionUsed)
        {
            ledger.skipped[key] += p;
            return {{state, p}};
        }
        before[actor].reactionUsed = true;
    }
    else
    {
        if (before.actionsLeft(actor) < a.cost)
        {
            ledger.skipped[key] += p;
            return {{state, p}};
        }
        before[actor].actionsUsed += a.cost;
    }
    if (!a.spentFlag.empty())
        before[actor].counters[a.spentFlag] = 1;

    int ally = a.allyTarget.empty() ? -1 : m_enc.indexOf(a.allyTarget);
    BattleState after = before;
    DegreeDist dist = roll(a, before, after, actor, turnOwner, ally);
    double noEffect = 1.0 - (dist[0] + dist[1] + dist[2] + dist[3]);

    Branches result;
    if (noEffect > 1e-12)
        result.emplace_back(after, p * noEffect);

    std::set<std::string> attackTraits = a.traits;
    if (a.weapon)
        attackTraits.insert(a.weapon->traits.begin(), a.weapon->traits.end());
    if (a.coupled)
        attackTraits.insert(a.coupled->action->traits.begin(), a.coupled->action->traits.end());

    for (int d = 0; d < kDegrees; ++d)
    {
        if (dist[d] <= 0.0)
            continue;
        Degree degree = static_cast<Degree>(d);
        OutcomeBranch base{after, p * dist[d], {}};
        applyOutcome(a, degree, base, before, actor, ally, key, ledger);

        std::vector<OutcomeBranch> parts;
        if (a.coupled)
            parts = resolveCoupled(a, degree, std::move(base), before, actor, turnOwner, ally, key, ledger);
        else
            parts.push_back(std::move(base));

        const Outcome& o = a.outcomes[d];
        for (OutcomeBranch& b : parts)
        {
            if (!b.rolls.empty())
                ledger.damage[key] += b.p * expectedDamage(b.rolls, m_enc.enemy.defenses, attackTraits);
            for (const DamageRoll& r : b.rolls)
                addPersistent(r, b.state);

            Branches outs = heal(o.heal, std::move(b.state), b.p, actor, ally, key, ledger);
            for (const Grant& g : o.grants)
            {
                int grantee = m_enc.indexOf(g.ally);
                Branches next;
                for (const auto& [bs, bp] : outs)
                {
                    Branches sub = execute(*g.action, bs, bp, grantee, turnOwner, ledger);
                    next.insert(next.end(), std::make_move_iterator(sub.begin()), std::make_move_iterator(sub.end()));
                }
                outs = merge(std::move(next));
            }
            result.insert(result.end(), std::make_move_iterator(outs.begin()), std::make_move_iterator(outs.end()));
        }
    }
    return result;
}

void Engine::applyOutcome(const Action& a, Degree degree, OutcomeBranch& b, const BattleState& before, int actor,
                          int ally, const LedgerKey& key, Ledger& ledger) const
{
    const Outcome& o = a.outcomes[static_cast<int>(degree)];
    const Monster& enemy = m_enc.enemy;
    BattleState& st = b.state;
    EffectCtx ctx{m_enc, a, degree, before, st, actor, nullptr};

    if (o.damage != DamageScale::None)
        b.rolls.push_back(DamageRoll{damageComponents(a, o.damage, ctx), o.damage});

    for (const ApplyCondition& c : o.conditions)
        for (int target : resolve(c.to, actor, ally))
        {
            if (target == kEnemy && enemy.defenses.immuneTo(toString(c.kind)))
                continue;
            int previous = st.conditionValue(target, c.kind);
            st.addCondition(target, c.kind, c.value, resolve(c.until, actor, ally));

            // Drained: the enemy loses level (min 1) x the increase in HP. It isn't damage,
            // so it skips immunity/weakness/resistance, but it's credited to the actor.
            int increase = st.conditionValue(target, c.kind) - previous;
            if (target == kEnemy && c.kind == ConditionKind::Drained && increase > 0)
                ledger.damage[LedgerKey{key.who, key.what + " (drained HP loss)"}] +=
                    b.p * std::max(1, enemy.level) * increase;
        }

    for (const ApplyModifier& m : o.modifiers)
    {
        Modifier mod = m.mod;
        if (m.onlyVsActor)
            mod.vsCreature = actor;
        for (int target : resolve(m.to, actor, ally))
            st.addModifier(target, ActiveModifier{mod, resolve(m.until, actor, ally), m.uses});
    }

    for (const SetCounter& c : o.counters)
        for (int target : resolve(c.on, actor, ally))
        {
            if (c.value == 0)
                st[target].counters.erase(c.name);
            else
                st[target].counters[c.name] = c.value;
        }

    if (o.custom)
        o.custom(ctx);
}

std::vector<Engine::OutcomeBranch> Engine::resolveCoupled(const Action& a, Degree degree, OutcomeBranch base,
                                                          const BattleState& before, int actor, int turnOwner,
                                                          int ally, const LedgerKey& key, Ledger& ledger) const
{
    const Action& spell = *a.coupled->action;
    if (a.coupled->roll == Coupling::SameRoll)
    {
        // Spell attack spells take the Strike's result.
        if (!immuneToTraits(spell))
        {
            int rank = spell.check.incapacitationRank;
            bool incapacitated = rank > 0 && m_enc.enemy.level > 2 * rank;
            Degree spellDegree = (incapacitated && degree == Degree::CritSuccess) ? Degree::Success : degree;
            applyOutcome(spell, spellDegree, base, before, actor, ally, key, ledger);
        }
        return {std::move(base)};
    }

    // Save spells: the target saves normally, unless the Strike critically failed (spell disrupted).
    if (degree == Degree::CritFail)
        return {std::move(base)};

    DegreeDist dist = roll(spell, before, base.state, actor, turnOwner, ally);
    std::vector<OutcomeBranch> out;
    double noEffect = 1.0 - (dist[0] + dist[1] + dist[2] + dist[3]);
    if (noEffect > 1e-12)
        out.push_back(OutcomeBranch{base.state, base.p * noEffect, base.rolls});
    for (int d = 0; d < kDegrees; ++d)
    {
        if (dist[d] <= 0.0)
            continue;
        OutcomeBranch b{base.state, base.p * dist[d], base.rolls};
        applyOutcome(spell, static_cast<Degree>(d), b, before, actor, ally, key, ledger);
        out.push_back(std::move(b));
    }
    return out;
}

std::vector<DamageComponent> Engine::damageComponents(const Action& a, DamageScale scale, EffectCtx& ctx) const
{
    bool doubled = scale == DamageScale::Double;
    std::vector<DamageComponent> comps = (doubled && !a.critDamage.empty()) ? a.critDamage : a.damage;
    ctx.damage = &comps;
    if (a.weapon)
        for (const Feature& f : m_enc.character(ctx.actor).features)
            if (f.onStrikeDamage)
                f.onStrikeDamage(ctx);
    ctx.damage = nullptr;

    if (a.damageTags != 0)
    {
        int bonus = ctx.before.modifierTotal(ctx.actor, RollQuery{a.damageTags, kEnemy});
        auto main = std::find_if(comps.begin(), comps.end(),
                                 [](const DamageComponent& c) { return c.category == DamageCategory::Normal; });
        if (bonus != 0 && main != comps.end())
            main->flat += bonus;
    }
    return comps;
}

void Engine::addPersistent(const DamageRoll& roll, BattleState& state) const
{
    for (DamageComponent c : roll.components)
    {
        if (c.category != DamageCategory::Persistent || m_enc.enemy.defenses.immuneTo(toString(c.type)))
            continue;
        if (roll.scale == DamageScale::Double && c.doublesOnCrit)
        {
            c.count *= 2;  // same mean as doubling the roll
            c.flat *= 2;
        }
        else if (roll.scale == DamageScale::Half)
        {
            c.count = (c.count + 1) / 2;
            c.flat /= 2;
        }
        c.category = DamageCategory::Normal;
        state.addPersistent(kEnemy, c);
    }
}

Branches Engine::heal(const HealSpec& h, BattleState state, double p, int actor, int ally, const LedgerKey& key,
                      Ledger& ledger) const
{
    if (!h.any())
        return {{std::move(state), p}};

    std::vector<int> targets = resolve(h.to, actor, ally);
    Dist healing = h.dist();
    ledger.healRaw[key] += p * healing.mean() * targets.size();
    // One roll heals every target; branch on the amount so later heals see the right missing HP.
    Branches outs;
    for (auto [amount, pa] : healing.values())
    {
        BattleState healed = state;
        int effective = 0;
        for (int t : targets)
        {
            int amountHealed = std::clamp(amount, 0, healed[t].hpMissing);
            healed[t].hpMissing -= amountHealed;
            effective += amountHealed;
        }
        ledger.healEffective[key] += p * pa * effective;
        outs.emplace_back(std::move(healed), p * pa);
    }
    return merge(std::move(outs));
}

bool Engine::immuneToTraits(const Action& a) const
{
    return std::any_of(a.traits.begin(), a.traits.end(),
                       [&](const std::string& t) { return m_enc.enemy.defenses.immuneTo(t); });
}

DegreeDist Engine::roll(const Action& a, const BattleState& before, BattleState& after, int actor, int turnOwner,
                        int ally) const
{
    const CheckSpec& c = a.check;
    const Monster& enemy = m_enc.enemy;
    DegreeDist none{};

    // Multiple attack penalty only counts on your own turn.
    bool countsForMAP = c.attackTrait && actor == turnOwner;
    if (countsForMAP)
        after[actor].attacksThisTurn += c.attacks;

    bool vsEnemy = c.kind == CheckKind::Attack || c.kind == CheckKind::Save;
    if (vsEnemy && immuneToTraits(a))
        return none;
    if (a.usable && !a.usable(before, actor, ally))
        return none;

    if (c.kind == CheckKind::None)
        return DegreeDist{0.0, 0.0, 1.0, 0.0};

    std::vector<Modifier> actorExtra;
    if (c.itemBonus != 0)
        actorExtra.push_back(Modifier{"item", BonusType::Item, c.itemBonus, kEverything});
    if (c.untypedPenalty != 0)
        actorExtra.push_back(Modifier{"penalty", BonusType::Untyped, c.untypedPenalty, kEverything});
    if (countsForMAP)
        if (int map = multipleAttackPenalty(before[actor].attacksThisTurn, c.agile); map != 0)
            actorExtra.push_back(Modifier{"MAP", BonusType::Untyped, map, kEverything});

    int opponent = vsEnemy ? kEnemy : ally;
    std::vector<std::string> actorUsed, enemyUsed;
    int actorSide = c.modifier - c.itemBonus +
                    before.modifierTotal(actor, RollQuery{c.tags, opponent}, actorExtra, &actorUsed);

    DegreeDist dist{};
    if (c.kind == CheckKind::Attack)
    {
        Tags dcTags = defenseDCTags(c.defense) | (c.tags & (Tag::Melee | Tag::Ranged));
        int dc = enemy.defenseDC(c.defense) + before.modifierTotal(kEnemy, RollQuery{dcTags, actor}, {}, &enemyUsed);
        std::string kind = c.attackTrait ? "attack" : ((c.tags & Tag::Skill) ? "skill" : "check");
        dist = resolveCheck(actorSide, dc, takeFortune(before, after, actor, kind, c.fortune));
        bool critImmune = c.attackTrait && enemy.defenses.immuneTo("critical-hits");
        bool incapacitated = c.incapacitationRank > 0 && enemy.level > 2 * c.incapacitationRank;
        if (critImmune || incapacitated)
            dist = remapDegree(dist, Degree::CritSuccess, Degree::Success);
    }
    else if (c.kind == CheckKind::Save)
    {
        int save = enemy.defenseValue(c.defense) +
                   before.modifierTotal(kEnemy, RollQuery{defenseRollTags(c.defense), actor}, {}, &enemyUsed);
        dist = resolveCheck(save, actorSide, takeFortune(before, after, kEnemy, "save", c.fortune));
        if (c.incapacitationRank > 0 && enemy.level > 2 * c.incapacitationRank)
            dist = shiftDegrees(dist, +1);
    }
    else // FixedDC
    {
        std::string kind = (c.tags & Tag::Skill) ? "skill" : "check";
        dist = resolveCheck(actorSide, c.fixedDC, takeFortune(before, after, actor, kind, c.fortune));
    }

    after.consumeUses(actor, actorUsed);
    if (vsEnemy)
        after.consumeUses(kEnemy, enemyUsed);

    // Concealed / hidden targets (or a dazzled actor): failing the flat check wastes the action.
    if (vsEnemy && c.targeted)
    {
        int flatDC = 0;
        if (before.conditionValue(kEnemy, ConditionKind::Hidden) > 0)
            flatDC = 11;
        else if (before.conditionValue(kEnemy, ConditionKind::Concealed) > 0 ||
                 before.conditionValue(actor, ConditionKind::Dazzled) > 0)
            flatDC = 5;
        if (flatDC > 0)
        {
            double pass = flatCheckPassChance(flatDC);
            for (double& x : dist)
                x *= pass;
        }
    }
    return dist;
}

std::vector<int> Engine::resolve(Who who, int actor, int ally) const
{
    switch (who)
    {
    case Who::Actor: return {actor};
    case Who::Enemy: return {kEnemy};
    case Who::Ally:
        if (ally < 0)
            throw std::runtime_error("action targets an ally but has no allyTarget");
        return {ally};
    case Who::AllAllies:
    {
        std::vector<int> all;
        for (std::size_t i = 1; i <= m_enc.party.size(); ++i)
            all.push_back(static_cast<int>(i));
        return all;
    }
    }
    return {};
}

Duration Engine::resolve(const DurationSpec& d, int actor, int ally) const
{
    if (d.when == Expiry::Never)
        return {};
    int creature = actor;
    if (d.of == Who::Enemy)
        creature = kEnemy;
    else if (d.of == Who::Ally && ally >= 0)
        creature = ally;
    return Duration{d.when, creature};
}

Branches Engine::merge(Branches in) const
{
    if (!m_settings.mergeStates)
        return in;
    std::map<BattleState, double> merged;
    for (auto& [s, p] : in)
        merged[std::move(s)] += p;
    return Branches(std::make_move_iterator(merged.begin()), std::make_move_iterator(merged.end()));
}

Snapshot Engine::snapshot(const Branches& b, const std::string& who, int actor) const
{
    Snapshot snap;
    snap.beforeTurnOf = who;
    snap.branches = b.size();
    for (const auto& [s, p] : b)
    {
        for (const auto& [kind, cond] : s[kEnemy].conditions)
        {
            snap.enemyConditionChance[kind] += p;
            snap.enemyConditionMean[kind] += p * cond.value;
        }
        if (s.isOffGuardTo(kEnemy, actor, Tag::Melee))
            snap.enemyOffGuardChance += p;
    }
    return snap;
}

void Engine::enemyEndOfTurn(Branches& b, Ledger& ledger) const
{
    // Each tick after the first only happens if the previous DC 15 flat check failed (70%).
    double ticks = 0.0;
    for (int k = 0; k < m_settings.persistentTicks; ++k)
        ticks += std::pow(1.0 - flatCheckPassChance(15), k);

    for (auto& [s, p] : b)
    {
        for (const auto& [type, comp] : s[kEnemy].persistent)
        {
            double ev = expectedDamage({comp}, DamageScale::Normal, m_enc.enemy.defenses);
            ledger.damage[LedgerKey{"(persistent)", toString(type)}] += p * ev * ticks;
        }
        s.endTurn(kEnemy);
    }
    b = merge(std::move(b));
}
