#include "Dice.h"

#include <algorithm>

Dist Dist::constant(int value)
{
    Dist d;
    d.m_p = {{value, 1.0}};
    return d;
}

Dist Dist::die(int sides)
{
    Dist d;
    d.m_p.clear();
    for (int face = 1; face <= sides; ++face)
        d.m_p[face] = 1.0 / sides;
    return d;
}

Dist Dist::dice(int count, int sides)
{
    Dist d;
    for (int i = 0; i < count; ++i)
        d += die(sides);
    return d;
}

Dist Dist::operator+(const Dist& other) const
{
    Dist out;
    out.m_p.clear();
    for (auto [a, pa] : m_p)
        for (auto [b, pb] : other.m_p)
            out.m_p[a + b] += pa * pb;
    return out;
}

Dist Dist::operator+(int flat) const
{
    return map([flat](int x) { return x + flat; });
}

Dist Dist::map(const std::function<int(int)>& f) const
{
    Dist out;
    out.m_p.clear();
    for (auto [v, p] : m_p)
        out.m_p[f(v)] += p;
    return out;
}

Dist Dist::atLeast(int minimum) const
{
    return map([minimum](int x) { return std::max(x, minimum); });
}

double Dist::mean() const
{
    return expected([](int x) { return static_cast<double>(x); });
}

double Dist::expected(const std::function<double(int)>& f) const
{
    double sum = 0.0;
    for (auto [v, p] : m_p)
        sum += p * f(v);
    return sum;
}
