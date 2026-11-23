/* WILD ENCOUNTERS: where a game keeps its sWildEncountersDisabled - the C twin of
 * the app's WildEncounters.find (WildEncounters.kt; keep in sync). Found by
 * StandardWildEncounter's code, no table per game: its first lines load the
 * switch from a literal pool, compare it with 1, call the header lookup and load
 * HEADER_NONE (0xFFFF), in agbcc's FireRed / LeafGreen and Emerald / Ruby /
 * Sapphire shapes (every language and the hacks that kept that code); the CFRU
 * hacks hook it with their own, which still reads FireRed's 0x020386DC. pk_wide.c's
 * pk_field_wild / pk_field_state then hold it. GPL-3.0, see ../NOTICE. */
#include "wild.h"

#include <string.h>

#define FIRERED_SWITCH 0x020386DCu
enum { LDR = -1, FFFF = -2, B = -3, BL1 = -4, BL2 = -5, END = -99 };
static const int FIRERED[] = {
    0xB5F0, 0x4647, 0xB480, 0x1C06, 0x0409, 0x0C0F, 0x46B8, LDR, 0x7800, 0x2801, 0xD100, B, BL1, BL2, 0x0400, 0x0C04,
    FFFF, END,
};
static const int EMERALD[] = {
    0xB5F0, 0x464F, 0x4646, 0xB4C0, 0x0400, 0x0C06, 0x46B1, 0x0409, 0x0C0F, 0x46B8, LDR, 0x7800, 0x2801, 0xD100, B,
    BL1, BL2, 0x0400, 0x0C05, FFFF, END,
};

static unsigned u16(const uint8_t* r, size_t o) {
    return r[o] | r[o + 1] << 8;
}

// The word an `ldr rX, [pc, #imm]` at o loads (false past the ROM's end).
static bool pool(const uint8_t* r, size_t size, size_t o, uint32_t* out) {
    size_t at = ((o + 4) & ~(size_t) 3) + (u16(r, o) & 0xFF) * 4;
    if (at + 4 > size) return false;
    *out = r[at] | r[at + 1] << 8 | r[at + 2] << 16 | (uint32_t) r[at + 3] << 24;
    return true;
}

static uint32_t match_at(const uint8_t* r, size_t size, long start, const int* shape) {
    if (start < 0) return 0;
    uint32_t sw = 0, v;
    for (int i = 0; shape[i] != END; i++) {
        size_t o = (size_t) start + 2 * (size_t) i;
        if (o + 2 > size) return 0;
        unsigned h = u16(r, o);
        switch (shape[i]) {
        case LDR:
            if (h >> 8 != 0x48 || !pool(r, size, o, &sw) || sw < 0x02000000u || sw >= 0x02040000u) return 0;
            break;
        case FFFF:
            if (h >> 8 != 0x48 || !pool(r, size, o, &v) || v != 0xFFFF) return 0;
            break;
        case B: if (h >> 11 != 0x1C) return 0; break;
        case BL1: if (h >> 11 != 0x1E) return 0; break;
        case BL2: if (h >> 11 != 0x1F) return 0; break;
        default: if ((int) h != shape[i]) return 0;
        }
    }
    return sw;
}

// CFRU's own: ldr rN, =0x020386DC; ldrb rM, [rN] within 4 lines; then cmp rM, #1.
static bool cfru_read(const uint8_t* r, size_t size, size_t o) {
    unsigned reg = (u16(r, o) >> 8) & 7;
    if (o + 10 > size) return false;
    for (int ld = 0; ld < 4; ld++) {
        unsigned h = u16(r, o + 2 + 2 * (size_t) ld);
        if ((h & 0xFFC0) != 0x7800 || ((h >> 3) & 7) != reg) continue;
        unsigned d = h & 7;
        for (int k = ld + 1; k < 4; k++)
            if (u16(r, o + 2 + 2 * (size_t) k) == (0x2800 | d << 8 | 1)) return true;
        return false;
    }
    return false;
}

uint32_t pk_wild_switch_find(const uint8_t* rom, size_t size) {
    bool bpre = size >= 0xB0 && !memcmp(rom + 0xAC, "BPR", 3), cfru = false;
    for (size_t o = 0; o + 6 <= size; o += 2) {
        // ldrb r0, [r0]; cmp r0, #1; bne: the switch read in both agbcc shapes.
        if (u16(rom, o) == 0x7800 && u16(rom, o + 2) == 0x2801 && u16(rom, o + 4) == 0xD100) {
            uint32_t sw = match_at(rom, size, (long) o - 2 * 8, FIRERED);
            if (!sw) sw = match_at(rom, size, (long) o - 2 * 11, EMERALD);
            if (sw) return sw;
        }
        uint32_t w;
        if (bpre && !cfru && u16(rom, o) >> 11 == 0x09 && pool(rom, size, o, &w) && w == FIRERED_SWITCH)
            cfru = cfru_read(rom, size, o);
    }
    return cfru ? FIRERED_SWITCH : 0;
}
