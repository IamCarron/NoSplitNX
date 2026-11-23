from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

MGBA_CORE_H = r'''#pragma once
#include <stdint.h>
enum mPlatform { mPLATFORM_NONE = -1, mPLATFORM_GBA = 0, mPLATFORM_GB = 1 };
struct mCore {
    void* board;
    enum mPlatform (*platform)(const struct mCore*);
    uint8_t (*busRead8)(struct mCore*, uint32_t);
    uint16_t (*busRead16)(struct mCore*, uint32_t);
    uint32_t (*busRead32)(struct mCore*, uint32_t);
    void (*busWrite8)(struct mCore*, uint32_t, uint8_t);
    void (*busWrite16)(struct mCore*, uint32_t, uint16_t);
    void (*busWrite32)(struct mCore*, uint32_t, uint32_t);
};
'''

MGBA_GBA_H = r'''#pragma once
#include <stdint.h>
#define GBA_SIZE_EWRAM 0x40000
#define GBA_BASE_PALETTE_RAM 0x05000000
struct GBAVideoRenderer {
    void (*drawScanline)(struct GBAVideoRenderer*, int y);
    void (*writeOAM)(struct GBAVideoRenderer*, uint32_t oam);
    void (*writePalette)(struct GBAVideoRenderer*, uint32_t address, uint16_t value);
    void (*writeVRAM)(struct GBAVideoRenderer*, uint32_t address);
};
struct GBA {
    struct {
        uint16_t io[0x200];
        uint16_t* iwram;
        uint16_t* wram;
        uint32_t* rom;
        uint32_t romSize;
    } memory;
    struct {
        struct GBAVideoRenderer* renderer;
        uint16_t* vram;
        uint16_t palette[0x200];
        struct { uint16_t raw[0x200]; } oam;
    } video;
};
'''


MGBA_GB_H = r'''#pragma once
#include <stdint.h>
#define GB_SIZE_WORKING_RAM_BANK0 0x1000
enum GBModel { GB_MODEL_AUTODETECT = 0, GB_MODEL_DMG = 0x00, GB_MODEL_SGB = 0x20, GB_MODEL_CGB = 0x80 };
struct GBVideoRenderer {
    void (*writePalette)(struct GBVideoRenderer*, int index, uint16_t value);
    void (*writeVRAM)(struct GBVideoRenderer*, uint16_t address);
};
struct GB {
    struct {
        uint8_t* rom;
        uint32_t romSize;
        uint8_t* wram;
        uint8_t hram[0x80];
        uint8_t io[0x100];
    } memory;
    struct {
        struct GBVideoRenderer* renderer;
        uint8_t* vram;
        uint16_t ly;
        struct { uint8_t raw[160]; } oam;
    } video;
    enum GBModel model;
};
'''

MGBA_GB_IO_H = r'''#pragma once
enum GBRegister {
    GB_REG_LCDC = 0x40,
    GB_REG_SCY  = 0x42,
    GB_REG_SCX  = 0x43,
    GB_REG_WY   = 0x4A,
    GB_REG_WX   = 0x4B,
};
'''

MGBA_GB_SW_H = r'''#pragma once
#include <mgba/internal/gb/gb.h>
struct GBVideoSoftwareRenderer {
    struct GBVideoRenderer d;
    uint32_t palette[64];
    uint8_t lookup[64];
    enum GBModel model;
};
'''


class GbaWidePokedaisyTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and (shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')),
                         'C and C++ compilers required')
    def test_pokedaisy_wide_compiles_and_recognises_soulgold_v121(self):
        cc = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        with tempfile.TemporaryDirectory() as folder:
            tmp = Path(folder)
            inc = tmp / 'inc'
            (inc / 'mgba/core').mkdir(parents=True)
            (inc / 'mgba/internal/gba').mkdir(parents=True)
            (inc / 'mgba/internal/gb/renderers').mkdir(parents=True)
            (inc / 'mgba/core/core.h').write_text(MGBA_CORE_H, encoding='utf-8')
            (inc / 'mgba/internal/gba/gba.h').write_text(MGBA_GBA_H, encoding='utf-8')
            (inc / 'mgba/internal/gb/gb.h').write_text(MGBA_GB_H, encoding='utf-8')
            (inc / 'mgba/internal/gb/io.h').write_text(MGBA_GB_IO_H, encoding='utf-8')
            (inc / 'mgba/internal/gb/renderers/software.h').write_text(MGBA_GB_SW_H, encoding='utf-8')

            obj_wide = tmp / 'pk_wide.o'
            obj_gb = tmp / 'pk_wide_gb.o'
            subprocess.run([
                cc, '-c', '-O2', '-DPK_WIDE_NO_GUARD', '-I' + str(inc),
                '-I' + str(ROOT / 'gba/frontend'),
                str(ROOT / 'gba/frontend/pokedaisy/pk_wide.c'),
                '-o', str(obj_wide),
            ], check=True)
            subprocess.run([
                cc, '-c', '-O2', '-DPK_WIDE_NO_GUARD', '-DPK_WIDE_GB_GLOW=1', '-I' + str(inc),
                '-I' + str(ROOT / 'gba/frontend'),
                str(ROOT / 'gba/frontend/pokedaisy/pk_wide_gb.c'),
                '-o', str(obj_gb),
            ], check=True)

            exe = tmp / 'wide-test'
            subprocess.run([
                'c++', '-std=c++17', '-O2',
                '-I' + str(inc),
                '-I' + str(ROOT / 'gba/frontend'),
                str(ROOT / 'tests/test_gba_wide_pokedaisy.cpp'),
                str(obj_wide), str(obj_gb),
                '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)
