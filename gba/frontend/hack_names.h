#pragma once
// The GBA ROM hacks with widescreen patches of their own (Radical Red, Unbound, ...): their names and
// pictures. A patch is chosen for any ROM whose code it fits (games.h, PatchFits), and other hacks
// on the same engine fit Radical Red's too (frostbytez0: several FireRed hacks were listed as
// RADICAL RED, with its picture). The hack's own name and picture go only to that hack: a known
// build of it (its SHA-1) or a file named after it. Any other keeps its own name, and widescreen.
#include <cctype>
#include <string>

namespace HackNames
{
struct Hack
{
    const char* patch;
    const char* name;
    const char* sha1[3]; // known builds (lower-case hex), or null
};
inline const Hack* Hacks(size_t& n)
{
    static const Hack k[] = {
        { "radicalred", "Radical Red", { "964f951a0fdaf209e4ea1344883ef0d557bb3a80", nullptr, nullptr } },  // 4.1
        { "unbound", "Unbound", { "b4776b82a4c7915d0fadeaa27e013523f99dfd94", nullptr, nullptr } },         // 2.1.1.1
        { "emeraldlegacy", "Emerald Legacy", { nullptr, nullptr, nullptr } },
        { "inclement", "Inclement Emerald", { nullptr, nullptr, nullptr } },
        { "monomyth", "Monomyth", { "ae1c4be9be8d706bb989602bc05731203d42860d", "eb660caffbc26e1f09f86714d03af4c3eccd9e4d", nullptr } }, // 1.0, 1.1
        { "gen3evolved", "Gen 3 Evolved", { nullptr, nullptr, nullptr } },
        { "emeraldrogue", "Emerald Rogue", { "7600af1fe08444c850c3c1227fd7dfd81336ae8e", nullptr, nullptr } }, // 2.2.1-EX
    };
    n = sizeof k / sizeof k[0];
    return k;
}
inline std::string Squash(const std::string& s)
{
    std::string o;
    for (char c : s)
        if (isalnum((unsigned char)c)) o += (char)tolower((unsigned char)c);
    return o;
}
// The hack a patch is named after, or null when the patch is a game's own (firered, emerald ...).
inline const Hack* ForPatch(const std::string& patch)
{
    size_t n = 0;
    const Hack* k = Hacks(n);
    for (size_t i = 0; i < n; i++)
        if (patch == k[i].patch) return &k[i];
    return nullptr;
}
// Whether this ROM is that hack: a known build, or a file named after it ("Radical Red 4.1.gba",
// "radicalred.ips", "Pokemon - Unbound (v2.1.1.1).gba").
inline bool IsThatHack(const Hack& h, const std::string& sha1, const std::string& fileName)
{
    for (const char* s : h.sha1)
        if (s && sha1 == s) return true;
    const std::string f = Squash(fileName);
    return f.find(Squash(h.name)) != std::string::npos || f.find(h.patch) != std::string::npos;
}
}
