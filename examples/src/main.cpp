/**
 * @file    main.cpp
 * @brief   Example program (`make run`).
 * @details "Is it better for the ranger to Strike 3 times, or to spend actions setting up the rest of the
 *          party?" -- one full round against a sample level 5 brute.
 */

#include "Scenario.h"
#include "Actions.h"
#include "ExampleParty.h"

/**
 * @brief   Compares five plans for the sample party's round and prints the reports and the ranking.
 * @return  0.
 */
int main()
{
    Character ranger = ExampleRanger();
    Character bard = ExampleBard();
    Character rogue = ExampleRogue();
    Monster brute = ExampleBrute();
    const std::string bow = "Composite Longbow";

    // Ranger hunted the brute in an earlier round.
    Scenario base("Ranger 3 Strikes");
    base.Against(brute)
        .Counter(ranger, "prey")
        .AddTurn(ranger, {Strike(ranger, bow), Strike(ranger, bow), Strike(ranger, bow)})
        .AddTurn(bard, {CourageousAnthem(bard), Heroism(bard, rogue)})
        .AddTurn(rogue, {Strike(rogue, "Rapier"), Strike(rogue, "Rapier"), Strike(rogue, "Rapier")});

    Scenario demoralizeFirst = base.Variant("Ranger Demoralize + 2 Strikes")
                                   .ReplaceTurn(ranger, {Demoralize(ranger), Strike(ranger, bow), Strike(ranger, bow)});

    Scenario rogueFeints = base.Variant("Rogue Feint + 2 Strikes")
                               .ReplaceTurn(rogue, {Feint(rogue), Strike(rogue, "Rapier"), Strike(rogue, "Rapier")});

    Scenario bardFears = base.Variant("Bard Fear + Anthem (no Heroism)")
                             .ReplaceTurn(bard, {CourageousAnthem(bard), Fear(bard)});

    // Adaptive: the ranger tries to Trip; the rogue only Feints in the branches where
    // the brute isn't already off-guard (prone), otherwise Strikes three times.
    Scenario tripAndAdapt = base.Variant("Ranger Trip, Rogue Feints if needed")
                                .ReplaceTurn(ranger, {Trip(ranger), Strike(ranger, bow), Strike(ranger, bow)})
                                .ReplaceTurn(rogue, {Choose([&](const View& view) {
                                                         return view.EnemyOffGuardToMe() ? Strike(rogue, "Rapier")
                                                                                         : Feint(rogue);
                                                     }),
                                                     Strike(rogue, "Rapier"), Strike(rogue, "Rapier")});

    Compare({base, demoralizeFirst, rogueFeints, bardFears, tripAndAdapt});
    return 0;
}
