#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

// Chooses how many DS frames the Nintendo Switch emulates per presented refresh batch (2..6)
// when fast-forwarding. Because OpenGL ES syncs SDL_RenderPresent to 60 Hz refreshes (~16.7 ms),
// a batch that overruns a refresh boundary loses an entire 16.7 ms refresh; picking the batch
// size and target refresh count (1..3, >= 20 FPS) from live frame timings maximises net speed.
struct SwitchFastForwardPacer
{
    int chosen = 4;
    int hold = 0;
    static constexpr int HoldBatches = 40;

    void Reset() { chosen = 4; hold = 0; }

    std::pair<int, int> Pick(double shownMs, double hiddenMs, double refreshMs, bool ahead, bool measured)
    {
        if (!measured || shownMs <= 0.0 || hiddenMs <= 0.0 || refreshMs <= 0.0)
            return { 4, 2 };

        const double overheadMs = ahead ? 3.5 : 5.5;
        auto score = [&](int n, double slack) -> std::pair<double, int> {
            const double total = (shownMs + (n - 1) * hiddenMs) * slack + overheadMs;
            const int refreshes = std::max(1, (int)std::ceil(total / refreshMs));
            return { refreshes <= 3 ? (double)n / refreshes : 0.0, refreshes };
        };

        int bestN = 2;
        double bestRate = 0.0;
        int bestRef = 4;
        for (int n = 2; n <= 6; n++)
        {
            const auto [rate, ref] = score(n, 1.05);
            if (rate > bestRate + 0.05 || (std::abs(rate - bestRate) <= 0.05 && ref < bestRef))
            {
                bestN = n;
                bestRate = rate;
                bestRef = ref;
            }
        }
        const auto [curRate, curRef] = score(chosen, 0.97);
        const auto [realRate, realRef] = score(chosen, 1.0);
        // A batch that no longer fits in 3 refreshes is left at once. Otherwise a change waits
        // for HoldBatches batches since the last one: a scene's cost moves a few ms from one
        // moment to the next, and following it every time made the speed jump up and down.
        if (hold > 0) hold--;
        if (curRate <= 0.0)
        {
            chosen = bestN;
            hold = HoldBatches;
        }
        else if (hold == 0 && chosen != bestN &&
                 (bestRate > curRate + 0.25 || (realRef > curRef && bestRate > realRate + 0.25) || bestRef < curRef))
        {
            chosen = bestN;
            hold = HoldBatches;
        }
        return { chosen, std::clamp(score(chosen, 1.0).second, 1, 3) };
    }
};
