// keyGrey leaves out the dark greys of a piece (greys no brighter than it, black too) and keeps
// its colours and its light greys and whites, in the nearest-pixel and the smooth compose and in
// the GPU compose alike, also once the whole picture is lightened or darkened with the limit
// moved to match, as Gen 5's battle buttons are drawn: each label (or icon) cut from the touch
// screen without the dark around it, set to the button's inside colour plus 40, the inside read
// off the touch screen as it lightens while a button is pressed.
// ROM-free: build with C++17 and SDL2, then run with SDL_VIDEODRIVER=dummy.
#include "gpucompose.h"
#include <cassert>
#include <cstdio>

static uint32_t At(const uint32_t* px, int pitch, int w, int h, float bx, float by)
{
    const MergedView v = MergedMakeView(w, h);
    const int x = (int)(v.originX + bx * v.unit), y = (int)(v.originY + by * v.unit);
    return px[(size_t)y * pitch + x] & 0xFFFFFF;
}

// The colour lightened toward white (f > 0) or darkened toward black (f < 0), as the DS's
// brightness effects do.
static uint32_t Bright(uint32_t c, float f)
{
    uint32_t out = 0;
    for (int shift : { 16, 8, 0 })
    {
        const float v = (float)((c >> shift) & 0xFF);
        const float w = f >= 0 ? v + (255 - v) * f : v * (1 + f);
        out |= (uint32_t)(w + 0.5f) << shift;
    }
    return out;
}

int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    // Black 2's touch screen: a bar of black under a label (0x202020), the dotted grey of a
    // button's inside (0x383838), a yellow label (0xFBCB28), a white arrow (0xEBF3FB) shaded
    // light grey (0xB2B2B2).
    const uint32_t bar = 0x202020, dots = 0x383838, yellow = 0xFBCB28, white = 0xEBF3FB, grey = 0xB2B2B2;
    const uint32_t behind = 0x204060;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    for (float f : { 0.0f, 0.44f, -0.4f }) // as it is, pressed (lightened), fading (darkened)
    {
        // A 64 x 32 piece in four columns of 16: bar, dots, yellow, white; light grey in the
        // white's lower half. Each 3 pixels from the next at least, so that the smooth compose has
        // its middles unblended.
        std::vector<uint32_t> pix(256 * 192, 0xFF000000 | Bright(bar, f));
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 64; x++)
            {
                const uint32_t c = x < 16 ? bar : x < 32 ? dots : x < 48 ? yellow : y < 16 ? white : grey;
                pix[y * 256 + x] = 0xFF000000 | Bright(c, f);
            }
        ScreenImage screen{ pix.data(), nullptr };
        ScreenImage screens[2] = { screen, screen };
        Scene sc;
        sc.items = { FillRect(-100, -100, 600, 400, behind), Crop(0, 0, 0, 64, 32) };
        sc.items[1].scale = 2;
        sc.items[1].keyGrey = (int)((Bright(dots, f) >> 16) & 0xFF) + 40;
        auto check = [&](const uint32_t* px, int pitch, const char* path) {
            auto at = [&](float x, float y) { return At(px, pitch, 1024, 768, 2 * x, 2 * y); };
            const uint32_t b = at(7.5f, 7.5f), d = at(23.5f, 7.5f), ye = at(39.5f, 7.5f), wh = at(55.5f, 7.5f), gr = at(55.5f, 23.5f);
            fprintf(stderr, "%s, brightness %+.2f: bar %06X dots %06X yellow %06X white %06X grey %06X\n", path, f, b, d, ye, wh, gr);
            assert(b == behind && d == behind);                                            // the dark goes
            assert(ye == Bright(yellow, f) && wh == Bright(white, f) && gr == Bright(grey, f)); // the label stays
        };
        for (bool smooth : { false, true })
        {
            std::vector<uint32_t> out;
            MergedCompose(sc, screens, 1024, 768, out, smooth);
            check(out.data(), 1024, smooth ? "smooth" : "nearest");
        }
        {
            GpuCompositor compositor(renderer);
            compositor.Draw(sc, screens, 1024, 768, false, nullptr);
            SDL_RenderPresent(renderer);
            check(static_cast<const uint32_t*>(surface->pixels), surface->pitch / 4, "gpu");
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("keyGrey: the dark around a label left out, its colours and light greys kept, lightened or darkened, by every compose path");
}
