#pragma once

#include <map>
#include <functional>

// Discrete probability distribution over integer values (damage, healing, ...).
// Kept exact so things like resistance (max(0, X - r)) are computed correctly
// instead of being applied to the average.
class Dist
{
public:
    Dist() : m_p{{0, 1.0}} {}

    static Dist constant(int value);
    static Dist die(int sides);
    static Dist dice(int count, int sides);

    Dist operator+(const Dist& other) const;   // convolution (sum of independent rolls)
    Dist operator+(int flat) const;
    Dist& operator+=(const Dist& other) { return *this = *this + other; }
    Dist& operator+=(int flat) { return *this = *this + flat; }

    Dist map(const std::function<int(int)>& f) const;
    Dist doubled() const { return map([](int x) { return x * 2; }); }
    Dist halved() const { return map([](int x) { return x / 2; }); }
    Dist atLeast(int minimum) const;

    double mean() const;
    double expected(const std::function<double(int)>& f) const;
    const std::map<int, double>& values() const { return m_p; }

private:
    std::map<int, double> m_p;
};
