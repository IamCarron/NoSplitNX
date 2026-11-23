// GAMEPLAY options for the GBA games (adapted from PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy,
// GPL 3: companion/data/InstantText.kt, ReusableTms.kt, WildEncounters.kt). Each is found in the player's own ROM by
// the code it changes, never by a table of addresses per game, so every language and the hacks that kept that code get
// it; a ROM where the code isn't found simply doesn't offer it.
//   INSTANT TEXT:  a text box's words appear at once (the game's text printer loses its wait between letters).
//   REUSABLE TMS:  a TM stays in the bag after it teaches a move, as an HM does.
//   WILD ENCOUNTERS: the game's own sWildEncountersDisabled switch (pk_wide.c rolls it each free frame on the field).
// The ROM changes are written to the loaded game only (mGBA's ROM patching), in small pieces, with the original bytes
// kept to put back; the file on disk is never touched.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace GbaGameplay
{
struct Write
{
    uint32_t addr; // bus address (0x08000000 + ROM offset)
    std::vector<uint8_t> orig, value;
};

namespace detail
{
inline uint16_t U16(const std::vector<uint8_t>& r, size_t o) { return (uint16_t)(r[o] | r[o + 1] << 8); }
inline uint32_t U32(const std::vector<uint8_t>& r, size_t o) { return r[o] | r[o + 1] << 8 | r[o + 2] << 16 | (uint32_t)r[o + 3] << 24; }
// The word an `ldr rX, [pc, #imm]` at o loads (0 past the end).
inline uint32_t Pool(const std::vector<uint8_t>& r, size_t o)
{
    const size_t at = ((o + 4) & ~(size_t)3) + (U16(r, o) & 0xFF) * 4;
    return at + 4 <= r.size() ? U32(r, at) : 0;
}
// Where the `bl` at o goes (a ROM offset).
inline long BlTarget(const std::vector<uint8_t>& r, size_t o)
{
    int hi = U16(r, o) & 0x7FF;
    if (hi & 0x400) hi -= 0x800;
    return (long)o + 4 + ((long)hi << 12) + ((U16(r, o + 2) & 0x7FF) << 1);
}
inline void Bl(uint8_t* out, long at, long target)
{
    const long off = (target - (at + 4)) >> 1;
    const int h1 = 0xF000 | ((off >> 11) & 0x7FF), h2 = 0xF800 | (off & 0x7FF);
    out[0] = (uint8_t)h1; out[1] = (uint8_t)(h1 >> 8); out[2] = (uint8_t)h2; out[3] = (uint8_t)(h2 >> 8);
}
enum { ANY = -1, BL_HIGH = -2, BL_LOW = -3 };
inline std::vector<size_t> Matches(const std::vector<uint8_t>& r, const std::vector<int>& sig, size_t limit)
{
    std::vector<size_t> out;
    const size_t end = std::min(r.size(), limit);
    for (size_t o = 0; o + sig.size() + 4 <= end; o += 2)
    {
        bool ok = true;
        for (size_t i = 0; i < sig.size() && ok; i++)
        {
            const int b = r[o + i];
            ok = sig[i] == ANY ? true : sig[i] == BL_HIGH ? (b >= 0xF0 && b <= 0xF7) : sig[i] == BL_LOW ? b >= 0xF8 : b == sig[i];
        }
        if (ok) out.push_back(o);
    }
    return out;
}
} // namespace detail

// INSTANT TEXT. The FireRed and Emerald engines print through RenderFont, once a frame until the font's function stops
// answering RENDER_REPEAT; it is rewritten in its own 40 bytes so a frame prints letters up to the next control code
// (line ends, colours, page breaks and pauses stay the game's). Ruby and Sapphire's UpdateWindowText gets a branch back
// to its print call instead of its letter delay. Searched in the ROM's first 64 KB, where every build keeps it.
inline std::vector<Write> InstantText(const std::vector<uint8_t>& rom)
{
    using namespace detail;
    const size_t SEARCH = 0x10000;
    static const std::vector<int> FONT = { 0x10, 0xB5, 0x04, 0x1C, 0x61, 0x79, 0x08, 0x48, 0x02, 0x68, 0x48, 0x00, 0x40, 0x18, 0x80, 0x00, 0x80, 0x18, 0x01, 0x68,
                                           0x20, 0x1C, ANY, BL_HIGH, ANY, BL_LOW, 0x00, 0x04, 0x00, 0x0C, 0x02, 0x28, 0xF0, 0xD0, 0x10, 0xBC, 0x02, 0xBC, 0x08, 0x47 };
    static const uint8_t FONT_CODE[40] = { 0x10, 0xB5, 0x04, 0x1C, 0x61, 0x79, 0x0C, 0x20, 0x41, 0x43, 0x07, 0x48, 0x00, 0x68, 0x41, 0x58, 0x20, 0x1C, 0, 0,
                                           0, 0, 0x41, 0x08, 0x05, 0xD2, 0xF3, 0xD1, 0x60, 0x77, 0x21, 0x68, 0x09, 0x78, 0xF7, 0x29, 0xEE, 0xD3, 0x10, 0xBD };
    static const std::vector<int> RS_PRINT = { 0x20, 0x1C, ANY, BL_HIGH, ANY, BL_LOW, 0xE0, 0x8A, 0x0A, 0x28, 0x25, 0xD8, 0x80, 0x00 };
    static const std::vector<int> RS_DELAY = { 0x03, 0x20, 0xE0, 0x82, 0x20, 0x1C, 0x00, 0xF0, ANY, BL_LOW, 0x60, 0x73, 0x00, 0x20, 0x10, 0xBC, 0x02, 0xBC, 0x08, 0x47 };
    const size_t FONT_CALL = 0x16, NEW_CALL = 0x12, RS_GAP = 0x58;
    std::vector<Write> out;
    if (rom.size() < 0x200) return out;
    const auto font = Matches(rom, FONT, SEARCH), print = Matches(rom, RS_PRINT, SEARCH), delay = Matches(rom, RS_DELAY, SEARCH);
    if (font.size() == 1 && print.empty())
    {
        const size_t at = font[0];
        if (at % 4) return out; // (the new code's `ldr` counts on it)
        uint8_t code[40];
        memcpy(code, FONT_CODE, 40);
        Bl(code + NEW_CALL, (long)(at + NEW_CALL), BlTarget(rom, at + FONT_CALL));
        for (size_t i = 0; i < 40; i += 4)
            if (memcmp(&rom[at + i], code + i, 4))
                out.push_back({ (uint32_t)(0x08000000 + at + i), { rom.begin() + at + i, rom.begin() + at + i + 4 }, { code + i, code + i + 4 } });
    }
    else if (font.empty() && print.size() == 1 && delay.size() == 1 && delay[0] == print[0] + RS_GAP)
    {
        const int branch = 0xE000 | ((((long)print[0] - (long)(delay[0] + 4)) >> 1) & 0x7FF);
        out.push_back({ (uint32_t)(0x08000000 + delay[0]), { rom[delay[0]], rom[delay[0] + 1] }, { (uint8_t)branch, (uint8_t)(branch >> 8) } });
    }
    return out;
}

// REUSABLE TMS. The party menu's `if (item < ITEM_HM01) RemoveBagItem(item, 1)` after a move is taught: its `bhi`
// becomes a plain `b`, so every TM is kept as an HM is. The FireRed engine has three such spots, Emerald, Ruby and
// Sapphire one; all are needed (and all calling the same function), or none is changed.
inline std::vector<Write> ReusableTms(const std::vector<uint8_t>& rom)
{
    using namespace detail;
    std::vector<Write> out;
    if (rom.size() < 0xC0) return out;
    const std::string code((const char*)&rom[0xAC], 3);
    const int want = (code == "BPR" || code == "BPG" || code == "PUV") ? 3 : (code == "BPE" || code == "AXV" || code == "AXP") ? 1 : 0;
    if (!want) return out;
    std::vector<size_t> sites;
    for (size_t o = 0; o + 16 <= rom.size(); o += 2)
    {
        if (rom[o] != 0xA9 || rom[o + 1] != 0x20 || U16(rom, o + 2) != 0x0040) continue;
        const int cmp = U16(rom, o + 4), n = cmp & 7;
        if (cmp == (0x4280 | n) && U16(rom, o + 6) == 0xD803 && U16(rom, o + 8) == (0x1C00 | n << 3) && U16(rom, o + 10) == 0x2101 &&
            (U16(rom, o + 12) & 0xF800) == 0xF000 && (U16(rom, o + 14) & 0xF800) == 0xF800)
            sites.push_back(o);
    }
    if ((int)sites.size() != want) return out;
    for (size_t s : sites)
        if (BlTarget(rom, s + 12) != BlTarget(rom, sites[0] + 12)) return out;
    for (size_t s : sites) out.push_back({ (uint32_t)(0x08000000 + s + 6), { rom[s + 6], rom[s + 7] }, { 0x03, 0xE0 } });
    return out;
}

// WILD ENCOUNTERS. StandardWildEncounter's first lines load the switch from a literal pool, compare it with 1, then
// call GetCurrentMapWildMonHeaderId and load 0xFFFF: FireRed's and Emerald's (Ruby and Sapphire's) shapes as agbcc
// built them, and CFRU's hook (Unbound, Radical Red), which still reads FireRed's switch. The switch's EWRAM address,
// or 0.
inline uint32_t WildSwitch(const std::vector<uint8_t>& rom)
{
    using namespace detail;
    enum { LDR = -1, FFFF = -2, B = -3, BL1 = -4, BL2 = -5 };
    static const std::vector<int> FIRERED = { 0xB5F0, 0x4647, 0xB480, 0x1C06, 0x0409, 0x0C0F, 0x46B8, LDR, 0x7800, 0x2801, 0xD100, B, BL1, BL2, 0x0400, 0x0C04, FFFF };
    static const std::vector<int> EMERALD = { 0xB5F0, 0x464F, 0x4646, 0xB4C0, 0x0400, 0x0C06, 0x46B1, 0x0409, 0x0C0F, 0x46B8, LDR, 0x7800, 0x2801, 0xD100, B,
                                              BL1, BL2, 0x0400, 0x0C05, FFFF };
    const uint32_t FIRERED_SWITCH = 0x020386DC;
    auto matchAt = [&](long start, const std::vector<int>& shape) -> uint32_t {
        if (start < 0 || (size_t)start + 2 * shape.size() > rom.size()) return 0;
        uint32_t sw = 0;
        for (size_t i = 0; i < shape.size(); i++)
        {
            const size_t o = start + 2 * i;
            const int h = U16(rom, o);
            switch (shape[i])
            {
            case LDR:
                if (h >> 8 != 0x48) return 0;
                sw = Pool(rom, o);
                if (sw < 0x02000000 || sw >= 0x02040000) return 0;
                break;
            case FFFF: if (h >> 8 != 0x48 || Pool(rom, o) != 0xFFFF) return 0; break;
            case B: if (h >> 11 != 0x1C) return 0; break;
            case BL1: if (h >> 11 != 0x1E) return 0; break;
            case BL2: if (h >> 11 != 0x1F) return 0; break;
            default: if (h != shape[i]) return 0;
            }
        }
        return sw;
    };
    const bool bpre = rom.size() >= 0xB0 && !memcmp(&rom[0xAC], "BPR", 3);
    for (size_t o = 0; o + 6 <= rom.size(); o += 2)
    {
        if (U16(rom, o) == 0x7800 && U16(rom, o + 2) == 0x2801 && U16(rom, o + 4) == 0xD100)
        {
            if (uint32_t s = matchAt((long)o - 16, FIRERED)) return s;
            if (uint32_t s = matchAt((long)o - 22, EMERALD)) return s;
        }
        // CFRU's own (gcc): ldr rN, =0x020386DC; ldrb rN, [rN]; ...; cmp rN, #1.
        if (bpre && U16(rom, o) >> 11 == 0x09 && Pool(rom, o) == FIRERED_SWITCH && o + 10 <= rom.size())
        {
            const int r = (U16(rom, o) >> 8) & 7;
            for (int k = 0; k < 4; k++)
            {
                const int h = U16(rom, o + 2 + 2 * k);
                if ((h & 0xFFC0) == 0x7800 && ((h >> 3) & 7) == r)
                {
                    const int d = h & 7;
                    for (int j = k + 1; j < 4; j++)
                        if (U16(rom, o + 2 + 2 * j) == (0x2800 | d << 8 | 1)) return FIRERED_SWITCH;
                    break;
                }
            }
        }
    }
    return 0;
}
// SHINY ODDS. Not by changing the game's own check (that would turn Pokemon already caught shiny, and plain again when
// switched off): each new wild foe is changed in gEnemyParty between the frame the game makes it and the battle drawing
// it, only while gMain.inBattle isn't set yet. Its personality becomes one that is shiny for the player's ID and keeps
// what the personality decides (nature, gender and ability bit, Unown's letter, Wurmple's evolution); the record is
// re-encrypted with its substructures in the new order. Only a record whose checksum reads right, with the player's
// own OT ID (a wild one), not an egg, is touched.
class ShinyRoller
{
public:
    // odds: 1 in [odds] (8192 = the game's own: nothing done). enemyParty, saveBlock2 (a pointer to it unless
    // staticBlocks), gMain's inBattle byte.
    template <class Read, class Write>
    int Tick(int odds, uint32_t enemyParty, uint32_t saveBlock2, bool staticBlocks, uint32_t inBattle, Read read, Write write)
    {
        int changed = 0;
        for (int slot = 0; slot < 2; slot++)
        {
            const uint32_t at = enemyParty + slot * 100;
            uint8_t rec[80];
            if (!read(at, rec, 80)) continue;
            const uint64_t key = Get32(rec, 0) | (uint64_t)Get32(rec, 4) << 32;
            if (key == seen[slot]) { settling[slot].clear(); continue; }
            if (settling[slot].size() != 80 || memcmp(settling[slot].data(), rec, 80)) { settling[slot].assign(rec, rec + 80); continue; }
            settling[slot].clear();
            seen[slot] = key;
            const double base = 8 / 65536.0;
            const double chance = odds >= 8192 || odds <= 0 ? 0 : std::min(1.0, std::max(0.0, (1.0 / odds - base) / (1.0 - base)));
            if (chance <= 0) continue;
            uint8_t b = 0;
            if (!read(inBattle, &b, 1) || (b & 2)) continue;
            uint32_t sb2 = saveBlock2;
            if (!staticBlocks) { uint8_t p[4]; if (!read(saveBlock2, p, 4)) continue; sb2 = Get32(p, 0); }
            if (sb2 < 0x02000000 || sb2 >= 0x03008000) continue;
            uint8_t idb[4];
            if (!read(sb2 + 0x0A, idb, 4)) continue;
            const uint32_t player = Get32(idb, 0), pid = Get32(rec, 0), ot = Get32(rec, 4);
            if (ot != player || pid == 0) continue;
            if (rec[0x13] & 0x05) continue; // (an egg, a bad egg)
            uint8_t data[48];
            if (!Decrypt(rec, data)) continue;
            const int species = data[0] | data[1] << 8; // (the growth substructure's first word)
            if (!species) continue;
            if ((((ot >> 16) ^ (ot & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) & 0xFFFF) < 8) continue; // (shiny already)
            if (Next() % 1000000 >= chance * 1000000) continue;
            uint32_t np;
            if (!ShinyPersonality(pid, ot, species, np)) continue;
            uint8_t out[80];
            memcpy(out, rec, 80);
            Put32(out, 0, np);
            Encrypt(out, data);
            if (write(at, out, 80)) { seen[slot] = np | (uint64_t)ot << 32; changed++; }
        }
        return changed;
    }
    void Reset() { seen[0] = seen[1] = ~0ull; settling[0].clear(); settling[1].clear(); }

private:
    uint64_t seen[2] = { ~0ull, ~0ull };
    std::vector<uint8_t> settling[2];
    uint32_t rng = 0x9E3779B9u;
    uint32_t Next() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
    static uint32_t Get32(const uint8_t* b, int o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
    static void Put32(uint8_t* b, int o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)(v >> (8 * i)); }
    // The 24 orders of the four substructures (growth, attacks, EVs, misc), by personality % 24.
    static const uint8_t* Order(uint32_t pid)
    {
        // (Slot -> substructure: GAEM, GAME, GEAM, GEMA, ... as the companion reads them.)
        static const uint8_t o[24][4] = { { 0, 1, 2, 3 }, { 0, 1, 3, 2 }, { 0, 2, 1, 3 }, { 0, 2, 3, 1 }, { 0, 3, 1, 2 }, { 0, 3, 2, 1 },
                                          { 1, 0, 2, 3 }, { 1, 0, 3, 2 }, { 1, 2, 0, 3 }, { 1, 2, 3, 0 }, { 1, 3, 0, 2 }, { 1, 3, 2, 0 },
                                          { 2, 0, 1, 3 }, { 2, 0, 3, 1 }, { 2, 1, 0, 3 }, { 2, 1, 3, 0 }, { 2, 3, 0, 1 }, { 2, 3, 1, 0 },
                                          { 3, 0, 1, 2 }, { 3, 0, 2, 1 }, { 3, 1, 0, 2 }, { 3, 1, 2, 0 }, { 3, 2, 0, 1 }, { 3, 2, 1, 0 } };
        return o[pid % 24];
    }
    // rec's 48 encrypted bytes into data (logical order G, A, E, M); false when the checksum doesn't match.
    static bool Decrypt(const uint8_t* rec, uint8_t* data)
    {
        const uint32_t pid = Get32(rec, 0), key = pid ^ Get32(rec, 4);
        const uint8_t* order = Order(pid);
        uint32_t sum = 0;
        for (int slot = 0; slot < 4; slot++)
            for (int w = 0; w < 3; w++)
            {
                const uint32_t word = Get32(rec, 0x20 + slot * 12 + w * 4) ^ key;
                Put32(data, order[slot] * 12 + w * 4, word);
                sum += (word & 0xFFFF) + (word >> 16);
            }
        return (sum & 0xFFFF) == (uint32_t)(rec[0x1C] | rec[0x1D] << 8);
    }
    static void Encrypt(uint8_t* rec, const uint8_t* data)
    {
        const uint32_t pid = Get32(rec, 0), key = pid ^ Get32(rec, 4);
        const uint8_t* order = Order(pid);
        uint32_t sum = 0;
        for (int i = 0; i < 24; i++) sum += data[i * 2] | data[i * 2 + 1] << 8;
        rec[0x1C] = (uint8_t)sum; rec[0x1D] = (uint8_t)(sum >> 8);
        for (int slot = 0; slot < 4; slot++)
            for (int w = 0; w < 3; w++) Put32(rec, 0x20 + slot * 12 + w * 4, Get32(data, order[slot] * 12 + w * 4) ^ key);
    }
    static uint32_t Unown(uint32_t p)
    {
        return ((((p >> 24) & 3) << 6) | (((p >> 16) & 3) << 4) | (((p >> 8) & 3) << 2) | (p & 3)) % 28;
    }
    // A personality shiny for ot that keeps what old decided for species; false if there's none.
    bool ShinyPersonality(uint32_t old, uint32_t ot, int species, uint32_t& out)
    {
        const int UNOWN = 201, WURMPLE = 290, WURMPLE_FEMALE_BELOW = 127;
        const uint32_t tsv = (ot >> 16) ^ (ot & 0xFFFF);
        const int oldLow = old & 0xFF;
        std::vector<uint32_t> found;
        for (int low = 0; low < 256; low++)
        {
            if (species == UNOWN ? (low & 3) != (oldLow & 3)
                : species == WURMPLE ? ((low & 1) != (oldLow & 1) || (low < WURMPLE_FEMALE_BELOW) != (oldLow < WURMPLE_FEMALE_BELOW))
                : low != oldLow)
                continue;
            for (int mid = 0; mid < 256; mid++)
            {
                const uint32_t lo = (uint32_t)mid << 8 | low;
                for (uint32_t x = 0; x < 8; x++)
                {
                    const uint32_t p = ((tsv ^ lo ^ x) & 0xFFFF) << 16 | lo;
                    if (p % 25 != old % 25) continue;
                    if (species == UNOWN && Unown(p) != Unown(old)) continue;
                    if (species == WURMPLE && ((p >> 16) % 10 <= 4) != ((old >> 16) % 10 <= 4)) continue;
                    found.push_back(p);
                }
            }
        }
        if (found.empty()) return false;
        out = found[Next() % found.size()];
        return true;
    }
};
} // namespace GbaGameplay
