// ssrecomp: where helper threads (the 3D render thread, the widescreen drawing threads) run.
//
// On the Nintendo Switch a new thread stays on the process's default core, core 0, which is
// where the emulation itself runs, unless it moves itself: Horizon does not spread threads
// over the cores as a desktop system does. So each helper thread calls this first and moves
// to cores 1 and 2 (core 3 belongs to the system).
//
// Horizon also does not share a core between threads of the same priority by time: one runs
// until it waits. The emulation waits for the DS-resolution 3D thread line by line, so with
// the JIT (which leaves the emulation waiting for it 3-9 ms a frame when it queued behind a
// widescreen thread) that thread has core 1 to itself at a higher priority than the
// widescreen threads, which take cores 2 and 1 and give way to it.
// The widescreen threads may also use core 0, at the lowest priority there is: while the
// emulation waits (for the DS-resolution 3D or for the widescreen picture itself) core 0 has
// nothing else to do, and Horizon moves a thread that is ready but kept waiting on its own
// core (behind the 3D thread on core 1, or the main thread drawing on core 2) to a core that
// sits idle. A third widescreen thread starts on core 0. "wide_core0=0" in ssrecomp.cfg keeps
// them off it (SSR_WIDE_CORE0=0), for comparing.
// Elsewhere this does nothing.
#pragma once

#ifdef __SWITCH__
#include <atomic>
#include <cstdint>
#include <cstdio>
// From libnx (svc.h); declared here so that this header does not pull in all of <switch.h>,
// whose short type names and macros (BIT, ALIGN, ...) the emulator code does not expect.
extern "C" uint32_t svcSetThreadCoreMask(uint32_t handle, int32_t core_id, uint32_t affinity_mask);
extern "C" uint32_t svcGetCurrentProcessorNumber(void);
extern "C" uint32_t svcSetThreadPriority(uint32_t handle, uint32_t priority);
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>

// ssrecomp: for the timing lines in the log: how long the emulation waited for the helper
// threads, and on which core each started its frames (Switch only).
struct SsrThreadStats
{
    std::atomic<uint64_t> Wait3DNs{0}, WaitWideNs{0};
    std::atomic<uint32_t> RenderCore[4]{}, WideCore[4]{};
};
inline SsrThreadStats& SsrStats()
{
    static SsrThreadStats s;
    return s;
}
// Adds the time from its creation to its end to a counter.
struct SsrWaitTimer
{
    std::atomic<uint64_t>& To;
    std::chrono::steady_clock::time_point T0 = std::chrono::steady_clock::now();
    explicit SsrWaitTimer(std::atomic<uint64_t>& to) : To(to) {}
    ~SsrWaitTimer()
    {
        To += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - T0).count();
    }
};
inline void SsrNoteCore(bool render)
{
#ifdef __SWITCH__
    const uint32_t core = svcGetCurrentProcessorNumber() & 3;
    (render ? SsrStats().RenderCore : SsrStats().WideCore)[core]++;
#else
    (void)render;
#endif
}

// Whether the widescreen threads may use core 0 (the Switch; see the top of this file).
inline bool SsrWideCore0()
{
    static const bool on = [] {
        const char* v = getenv("SSR_WIDE_CORE0");
        return !(v && v[0] == '0');
    }();
    return on;
}

// role: 0 for the DS-resolution 3D thread, 1, 2, ... for the widescreen drawing threads, -1
// for the thread that builds the wide view's polygon lists (GPU3D_Wide.cpp, ListHelper), -2 for
// the one that sends the pictures to the graphics chip (GPU3D_WideGpu.cpp, WideGpuSend).
inline void SsrHelperThreadStart(int role)
{
#ifdef __SWITCH__
    const uint32_t currentThread = 0xFFFF8000; // libnx's CUR_THREAD_HANDLE
    if (role == -2)
    {
        // The thread that sends the pictures to the graphics chip (GPU3D_WideGpu.h): cores 1
        // and 2, at the main thread's priority. (Core 1's DS-resolution 3D thread is idle while
        // the graphics chip draws the 3D.)
        const uint32_t rc = svcSetThreadCoreMask(currentThread, 1, (1u << 1) | (1u << 2));
        const uint32_t prc = svcSetThreadPriority(currentThread, 0x2C);
        fprintf(stderr, "[switch] graphics chip sending thread on core 1: 0x%x 0x%x\n", (unsigned)rc, (unsigned)prc);
        return;
    }
    if (role < 0)
    {
        // Core 2 (or 1 when 2 is busy), at the main thread's priority: what it builds is
        // waited for 23 lines after it starts, and starting on core 2 avoids contending with
        // the graphics chip sending thread on core 1 during the VBlank burst.
        const uint32_t rc = svcSetThreadCoreMask(currentThread, 2, (1u << 1) | (1u << 2));
        const uint32_t prc = svcSetThreadPriority(currentThread, 0x2C);
        fprintf(stderr, "[switch] polygon list thread on core 2: 0x%x 0x%x\n", (unsigned)rc, (unsigned)prc);
        return;
    }
    // The 3D thread: core 1 only, at the main thread's priority (0x2C; new threads get 0x3B).
    // Widescreen threads (0x3B): the first starts on core 2, the next on core 1, a third on
    // core 0; all may move between cores 1 and 2, and to core 0 unless SsrWideCore0() is off.
    static const int start[3] = { 0, 2, 1 };
    const int core = role == 0 ? 1 : start[role % 3];
    const uint32_t mask = role == 0 ? (1u << 1)
        : (1u << 1) | (1u << 2) | (SsrWideCore0() ? 1u : 0u);
    const uint32_t rc = svcSetThreadCoreMask(currentThread, core, mask);
    if (rc != 0)
        fprintf(stderr, "[switch] a helper thread could not move to core %d (0x%x); it shares core 0\n", core, (unsigned)rc);
    else
        fprintf(stderr, "[switch] %s on core %d\n", role == 0 ? "3D thread" : "widescreen thread", core);
    if (role == 0)
    {
        const uint32_t prc = svcSetThreadPriority(currentThread, 0x2C);
        if (prc != 0) fprintf(stderr, "[switch] the 3D thread's priority could not be raised (0x%x)\n", (unsigned)prc);
    }
#else
    (void)role;
#endif
}
