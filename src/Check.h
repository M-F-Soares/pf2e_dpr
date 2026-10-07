#pragma once

#include <array>

enum class Degree
{
    CritFail = 0,
    Fail,
    Success,
    CritSuccess
};

constexpr int kDegrees = 4;
using DegreeDist = std::array<double, kDegrees>;

const char* toString(Degree d);

// Fortune: roll twice, keep higher. Misfortune: keep lower.
// Having both on the same roll cancels them out.
enum class Fortune
{
    None,
    Fortune,
    Misfortune
};

Fortune combineFortune(bool hasFortune, bool hasMisfortune);

// Probability of each d20 face (1..20) under the given fortune state.
double d20FaceProbability(int face, Fortune fortune);

// Degree of success for one check: total vs DC (+/-10 for crits), then a
// natural 20 raises one degree and a natural 1 lowers one degree.
Degree degreeFor(int naturalRoll, int modifier, int dc);

// Exact degree distribution of `d20 + modifier` against `dc`.
DegreeDist resolveCheck(int modifier, int dc, Fortune fortune = Fortune::None);

// Chance to pass a flat check (d20 >= dc, no modifiers).
double flatCheckPassChance(int dc);

// Shift every outcome by `steps` degrees (clamped). Used for incapacitation
// and "your successes become critical successes" style effects.
DegreeDist shiftDegrees(const DegreeDist& dist, int steps);

// Replace one degree by another (e.g. crit immunity: CritSuccess -> Success).
DegreeDist remapDegree(const DegreeDist& dist, Degree from, Degree to);
