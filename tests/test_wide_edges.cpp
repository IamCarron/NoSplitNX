#include "merged.h"
#include <cassert>
#include <cstdio>
int main() {
    std::vector<uint32_t> pixels(256 * 192);
    std::vector<uint8_t> flags(256 * 192, 2);
    uint32_t edge[384] = {};
    uint16_t blend[384]; std::fill(blend, blend + 384, 16);
    uint8_t flat[384] = {};
    ScreenImage top{pixels.data(), flags.data(), edge, blend, flat};
    Scene scene; scene.smartEdges = true; scene.items = {FullScreen(0)};
    for (int x = 0; x < 256; ++x) pixels[x] = (x % 16 < 4 ? 0x445566 : 0xCCDDEE);
    assert(MergedEdgeContinuation(scene, top, 0, 0).pattern);
    assert(MergedEdgeContinuation(scene, top, 0, 1).pattern);
    // A non-repeating window border and its advance arrow are not tiles.
    std::fill(pixels.begin(), pixels.end(), 0xEEEEEE);
    for (int x : {0, 3, 252, 255}) pixels[x] = 0x334455;
    pixels[246] = pixels[247] = 0x222222;
    assert(!MergedEdgeContinuation(scene, top, 0, 0).pattern);
    assert(!MergedEdgeContinuation(scene, top, 0, 1).pattern);
    // Flat fades and fills still extend, and a 3D edge remains transparent.
    flat[0] = 1;
    auto fill = MergedEdgeContinuation(scene, top, 0, 0);
    assert(!fill.none() && !fill.pattern && fill.evb == 0);
    flags[0] = flags[1] = 0x40; // (one 3D column alone is stepped over: Fly's sky band)
    assert(MergedEdgeContinuation(scene, top, 0, 0).none());
    // The flat strokes of the same closed window must not stick out either.
    std::fill(flags.begin(), flags.end(), 2);
    std::fill(flat, flat + 384, 1);
    flat[5 * 2] = 0;
    pixels[5 * 256] = 0x123456;
    MergedPrepareEdgeWindows(scene, &top);
    assert(MergedEdgeContinuation(scene, top, 0, 0).none());
    assert(MergedEdgeContinuation(scene, top, 5, 0).none());
    // Recomputing for the next frame must clear the mask: a full fade still fills.
    std::fill(flat, flat + 384, 1);
    MergedPrepareEdgeWindows(scene, &top);
    assert(!MergedEdgeContinuation(scene, top, 0, 0).none());
    // A fine pattern (the same 8 pixels twice) goes on though the next tile in has letters on it
    // (Black 2's SUMMER band at the right edge).
    std::fill(pixels.begin(), pixels.end(), 0xEEEEEE);
    std::fill(flags.begin(), flags.end(), 2);
    std::fill(flat, flat + 384, 0);
    for (int y = 40; y < 44; ++y)
        for (int x = 0; x < 256; ++x) pixels[y * 256 + x] = ((x + y) & 1) ? 0x202020 : 0x000000;
    for (int x = 226; x < 234; ++x) pixels[40 * 256 + x] = 0x66CCEE;
    MergedPrepareEdgeWindows(scene, &top);
    assert(MergedEdgeContinuation(scene, top, 40, 1).pattern);
    assert(MergedEdgeContinuation(scene, top, 40, 1).row == 40);
    // A sprite over the band's edge tile (the badge beside a place's name): the row goes on as
    // the band's row two away does.
    for (int x = 2; x < 12; ++x) { flags[40 * 256 + x] = 0x10; pixels[40 * 256 + x] = 0x118899; }
    const EdgeContinuation lent = MergedEdgeContinuation(scene, top, 40, 0);
    assert(lent.pattern && lent.row == 42);
    puts("Wide edges: tiled banners preserved; frame borders/arrows not repeated");
}
