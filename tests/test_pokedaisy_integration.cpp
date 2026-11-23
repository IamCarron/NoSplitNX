#include "ds_bridge.h"

#include <SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> MakeSyntheticDsRom(const char* gameCode, const char* title)
{
    std::vector<uint8_t> rom(0x4000, 0);
    std::memcpy(rom.data(), title, std::min<size_t>(12, std::strlen(title)));
    std::memcpy(rom.data() + 0x0C, gameCode, 4);
    rom[0x10] = '0';
    rom[0x11] = '1';
    return rom;
}

int main(int argc, char** argv)
{
    const std::string baseDir = (argc > 1 && argv[1] && argv[1][0]) ? argv[1] : "/tmp";
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surf != nullptr);
    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(surf);
    assert(ren != nullptr);

    PdDs::HostActions actions;
    bool ff = false;
    int toasts = 0;
    actions.saveState = []() { return true; };
    actions.loadState = []() { return true; };
    actions.stateExists = []() { return true; };
    actions.getFf = [&]() { return ff; };
    actions.setFf = [&](bool on) { ff = on; };
    actions.toast = [&](const std::string&) { toasts++; };

    std::vector<uint8_t> mainRam(0x400000, 0);

    // Every Gen 4 / Gen 5 DS family supported by NoSplitNX should be recognised by PdDs::Bridge.
    const char* codes[] = { "CPUE", "IPGE", "IPKE", "IRBO", "IRAO", "IREO", "IRDO" };
    for (const char* code : codes)
    {
        PdDs::Bridge bridge;
        auto rom = MakeSyntheticDsRom(code, "POKEMON TEST");
        const std::string gameDataDir = baseDir + "/" + code;
        assert(bridge.Load(rom.data(), rom.size(), gameDataDir, "es"));
        assert(bridge.Supported());
        bridge.SetHud(true);
        assert(bridge.HudEnabled());
        assert(!bridge.AutoRunEnabled());
        assert(!bridge.WildOffEnabled());

        bridge.SyncLanguage("fr");
        bridge.SyncLanguage("es");
        bridge.SetAutoRun(true);
        assert(bridge.AutoRunEnabled());
        bridge.SetWildOff(true);
        assert(bridge.WildOffEnabled());

        // Tick and render overworld HUD, Battle Strip, Quick Menu, and Companion without crashing.
        std::vector<uint32_t> fakeFb(256u * 192u, 0xFF203040u);
        bridge.Tick(mainRam.data(), mainRam.size(), false, false, actions);
        bridge.Draw(ren, 1280, 720, true, actions);
        auto btap = bridge.TickBattle(mainRam.data(), mainRam.size(), fakeFb.data(), true, 1, 0, false, false);
        (void)btap;
        bridge.HandleBattleTouch(640, 680, 1280, 720, true);
        bridge.DrawBattleStrip(ren, 1280, 720, fakeFb.data());

        bridge.ToggleQuickMenu();
        assert(bridge.QuickOpen());
        assert(bridge.AnyMenuOpen());
        bridge.Draw(ren, 1280, 720, true, actions);
        PdDs::NavInput nav;
        nav.right = true;
        bridge.HandleNav(nav, mainRam.data(), mainRam.size(), actions);
        nav = {};
        nav.back = true;
        bridge.HandleNav(nav, mainRam.data(), mainRam.size(), actions);
        assert(!bridge.QuickOpen());

        bridge.ToggleCompanion();
        assert(bridge.CompanionOpen());
        bridge.Draw(ren, 1280, 720, true, actions);
        nav = {};
        nav.pageR = true;
        bridge.HandleNav(nav, mainRam.data(), mainRam.size(), actions);
        bridge.Draw(ren, 1280, 720, true, actions);
        // Tap inside Companion screen and verify touch is consumed cleanly.
        assert(bridge.HandleTouch(640, 360, 1280, 720, true, mainRam.data(), mainRam.size(), actions));
        bridge.HandleTouch(640, 360, 1280, 720, false, mainRam.data(), mainRam.size(), actions);
        bridge.CloseMenus();
        assert(!bridge.AnyMenuOpen());

        bridge.OnReset();
        bridge.Unload();
        assert(!bridge.Supported());
    }

    // Non-Pokemon DS ROM should gracefully report unsupported.
    {
        PdDs::Bridge bridge;
        auto rom = MakeSyntheticDsRom("XXXX", "OTHER GAME");
        assert(!bridge.Load(rom.data(), rom.size(), "/tmp", "en"));
        assert(!bridge.Supported());
    }

    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    std::puts("PokeDaisy DS Bridge compile, link, Gen 4/5 detection, Quick Menu, Companion & SDL render OK.");
    return 0;
}
