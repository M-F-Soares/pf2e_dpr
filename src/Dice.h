/**
 * @file    Dice.h
 * @brief   Exact discrete probability distributions for dice rolls.
 */

#pragma once

#include <map>
#include <functional>

/**
 * @brief   Discrete probability distribution over integer values (damage, healing, ...).
 * @details Kept exact so things like resistance (max(0, X - r)) are computed correctly instead of being
 *          applied to the average.
 */
class Dist
{
public:
    /**
     * @brief   A distribution that is always 0.
     */
    Dist() : m_p{{0, 1.0}} {}

    /**
     * @brief   A distribution that is always @p value.
     * @param   [in] value - The only possible value.
     * @return  The distribution.
     */
    static Dist constant(int value);

    /**
     * @brief   One fair die.
     * @param   [in] sides - Number of faces, at least 1.
     * @return  Values 1 to @p sides, each equally likely.
     */
    static Dist die(int sides);

    /**
     * @brief   The sum of several identical dice, e.g. `dice(2, 6)` for 2d6.
     * @param   [in] count - Number of dice; 0 gives a constant 0.
     * @param   [in] sides - Number of faces on each die.
     * @return  The exact distribution of the sum.
     */
    static Dist dice(int count, int sides);

    /**
     * @brief   Convolution (sum of independent rolls).
     * @param   [in] other - The other roll.
     * @return  The distribution of this roll plus @p other.
     */
    Dist operator+(const Dist& other) const;

    /**
     * @brief   Adds a flat amount to every value.
     * @param   [in] flat - The amount to add.
     * @return  The shifted distribution.
     */
    Dist operator+(int flat) const;

    /**
     * @brief   Adds an independent roll to this one.
     * @param   [in] other - The other roll.
     * @return  This distribution.
     */
    Dist& operator+=(const Dist& other) { return *this = *this + other; }

    /**
     * @brief   Adds a flat amount to every value.
     * @param   [in] flat - The amount to add.
     * @return  This distribution.
     */
    Dist& operator+=(int flat) { return *this = *this + flat; }

    /**
     * @brief   Distribution of `f(X)`.
     * @details Values that @p f maps to the same result are combined.
     * @param   [in] f - Function applied to each value.
     * @return  The transformed distribution.
     */
    Dist map(const std::function<int(int)>& f) const;

    /**
     * @brief   Every value doubled (critical hit).
     * @return  The doubled distribution.
     */
    Dist doubled() const { return map([](int x) { return x * 2; }); }

    /**
     * @brief   Every value halved, rounded down for non-negative values (basic save success).
     * @return  The halved distribution.
     */
    Dist halved() const { return map([](int x) { return x / 2; }); }

    /**
     * @brief   Every value raised to at least @p minimum.
     * @param   [in] minimum - The lowest possible value afterwards.
     * @return  The clamped distribution.
     */
    Dist atLeast(int minimum) const;

    /**
     * @brief   Expected value.
     * @return  The mean of the distribution.
     */
    double mean() const;

    /**
     * @brief   Expected value of `f(X)`.
     * @param   [in] f - Function applied to each value.
     * @return  The probability-weighted sum of @p f over the values.
     */
    double expected(const std::function<double(int)>& f) const;

    /**
     * @brief   The underlying probabilities.
     * @return  A map from each possible value to its probability.
     */
    const std::map<int, double>& values() const { return m_p; }

private:
    std::map<int, double> m_p;  /**< Probability of each value; missing values have probability 0. */
};
