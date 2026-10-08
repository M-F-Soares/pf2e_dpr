/**
 * @file    Dice.cpp
 * @brief   Construction and arithmetic of exact dice distributions.
 */

#include "Dice.h"

#include <algorithm>

Dist Dist::Constant(int value)
{
    Dist result;
    result.m_probabilities = {{value, 1.0}};
    return result;
}

Dist Dist::Die(int sides)
{
    Dist result;
    result.m_probabilities.clear();
    for (int face = 1; face <= sides; ++face)
        result.m_probabilities[face] = 1.0 / sides;
    return result;
}

Dist Dist::Dice(int count, int sides)
{
    Dist result;
    for (int i = 0; i < count; ++i)
        result += Die(sides);
    return result;
}

Dist Dist::operator+(const Dist& other) const
{
    Dist out;
    out.m_probabilities.clear();
    for (auto [value, probability] : m_probabilities)
        for (auto [otherValue, otherProbability] : other.m_probabilities)
            out.m_probabilities[value + otherValue] += probability * otherProbability;
    return out;
}

Dist Dist::operator+(int flat) const
{
    return Map([flat](int value) { return value + flat; });
}

Dist Dist::Map(const std::function<int(int)>& transform) const
{
    Dist out;
    out.m_probabilities.clear();
    for (auto [value, probability] : m_probabilities)
        out.m_probabilities[transform(value)] += probability;
    return out;
}

Dist Dist::AtLeast(int minimum) const
{
    return Map([minimum](int value) { return std::max(value, minimum); });
}

double Dist::Mean() const
{
    return Expected([](int value) { return static_cast<double>(value); });
}

double Dist::Expected(const std::function<double(int)>& transform) const
{
    double sum = 0.0;
    for (auto [value, probability] : m_probabilities)
        sum += probability * transform(value);
    return sum;
}
