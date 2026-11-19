#include "../frontend/romhacks.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

// Full-size DS dumps matching no listed release remain Unknown (their own game) rather than
// Modified; trimmed unrecognised dumps count as hacks and cache under ds-v5.
static void make(const std::string& path, uint64_t size)
{
    std::ofstream out(path, std::ios::binary);
    std::string header(0x200, '\0');
    header.replace(0x0C, 4, "IPKS");
    out << header;
    out.seekp((std::streamoff)size - 1);
    out.put('\x5A');
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string dir = argv[1];
    const std::string full = dir + "/full.nds", trimmed = dir + "/trimmed.nds";
    make(full, 134217728u);
    make(trimmed, 100000000u);
    RomHacks::Cache cache;
    assert(RomHacks::CheckDs(full, "IPKS", cache, true) != RomHacks::Kind::Modified);
    assert(RomHacks::CheckDs(trimmed, "IPKS", cache, true) == RomHacks::Kind::Modified);
    std::string cached;
    assert(cache.Get(RomHacks::Cache::Key(trimmed, "ds-v5"), cached) && cached == "modified");
    std::puts("full-size dumps stay their game under ds-v5 while trimmed unknown dumps are hacks");
}
