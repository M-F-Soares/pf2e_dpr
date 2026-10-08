/**
 * @file    Check.cpp
 * @brief   Degree of success rules and exact d20 distributions.
 */

#include "Check.h"

#include <algorithm>

const char* ToString(Degree degree)
{
    switch (degree)
    {
    case Degree::CritFail: return "crit fail";
    case Degree::Fail: return "fail";
    case Degree::Success: return "success";
    case Degree::CritSuccess: return "crit success";
    }
    return "?";
}

Fortune CombineFortune(bool hasFortune, bool hasMisfortune)
{
    if (hasFortune == hasMisfortune)
        return Fortune::None;
    return hasFortune ? Fortune::Fortune : Fortune::Misfortune;
}

double D20FaceProbability(int face, Fortune fortune)
{
    switch (fortune)
    {
    case Fortune::None: return 1.0 / 20.0;
    case Fortune::Fortune: return (2.0 * face - 1.0) / 400.0;      // P(max of 2d20 = face)
    case Fortune::Misfortune: return (41.0 - 2.0 * face) / 400.0;  // P(min of 2d20 = face)
    }
    return 0.0;
}

Degree DegreeFor(int naturalRoll, int modifier, int dc)
{
    int total = naturalRoll + modifier;
    int degree;
    if (total >= dc + 10)
        degree = static_cast<int>(Degree::CritSuccess);
    else if (total >= dc)
        degree = static_cast<int>(Degree::Success);
    else if (total <= dc - 10)
        degree = static_cast<int>(Degree::CritFail);
    else
        degree = static_cast<int>(Degree::Fail);

    if (naturalRoll == 20)
        degree = std::min(degree + 1, static_cast<int>(Degree::CritSuccess));
    else if (naturalRoll == 1)
        degree = std::max(degree - 1, static_cast<int>(Degree::CritFail));
    return static_cast<Degree>(degree);
}

DegreeDist ResolveCheck(int modifier, int dc, Fortune fortune)
{
    DegreeDist dist{};
    for (int face = 1; face <= 20; ++face)
        dist[static_cast<int>(DegreeFor(face, modifier, dc))] += D20FaceProbability(face, fortune);
    return dist;
}

double FlatCheckPassChance(int dc)
{
    return std::clamp((21.0 - dc) / 20.0, 0.0, 1.0);
}

DegreeDist ShiftDegrees(const DegreeDist& dist, int steps)
{
    DegreeDist out{};
    for (int i = 0; i < kDegrees; ++i)
        out[std::clamp(i + steps, 0, kDegrees - 1)] += dist[i];
    return out;
}

DegreeDist RemapDegree(const DegreeDist& dist, Degree from, Degree to)
{
    DegreeDist out = dist;
    out[static_cast<int>(to)] += out[static_cast<int>(from)];
    out[static_cast<int>(from)] = 0.0;
    return out;
}
