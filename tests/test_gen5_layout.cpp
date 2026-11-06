// ROM-free regression: build with C++17 and SDL2, then run with SDL_VIDEODRIVER=dummy.
#include <cassert>
#include <cstdio>
#include "../frontend/gpucompose.h"

int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    constexpr uint32_t blue = 0xFF103870, white = 0xFFF8F8F8, ink = 0xFF303030;
    std::vector<uint32_t> top(256 * 192, blue), bottom(256 * 192, blue);
    // An item's description and a distinct colour in the last row of its move stats.
    for (int y = 40; y < 50; y++)
        for (int x = 16; x < 96; x++) top[y * 256 + x] = white;
    for (int x = 8; x < 248; x++) top[191 * 256 + x] = 0xFF00E000;
    ScreenImage screens[2] = { { top.data(), nullptr }, { bottom.data(), nullptr } };
    for (int width : { 1024, 1280, 1920, 2560 })
        for (bool isTm : { false, true })
            for (bool message : { false, true })
            {
                std::fill(top.begin(), top.end(), blue);
                for (int y = 40; y < 50; y++)
                    for (int x = 16; x < 96; x++) top[y * 256 + x] = white;
                if (isTm)
                {
                    for (int y = 160; y < 176; y++)
                        for (int x = 16; x < 160; x++) top[y * 256 + x] = white;
                }
                std::fill(bottom.begin(), bottom.end(), blue);
                if (message)
                {
                    for (int y = 2; y < 44; y++)
                        for (int x = 2; x < 254; x++) bottom[y * 256 + x] = white;
                    for (int y = 8; y < 18; y++)
                        for (int x = 8; x < 90; x++) bottom[y * 256 + x] = ink;
                }
                const MergedView view = MergedMakeView(width, 768);
                const Scene sc = BagScene(view.width, screens);
                const Element& list = sc.items[0];
                const Element& card = sc.items.back();
                assert(list.screen == 1 && list.sw == 256 && list.sh == 192);
                assert(card.screen == 0 && card.sw == 240 && card.sh == (isTm ? 164 : 122));
                assert(card.border);
                assert(card.dx >= 128 - view.width / 2 - 0.01f);
                assert(card.dx + card.sw * card.scale <= list.dx + 112.0f + 0.01f); // Never covers the item list.
                if (message)
                    assert(card.dy >= 46.0f); // Leaves the top message box uncovered.
                else
                    assert(card.dy + card.sh * card.scale <= 168.0f + 0.01f); // Above the bottom pocket bar.

                std::vector<uint32_t> cpu;
                MergedCompose(sc, screens, width, 768, cpu, false, nullptr);
                SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, 768, 32, SDL_PIXELFORMAT_ARGB8888);
                assert(surface);
                SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
                assert(renderer);
                {
                    GpuCompositor compositor(renderer);
                    compositor.Draw(sc, screens, width, 768, false, nullptr);
                    SDL_RenderPresent(renderer);
                    if (message)
                    {
                        // The touch screen's message is shown whole by both composition paths.
                        const int x = (int)(view.originX + (list.dx + 30) * view.unit);
                        const int y = (int)(view.originY + (list.dy + 12) * view.unit);
                        assert((cpu[y * width + x] & 0xFFFFFF) == (ink & 0xFFFFFF));
                        const auto* row = reinterpret_cast<const uint32_t*>(
                            static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch);
                        assert((row[x] & 0xFFFFFF) == (ink & 0xFFFFFF));
                    }
                }
                SDL_DestroyRenderer(renderer);
                SDL_FreeSurface(surface);
            }
    // Empty pockets and missing screen images keep the single-screen Bag list without a card.
    std::fill(top.begin(), top.end(), blue);
    assert(BagScene(341.33f, screens).items.back().screen != 0);
    assert(BagScene(341.33f, nullptr).items.size() == 1);

    // Battle questions: the question at full size, both answers in the message bar,
    // right of the text and inside the window.
    for (float width : { 256.0f, 341.33f, 455.11f, 512.0f })
    {
        const Scene question = BattleChoiceScene(width);
        assert(question.items.size() == 3);
        const Element& text = question.items[0];
        assert(text.screen == 0 && text.sy == 0 && text.sh == 192 && text.scale == 1);
        float last = 0;
        for (int i = 1; i <= 2; i++)
        {
            const Element& b = question.items[i];
            assert(b.screen == 1 && b.sw == 252 && b.sh == 47);
            assert(b.dy >= 147 && b.dy + b.sh * b.scale <= 190); // In the bar.
            assert(b.dy >= last);                                // Stacked, no overlap.
            assert(b.dx >= 168);                                 // Clear of the question.
            assert(b.dx + b.sw * b.scale <= 128 + width / 2);    // Inside the window.
            last = b.dy + b.sh * b.scale;
        }
    }
    SDL_Quit();
    puts("Gen 5 layouts: single-screen Bag, battle questions and compositors passed.");
}
