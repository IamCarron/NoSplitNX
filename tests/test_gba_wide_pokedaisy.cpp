extern "C" {
#include "pokedaisy/pk_wide.h"
#include <mgba/core/core.h>
#include <mgba/internal/gba/gba.h>
}
#include "pokedaisy/wide_camera.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static enum mPlatform FakePlatform(const struct mCore*) { return mPLATFORM_GBA; }

int main()
{
    // 1. PokeDaisy WIDE CRC database recognises SoulGold (all builds incl. v1.2.1) and major hacks.
    const uint32_t knownHacks[] = {
        0x5578ebea, // SoulGold v1.2.1 (Alt 1)
        0x601664c3, // SoulGold v1.2.1
        0x283238a6, // SoulGold v1.2
        0xba7394a6, // SoulGold v1.1.4
        0x558ae42f, // Lazarus v2.0
        0x01713508, // Heart and Soul v2.0.6
        0x14465a33, // Seaglass v3.0
        0x6c5ee1b2, // Odyssey v4.1.1
    };
    for (uint32_t crc : knownHacks) assert(pk_wide_knows(crc) == 1);
    assert(pk_wide_knows(0) == 0);
    assert(pk_wide_knows(0xDEADBEEFu) == 0);

    // 2. Configure SoulGold v1.2.1 and compute the 1280x720 1x widescreen camera window.
    assert(pk_wide_configure(0x601664c3) == 1);
    PkWideCamera::Camera cam;
    cam.viewW = 1280;
    cam.viewH = 720;
    const PkWideCamera::Window w1x = cam.Frame(PkWideCamera::SCENE_FIELD, 1000000000LL);
    assert(w1x.valid);
    assert(w1x.x < 0 && w1x.w >= 285 && w1x.y == 0 && w1x.h == 160);
    assert(std::fabs(w1x.scale - 4.5f) < 0.01f);
    assert(cam.Plain());

    // 3. Stepping Zoom out produces a wider zoomed-out view, and cycling returns to 1x.
    assert(cam.Cycle() == 1);
    PkWideCamera::Window wOut;
    for (int64_t t = 2000000000LL; t <= 6000000000LL; t += 500000000LL) wOut = cam.Frame(PkWideCamera::SCENE_FIELD, t);
    assert(wOut.valid && wOut.w > w1x.w && wOut.h > w1x.h);
    while (cam.Cycle() != 0) {}

    // 4. Smoke-run pk_wide_view on SoulGold v1.2.1 at 1x 16:9 widescreen window with synthetic GBA RAM/VRAM.
    auto gba = std::make_unique<struct GBA>();
    auto rom = std::make_unique<std::vector<uint32_t>>(0x02000000 / 4, 0);
    auto wram = std::make_unique<std::vector<uint16_t>>(0x40000 / 2, 0);
    auto iwram = std::make_unique<std::vector<uint16_t>>(0x8000 / 2, 0);
    auto vram = std::make_unique<std::vector<uint16_t>>(0x18000 / 2, 0);
    std::memset(gba.get(), 0, sizeof(struct GBA));
    static struct GBAVideoRenderer renderer {};
    gba->memory.rom = rom->data();
    gba->memory.romSize = (uint32_t)(rom->size() * 4);
    gba->memory.wram = wram->data();
    gba->memory.iwram = iwram->data();
    gba->video.vram = vram->data();
    gba->video.renderer = &renderer;

    struct mCore core {};
    core.board = gba.get();
    core.platform = FakePlatform;

    std::vector<uint32_t> frame(PK_WIDE_W * PK_WIDE_H);
    for (int y = 0; y < PK_WIDE_H; y++)
        for (int x = 0; x < PK_WIDE_W; x++)
            frame[(size_t)y * PK_WIDE_W + x] = 0xFF104080u | ((uint32_t)(x + y) & 0xFFu);

    std::vector<uint32_t> out((size_t)w1x.w * w1x.h, 0);
    struct PkWideView v = { w1x.x, w1x.y, w1x.w, w1x.h };
    pk_wide_view(&core, frame.data(), out.data(), v, nullptr);

    // Centre 240x160 pixels are preserved identically, while the left/right edges are filled.
    for (int y = 0; y < PK_WIDE_H; y++)
    {
        assert((out[(size_t)y * w1x.w] >> 24) == 0xFF);
        for (int x = 0; x < PK_WIDE_W; x++)
        {
            const uint32_t got = out[(size_t)y * w1x.w + (x - w1x.x)];
            assert((got & 0xFFFFFFu) == (frame[(size_t)y * PK_WIDE_W + x] & 0xFFFFFFu));
        }
        assert((out[(size_t)y * w1x.w + w1x.w - 1] >> 24) == 0xFF);
    }

    pk_wide_reset();
    std::puts("PokeDaisy WIDE compile, SoulGold v1.2.1 CRC recognition, 1x widescreen window and Zoom steps OK.");
    return 0;
}
