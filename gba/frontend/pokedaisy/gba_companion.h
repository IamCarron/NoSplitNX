// PokeDaisy Companion, Field HUD, Quick Menu, Free Flight & Rewind bridge for NoSplitNX's
// mGBA backend (GB, GBC and GBA).
// Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy (GNU GPL version 3).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <SDL.h>

extern "C" {
#include <mgba/core/core.h>
#include <mgba/internal/gb/gb.h>
#include "pokedaisy/pk_rewind.h"
#include "pokedaisy/pk_wide.h"
#ifdef GBAW_POKEDAISY
#include "pd_canvas.h"
#include "pd_dex.h"
#include "pd_font.h"
#include "pd_game.h"
#include "pd_gb.h"
#include "pd_map.h"
#include "pd_romart.h"
#include "pd_settings.h"
#include "pd_snapshot.h"
#include "pd_style.h"
#include "pd_tr.h"
#include "pd_ui.h"
#include "pd_ui_internal.h"
#include "hud.h"
#include "quick_menu.h"
#include "pd_fs.h"
#include "pokedaisy/field_items.h"
#include "pokedaisy/flight.h"
#include "pokedaisy/fly.h"
#include "pokedaisy/gb_field.h"
#include "pokedaisy/pd_flight_area.h"
#include "pokedaisy/pd_flight_gen.h"
#include "pokedaisy/pk_call.h"
#include "pokedaisy/pk_flight.h"
#include "pokedaisy/wild.h"
#endif
}

namespace gbaw
{

class GbaCompanion
{
public:
    struct HostActions
    {
        std::function<bool()> saveState;
        std::function<bool()> loadState;
        std::function<bool()> stateExists;
        std::function<bool()> getFf;
        std::function<void(bool)> setFf;
        std::function<bool()> getWildOff;
        std::function<void(bool)> setWildOff;
        std::function<void(const std::string&)> toast;
    };

    bool active = false;
    bool companionOpen = false;
    bool quickOpen = false;
    bool rewindEnabled = true;

    ~GbaCompanion() { Shutdown(nullptr); }

    bool AnyMenuOpen() const { return companionOpen || quickOpen; }
    bool TakesInput() const { return companionOpen || quickOpen; }
    bool IsFlying() const
    {
#ifdef GBAW_POKEDAISY
        return freeFlight && pk_flight_state() != PK_FLIGHT_IDLE;
#else
        return false;
#endif
    }

    // Lead Pokemon National Dex ID and shiny flag (for Following Pokemon in GBA overworlds).
    bool LeadInfo(int& national, bool& shiny) const
    {
#ifdef GBAW_POKEDAISY
        if (!active || isGb || !snap.valid || snap.partyCount <= 0) return false;
        const struct pd_mon& m = snap.party[0];
        if (m.egg || m.species <= 0) return false;
        national = pd_dex_national(&game, m.species);
        shiny = m.shiny;
        return national > 0;
#else
        (void)national; (void)shiny;
        return false;
#endif
    }

    // Addresses for GbaGameplay::ShinyRoller (gEnemyParty, gSaveBlock2Ptr, staticSaveBlocks, gMain.inBattle).
    bool ShinyAddrs(const std::string& gameCode, uint32_t& enemyParty, uint32_t& saveBlock2,
                    bool& staticBlocks, uint32_t& inBattleAddr) const
    {
#ifdef GBAW_POKEDAISY
        if (!active || isGb || !game.cfg || !game.cfg->gMain || !game.cfg->saveBlock2Ptr ||
            !game.cfg->partyFns || !game.cfg->partyFns->party || gameCode.size() < 3)
            return false;
        const std::string f = gameCode.substr(0, 3);
        const uint32_t party = game.cfg->partyFns->party;
        enemyParty = (f == "AXV" || f == "AXP") ? (party + 0x260u)
                   : (f == "BPE") ? (party + 0x258u)
                   : (f == "BPR" || f == "BPG") ? (party - 0x258u) : 0u;
        if (!enemyParty) return false;
        saveBlock2 = game.cfg->saveBlock2Ptr;
        staticBlocks = game.cfg->staticSaveBlocks;
        inBattleAddr = game.cfg->gMain + game.cfg->inBattleOff;
        return true;
#else
        (void)gameCode; (void)enemyParty; (void)saveBlock2; (void)staticBlocks; (void)inBattleAddr;
        return false;
#endif
    }

    static bool ReadMemory(void* ctx, uint32_t a, void* out, size_t n)
    {
        struct mCore* c = static_cast<struct mCore*>(ctx);
        if (!c || !out) return false;
        unsigned char* p = static_cast<unsigned char*>(out);
        if (c->platform(c) == mPLATFORM_GB && a >= 0x10000u)
        {
            const unsigned bank = a >> 16, addr = a & 0xFFFFu;
            struct GB* gb = static_cast<struct GB*>(c->board);
            if (!gb || bank > 7 || addr < 0xD000u || addr >= 0xE000u || n > 0xE000u - addr || !gb->memory.wram)
                return false;
            memcpy(p, gb->memory.wram + bank * 0x1000u + (addr - 0xD000u), n);
            return true;
        }
        if (c->platform(c) == mPLATFORM_GBA && a >= 0x08000000u && a < 0x0E000000u)
        {
            size_t size = 0;
            const unsigned char* r = static_cast<const unsigned char*>(c->getMemoryBlock(c, 0x08, &size));
            const size_t start = (a - 0x08000000u) & 0x01FFFFFFu;
            if (!r || start > size || n > size - start) return false;
            memcpy(p, r + start, n);
            return true;
        }
        for (size_t i = 0; i < n; i++) p[i] = c->busRead8(c, a + (uint32_t)i);
        return true;
    }

    static int ToPdLang(const std::string& code)
    {
#ifdef GBAW_POKEDAISY
        std::string lo;
        lo.reserve(code.size());
        for (unsigned char c : code) lo.push_back((c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : (char)c);
        if (lo == "es") return PD_LANG_ES;
        if (lo == "fr") return PD_LANG_FR;
        if (lo == "de") return PD_LANG_DE;
        if (lo == "it") return PD_LANG_IT;
        if (lo == "pt-br" || lo == "br") return PD_LANG_BR;
        if (lo == "pt") return PD_LANG_PT;
        if (lo == "en") return PD_LANG_EN;
        return PD_LANG_AUTO;
#else
        (void)code;
        return 0;
#endif
    }

    void SyncLanguage(const std::string& langCode)
    {
#ifdef GBAW_POKEDAISY
        const int lang = ToPdLang(langCode);
        if (lang != PD_LANG_AUTO) pd_tr_set_system(lang);
        const char letter = (!isGb && romLen > 0xAF) ? (char)romBytes[0xAF] : 0;
        pd_tr_apply(lang, letter);
        if (lang != PD_LANG_AUTO) settings.language = lang;
#else
        (void)langCode;
#endif
    }

    void Start(struct mCore* core, const uint8_t* rom, size_t len, uint32_t crc, bool gbMode,
               const std::string& dataDir, const std::string& langCode, bool enableRewind)
    {
        Shutdown(core);
        isGb = gbMode;
        romCrc = crc;
        romBytes.assign(rom, rom + len);
        romLen = romBytes.size();
        rewindEnabled = enableRewind;
        if (rewindEnabled && core)
        {
            pk_rewind_init(&rewindCtx, 180);
            rewindInited = true;
            rewindTick = 0;
        }
#ifdef GBAW_POKEDAISY
        active = true;
        companionOpen = false;
        quickOpen = false;
        flyList = false;
        repelList = false;
        flyAsk = flyStage = scenicAsk = 0;
        job = JOB_NONE;
        snapTick = 0;
        hudDrawnKey = 0;
        repelFull = 0;
        wildWasOff = false;
        pdDir = dataDir + "/pokedaisy";
        pd_mkdir(pdDir.c_str());
        settingsPath = pdDir + "/settings.ini";
        pd_settings_defaults(&settings);
        settings.hud = true; // PokeDaisy Field HUD enabled by default (toggleable in Quick Menu / Pause)
        settings.quickGrid = false;
        pd_settings_load(&settings, settingsPath.c_str());

        memset(&game, 0, sizeof game);
        memset(&snap, 0, sizeof snap);
        pd_game_detect(&game, romBytes.data(), romLen);
        game.rom = romBytes.data();
        game.romSize = romLen;
        pd_game_identify(&game);
        if (!isGb)
        {
            pd_romart_cache_dir(pdDir.c_str());
            pd_romart_scan(&game);
        }
        SyncLanguage(langCode);

        pk_flight_set_call(pk_call);
        pk_flight_configure(nullptr, 0);
        flyCfg = nullptr;
        flightWords = nullptr;
        freeFlight = false;
        itemsOk = bikeOk = false;
        gbf = nullptr;
        gbWildHeld = false;

        if (isGb)
        {
            gbf = gb_field_find(romBytes.data(), romLen);
            gbWildHeld = gbf != nullptr;
        }
        else if (game.cfg)
        {
            if (fly_supported(game.cfg, romBytes.data(), romLen)) flyCfg = game.cfg;
            itemsOk = field_items_supported(game.cfg, romBytes.data(), romLen);
            bikeOk = bike_supported(game.cfg);
            for (size_t i = 0; i < sizeof(flight_profiles) / sizeof(flight_profiles[0]); i++)
            {
                if (game.sha1[0] && !strcmp(flight_profiles[i].sha1, game.sha1))
                {
                    flightWords = flight_profiles[i].words;
                    freeFlight = true;
                    ConfigureFlightWords();
                    pd_flight_area_begin(&flight_profiles[i].area, romBytes.data(), romLen, romCrc, pdDir.c_str());
                    break;
                }
            }
        }
        wildSwitch = (!isGb && game.cfg) ? pk_wild_switch_find(romBytes.data(), romLen) : 0;

        pd_ui_init(&ui, &settings);
        ui.handheldDex = ui.handheldParty = ui.shoulderTabs = ui.hideSettingsTab = true;
        memset(&host, 0, sizeof host);
        host.hideScreenMode = true;
        host.hasQuickMenu = true;
        host.hasHud = true;
        host.hasExpand = false;
        host.hasButtonRows = false;
        host.hasDisplayFilter = false;
        host.hasPerf = false;
        host.fullSettings = false;
        host.frontendOwnsPlayback = true;
        if (core) pd_snapshot_read(&snap, &game, ReadMemory, core);
        fprintf(stderr, "[pokedaisy] %s (%s) fly=%d flight=%d wild=%d\n",
                game.title ? game.title : "unknown", isGb ? "GB/GBC" : "GBA",
                (flyCfg || gbf) ? 1 : 0, freeFlight ? 1 : 0, (wildSwitch || gbf) ? 1 : 0);
#else
        (void)dataDir;
        (void)langCode;
#endif
    }

    void Shutdown(struct mCore* core)
    {
        (void)core;
        if (rewindInited)
        {
            pk_rewind_deinit(&rewindCtx);
            rewindInited = false;
        }
#ifdef GBAW_POKEDAISY
        if (active)
        {
            pd_settings_save(&settings, settingsPath.c_str());
            pk_flight_configure(nullptr, 0);
            pd_flight_area_end();
            active = false;
        }
#endif
        companionOpen = false;
        quickOpen = false;
        if (overlayTex) { SDL_DestroyTexture(overlayTex); overlayTex = nullptr; }
        overlayTexW = overlayTexH = 0;
        if (hudTex) { SDL_DestroyTexture(hudTex); hudTex = nullptr; }
        hudTexW = hudTexH = 0;
        romBytes.clear();
        romLen = 0;
    }

    void OnReset(struct mCore* core)
    {
        if (rewindInited)
        {
            pk_rewind_deinit(&rewindCtx);
            pk_rewind_init(&rewindCtx, 180);
            rewindTick = 0;
        }
#ifdef GBAW_POKEDAISY
        companionOpen = quickOpen = flyList = repelList = false;
        flyAsk = flyStage = scenicAsk = 0;
        job = JOB_NONE;
        hudDrawnKey = 0;
        if (active && core)
        {
            if (freeFlight)
            {
                ConfigureFlightWords();
                pd_flight_area_resend();
            }
            pd_snapshot_read(&snap, &game, ReadMemory, core);
        }
#else
        (void)core;
#endif
    }

    void SetRewind(struct mCore* core, bool on)
    {
        rewindEnabled = on;
        if (on && !rewindInited && core)
        {
            pk_rewind_init(&rewindCtx, 180);
            rewindInited = true;
            rewindTick = 0;
        }
        else if (!on && rewindInited)
        {
            pk_rewind_deinit(&rewindCtx);
            rewindInited = false;
        }
    }

    bool HudEnabled() const
    {
#ifdef GBAW_POKEDAISY
        return active && settings.hud;
#else
        return false;
#endif
    }

    void SetHud(bool on)
    {
#ifdef GBAW_POKEDAISY
        if (!active) return;
        settings.hud = on;
        hudDrawnKey = 0;
        pd_settings_save(&settings, settingsPath.c_str());
#else
        (void)on;
#endif
    }

    bool AutoRunEnabled() const
    {
#ifdef GBAW_POKEDAISY
        return active && settings.autoRun;
#else
        return false;
#endif
    }

    void SetAutoRun(bool on)
    {
#ifdef GBAW_POKEDAISY
        if (!active) return;
        settings.autoRun = on;
        pd_settings_save(&settings, settingsPath.c_str());
#else
        (void)on;
#endif
    }

    void ToggleCompanion(struct mCore* core)
    {
#ifdef GBAW_POKEDAISY
        if (!active) return;
        quickOpen = false;
        flyList = repelList = false;
        companionOpen = !companionOpen;
        if (companionOpen && core)
        {
            pd_snapshot_read(&snap, &game, ReadMemory, core);
            pd_ui_update(&ui, &snap);
            if (snap.inBattle) ui.tab = PD_TAB_BATTLE;
        }
#else
        (void)core;
#endif
    }

    void ToggleQuickMenu(struct mCore* core)
    {
#ifdef GBAW_POKEDAISY
        if (!active) return;
        companionOpen = false;
        flyList = repelList = false;
        quickOpen = !quickOpen;
        if (quickOpen)
        {
            quickSel = 0;
            if (core) pd_snapshot_read(&snap, &game, ReadMemory, core);
        }
#else
        (void)core;
#endif
    }

    // Returns true when rewind restored a previous frame (caller skips normal core->runFrame).
    bool PreFrame(struct mCore* core, uint32_t& keys, bool rewindHeld)
    {
        if (!core) return false;
        if (rewindEnabled && rewindInited && rewindHeld && !AnyMenuOpen())
        {
            if ((++rewindTick & 1) == 0) pk_rewind_restore(&rewindCtx, core);
            return true;
        }
#ifdef GBAW_POKEDAISY
        if (active && !AnyMenuOpen())
        {
            if (settings.autoRun && (keys & 0xF0u) && pk_wide_scene() == PK_WIDE_SCENE_FIELD && !snap.inBattle)
                keys |= (1u << 1); // K_B
            if (injectKeyFrames > 0)
            {
                keys |= injectKeys;
                if (--injectKeyFrames == 0) injectKeys = 0;
            }
        }
#endif
        return false;
    }

    void PostFrame(struct mCore* core, const HostActions& actions, bool shown = true)
    {
        if (!core) return;
        if (shown && rewindEnabled && rewindInited && !AnyMenuOpen())
        {
            if (++rewindTick >= 6)
            {
                rewindTick = 0;
                pk_rewind_append(&rewindCtx, core);
            }
        }
#ifdef GBAW_POKEDAISY
        if (!active) return;
        if (freeFlight && pk_flight_state() != PK_FLIGHT_IDLE)
            pk_flight_tick(core);
        if (freeFlight && pk_flight_area_turned())
            Say("FLIGHT AREA EDGE", actions);

        const int scene = pk_wide_scene();
        const bool busy = scene != PK_WIDE_SCENE_FIELD;
        const bool battle = snap.inBattle;

        if (shown && ++snapTick >= (companionOpen ? 6 : 12))
        {
            snapTick = 0;
            pd_snapshot_read(&snap, &game, ReadMemory, core);
            lastRepelSteps = (!isGb && game.cfg) ? repel_steps(core, game.cfg) : 0;
            if (!isGb && freeFlight && pk_call_safe(core))
                pd_flight_area_sync(core, settings.flightAnywhere, false);
        }

        // Wild encounters toggle (GB/GBC via gbf, or GBA fallback when host gameplay patch isn't active).
        if (!actions.setWildOff)
        {
            if (settings.wildOff)
            {
                if (gbf) { gb_field_hold_wild(core, gbf); gbWildHeld = true; }
                else if (wildSwitch) { core->busWrite8(core, wildSwitch, 1); wildWasOff = true; }
            }
            else
            {
                if (gbWildHeld && gbf && !busy && gb_field_release_wild(core, gbf, romBytes.data(), romLen))
                    gbWildHeld = false;
                if (wildWasOff && wildSwitch) { core->busWrite8(core, wildSwitch, 0); wildWasOff = false; }
            }
        }

        if (gbTake > 0 && gbf)
        {
            if (--gbTake == 0 && !gb_field_taken(core, gbf))
                Say("NOT AVAILABLE HERE", actions);
        }

        if ((flyCfg || gbf) && flyAsk && quickOpen && !companionOpen)
        {
            bool answered = false;
            if (gbf)
            {
                gb_field_fly_targets(core, gbf, romBytes.data(), romLen, busy, &flyT);
                answered = true;
            }
            else if (pk_call_safe(core))
            {
                pd_snapshot_read(&snap, &game, ReadMemory, core);
                fly_targets(core, flyCfg, romBytes.data(), romLen, &snap, busy, battle, &flyT);
                answered = true;
            }
            if (answered)
            {
                flyAsk = 0;
                if (flyT.result == FLY_OK && flyT.count > 0)
                {
                    for (int i = 0; i < flyT.count; i++)
                    {
                        const char* nm = pd_mapsec_name(&game, flyT.towns[i]);
                        flyNames[i] = (nm && *nm) ? nm : "?";
                    }
                    flyList = true;
                    flySel = 0;
                }
                else
                {
                    quickOpen = false;
                    Say(flyT.result == FLY_OK ? "NO FLY DESTINATIONS YET" : fly_reason(flyT.result), actions);
                }
            }
            else if (++flyAsk > 120)
            {
                flyAsk = 0;
                quickOpen = false;
                Say(fly_reason(FLY_FAILED), actions);
            }
        }

        if (scenicAsk && !companionOpen)
        {
            if (pk_call_safe(core))
            {
                scenicAsk = 0;
                pd_snapshot_read(&snap, &game, ReadMemory, core);
                enum fly_result gate = flight_check(core, game.cfg, romBytes.data(), romLen, &snap, busy, battle);
                if (gate != FLY_OK) Say(fly_reason(gate), actions);
                else
                {
                    pd_flight_area_sync(core, settings.flightAnywhere, true);
                    static uint8_t route[FLIGHT_MAX_STEPS];
                    struct flight_spot here{}, dest{};
                    int steps = -1;
                    if (flight_here(core, game.cfg, &here) &&
                        flight_destination(core, game.cfg, romBytes.data(), romLen, scenicMapsec, &dest))
                        steps = flight_route(core, game.cfg, romBytes.data(), romLen, &here, &dest, nullptr, route);
                    int code = steps < 0 ? PK_FLIGHT_NOT_NOW : pk_flight_take_off(core);
                    if (steps >= 0 && code == PK_FLIGHT_OK) pk_flight_route(route, steps);
                    else { flyMapsec = scenicMapsec; flyStage = 1; flyWait = 0; }
                }
            }
            else if (++scenicAsk > 120) { scenicAsk = 0; Say(fly_reason(FLY_FAILED), actions); }
        }

        if (takeOff && !companionOpen)
        {
            if (pk_call_safe(core))
            {
                takeOff = false;
                pd_snapshot_read(&snap, &game, ReadMemory, core);
                enum fly_result gate = flight_check(core, game.cfg, romBytes.data(), romLen, &snap, busy, battle);
                if (gate != FLY_OK) Say(fly_reason(gate), actions);
                else
                {
                    ConfigureFlightWords();
                    const int code = pk_flight_take_off(core);
                    if (code != PK_FLIGHT_OK) Say(flight_reason(code), actions);
                }
            }
        }

        if (gbf && flyStage == 1 && !companionOpen)
        {
            enum fly_result r = gb_field_fly(core, gbf, romBytes.data(), romLen, flyMapsec, busy);
            flyStage = 0;
            if (r == FLY_OK) gbTake = GB_FIELD_TAKE_FRAMES;
            else Say(fly_reason(r), actions);
        }
        else if (flyCfg && flyStage == 1 && !companionOpen && pk_call_safe(core))
        {
            pd_snapshot_read(&snap, &game, ReadMemory, core);
            enum fly_result r = fly_start(core, flyCfg, romBytes.data(), romLen, &snap, flyMapsec, &flyT, busy, battle);
            if (r == FLY_OK) { flyStage = 2; flyWait = 0; flyCallback = fly_main_callback(core, flyCfg); }
            else { flyStage = 0; Say(fly_reason(r), actions); }
        }
        else if (flyCfg && flyStage == 2 && !companionOpen && ++flyWait >= FLY_FADE_FRAMES && pk_call_safe(core))
        {
            flyStage = 0;
            if (!fly_finish(core, flyCfg, romBytes.data(), romLen, flyMapsec, &flyT,
                            fly_main_callback(core, flyCfg) != flyCallback))
                Say(fly_reason(FLY_FAILED), actions);
        }

        const bool safe = job != JOB_NONE && !companionOpen && (gbf ? true : pk_call_safe(core));
        if (job == JOB_REPEL && safe)
        {
            job = JOB_NONE;
            int steps = 0;
            enum item_result r = repel_use(core, game.cfg, romBytes.data(), romLen, jobItem, busy, battle, &steps);
            if (r == ITEM_USED)
            {
                char msg[64];
                snprintf(msg, sizeof msg, "%s (%d %s)", pd_tr("REPEL ACTIVE"), steps, pd_tr("STEPS"));
                Say(msg, actions);
            }
            else Say(item_reason(r), actions);
        }
        else if (job == JOB_BIKE && safe)
        {
            enum item_result r = bike_register(core, game.cfg, romBytes.data(), romLen, jobItem, busy, battle, &bikeOld);
            if (r == ITEM_USED)
            {
                injectKeys = (1u << 2); // K_SELECT
                injectKeyFrames = 3;
                job = JOB_BIKE_RESTORE;
                jobFrames = 0;
            }
            else { job = JOB_NONE; Say(item_reason(r), actions); }
        }
        else if (job == JOB_BIKE_RESTORE && ++jobFrames >= 24)
        {
            job = JOB_NONE;
            bike_restore(core, game.cfg, jobItem, bikeOld);
        }
        else if ((job == JOB_REPEL || job == JOB_BIKE) && !companionOpen && ++jobFrames > 120)
        {
            job = JOB_NONE;
            Say(item_reason(ITEM_FAILED), actions);
        }
#else
        (void)actions;
#endif
    }

    void HandleNav(struct mCore* core, bool up, bool down, bool left, bool right,
                   bool accept, bool back, bool pageL, bool pageR, const HostActions& actions)
    {
#ifdef GBAW_POKEDAISY
        if (!active) return;
        if (companionOpen)
        {
            if (pageL) pd_ui_next_tab(&ui, -1);
            if (pageR) pd_ui_next_tab(&ui, 1);
            if (up) pd_ui_focus_move(&ui, 0);
            if (down) pd_ui_focus_move(&ui, 1);
            if (left) pd_ui_focus_move(&ui, 2);
            if (right) pd_ui_focus_move(&ui, 3);
            if (accept) pd_ui_focus_press(&ui);
            if (back)
            {
                if (!pd_ui_back(&ui)) companionOpen = false;
            }
            ProcessUiAction(core, actions);
            return;
        }
        if (quickOpen)
        {
            if (flyList)
            {
                if (up && flySel > 0) flySel--;
                if (down && flySel + 1 < flyT.count) flySel++;
                if (back) { flyList = false; return; }
                if (accept && flySel >= 0 && flySel < flyT.count)
                {
                    flyList = false;
                    quickOpen = false;
                    if (freeFlight && !settings.flyClassic && game.cfg && game.cfg->flightMapGroups)
                    {
                        scenicMapsec = flyT.towns[flySel];
                        scenicAsk = 1;
                    }
                    else
                    {
                        flyMapsec = flyT.towns[flySel];
                        flyStage = 1;
                        flyWait = 0;
                    }
                }
                return;
            }
            if (repelList)
            {
                if (up && repelSel > 0) repelSel--;
                if (down && repelSel + 1 < repelKindCount) repelSel++;
                if (back) { repelList = false; return; }
                if (accept && repelSel >= 0 && repelSel < repelKindCount)
                {
                    job = JOB_REPEL;
                    jobItem = repelItemIds[repelSel];
                    jobFrames = 0;
                    repelList = false;
                    quickOpen = false;
                }
                return;
            }
            struct quick_item qItems[QUICK_COUNT];
            const int qn = BuildQuickItems(qItems, actions);
            if (qn <= 0 || back) { quickOpen = false; return; }
            if (quickSel >= qn) quickSel = 0;
            const int dx = (right ? 1 : 0) - (left ? 1 : 0);
            const int dy = (down ? 1 : 0) - (up ? 1 : 0);
            if (dx || dy) quickSel = quick_step(qn, quickSel, dx, dy, lastCanvasW, lastCanvasH);
            if (accept) RunQuickAction(core, qItems[quickSel], actions);
        }
#else
        (void)core; (void)up; (void)down; (void)left; (void)right;
        (void)accept; (void)back; (void)pageL; (void)pageR; (void)actions;
#endif
    }

    bool HandleTouch(struct mCore* core, int sx, int sy, int ow, int oh, bool down, const HostActions& actions)
    {
#ifdef GBAW_POKEDAISY
        if (!active || ow <= 0 || oh <= 0) return false;
        if (companionOpen && overlayDst.w > 0 && overlayDst.h > 0)
        {
            const int cx = (sx - overlayDst.x) * lastCanvasW / overlayDst.w;
            const int cy = (sy - overlayDst.y) * lastCanvasH / overlayDst.h;
            if (down)
            {
                if (cx >= 0 && cx < lastCanvasW && cy >= 0 && cy < lastCanvasH)
                    pd_ui_touch(&ui, cx, cy, true);
                else
                    companionOpen = false;
            }
            else
            {
                pd_ui_touch(&ui, std::clamp(cx, 0, lastCanvasW - 1), std::clamp(cy, 0, lastCanvasH - 1), false);
            }
            ProcessUiAction(core, actions);
            return true;
        }
        if (quickOpen && down && overlayDst.w > 0 && overlayDst.h > 0)
        {
            const int cx = (sx - overlayDst.x) * lastCanvasW / overlayDst.w;
            const int cy = (sy - overlayDst.y) * lastCanvasH / overlayDst.h;
            if (flyList)
            {
                const int hit = quick_list_hit(cx, cy);
                if (hit >= 0 && hit < flyT.count)
                {
                    flySel = hit;
                    HandleNav(core, false, false, false, false, true, false, false, false, actions);
                }
                else flyList = false;
                return true;
            }
            if (repelList)
            {
                const int hit = quick_list_hit(cx, cy);
                if (hit >= 0 && hit < repelKindCount)
                {
                    repelSel = hit;
                    HandleNav(core, false, false, false, false, true, false, false, false, actions);
                }
                else repelList = false;
                return true;
            }
            const int hit = quick_hit(cx, cy);
            if (hit >= 0)
            {
                struct quick_item qItems[QUICK_COUNT];
                const int qn = BuildQuickItems(qItems, actions);
                if (hit < qn) { quickSel = hit; RunQuickAction(core, qItems[hit], actions); }
            }
            else quickOpen = false;
            return true;
        }
        if (settings.hud && down && hudDst.w > 0 && hudDst.h > 0 && hudHits.n > 0)
        {
            const int hx = (sx - hudDst.x) * hudW / hudDst.w;
            const int hy = (sy - hudDst.y) * hudH / hudDst.h;
            for (int i = 0; i < hudHits.n; i++)
            {
                const auto& r = hudHits.at[i];
                if (hx >= r.x && hx < r.x + r.w && hy >= r.y && hy < r.y + r.h)
                {
                    if (r.id == HUD_COMPANION) ToggleCompanion(core);
                    else if (r.id == HUD_BAG) { ToggleCompanion(core); ui.tab = PD_TAB_BAG; }
                    else if (r.id == HUD_MAP || r.id == HUD_MAP_BUTTON) { ToggleCompanion(core); ui.tab = PD_TAB_MAP; }
                    else if (r.id >= HUD_SLOT && r.id < HUD_SLOT + 6) { ToggleCompanion(core); ui.tab = PD_TAB_PARTY; }
                    return true;
                }
            }
        }
        return false;
#else
        (void)core; (void)sx; (void)sy; (void)ow; (void)oh; (void)down; (void)actions;
        return false;
#endif
    }

    void Draw(SDL_Renderer* ren, int ow, int oh, const HostActions& actions)
    {
#ifdef GBAW_POKEDAISY
        if (!active || !ren || ow <= 0 || oh <= 0) return;

        if (settings.hud && !companionOpen && !quickOpen && pk_wide_scene() == PK_WIDE_SCENE_FIELD && !snap.inBattle)
        {
            const int cw = (ow * 9 >= oh * 14) ? 426 : 320;
            const int ch = 240;
            struct hud_state hs{};
            hs.repel = lastRepelSteps;
            if (hs.repel > 0) repelFull = std::max(repelFull, hud_repel_full(hs.repel));
            else repelFull = 0;
            hs.repelFull = repelFull;
            hs.noButtons = true;
            hs.pc = false;
            hs.wild = -1;
            hs.focus = -1;
            hs.mini = (settings.hudSize != PD_HUD_LARGE);

            const uint32_t key = hud_key(&game, &snap, &hs, &settings) ^ ((uint32_t)cw << 16);
            if (key != hudDrawnKey || hudTexW != cw || hudTexH != ch)
            {
                hudW = cw;
                hudH = ch;
                hudBuf.assign((size_t)cw * ch, QUICK_CLEAR);
                struct pd_canvas hc{};
                pd_canvas_init(&hc, cw, ch, hudBuf.data());
                hudHits.n = 0;
                const bool drew = hud_draw(&hc, &game, &snap, &hs, &settings, &hudHits);
                if (drew)
                {
                    quick_finish(&hc);
                    for (uint32_t& p : hudBuf)
                        if (p == QUICK_CLEAR) p = 0;
                }
                else std::fill(hudBuf.begin(), hudBuf.end(), 0u);
                UploadTexture(ren, hudTex, hudTexW, hudTexH, cw, ch, hudBuf.data());
                hudDrawnKey = key;
            }
            if (hudTex)
            {
                const int scale = std::max(1, std::min(ow / hudW, oh / hudH));
                hudDst = { (ow - hudW * scale) / 2, (oh - hudH * scale) / 2, hudW * scale, hudH * scale };
                SDL_RenderCopy(ren, hudTex, nullptr, &hudDst);
            }
        }

        if (!companionOpen && !quickOpen) return;
        const int cw = (ow * 9 >= oh * 14) ? 426 : 320;
        const int ch = 240;
        lastCanvasW = cw;
        lastCanvasH = ch;
        overlayBuf.assign((size_t)cw * ch, companionOpen ? 0xFF000000u : QUICK_CLEAR);
        struct pd_canvas c{};
        pd_canvas_init(&c, cw, ch, overlayBuf.data());

        if (companionOpen)
        {
            pd_ui_update(&ui, &snap);
            host.ffOn = actions.getFf ? actions.getFf() : false;
            host.hasState = actions.stateExists ? actions.stateExists() : false;
            host.flightReady = freeFlight && fly_flyer(&snap) >= 0;
            host.flying = freeFlight && pk_flight_state() != PK_FLIGHT_IDLE;
            const bool gen2 = game.cfg && game.cfg->gb && game.cfg->gb->gen == 2;
            const struct pd_glyph* glyphs = nullptr;
            int glyphCount = 0;
            if (gen2) { pd_font_get(&glyphs, &glyphCount); pd_font_set(nullptr, 0); }
            pd_ui_draw(&ui, &c, &game, &snap, &host);
            if (gen2) pd_font_set(glyphs, glyphCount);
            for (uint32_t& p : overlayBuf) p |= 0xFF000000u;
        }
        else if (quickOpen)
        {
            if (flyList) quick_list_draw(&c, "FLY TO", flyNames, flyT.count, flySel, settings.hudLook);
            else if (repelList) quick_list_draw(&c, "REPEL", repelNames, repelKindCount, repelSel, settings.hudLook);
            else
            {
                struct quick_item qItems[QUICK_COUNT];
                const int qn = BuildQuickItems(qItems, actions);
                if (quickSel >= qn) quickSel = 0;
                quick_draw(&c, &game, qItems, qn, quickSel, settings.hudLook);
            }
            quick_finish(&c);
            for (uint32_t& p : overlayBuf)
                if (p == QUICK_CLEAR) p = 0;
        }

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 6, 10, 18, companionOpen ? 210 : 120);
        SDL_RenderFillRect(ren, nullptr);

        UploadTexture(ren, overlayTex, overlayTexW, overlayTexH, cw, ch, overlayBuf.data());
        if (overlayTex)
        {
            const int scale = std::max(1, std::min(ow / cw, oh / ch));
            overlayDst = { (ow - cw * scale) / 2, (oh - ch * scale) / 2, cw * scale, ch * scale };
            SDL_RenderCopy(ren, overlayTex, nullptr, &overlayDst);
        }
#else
        (void)ren; (void)ow; (void)oh; (void)actions;
#endif
    }

private:
    bool isGb = false;
    uint32_t romCrc = 0;
    std::vector<uint8_t> romBytes;
    size_t romLen = 0;

    struct mCoreRewindContext rewindCtx{};
    bool rewindInited = false;
    int rewindTick = 0;

    SDL_Texture* overlayTex = nullptr;
    int overlayTexW = 0, overlayTexH = 0;
    SDL_Rect overlayDst{};
    SDL_Texture* hudTex = nullptr;
    int hudTexW = 0, hudTexH = 0;
    SDL_Rect hudDst{};
    int lastCanvasW = 426, lastCanvasH = 240;

#ifdef GBAW_POKEDAISY
    std::string pdDir, settingsPath;
    struct pd_game game{};
    struct pd_snapshot snap{};
    struct pd_settings settings{};
    struct pd_ui ui{};
    struct pd_host_info host{};
    std::vector<uint32_t> overlayBuf, hudBuf;
    struct hud_hits hudHits{};
    uint32_t hudDrawnKey = 0;
    int hudW = 426, hudH = 240;
    int snapTick = 0;
    int repelFull = 0;
    int lastRepelSteps = 0;

    const struct pd_config* flyCfg = nullptr;
    const uint32_t* flightWords = nullptr;
    const struct gb_field* gbf = nullptr;
    uint32_t wildSwitch = 0;
    bool wildWasOff = false;
    bool freeFlight = false, takeOff = false;
    bool itemsOk = false, bikeOk = false;
    bool gbWildHeld = false;
    int gbTake = 0;

    struct fly_targets flyT{};
    const char* flyNames[FLY_MAX_TOWNS] = {};
    int quickSel = 0, flyAsk = 0, flySel = 0, flyStage = 0, flyMapsec = 0, flyWait = 0;
    int scenicAsk = 0, scenicMapsec = 0;
    bool flyList = false, repelList = false;
    uint32_t flyCallback = 0;

    enum { JOB_NONE, JOB_REPEL, JOB_BIKE, JOB_BIKE_RESTORE } job = JOB_NONE;
    int jobItem = 0, jobFrames = 0;
    uint16_t bikeOld = 0;
    uint32_t injectKeys = 0;
    int injectKeyFrames = 0;
    int repelItemIds[REPEL_KINDS] = {}, repelKindCount = 0, repelSel = 0;
    char repelRows[REPEL_KINDS][48] = {};
    const char* repelNames[REPEL_KINDS] = {};

    static void UploadTexture(SDL_Renderer* ren, SDL_Texture*& tex, int& tw, int& th, int w, int h, const uint32_t* px)
    {
        if (!tex || tw != w || th != h)
        {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            tw = w; th = h;
            if (tex)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
            }
        }
        if (tex) SDL_UpdateTexture(tex, nullptr, px, w * 4);
    }

    void Say(const char* msg, const HostActions& actions)
    {
        if (msg && *msg && actions.toast) actions.toast(pd_tr(msg));
    }

    void ConfigureFlightWords()
    {
        if (!flightWords) return;
        uint32_t w[PK_FLIGHT_WORDS];
        memcpy(w, flightWords, sizeof w);
        const int v = (settings.flightVehicle >= 0 && settings.flightVehicle < FLIGHT_VEHICLES) ? settings.flightVehicle : 0;
        w[PK_FLIGHT_VEHICLE] = (uint32_t)v;
        pk_flight_configure(w, PK_FLIGHT_WORDS);
    }

    void ProcessUiAction(struct mCore* core, const HostActions& actions)
    {
        (void)core;
        const enum pd_action act = pd_ui_take_action(&ui);
        if (act == PD_ACTION_SETTINGS_CHANGED)
        {
            quick_set_grid(settings.quickGrid);
            if (freeFlight) ConfigureFlightWords();
            hudDrawnKey = 0;
            pd_settings_save(&settings, settingsPath.c_str());
        }
        else if (act == PD_ACTION_TOGGLE_FF && actions.getFf && actions.setFf)
            actions.setFf(!actions.getFf());
        else if (act == PD_ACTION_SAVE_STATE && actions.saveState)
            actions.saveState();
        else if (act == PD_ACTION_LOAD_STATE && actions.loadState)
        {
            if (actions.loadState()) companionOpen = false;
        }
        else if (act == PD_ACTION_FLY_ASK && (flyCfg || gbf))
        {
            companionOpen = false;
            quickOpen = true;
            flyAsk = 1;
        }
        else if (act == PD_ACTION_FLY_TO && (flyCfg || gbf))
        {
            companionOpen = false;
            if (freeFlight && !settings.flyClassic && game.cfg && game.cfg->flightMapGroups)
            {
                scenicMapsec = ui.flyPick;
                scenicAsk = 1;
            }
            else
            {
                flyMapsec = ui.flyPick;
                flyStage = 1;
                flyWait = 0;
            }
        }
        else if (act == PD_ACTION_FLIGHT && freeFlight)
        {
            companionOpen = false;
            if (pk_flight_state() != PK_FLIGHT_IDLE) pk_flight_land();
            else takeOff = true;
        }
    }

    int BuildQuickItems(struct quick_item out[QUICK_COUNT], const HostActions& actions) const
    {
        (void)actions;
        int n = 0;
        auto add = [&](enum quick_action a) -> struct quick_item& {
            memset(&out[n], 0, sizeof out[n]);
            out[n].action = a;
            return out[n++];
        };
        if (freeFlight && pk_flight_state() != PK_FLIGHT_IDLE)
            add(QUICK_LAND);
        else
        {
            if (freeFlight)
            {
                auto& it = add(QUICK_FLIGHT);
                const int fl = fly_flyer(&snap);
                if (fl >= 0 && fl < snap.partyCount) it.species = snap.party[fl].species;
            }
            if (itemsOk)
            {
                add(QUICK_REPEL);
                if (bikeOk) add(QUICK_BIKE);
            }
        }
        {
            auto& it = add(QUICK_HUD);
            it.lit = settings.hud;
        }
        add(QUICK_COMPANION);
        return n;
    }

    void RunQuickAction(struct mCore* core, const struct quick_item& item, const HostActions& actions)
    {
        switch (item.action)
        {
        case QUICK_FLIGHT:
            takeOff = true; quickOpen = false;
            break;
        case QUICK_LAND:
            pk_flight_land(); quickOpen = false;
            break;
        case QUICK_REPEL:
        {
            repelKindCount = 0;
            for (int k = 0; k < REPEL_KINDS; k++)
            {
                const int id = repel_kinds[k][0];
                const int cnt = bag_count(&snap, id);
                if (cnt > 0)
                {
                    repelItemIds[repelKindCount] = id;
                    const char* nm = pd_item_name(&game, id);
                    snprintf(repelRows[repelKindCount], sizeof repelRows[repelKindCount], "%s x%d", (nm && *nm) ? nm : pd_tr("REPEL"), cnt);
                    repelNames[repelKindCount] = repelRows[repelKindCount];
                    repelKindCount++;
                }
            }
            if (repelKindCount <= 0) { Say("NO REPEL IN BAG", actions); quickOpen = false; }
            else if (repelKindCount == 1) { job = JOB_REPEL; jobItem = repelItemIds[0]; jobFrames = 0; quickOpen = false; }
            else { repelSel = 0; repelList = true; }
            break;
        }
        case QUICK_BIKE:
        {
            const int bk = bag_bike(&snap);
            if (bk <= 0) { Say("NO BIKE IN BAG", actions); quickOpen = false; }
            else { job = JOB_BIKE; jobItem = bk; jobFrames = 0; quickOpen = false; }
            break;
        }
        case QUICK_HUD:
            SetHud(!settings.hud);
            Say(settings.hud ? "HUD ON" : "HUD OFF", actions);
            quickOpen = false;
            break;
        case QUICK_COMPANION:
            quickOpen = false;
            ToggleCompanion(core);
            break;
        case QUICK_FF:
            if (actions.getFf && actions.setFf) actions.setFf(!actions.getFf());
            quickOpen = false;
            break;
        case QUICK_SAVE_STATE:
            if (actions.saveState) actions.saveState();
            quickOpen = false;
            break;
        case QUICK_LOAD_STATE:
            if (actions.loadState) actions.loadState();
            quickOpen = false;
            break;
        default:
            quickOpen = false;
            break;
        }
    }
#endif
};

} // namespace gbaw
