from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SDL_SHIM = r'''#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
typedef int64_t Sint64;
struct SDL_RWops { FILE* f; };
inline SDL_RWops* SDL_RWFromFile(const char* path, const char* mode) {
    FILE* f = fopen(path, mode);
    if (!f) return nullptr;
    auto* rw = new SDL_RWops{f};
    return rw;
}
inline Sint64 SDL_RWsize(SDL_RWops* rw) {
    long cur = ftell(rw->f);
    fseek(rw->f, 0, SEEK_END);
    Sint64 sz = ftell(rw->f);
    fseek(rw->f, cur, SEEK_SET);
    return sz;
}
inline size_t SDL_RWread(SDL_RWops* rw, void* ptr, size_t size, size_t maxnum) {
    return fread(ptr, size, maxnum, rw->f);
}
inline size_t SDL_RWwrite(SDL_RWops* rw, const void* ptr, size_t size, size_t num) {
    return fwrite(ptr, size, num, rw->f);
}
inline int SDL_RWclose(SDL_RWops* rw) {
    int rc = fclose(rw->f);
    delete rw;
    return rc;
}
'''

CRC_SHIM = r'''#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline uint32_t doCrc32(const void* buf, size_t size) {
    const uint8_t* p = (const uint8_t*)buf;
    uint32_t c = ~0u;
    for (size_t i = 0; i < size; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    return ~c;
}
#ifdef __cplusplus
}
#endif
'''

SHA1_SHIM = r'''#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static inline uint32_t _sh_rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }
static inline void sha1Buffer(const void* buf, size_t len, uint8_t out[20]) {
    const uint8_t* data = (const uint8_t*)buf;
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    size_t padded = ((len + 9 + 63) / 64) * 64;
    uint8_t* msg = (uint8_t*)__builtin_malloc(padded);
    memset(msg, 0, padded);
    if (len) memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) msg[padded - 1 - i] = (uint8_t)(bits >> (8 * i));
    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[off + 4 * i] << 24) | ((uint32_t)msg[off + 4 * i + 1] << 16) |
                   ((uint32_t)msg[off + 4 * i + 2] << 8) | msg[off + 4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = _sh_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            uint32_t tmp = _sh_rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = _sh_rol(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    __builtin_free(msg);
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}
'''


class GbaRomHackTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_gba_romhack_detection_patching_and_ui(self):
        cc = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        self.assertIsNotNone(cc)
        with tempfile.TemporaryDirectory() as folder:
            tmp = Path(folder)
            inc = tmp / 'inc'
            (inc / 'mgba-util').mkdir(parents=True)
            (inc / 'SDL.h').write_text(SDL_SHIM, encoding='utf-8')
            (inc / 'mgba-util' / 'crc32.h').write_text(CRC_SHIM, encoding='utf-8')
            (inc / 'mgba-util' / 'sha1.h').write_text(SHA1_SHIM, encoding='utf-8')
            subprocess.run([
                sys.executable,
                str(ROOT / 'gba/tools/embed_patches.py'),
                str(ROOT / 'gba/wide-patches'),
                str(inc / 'patches.h'),
            ], check=True)

            lzma_objs = []
            for src in [
                ROOT / 'frontend/romhacks_lzma.c',
                ROOT / 'frontend/thirdparty/lzma/LzmaDec.c',
                ROOT / 'frontend/thirdparty/lzma/Lzma2Dec.c',
            ]:
                obj = tmp / (src.stem + '.o')
                subprocess.run([
                    cc, '-c', '-O2',
                    '-I' + str(ROOT / 'frontend/thirdparty/lzma'),
                    str(src), '-o', str(obj),
                ], check=True)
                lzma_objs.append(str(obj))

            exe = tmp / 'test_gba_romhacks'
            subprocess.run([
                'c++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                '-I' + str(inc),
                '-I' + str(ROOT / 'gba/frontend'),
                '-I' + str(ROOT / 'frontend'),
                str(ROOT / 'tests/test_gba_romhacks.cpp'),
                *lzma_objs,
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe), str(tmp / 'work')], check=True)

    def test_no_looks_like_pokemon_filter_and_switch_gba_enabled(self):
        gba_main = (ROOT / 'gba/frontend/main.cpp').read_text(encoding='utf-8')
        build_game = (ROOT / 'tools/switch/build_game.sh').read_text(encoding='utf-8')
        sw_plat = (ROOT / 'frontend/switch_platform.cpp').read_text(encoding='utf-8')
        self.assertNotIn('LooksLikePokemon', gba_main)
        self.assertIn('SSR_GBA_DIR="$HERE/gba"', build_game)
        self.assertIn('GBAW_MGBA="$HERE/build-gba-mgba"', build_game)
        self.assertIn('LICENSE-mGBA.txt', build_game)
        # Switch/embedded GBA defaults START button to PAUSE MENU, syncs start_button, and keeps forced hack names.
        self.assertIn('"menu",', gba_main)
        self.assertIn('!strcmp(key, "gba_start")', gba_main)
        self.assertIn('if (!g.forced && g.patch == n.patch) l.name = n.name;', gba_main)
        # HOME Menu forwarders include ROM hacks cleanly and generate fallback icons via DrawCartridgeCase.
        self.assertIn('ForwarderKey(', sw_plat)
        self.assertNotIn('!Playable(g) || g.hack', sw_plat)
        self.assertIn('Launcher::DrawCartridgeCase(card,', sw_plat)
