#include "ds_bridge.h"
#include "companion.h"

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

    // Every Gen 4 / Gen 5 DS family supported by NoSplitNX (including Spanish/regional builds) should be recognised.
    const char* codes[] = { "CPUE", "CPUS", "IPGE", "IPKE", "IPKS", "IRBO", "IRBS", "IRAO", "IREO", "IRES", "IRDO" };
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

    // Verify Spanish Black (IRBS) port selection reads saveControl at 0x020A7258 instead of US 0x020A7298.
    {
        std::vector<uint8_t> esRam(0x400000, 0);
        auto write32 = [&](uint32_t addr, uint32_t val) {
            const size_t off = addr - 0x02000000u;
            esRam[off + 0] = (uint8_t)(val & 0xFFu);
            esRam[off + 1] = (uint8_t)((val >> 8) & 0xFFu);
            esRam[off + 2] = (uint8_t)((val >> 16) & 0xFFu);
            esRam[off + 3] = (uint8_t)((val >> 24) & 0xFFu);
        };
        // Set up Spanish Black saveControl chain: 0x020A7258 -> 0x02100000 (+16) -> 0x02100100 (+52) -> 0x02200000
        write32(0x020A7258u, 0x02100000u);
        write32(0x02100010u, 0x02100100u);
        write32(0x02100134u, 0x02200000u);
        // Trainer name "DIEGO" in UTF-16 at save + 0x19404, money 5000 at save + 0x21200, party count 0 at save + 0x18E00.
        const uint32_t saveBase = 0x02200000u;
        const char* name = "DIEGO";
        for (int i = 0; name[i]; ++i)
            esRam[(saveBase - 0x02000000u) + 0x19404u + i * 2] = (uint8_t)name[i];
        esRam[(saveBase - 0x02000000u) + 0x19404u + 10] = 0xFF;
        esRam[(saveBase - 0x02000000u) + 0x19404u + 11] = 0xFF;
        write32(saveBase + 0x21200u, 5000u);

        ds_open_cart("0000000000000000000000000000000000000000");
        ds_select_port_by_code("IRBS", 0);
        const DsCompanion snap = ds_read_companion(esRam.data(), esRam.size(), DsGame::Black);
        assert(snap.valid);
        assert(snap.trainer == "DIEGO");
        assert(snap.money == 5000);
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
