#include <SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include "gba_host_ui.h"

int main()
{
    std::vector<GbaWide::MenuHostItem> items;
    items.push_back({ "RESUME", "", false, -1 });
    for (int i = 0; i < 30; i++)
        items.push_back({ "CHEAT " + std::to_string(i), "OFF", false, -1 });
    items.push_back({ "BACK", "", false, -1 });

    std::vector<uint32_t> px;
    GbaWide::MenuHostState hostMenu;
    int selected = 0;
    const GbaWide::UiHost* api = GbaHostUi::Api();
    auto redraw = [&](int w, int h) {
        api->drawOverlay(api->ud, px, w, h, true, items, selected, "CHEATS", "A: ON / OFF   B: BACK", "", "", 0, 60, false, "cyan", hostMenu);
    };

    for (auto sz : { std::pair<int, int>{ 1280, 720 }, { 720, 1280 } })
    {
        hostMenu = {};
        selected = 0;
        redraw(sz.first, sz.second);
        assert(hostMenu.lineH > 0 && hostMenu.shown > 0 && hostMenu.count == (int)items.size());

        // Simulate Finger() swipe tracking extracted contract:
        bool down = false, moved = false, accepted = false, wentBack = false;
        SDL_FingerID fid = 0;
        int x0 = 0, y0 = 0, lastY = 0;
        int page = 0, downPage = 0;

        auto finger = [&](Uint32 type, SDL_FingerID id, int x, int y) {
            if (type == SDL_FINGERDOWN)
            {
                if (!down) { down = true; moved = false; fid = id; x0 = x; y0 = lastY = y; downPage = page; }
                return;
            }
            if (!down || id != fid) return;
            if (type == SDL_FINGERMOTION)
            {
                if (!moved && std::hypot((double)(x - x0), (double)(y - y0)) > 12.0) moved = true;
                if (moved && hostMenu.lineH > 0 && hostMenu.shown > 0 && hostMenu.count > hostMenu.shown)
                {
                    const int stepPx = std::max(8, hostMenu.lineH);
                    const int maxScroll = std::max(0, hostMenu.count - hostMenu.shown);
                    while (lastY - y >= stepPx)
                    {
                        lastY -= stepPx;
                        if (hostMenu.scroll < maxScroll)
                        {
                            hostMenu.scroll++;
                            selected = std::clamp(selected, hostMenu.scroll, hostMenu.scroll + hostMenu.shown - 1);
                        }
                    }
                    while (y - lastY >= stepPx)
                    {
                        lastY += stepPx;
                        if (hostMenu.scroll > 0)
                        {
                            hostMenu.scroll--;
                            selected = std::clamp(selected, hostMenu.scroll, hostMenu.scroll + hostMenu.shown - 1);
                        }
                    }
                }
                return;
            }
            if (type == SDL_FINGERUP)
            {
                const bool tap = !moved && downPage == page;
                down = moved = false;
                if (!tap) return;
                const int hit = hostMenu.ItemAt(x, y);
                if (hit == -2) wentBack = true;
                else if (hit >= 0) { selected = hit; accepted = true; }
            }
        };

        const int hitX = hostMenu.boxX + hostMenu.boxW / 2;
        const int hitY = hostMenu.firstY + 2 * hostMenu.lineH + hostMenu.lineH / 2;
        assert(hostMenu.ItemAt(hitX, hitY) == 2);

        // Touch down -> another finger cannot take over -> vertical drag scrolls without activating.
        finger(SDL_FINGERDOWN, 7, hitX, hitY);
        assert(!accepted && !wentBack);
        finger(SDL_FINGERMOTION, 8, hitX, hitY - hostMenu.lineH * 3);
        assert(hostMenu.scroll == 0);
        finger(SDL_FINGERMOTION, 7, hitX, hitY - hostMenu.lineH * 3);
        assert(hostMenu.scroll == 3 && !accepted && !wentBack);
        finger(SDL_FINGERUP, 7, hitX, hitY - hostMenu.lineH * 3);
        assert(!accepted && !wentBack);
        redraw(sz.first, sz.second);
        assert(hostMenu.scroll == 3); // Menu::Draw kept scrolled position

        // Short tap activates once on release.
        const int targetRowY = hostMenu.firstY + 2 * hostMenu.lineH + hostMenu.lineH / 2;
        finger(SDL_FINGERDOWN, 9, hitX, targetRowY);
        assert(!accepted);
        finger(SDL_FINGERUP, 9, hitX, targetRowY);
        assert(accepted && selected == 5);
        accepted = false;

        // Changing page mid-touch cancels release activation.
        finger(SDL_FINGERDOWN, 10, hitX, targetRowY);
        page = 1;
        finger(SDL_FINGERUP, 10, hitX, targetRowY);
        assert(!accepted && !wentBack);

        // Tap outside box triggers back.
        finger(SDL_FINGERDOWN, 11, std::max(0, hostMenu.boxX - 20), hostMenu.boxY);
        finger(SDL_FINGERUP, 11, std::max(0, hostMenu.boxX - 20), hostMenu.boxY);
        assert(wentBack);
    }

    std::puts("GBA host pause menu swipe-vs-tap & scroll persistence OK.");
    return 0;
}
