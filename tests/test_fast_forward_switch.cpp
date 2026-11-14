#include "switch_ff_pacing.h"
#include <cassert>
#include <cstdio>

int main()
{
    SwitchFastForwardPacer pacer;

    // Startup / unmeasured frames start cleanly at 2x (4 DS frames per 2 refreshes).
    auto [n0, r0] = pacer.Pick(0.0, 0.0, 16.68, true, false);
    assert(n0 == 4 && r0 == 2);

    // Boosted handheld overworld (7.5 ms shown, 4.3 ms hidden): picks 5 frames / 2 refreshes (2.5x @ 30 Hz).
    auto [n1, r1] = pacer.Pick(7.5, 4.3, 16.68, true, true);
    assert(n1 == 5 && r1 == 2 && (double)n1 / r1 >= 2.0);

    // Light interior / battle scene (5.8 ms shown, 3.2 ms hidden): picks 3 frames / 1 refresh (3.0x @ 60 Hz).
    pacer.Reset();
    auto [n2, r2] = pacer.Pick(5.8, 3.2, 16.68, true, true);
    assert(n2 == 3 && r2 == 1 && (double)n2 / r2 >= 2.0);

    // Heavy boosted scene where 4 still makes 2 refreshes (32.1 ms -> 2.0x @ 30 Hz),
    // and heavier scene where 4 crosses 2 refreshes so pacer moves to 6 frames / 3 refreshes (2.0x @ 20 Hz).
    pacer.Reset();
    auto [n3a, r3a] = pacer.Pick(10.0, 6.2, 16.68, true, true);
    assert(n3a == 4 && r3a == 2 && (double)n3a / r3a >= 2.0);
    pacer.Reset();
    auto [n3, r3] = pacer.Pick(11.5, 6.2, 16.68, true, true);
    assert(n3 == 6 && r3 == 3 && (double)n3 / r3 >= 2.0);

    // Extreme stall backs down safely to 2 instead of leaving chosen at 4.
    auto [n4, r4] = pacer.Pick(30.0, 20.0, 16.68, false, true);
    assert(n4 == 2 && r4 <= 3);

    // Hysteresis: minor timing jitter keeps the active batch steady without oscillating.
    pacer.Reset();
    auto [na, ra] = pacer.Pick(7.5, 4.3, 16.68, true, true);
    auto [nb, rb] = pacer.Pick(7.7, 4.4, 16.68, true, true);
    assert(na == nb && ra == rb);

    puts("Switch FF pacer: >=2.0x batch selection and hysteresis verified");
    return 0;
}
