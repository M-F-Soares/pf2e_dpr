// "Is it better for the ranger to Strike 3 times, or to spend actions setting
// up the rest of the party?" -- one full round against a sample level 5 brute.

#include "../src/Scenario.h"
#include "../src/content/Actions.h"
#include "../src/content/ExampleParty.h"

int main()
{
    Character ranger = exampleRanger();
    Character bard = exampleBard();
    Character rogue = exampleRogue();
    Monster brute = exampleBrute();
    const std::string bow = "Composite Longbow";

    // Ranger hunted the brute in an earlier round.
    Scenario base("Ranger 3 Strikes");
    base.against(brute)
        .counter(ranger, "prey")
        .turn(ranger, {strike(ranger, bow), strike(ranger, bow), strike(ranger, bow)})
        .turn(bard, {courageousAnthem(bard), heroism(bard, rogue)})
        .turn(rogue, {strike(rogue, "Rapier"), strike(rogue, "Rapier"), strike(rogue, "Rapier")});

    Scenario demoralizeFirst = base.variant("Ranger Demoralize + 2 Strikes")
                                   .replaceTurn(ranger, {demoralize(ranger), strike(ranger, bow), strike(ranger, bow)});

    Scenario rogueFeints = base.variant("Rogue Feint + 2 Strikes")
                               .replaceTurn(rogue, {feint(rogue), strike(rogue, "Rapier"), strike(rogue, "Rapier")});

    Scenario bardFears = base.variant("Bard Fear + Anthem (no Heroism)")
                             .replaceTurn(bard, {courageousAnthem(bard), fear(bard)});

    // Adaptive: the ranger tries to Trip; the rogue only Feints in the branches where
    // the brute isn't already off-guard (prone), otherwise Strikes three times.
    Scenario tripAndAdapt = base.variant("Ranger Trip, Rogue Feints if needed")
                                .replaceTurn(ranger, {trip(ranger), strike(ranger, bow), strike(ranger, bow)})
                                .replaceTurn(rogue, {choose([&](const View& v) {
                                                         return v.enemyOffGuardToMe() ? strike(rogue, "Rapier")
                                                                                      : feint(rogue);
                                                     }),
                                                     strike(rogue, "Rapier"), strike(rogue, "Rapier")});

    compare({base, demoralizeFirst, rogueFeints, bardFears, tripAndAdapt});
    return 0;
}
