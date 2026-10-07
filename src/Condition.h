#pragma once

#include "Modifier.h"

#include <vector>

enum class ConditionKind
{
    Frightened,
    Sickened,
    Clumsy,
    Enfeebled,
    Drained,
    Stupefied,
    OffGuard,
    Prone,
    Grabbed,
    Restrained,
    Concealed,  // attackers must pass a DC 5 flat check
    Hidden,     // attackers must pass a DC 11 flat check
    Dazzled,    // everything is concealed to this creature
    Slowed,
    Stunned,
    Quickened,
};

const char* toString(ConditionKind k);

// The modifiers a condition puts on the creature that has it. Most valued
// conditions are status penalties, so they don't stack with each other or with
// other status penalties (frightened 1 + clumsy 2 = -2 AC, not -3).
std::vector<Modifier> conditionModifiers(ConditionKind kind, int value);

// Change to the number of actions per turn (slowed/stunned/quickened).
int conditionActionDelta(ConditionKind kind, int value);
