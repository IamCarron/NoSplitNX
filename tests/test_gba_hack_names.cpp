#include "hack_names.h"
#include <cassert>
#include <cstdio>

int main()
{
    const HackNames::Hack* rr = HackNames::ForPatch("radicalred");
    assert(rr && HackNames::ForPatch("unbound") && !HackNames::ForPatch("firered") && !HackNames::ForPatch(""));
    assert(HackNames::IsThatHack(*rr, "0", "Radical Red 4.1.gba"));
    assert(HackNames::IsThatHack(*rr, "0", "radicalred.ips"));
    assert(HackNames::IsThatHack(*rr, "964f951a0fdaf209e4ea1344883ef0d557bb3a80", "rr.gba"));
    // Another FireRed-engine hack that fits Radical Red's patch keeps its own name and cover.
    assert(!HackNames::IsThatHack(*rr, "1111111111111111111111111111111111111111", "FireRed Ultimate.gba"));
    assert(!HackNames::IsThatHack(*rr, "0", "Pokemon Fire Red Plus.gba"));
    const HackNames::Hack* mono = HackNames::ForPatch("monomyth");
    assert(HackNames::IsThatHack(*mono, "eb660caffbc26e1f09f86714d03af4c3eccd9e4d", "x.gba"));
    assert(HackNames::IsThatHack(*HackNames::ForPatch("inclement"), "0", "Pokemon Inclement Emerald 1.13.gba"));
    std::puts("GBA hack names: only the hack itself takes its name and picture.");
}
