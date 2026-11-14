#include <cassert>
#include <cstdint>
#include <cstdio>

// Verifies the lockstep-safe mathematical identities introduced for Switch
// performance in SPU::Run (TimerInv reciprocal) and GpuCompositor (div255).

int main()
{
    // 1. SPUChannel::UpdateTimerInv / samplepos:
    // Both ref(dt) = (dt * 256) / denom and got(dt) = (dt * TimerInv) >> 32 are
    // monotonically non-decreasing in dt in [0, denom - 1]. Checking dt = 0,
    // every transition boundary ceil(k * denom / 256) - 1 and ceil(k * denom / 256),
    // and dt = denom - 1 proves bit-identical equality across all ~2.15 billion
    // valid (TimerReload, Timer) states.
    uint64_t boundaryChecks = 0;
    for (uint32_t reload = 0; reload <= 0xFFFF; reload++)
    {
        const uint64_t denom = 0x10000ULL - reload;
        const uint64_t inv = ((0x100ULL << 32) + denom - 1) / denom;
        assert((((0ULL * inv) >> 32) == 0ULL));
        boundaryChecks++;

        for (uint32_t k = 1; k <= 0x100; k++)
        {
            const uint64_t b = (k * denom + 255ULL) / 256ULL;
            for (int d = -1; d <= 0; d++)
            {
                const int64_t dt = (int64_t)b + d;
                if (dt < 0 || (uint64_t)dt >= denom) continue;
                const uint64_t ref = ((uint64_t)dt * 0x100ULL) / denom;
                const uint64_t got = ((uint64_t)dt * inv) >> 32;
                assert(ref == got);
                boundaryChecks++;
            }
        }
        const uint64_t lastDt = denom - 1;
        assert(((lastDt * 0x100ULL) / denom) == ((lastDt * inv) >> 32));
        boundaryChecks++;
    }

    // 2. GpuCompositor div255(a * b):
    // Exhaustive verification over all 65,536 (a, b) in [0..255]^2.
    uint32_t divChecks = 0;
    for (uint32_t a = 0; a < 256; a++)
    {
        for (uint32_t b = 0; b < 256; b++)
        {
            const uint32_t val = a * b;
            const uint32_t ref = val / 255u;
            const uint32_t got = (val + 1u + (val >> 8)) >> 8;
            assert(ref == got);
            divChecks++;
        }
    }

    std::printf("Switch perf math OK: %llu SPU boundary points + %u div255 pairs\n",
                (unsigned long long)boundaryChecks, divChecks);
    return 0;
}
