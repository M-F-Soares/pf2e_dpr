/**
 * @file    Check.h
 * @brief   Degrees of success and exact d20 check distributions.
 * @details Checks are resolved exactly over the faces of the d20 (or of 2d20 for fortune and misfortune), so
 *          a check yields the probability of each degree, never a sample.
 */

#pragma once

#include <array>

/**
 * @brief   Degree of success of a check, from worst to best.
 * @details The underlying values index DegreeDist and Action::outcomes.
 */
enum class Degree
{
    CritFail = 0,  /**< Critical failure. */
    Fail,          /**< Failure. */
    Success,       /**< Success. */
    CritSuccess    /**< Critical success. */
};

/**
 * @brief   Number of degrees of success (the size of a DegreeDist).
 */
constexpr int kDegrees = 4;

/**
 * @brief   Probability of each Degree, indexed by its underlying value.
 * @details The entries can sum to less than 1; the rest is the chance the check had no effect (immunity, a
 *          failed flat check, ...).
 */
using DegreeDist = std::array<double, kDegrees>;

/**
 * @brief   Name of a degree for reports ("crit fail", "fail", "success", "crit success").
 * @param   [in] degree - The degree.
 * @return  A static string; "?" for an out-of-range value.
 */
const char* ToString(Degree degree);

/**
 * @brief   Fortune state of a d20 roll.
 * @details Fortune: roll twice, keep higher. Misfortune: keep lower. Having both on the same roll cancels
 *          them out.
 */
enum class Fortune
{
    None,       /**< Roll once. */
    Fortune,    /**< Roll twice and keep the higher result. */
    Misfortune  /**< Roll twice and keep the lower result. */
};

/**
 * @brief   Fortune state of a roll that may have fortune and misfortune effects.
 * @param   [in] hasFortune - Whether a fortune effect applies to the roll.
 * @param   [in] hasMisfortune - Whether a misfortune effect applies to the roll.
 * @return  Fortune::None when both or neither apply, otherwise the one that applies.
 */
Fortune CombineFortune(bool hasFortune, bool hasMisfortune);

/**
 * @brief   Probability of each d20 face (1..20) under the given fortune state.
 * @param   [in] face - The natural result, from 1 to 20.
 * @param   [in] fortune - Whether the d20 is rolled once, or twice keeping the higher or lower result.
 * @return  The probability that the natural result is @p face.
 */
double D20FaceProbability(int face, Fortune fortune);

/**
 * @brief   Degree of success for one check.
 * @details Total vs DC (+/-10 for crits), then a natural 20 raises one degree and a natural 1 lowers one
 *          degree.
 * @param   [in] naturalRoll - The number shown on the d20, from 1 to 20.
 * @param   [in] modifier - Total modifier added to the roll.
 * @param   [in] dc - The DC to meet or beat.
 * @return  The degree of success.
 */
Degree DegreeFor(int naturalRoll, int modifier, int dc);

/**
 * @brief   Exact degree distribution of `d20 + modifier` against `dc`.
 * @param   [in] modifier - Total modifier added to the roll.
 * @param   [in] dc - The DC to meet or beat.
 * @param   [in] fortune - Fortune state of the roll.
 * @return  The probability of each degree; the entries sum to 1.
 */
DegreeDist ResolveCheck(int modifier, int dc, Fortune fortune = Fortune::None);

/**
 * @brief   Chance to pass a flat check (d20 >= dc, no modifiers).
 * @param   [in] dc - The flat check's DC (5 for concealed, 11 for hidden, ...).
 * @return  The pass chance, clamped to [0, 1].
 */
double FlatCheckPassChance(int dc);

/**
 * @brief   Shift every outcome by `steps` degrees (clamped).
 * @details Used for incapacitation and "your successes become critical successes" style effects.
 * @param   [in] dist - The distribution to shift.
 * @param   [in] steps - Degrees to move each outcome; positive is better, negative is worse.
 * @return  The shifted distribution. Outcomes pushed past either end pile up there.
 */
DegreeDist ShiftDegrees(const DegreeDist& dist, int steps);

/**
 * @brief   Replace one degree by another (e.g. crit immunity: CritSuccess -> Success).
 * @param   [in] dist - The distribution to change.
 * @param   [in] from - The degree that can no longer happen.
 * @param   [in] to - The degree that gets its probability.
 * @return  The new distribution.
 */
DegreeDist RemapDegree(const DegreeDist& dist, Degree from, Degree to);
