// A small sampling profiler for development: SSR_SAMPLE=<file> interrupts whichever thread
// is using the CPU about 1000 times a second and records its call stack. tools/samples.py
// turns the file into a list of the functions the time went to.
#pragma once
#if !defined(__linux__)
// The sampler uses desktop Linux interfaces (SIGPROF, backtrace); elsewhere it does nothing.
namespace Sampler { inline void Start() {} inline void Stop() {} }
#else
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <link.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

namespace Sampler
{
enum { Depth = 12, MaxSamples = 400000 };
struct Sample { int tid; int n; void* pc[Depth]; };
inline Sample* Samples = nullptr;
inline volatile int Count = 0;
inline const char* Path = nullptr;

inline void OnSignal(int, siginfo_t*, void*)
{
    int i = __sync_fetch_and_add(&Count, 1);
    if (i >= MaxSamples) return;
    Sample& s = Samples[i];
    s.tid = (int)syscall(SYS_gettid);
    s.n = backtrace(s.pc, Depth);
}

inline void Start()
{
    Path = getenv("SSR_SAMPLE");
    if (!Path) return;
    Samples = (Sample*)calloc(MaxSamples, sizeof(Sample));
    void* prime[4];
    backtrace(prime, 4); // loads the unwinder outside the signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, nullptr);
    itimerval tv = { { 0, 1000 }, { 0, 1000 } };
    setitimer(ITIMER_PROF, &tv, nullptr);
}

inline void Stop()
{
    if (!Path) return;
    itimerval tv = { { 0, 0 }, { 0, 0 } };
    setitimer(ITIMER_PROF, &tv, nullptr);
    FILE* f = fopen(Path, "w");
    if (!f) return;
    uintptr_t base = _r_debug.r_map ? (uintptr_t)_r_debug.r_map->l_addr : 0;
    int n = Count < MaxSamples ? Count : MaxSamples;
    for (int i = 0; i < n; i++)
    {
        fprintf(f, "%d", Samples[i].tid);
        // The first two entries are the handler and the signal trampoline.
        for (int k = 2; k < Samples[i].n; k++) fprintf(f, " %lx", (unsigned long)((uintptr_t)Samples[i].pc[k] - base));
        fprintf(f, "\n");
    }
    fclose(f);
}
}
#endif
