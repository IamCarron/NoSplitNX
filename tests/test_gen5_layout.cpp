// ROM-free regression: build with C++17 and SDL2, then run with SDL_VIDEODRIVER=dummy.
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../frontend/gpucompose.h"
#include "../frontend/bw1.h"
#include "../frontend/gen5strip.h"
#include "../frontend/ds_zoom.h"
#include "../frontend/ui.h"
#include "../frontend/sm_buttons_art.h"

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

    // Gen5Strip::ChoiceBars, KeepBoxes, Commands and Below.
    {
        std::vector<uint32_t> q(256 * 192, 0xFF204020), t(256 * 192, 0xFF101010);
        std::vector<uint8_t> tf(256 * 192, 0);
        for (int y = 160; y < 172; y++)
            for (int x = 8; x < 150; x += 3) q[y * 256 + x] = 0xFFF8F8F8;
        const ScreenImage choice[2] = { { q.data(), nullptr }, { t.data(), tf.data() } };
        for (float width : { 256.0f, 307.2f, 341.33f, 455.11f })
        {
            const float right = 128 + width / 2 - 2;
            Scene sc = BattleChoiceScene(width);
            assert(!Gen5Strip::ChoiceBars(sc, right, choice, 147, 42));
            for (const Element& e : sc.items) assert(e.screen != 1);
            for (int topRow : { 34, 82 })
                for (int y = topRow; y < topRow + 44; y++)
                    for (int x = 9; x < 247; x++) tf[y * 256 + x] = LAYER_BG1;
            sc = BattleChoiceScene(width);
            assert(Gen5Strip::ChoiceBars(sc, right, choice, 147, 42));
            int bars = 0;
            for (const Element& e : sc.items)
                if (e.screen == 1)
                {
                    bars++;
                    assert(e.sx == 0 && e.sy == 32 && e.sw == 256 && e.sh == 96 && e.drop == LAYER_BACKDROP);
                    assert(e.dy >= 146.5f && e.dy + e.sh * e.scale <= 189.5f);
                    assert(e.dx >= 150 + 4 - 0.01f || e.scale <= 0.36f + 1e-4f);
                    assert(e.dx + e.sw * e.scale <= right + 0.01f);
                }
            assert(bars == 1);
            std::fill(tf.begin(), tf.end(), 0);
        }
    }
    {
        Element battleTop = FullScreen(0);
        battleTop.holeCount = 2;
        const int holes[2][4] = { { 0, 26, 116, 56 }, { 138, 106, 256, 142 } };
        std::copy(&holes[0][0], &holes[0][0] + 8, &battleTop.holes[0][0]);
        Element foe = Crop(0, 0, 26, 116, 30), mine = Crop(0, 138, 106, 118, 36);
        foe.dx = -25.6f;
        mine.dx = 281.6f - 118;
        foe.wideOnly = mine.wideOnly = true;
        Scene sc = BattleChoiceScene(307.2f);
        const size_t before = sc.items.size();
        Gen5Strip::KeepBoxes(sc, battleTop, { foe, mine }, 1);
        assert(sc.items.size() == before + 2);
        assert(sc.items[0].holeCount == 2 && sc.items[0].holes[1][0] == 138 && sc.items[0].holes[1][3] == 142);
        assert(sc.items[1].wideOnly && sc.items[1].dx == foe.dx && sc.items[2].dx == mine.dx);
        Scene plain = BattleChoiceScene(307.2f);
        Gen5Strip::KeepBoxes(plain, FullScreen(0), { foe, mine }, 1);
        assert(plain.items.size() == before && plain.items[0].holeCount == 0);
    }
    {
        std::vector<uint32_t> t(256 * 192, 0xFF303030);
        std::vector<uint8_t> tf(256 * 192, LAYER_BG0);
        const ScreenImage touch{ t.data(), tf.data() };
        for (const Gen5Strip::CommandLook* look : { &Gen5Strip::Black2Commands(), &Gen5Strip::BlackCommands() })
        {
            const float s = 0.44f, right = 279.6f, left = right - 256 * s, topY = 148 + (40 - 96 * s) / 2 - 32 * s;
            Scene sc;
            Gen5Strip::Commands(sc, touch, *look, left, topY, s);
            int fills = 0, areas = 0;
            for (const Element& e : sc.items)
            {
                if (e.screen < 0)
                {
                    fills++;
                    assert(e.border && e.borderRadius > 0);
                    assert(e.dy >= 146.5f && e.dy + e.sh * e.scale <= 189.5f);
                    assert(e.dx >= left - 0.01f && e.dx + e.sw * e.scale <= right + 0.01f);
                }
                if (e.alpha == 0) areas++;
            }
            assert(fills == 4 && areas == 4);
            for (int i = 0; i < 4; i++)
            {
                const int* c = look->cell[i];
                const int* area = look->button[i].touch;
                for (float fx : { 0.05f, 0.5f, 0.95f })
                    for (float fy : { 0.05f, 0.5f, 0.95f })
                    {
                        int tx, ty;
                        assert(MergedTouch(sc, left + (c[0] + c[2] * fx) * s, topY + (c[1] + c[3] * fy) * s, tx, ty));
                        assert(tx >= area[0] && tx < area[0] + area[2] && ty >= area[1] && ty < area[1] + area[3]);
                    }
            }
            for (int y = 109; y <= 125; y++)
                for (int x : { 95, 100, 111, 140, 150, 156 }) tf[y * 256 + x] = 0x10;
            Scene two;
            Gen5Strip::Commands(two, touch, *look, left, topY, s);
            const int* fight = look->cell[0];
            const float middle = left + (fight[0] + fight[2] / 2.0f) * s;
            const Element* label = nullptr;
            const Element* icons = nullptr;
            for (const Element& e : two.items)
            {
                if (e.screen == 1 && e.sx == look->button[0].label[0] && e.sy == look->button[0].label[1]) label = &e;
                if (e.screen == 1 && e.sx == 95 && e.sy == 109 && e.sw == 62) icons = &e;
            }
            assert(label && icons);
            assert(label->dx + label->sw * label->scale / 2 < middle);
            assert(icons->dx > label->dx + label->sw * label->scale && icons->dx + icons->sw * icons->scale <= left + (fight[0] + fight[2]) * s);
            std::fill(tf.begin(), tf.end(), LAYER_BG0);
            t[150 * 256 + 2] = t[188 * 256 + 76] = 0xFF000000 | look->cursor;
            Scene cur;
            Gen5Strip::Commands(cur, touch, *look, left, topY, s);
            const int* bag = look->cell[1];
            int brackets = 0;
            for (const Element& e : cur.items)
                if (e.screen == 1 && e.alpha && e.drop == (LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP) && e.sw == 16)
                {
                    brackets++;
                    const float cx = e.dx + 8 * s, cy = e.dy + 8 * s;
                    assert(cx > left + (bag[0] - 16) * s && cx < left + (bag[0] + bag[2] + 16) * s);
                    assert(cy > topY + (bag[1] - 16) * s && cy < topY + (bag[1] + bag[3] + 16) * s);
                }
            assert(brackets == 4);
            t[150 * 256 + 2] = t[188 * 256 + 76] = 0xFF303030;
        }
        // Localized wider label (like Spanish BOLSA on Black BAG button) expands crop safely inside interior.
        for (int x = 20; x < 60; x++) t[167 * 256 + x] = white;
        Scene esSc;
        Gen5Strip::Round(esSc, touch, Gen5Strip::BlackCommands().button[1], 160, 165, 40, 20, 0.4f);
        assert(esSc.items.size() == 2 && esSc.items[1].sx <= 20 && esSc.items[1].sx + esSc.items[1].sw >= 60);
    }
    {
        std::vector<uint32_t> t(256 * 192, 0xFF303030);
        std::vector<uint8_t> tf(256 * 192, LAYER_BG0);
        const ScreenImage touch{ t.data(), tf.data() };
        const Gen5Strip::BelowButton* below[2] = { &Gen5Strip::Black2Shift(), &Gen5Strip::Black2Back() };
        const float s = 0.4f, from = 180, y = 150, h = 36;
        Scene sc;
        Gen5Strip::Below(sc, touch, below, 2, from, y, h, s, 3, 0xC31028);
        std::vector<const Element*> fills;
        for (const Element& e : sc.items)
            if (e.screen < 0) fills.push_back(&e);
        assert(fills.size() == 2);
        assert(std::fabs(fills[0]->dx + fills[0]->sw * fills[0]->scale - (from - 3)) < 0.01f);
        assert(fills[1]->dx + fills[1]->sw * fills[1]->scale < fills[0]->dx);
        for (int i = 0; i < 2; i++)
        {
            const Element& f = *fills[i];
            assert(std::fabs(f.dy - y) < 0.01f && std::fabs(f.sh * f.scale - h) < 0.05f);
            const int* area = below[1 - i]->look.touch;
            int tx, ty;
            assert(MergedTouch(sc, f.dx + f.sw * f.scale / 2, f.dy + h / 2, tx, ty));
            assert(tx >= area[0] && tx < area[0] + area[2] && ty >= area[1] && ty < area[1] + area[3]);
        }
    }

    // SmButtonsArt: atlas fits in 256x192 with valid cap metrics across supported languages.
    for (int lang : { 0, 1, 2, 3, 4 })
    {
        SmButtonsArt::Ensure(lang);
        for (int b = 0; b < SmButtons::COUNT; b++)
        {
            const SmButtons::Label& l = SmButtons::S().label[b];
            assert(l.w > 0 && l.h > 0 && l.x + l.w <= 256 && l.y + l.h <= 192);
            assert(l.capH > 5.0f && l.capTop >= 0.0f);
        }
    }

    // DsZoom camera: steps, eases out on overworld, looks around and snaps back on close.
    {
        DsZoom z;
        assert(z.Cycle() == 1 && z.Factor(1) == 1.5f);
        for (int i = 0; i < 60; i++) z.Update(false, 0.8f, -0.5f, false, 1.0f / 60);
        assert(z.zoom > 1.35f && z.lookX > 0.1f && z.lookY > 0.05f);
        for (int i = 0; i < 60; i++) z.Update(true, 0.0f, 0.0f, false, 1.0f / 60);
        assert(std::fabs(z.zoom - 1.0f) < 0.01f && std::fabs(z.lookX) < 0.01f && std::fabs(z.lookY) < 0.01f);
    }

    // Unified STRIP (authentic BW1 + B2W2 commands, PartyRows Poke Balls, hexagonal moves + Shift/Back, and mutual AdaptGen5BattlePrompt co-sizing).
    {
        constexpr uint32_t darkTray = 0xFF1A2024u, crimson = 0xFF682428u, arena = 0xFF103020u;
        std::vector<uint32_t> promptTop(256 * 192, arena), bw1Touch(256 * 192, 0xFF282830u), bw2Touch(256 * 192, 0xFF202028u);
        std::vector<uint8_t> bw1Flags(256 * 192, LAYER_BACKDROP), bw2Flags(256 * 192, LAYER_BACKDROP);
        for (int x = 0; x < 256; x++)
        {
            for (int y = 147; y < 150; y++) promptTop[y * 256 + x] = crimson;
            for (int y = 150; y < 192; y++) promptTop[y * 256 + x] = darkTray;
        }
        for (int y = 158; y < 174; y++)
            for (int x = 16; x < 185; x++)
                promptTop[y * 256 + x] = white;

        auto setupCommandFlags = [](std::vector<uint8_t>& fl, int topBagY, int topRunY) {
            std::fill(fl.begin(), fl.end(), LAYER_BACKDROP);
            fl[170 * 256 + 39] = LAYER_BG0;
            fl[180 * 256 + 127] = LAYER_BG0;
            fl[170 * 256 + 216] = LAYER_BG0;
            fl[topBagY * 256 + 39] = LAYER_BG0;
            fl[topRunY * 256 + 127] = LAYER_BG0;
            fl[topBagY * 256 + 216] = LAYER_BG0;
        };
        auto setupMoveFlags = [](std::vector<uint8_t>& fl) {
            std::fill(fl.begin(), fl.end(), LAYER_BACKDROP);
            for (int y : { 74, 120 })
                for (int x : { 40, 216 })
                    fl[y * 256 + x] = LAYER_BG1;
            fl[175 * 256 + 39] = LAYER_BG0;
            fl[150 * 256 + 39] = LAYER_BG0;
            fl[175 * 256 + 216] = LAYER_BG0;
        };

        const ScreenImage sBw1[2] = { { promptTop.data(), nullptr }, { bw1Touch.data(), bw1Flags.data() } };
        const ScreenImage sBw2[2] = { { promptTop.data(), nullptr }, { bw2Touch.data(), bw2Flags.data() } };
        const float viewW = 341.333f, viewLeft = 128.0f - viewW / 2.0f, viewRight = 128.0f + viewW / 2.0f;
        const Gen5Strip::PartyPlaces party{ viewLeft + 109.0f, 53.0f, viewRight - 3.0f, 131.0f };

        for (bool isMoves : { false, true })
        {
            if (!isMoves)
            {
                setupCommandFlags(bw1Flags, 133, 149);
                setupCommandFlags(bw2Flags, 146, 156);
                // BW1 magenta cursor around BAG + BW2 crimson cursor around RUN.
                std::fill(bw1Touch.begin(), bw1Touch.end(), 0xFF282830u);
                std::fill(bw2Touch.begin(), bw2Touch.end(), 0xFF202028u);
                bw1Touch[146 * 256 + 6] = bw1Touch[182 * 256 + 74] = 0xFFFB0092u;
                bw2Touch[154 * 256 + 94] = bw2Touch[188 * 256 + 162] = 0xFFC31028u;
            }
            else
            {
                setupMoveFlags(bw1Flags);
                setupMoveFlags(bw2Flags);
            }

            Scene sc1;
            sc1.name = "battle";
            sc1.wide = true;
            sc1.items = { FullScreen(0) };
            float pr1 = 0.0f, btnW1 = 0.0f, minW = 0.0f, idealW = 0.0f, maxW = 0.0f;
            Gen5BattleButtonBounds(1, isMoves, false, 103, minW, idealW, maxW);
            assert(AdaptGen5BattlePrompt(sc1, viewW, sBw1, minW, idealW, maxW, pr1, btnW1));
            const float avail1 = std::max(84.0f, viewRight - 2.0f - (pr1 + 5.0f));
            Bw1Layouts::BattleStrip(sc1, viewRight, sBw1, isMoves, party, avail1, true);

            Scene sc2;
            sc2.name = "battle";
            sc2.wide = true;
            sc2.items = { FullScreen(0) };
            float pr2 = 0.0f, btnW2 = 0.0f;
            Gen5BattleButtonBounds(1, isMoves, false, 110, minW, idealW, maxW);
            assert(AdaptGen5BattlePrompt(sc2, viewW, sBw2, minW, idealW, maxW, pr2, btnW2));
            const float avail2 = std::max(84.0f, viewRight - 2.0f - (pr2 + 5.0f));
            Gen5Strip::Black2(sc2, viewRight, sBw2, isMoves, party, avail2, true);

            for (const auto& pair : { std::make_pair(&sc1, pr1), std::make_pair(&sc2, pr2) })
            {
                const Scene& sc = *pair.first;
                const float pr = pair.second;
                int balls = 0, cmdPlates = 0, hexagons = 0, curBrackets = 0;
                for (const Element& e : sc.items)
                {
                    if (e.screen == 1 && e.drop == (LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP)
                        && (e.sw == 49 || e.sw == 50 || e.sw == 94))
                        balls++;
                    if (e.screen == 1 && e.alpha > 0 && e.dy >= 146.0f)
                    {
                        if (e.drop == LAYER_BACKDROP && e.sw == 256 && e.sh == 96) hexagons++;
                        else if (e.drop == LAYER_BACKDROP || e.sw == 92 || e.sw == 148) cmdPlates++;
                        assert(e.dx >= pr + 4.9f || e.sw == 16); // cursor brackets overhang slightly left
                        assert(e.dx + e.sw * e.scale <= viewRight + 0.01f);
                    }
                    if (e.screen == 1 && e.drop == (LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP) && e.sw == 16)
                        curBrackets++;
                }
                assert(balls == 2); // Both foe and player Poke Ball rows rendered
                if (!isMoves)
                {
                    assert(cmdPlates == 4); // Authentic FIGHT! banner + BAG/RUN/POKEMON plates
                    assert(curBrackets == 4);
                }
                else
                    assert(hexagons == 1); // Moves + Shift + Back inside tray
            }
            if (!isMoves)
            {
                int tx = 0, ty = 0;
                assert(MergedTouch(sc2, viewRight - 20.0f, 155.0f, tx, ty));
                assert(tx >= 100 && tx < 156 && ty >= 60 && ty < 120);
            }
            else
            {
                int tx = 0, ty = 0;
                assert(MergedTouch(sc2, viewRight - 140.0f, 168.0f, tx, ty));
                assert((tx >= 8 && tx < 68) || (tx >= 186 && tx < 246));
            }
        }

        // Authentic FIGHT! with double/triple partner Pokemon icons + authentic B2W2 Below stripes + animation guard.
        setupCommandFlags(bw2Flags, 146, 156);
        for (int y = 109; y <= 125; y++)
            for (int x : { 95, 111, 140, 156 }) bw2Flags[y * 256 + x] = 0x10;
        Scene twoAuth;
        Gen5Strip::Commands(twoAuth, sBw2[1], Gen5Strip::Black2Commands(), viewRight - 115.0f, 134.0f, 0.44f, true);
        const Element* banner = nullptr;
        const Element* partnerIcons = nullptr;
        for (const Element& e : twoAuth.items)
        {
            if (e.screen == 1 && e.sx == 82 && e.sy == 74 && e.sw == 92) banner = &e;
            if (e.screen == 1 && e.sx == 95 && e.sy == 109 && e.sw == 62) partnerIcons = &e;
        }
        assert(banner && partnerIcons && partnerIcons->dx > banner->dx + banner->sw * banner->scale);

        Scene belowAuth;
        const Gen5Strip::BelowButton* b[1] = { &Gen5Strip::Black2Shift() };
        Gen5Strip::Below(belowAuth, sBw2[1], b, 1, 200.0f, 150.0f, 36.0f, 0.4f, 3.0f, 0xC31028, true);
        int stripeBars = 0;
        for (const Element& e : belowAuth.items)
            if (e.screen < 0 && !e.border) stripeBars++;
        assert(stripeBars == 3); // top metallic bevel + upper & lower B2W2 accent bars

        std::fill(bw2Flags.begin(), bw2Flags.end(), LAYER_BACKDROP);
        Scene midAnim;
        midAnim.items = { FullScreen(0) };
        Gen5Strip::Black2(midAnim, viewRight, sBw2, false, party, 113.0f, true);
        assert(midAnim.items.size() == 1); // mid-animation Poke Ball skips premature buttons/balls
    }

    SDL_Quit();
    puts("Gen 5 layouts: single-screen Bag, battle questions, Gen5Strip, authentic STRIP + Poke Balls, SmButtons, DsZoom and compositors passed.");
}
