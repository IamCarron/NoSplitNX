// ROM-free regression: build with C++17 and SDL2, then run with SDL_VIDEODRIVER=dummy.
#include <cassert>
#include <cstdio>
#include "../frontend/gpucompose.h"
#include "../frontend/bw1.h"

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

    // Uniform black fade frames must never keep the Bag blue FillRect or empty card border.
    std::fill(top.begin(), top.end(), 0xFF000000u);
    std::fill(bottom.begin(), bottom.end(), 0xFF000000u);
    {
        const Scene fadedBag = BagScene(341.33f, screens);
        assert(fadedBag.items.size() == 1);
        assert(fadedBag.items[0].screen == 1 && !fadedBag.items[0].border);
    }

    // Gen 5 battle prompt adaptation: moves bottom prompt into a framed left box on a dark tray
    // across wide screens, mutually co-sized with right-hand buttons so both halves meet evenly.
    {
        constexpr uint32_t darkTray = 0xFF1A2024u, crimson = 0xFF682428u, arena = 0xFF103020u;
        std::fill(top.begin(), top.end(), arena);
        std::fill(bottom.begin(), bottom.end(), 0xFF202028u);
        for (int x = 0; x < 256; x++)
        {
            for (int y = 147; y < 150; y++) top[y * 256 + x] = crimson;
            for (int y = 150; y < 192; y++) top[y * 256 + x] = darkTray;
        }
        // Prompt glyphs spanning x=16..180 (similar to "¿Qué debería hacer Latios?").
        for (int y = 158; y < 174; y++)
            for (int x = 16; x < 180; x++)
                top[y * 256 + x] = white;

        Scene battle;
        battle.name = "battle";
        battle.wide = true;
        battle.items = { FullScreen(0) };
        float promptRight = 0.0f, usedBtnW = 0.0f;
        const float viewW = 341.333f;
        const float viewLeft = 128.0f - viewW / 2.0f;
        const float viewRight = 128.0f + viewW / 2.0f;
        float minBtnW = 0.0f, idealBtnW = 0.0f, maxBtnW = 0.0f;
        Gen5BattleButtonBounds(0, false, false, 110, minBtnW, idealBtnW, maxBtnW);
        assert(AdaptGen5BattlePrompt(battle, viewW, screens, minBtnW, idealBtnW, maxBtnW, promptRight, usedBtnW));
        assert(battle.items[0].holeCount == 1);
        assert(battle.items[0].holes[0][1] == 147 && battle.items[0].holes[0][3] == 192);
        const Element& textBox = battle.items.back();
        const Element& frame = battle.items[3];
        assert(frame.screen == -1 && frame.border);
        assert(textBox.screen == 0 && textBox.stretchX == 1.0f && textBox.scale >= 0.99f);
        assert(frame.dx < 0.0f && frame.dx <= viewLeft + 4.5f); // shifted into widescreen left margin
        const float trayGap = (viewRight - 2.0f - usedBtnW) - promptRight;
        assert(trayGap >= 4.0f && trayGap <= 6.5f); // mutually meets right-hand buttons cleanly

        std::vector<uint32_t> cpu;
        MergedCompose(battle, screens, 1280, 720, cpu, false, nullptr);
        const MergedView mv = MergedMakeView(1280, 720);
        const int gx = (int)(mv.originX + (textBox.dx + 12.0f) * mv.unit);
        const int gy = (int)(mv.originY + 166.0f * mv.unit);
        assert(gx >= 0 && gx < 1280 && gy >= 0 && gy < 720);
        assert((cpu[gy * 1280 + gx] & 0xFFFFFF) == (white & 0xFFFFFF));

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
        assert(surface);
        SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
        assert(renderer);
        {
            GpuCompositor compositor(renderer);
            compositor.Draw(battle, screens, 1280, 720, false, nullptr);
            SDL_RenderPresent(renderer);
            const auto* row = reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(surface->pixels) + gy * surface->pitch);
            assert((row[gx] & 0xFFFFFF) == (white & 0xFFFFFF));
        }
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
    }

    // Black 1 bottom arrow banner detector must match real 2D strips and ignore bright 3D map snow/water.
    {
        std::vector<uint8_t> flags(256 * 192, 0x02); // LAYER_BG1 (2D)
        std::fill(top.begin(), top.end(), 0xFF202020u);
        for (int y = 172; y < 192; y++)
            for (int x = 0; x < 256; x++)
                top[y * 256 + x] = 0xFFF8F8F8u;
        for (int y = 174; y < 188; y++)
            for (int x = 122; x < 134; x++)
                top[y * 256 + x] = 0xFF00D8F0u; // cyan arrow
        assert(Bw1Layouts::TopHasBottomArrowBanner(top.data(), flags.data()));

        // Same pixels rendered on 3D layer (0x40) or broken by terrain columns must be rejected.
        std::fill(flags.begin() + 170 * 256, flags.end(), 0x40); // LAYER_3D
        assert(!Bw1Layouts::TopHasBottomArrowBanner(top.data(), flags.data()));
        std::fill(flags.begin(), flags.end(), 0x02);
        for (int y = 172; y < 192; y++) top[y * 256 + 48] = 0xFF305080u;
        assert(!Bw1Layouts::TopHasBottomArrowBanner(top.data(), flags.data()));
    }

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
