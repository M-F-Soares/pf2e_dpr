/**
 * @file    main.cpp
 * @brief   Example program (`make run`).
 * @details "Is it better for the ranger to Strike 3 times, or to spend actions setting up the rest of the
 *          party?" -- one full round against a sample level 5 brute.
 */

#include "ContentLibrary.h"
#include "ExampleParty.h"
#include "Scenario.h"

#include <exception>
#include <iostream>

/**
 * @brief   Compares five plans for the sample party's round and prints the reports and the ranking.
 * @param   [in] content - The actions, spells and features to use.
 * @throws  ContentError if @p content lacks something the plans use.
 */
void RunExample(const ContentLibrary& content)
{
    Character ranger = ExampleRanger(content);
    Character bard = ExampleBard();
    Character rogue = ExampleRogue(content);
    Monster brute = ExampleBrute();
    ActionPtr bowStrike = content.Use("Strike", ranger, {.weapon = "Composite Longbow"});
    ActionPtr rapierStrike = content.Use("Strike", rogue, {.weapon = "Rapier"});
    ActionPtr anthem = content.Use("Courageous Anthem", bard);
    ActionPtr heroism = content.Use("Heroism", bard, {.ally = &rogue});
    ActionPtr fear = content.Use("Fear", bard);
    ActionPtr demoralize = content.Use("Demoralize", ranger);
    ActionPtr trip = content.Use("Trip", ranger);
    ActionPtr feint = content.Use("Feint", rogue);

    // Ranger hunted the brute in an earlier round.
    Scenario base("Ranger 3 Strikes");
    base.Against(brute)
        .Counter(ranger, "prey")
        .AddTurn(ranger, {bowStrike, bowStrike, bowStrike})
        .AddTurn(bard, {anthem, heroism})
        .AddTurn(rogue, {rapierStrike, rapierStrike, rapierStrike});

    Scenario demoralizeFirst =
        base.Variant("Ranger Demoralize + 2 Strikes").ReplaceTurn(ranger, {demoralize, bowStrike, bowStrike});

    Scenario rogueFeints =
        base.Variant("Rogue Feint + 2 Strikes").ReplaceTurn(rogue, {feint, rapierStrike, rapierStrike});

    Scenario bardFears = base.Variant("Bard Fear + Anthem (no Heroism)").ReplaceTurn(bard, {anthem, fear});

    // Adaptive: the ranger tries to Trip; the rogue only Feints in the branches where
    // the brute isn't already off-guard (prone), otherwise Strikes three times.
    Scenario tripAndAdapt =
        base.Variant("Ranger Trip, Rogue Feints if needed")
            .ReplaceTurn(ranger, {trip, bowStrike, bowStrike})
            .ReplaceTurn(rogue, {Choose([&](const View& view) {
                                     return view.EnemyOffGuardToMe() ? rapierStrike : feint;
                                 }),
                                 rapierStrike, rapierStrike});

    Compare({base, demoralizeFirst, rogueFeints, bardFears, tripAndAdapt});
}

/**
 * @brief   Loads the content and runs the example.
 * @param   [in] argc - Number of command-line arguments.
 * @param   [in] argv - The arguments; the first one, if given, is the content directory (default "data").
 * @return  0, or 1 if the content can't be loaded or lacks something the example uses.
 */
int main(int argc, char* argv[])
{
    try
    {
        RunExample(ContentLibrary::Load(argc > 1 ? argv[1] : "data"));
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
