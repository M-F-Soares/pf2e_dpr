/**
 * @file    Action.cpp
 * @brief   Lookups on the creatures of an encounter.
 */

#include "Action.h"

#include <stdexcept>

int Encounter::indexOf(const std::string& name) const
{
    for (std::size_t i = 0; i < party.size(); ++i)
        if (party[i].name == name)
            return static_cast<int>(i) + 1;
    throw std::runtime_error("no party member named " + name);
}
