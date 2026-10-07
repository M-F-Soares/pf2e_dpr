#pragma once

#include "Action.h"

#include <compare>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct Settings
{
    // How many end-of-turn ticks of persistent damage to count (each tick after
    // the first happens only if the DC 15 recovery check failed). 1 = only the
    // tick at the end of the enemy's next turn.
    int persistentTicks = 1;
    bool mergeStates = true;  // merge identical branches (never changes results, only speed)
};

struct LedgerKey
{
    std::string who;
    std::string what;
    auto operator<=>(const LedgerKey&) const = default;
};

// Expected values, accumulated across all branches (linearity of expectation).
struct Ledger
{
    std::map<LedgerKey, double> damage;
    std::map<LedgerKey, double> healRaw;
    std::map<LedgerKey, double> healEffective;  // capped by the target's missing HP
    std::map<LedgerKey, double> skipped;        // probability the action couldn't be taken

    double totalDamage() const;
    double totalHealRaw() const;
    double totalHealEffective() const;
    double damageBy(const std::string& who) const;
};

struct Turn
{
    std::string actor;
    std::vector<Step> steps;
};

// Probability picture of the enemy right before someone's turn.
struct Snapshot
{
    std::string beforeTurnOf;
    std::map<ConditionKind, double> enemyConditionChance;
    std::map<ConditionKind, double> enemyConditionMean;
    double enemyOffGuardChance = 0.0;  // off-guard to this actor's melee attacks
    std::size_t branches = 0;
};

using Branches = std::vector<std::pair<BattleState, double>>;

struct RunResult
{
    std::string name;
    Ledger ledger;
    std::vector<Snapshot> snapshots;
    Branches finalStates;
    std::size_t peakBranches = 0;
};

class Engine
{
public:
    Engine(const Encounter& enc, Settings settings) : m_enc(enc), m_settings(settings) {}

    RunResult run(const std::string& name, const BattleState& initial, const std::vector<Turn>& turns);

    // Runs one action on one branch and returns the resulting branches.
    Branches execute(const Action& action, const BattleState& state, double p, int actor, int turnOwner,
                     Ledger& ledger) const;

private:
    // One outcome being resolved: its state, probability, and the damage rolls
    // that land on the enemy as one instance.
    struct OutcomeBranch
    {
        BattleState state;
        double p = 0.0;
        std::vector<DamageRoll> rolls;
    };

    void applyOutcome(const Action& a, Degree degree, OutcomeBranch& b, const BattleState& before, int actor,
                      int ally, const LedgerKey& key, Ledger& ledger) const;
    std::vector<OutcomeBranch> resolveCoupled(const Action& a, Degree degree, OutcomeBranch base,
                                              const BattleState& before, int actor, int turnOwner, int ally,
                                              const LedgerKey& key, Ledger& ledger) const;
    std::vector<DamageComponent> damageComponents(const Action& a, DamageScale scale, EffectCtx& ctx) const;
    void addPersistent(const DamageRoll& roll, BattleState& state) const;
    Branches heal(const HealSpec& h, BattleState state, double p, int actor, int ally, const LedgerKey& key,
                  Ledger& ledger) const;
    bool immuneToTraits(const Action& a) const;

    Branches merge(Branches in) const;
    Snapshot snapshot(const Branches& b, const std::string& who, int actor) const;
    void enemyEndOfTurn(Branches& b, Ledger& ledger) const;
    std::vector<int> resolve(Who who, int actor, int ally) const;
    Duration resolve(const DurationSpec& d, int actor, int ally) const;
    DegreeDist roll(const Action& a, const BattleState& before, BattleState& after, int actor, int turnOwner,
                    int ally) const;

    const Encounter& m_enc;
    Settings m_settings;
};
