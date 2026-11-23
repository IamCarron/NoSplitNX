// PokeDaisy Companion, Field HUD & Quick Menu bridge for NoSplitNX's melonDS backend.
// Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy (GNU GPL version 3).
#include "ds_bridge.h"

#include "battle_strip.h"
#include "companion.h"
#include "companion_ui.h"
#include "data_gen.h"
#include "guide_view.h"
#include "layout.h"
#include "libretro.h"
#include "map_art.h"
#include "map_view.h"
#include "party_view.h"
#include "pk_nds_field.h"

extern "C" {
#include "hud.h"
#include "pd_canvas.h"
#include "pd_fs.h"
#include "pd_icon.h"
#include "pd_sha1.h"
#include "pd_tr.h"
#include "quick_menu.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <SDL.h>

namespace PdDs
{

namespace
{

thread_local uint8_t* g_ram = nullptr;
thread_local size_t g_ramSize = 0;

uint32_t ReadRam32(uint32_t addr)
{
    if (!g_ram || addr < 0x02000000u) return 0;
    const uint32_t off = addr - 0x02000000u;
    if ((size_t)off + 4 > g_ramSize) return 0;
    uint32_t v = 0;
    memcpy(&v, g_ram + off, 4);
    return v;
}

void WriteRam32(uint32_t addr, uint32_t v)
{
    if (!g_ram || addr < 0x02000000u) return;
    const uint32_t off = addr - 0x02000000u;
    if ((size_t)off + 4 > g_ramSize) return;
    memcpy(g_ram + off, &v, 4);
}

void WriteRam8(uint32_t addr, uint8_t v)
{
    if (!g_ram || addr < 0x02000000u) return;
    const uint32_t off = addr - 0x02000000u;
    if ((size_t)off + 1 > g_ramSize) return;
    g_ram[off] = v;
}

int ToPdLang(const std::string& code)
{
    if (code == "es") return PD_LANG_ES;
    if (code == "fr") return PD_LANG_FR;
    if (code == "de") return PD_LANG_DE;
    if (code == "it") return PD_LANG_IT;
    if (code == "pt") return PD_LANG_PT;
    if (code == "en") return PD_LANG_EN;
    return PD_LANG_AUTO;
}

// Fallback game detection by 4-char DS header code when a ROM hack or trimmed dump has an unlisted SHA-1.
DsGame FallbackGameFromCode(const uint8_t* rom, size_t len, bool& soulSilver)
{
    soulSilver = false;
    if (!rom || len < 0x20) return DsGame::Unsupported;
    char code[5] = {};
    memcpy(code, rom + 12, 4);
    if (!strncmp(code, "CPU", 3)) return DsGame::Platinum;
    if (!strncmp(code, "IPK", 3)) return DsGame::Johto;
    if (!strncmp(code, "IPG", 3)) { soulSilver = true; return DsGame::Johto; }
    if (!strncmp(code, "IRB", 3)) return DsGame::Black;
    if (!strncmp(code, "IRA", 3)) return DsGame::White;
    if (!strncmp(code, "IRE", 3)) return DsGame::Black2;
    if (!strncmp(code, "IRD", 3)) return DsGame::White2;
    return DsGame::Unsupported;
}

// Fallback battle strip theme by DS game family so regional/Spanish/hack ROMs also get PokeDaisy's strip.
const DsBattleStyle* BattleStyleFor(const char* shaHex, DsGame game, bool soulSilver)
{
    if (shaHex && *shaHex)
    {
        if (const DsBattleStyle* exact = ds_battle_style(shaHex)) return exact;
    }
    static const DsBattleStyle gold{ 144, 146, ds_platinum_battle_style.actions, DsBattleLook::HeartGold };
    static const DsBattleStyle silver{ 144, 146, ds_platinum_battle_style.actions, DsBattleLook::SoulSilver };
    static const DsBattleStyle unova{ 145, 149, {{
        { 0xff6e82, 0, 0, 0, 0, 0 }, { 0xfbb228, 0, 0, 0, 0, 0 },
        { 0x51cb28, 0, 0, 0, 0, 0 }, { 0x28baf3, 0, 0, 0, 0, 0 },
    }}, DsBattleLook::Unova };
    static const DsBattleStyle unova2{ 146, 149, {{
        { 0xfb2018, 0x921818, 0, 0, 0, 0 }, { 0xfbcb28, 0x927108, 0, 0, 0, 0 },
        { 0x71db30, 0x207108, 0, 0, 0, 0 }, { 0x20a2fb, 0x005992, 0, 0, 0, 0 },
    }}, DsBattleLook::Unova2 };
    switch (game)
    {
    case DsGame::Platinum: return &ds_platinum_battle_style;
    case DsGame::Johto: return soulSilver ? &silver : &gold;
    case DsGame::Black:
    case DsGame::White: return &unova;
    case DsGame::Black2:
    case DsGame::White2: return &unova2;
    default: return nullptr;
    }
}

thread_local void* g_hudImpl = nullptr;

} // namespace

struct Bridge::Impl
{
    DsGame game = DsGame::Unsupported;
    const PkNdsFieldGame* fieldGame = nullptr;
    const DsBattleStyle* battleStyle = nullptr;
    bool supported = false;
    bool soulSilver = false;
    bool loaded = false;
    bool assetsLoaded = false;
    char headerLang = 0;

    std::vector<uint8_t> romCopy; // kept only until lazy cart art load completes, then freed
    std::string dir;

    DsCompanionUi shell{};
    DsCompanion snapshot{};
    DsMapArt mapArt{};
    std::vector<std::vector<uint32_t>> partyIcons;
    std::vector<std::vector<uint32_t>> hudItemIcons;

    bool companionOpen = false;
    bool quickOpen = false;
    bool repelList = false;
    int quickSel = 0;
    int repelSel = 0;
    int repelChoices[3] = {};
    int repelCount = 0;
    std::string repelLabels[3];
    bool wildOff = false;

    int tickCounter = 0;
    int repelFull = 0;
    int hudWild = -1;
    uint32_t hudHash = 0;
    hud_hits hudHits{};

    // PokeDaisy DS Battle Strip state (mirrors pd_ds_libretro.cpp).
    DsBattleStrip battleStrip{};
    DsBattleView drawnBattle{};
    std::string battleName;
    std::vector<uint32_t> battlePanelArt;
    bool battlePanelArtValid = false;
    bool battleActive = false;
    int battleHold = 0;
    unsigned battleTick = 0;
    bool pendingTouchAccept = false;
    bool pendingTouchBack = false;
    std::vector<uint32_t> battleStage;
    int battleStageW = 512;
    SDL_Texture* battleTex = nullptr;
    int battleTexW = 0, battleTexH = 0;
    SDL_Rect battleDst{};

    std::vector<uint32_t> overlayBuf;
    std::vector<uint32_t> hudBuf;
    SDL_Texture* overlayTex = nullptr;
    int overlayTexW = 0, overlayTexH = 0;
    SDL_Rect overlayDst{};
    SDL_Texture* hudTex = nullptr;
    int hudTexW = 0, hudTexH = 0;
    SDL_Rect hudDst{};
    int lastCanvasW = 426, lastCanvasH = 240;

    ~Impl() { FreeTextures(); }

    void FreeTextures()
    {
        if (overlayTex) { SDL_DestroyTexture(overlayTex); overlayTex = nullptr; }
        overlayTexW = overlayTexH = 0;
        if (hudTex) { SDL_DestroyTexture(hudTex); hudTex = nullptr; }
        hudTexW = hudTexH = 0;
        if (battleTex) { SDL_DestroyTexture(battleTex); battleTex = nullptr; }
        battleTexW = battleTexH = 0;
    }

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

    void EnsureCartAssets()
    {
        if (assetsLoaded || game == DsGame::Unsupported || romCopy.empty()) return;
        assetsLoaded = true;
        mapArt = ds_map_art(romCopy, game);
        partyIcons = ds_party_icons(romCopy, game);
        shell.dexArt = ds_dex_art(romCopy, game);
        hudItemIcons = ds_hud_item_icons(romCopy, game);
        ds_battle_set_base_pp(ds_move_base_pp(romCopy, game));
        // Release the temporary ROM copy once art/tables are unpacked into compact vectors.
        std::vector<uint8_t>().swap(romCopy);
    }

    void Refresh(uint8_t* mainRam, size_t mainRamSize, bool inBattle)
    {
        if (game == DsGame::Unsupported || !mainRam || mainRamSize < 0x400000u) return;
        DsCompanion next = ds_read_companion(mainRam, mainRamSize, game);
        if (next.valid || game == DsGame::Platinum)
        {
            snapshot = std::move(next);
            shell.guideLoadedPage = -1;
        }
        if (assetsLoaded) ds_map_update(mapArt, snapshot);
        if (inBattle && fieldGame)
        {
            g_ram = mainRam;
            g_ramSize = mainRamSize;
            ds_read_battle_companion(fieldGame, ReadRam32, mainRam, mainRamSize, snapshot, shell.battle);
            shell.update_battle();
            g_ram = nullptr;
            g_ramSize = 0;
        }
        else
        {
            shell.battle.inBattle = false;
        }
        if (shell.mapX < 0)
        {
            shell.mapX = snapshot.mapX;
            shell.mapY = snapshot.mapY;
        }
        // Count uncaught wild species on the current map for the HUD badge.
        hudWild = -1;
        if (snapshot.valid && snapshot.dexValid)
        {
            const DsGuideHere here = ds_guide_here(snapshot, soulSilver);
            if (here.entries && here.count > 0)
            {
                std::vector<int> seen;
                int uncaught = 0;
                for (int i = 0; i < here.count; i++)
                {
                    const int sp = here.entries[i].species;
                    if (sp <= 0 || std::find(seen.begin(), seen.end(), sp) != seen.end()) continue;
                    seen.push_back(sp);
                    const size_t byteIdx = (size_t)(sp - 1) / 8;
                    const uint8_t bit = (uint8_t)(1u << ((sp - 1) & 7));
                    const bool caught = byteIdx < snapshot.dexCaught.size() && (snapshot.dexCaught[byteIdx] & bit);
                    if (!caught) uncaught++;
                }
                hudWild = uncaught;
            }
        }
    }

    int BuildQuickItems(struct quick_item out[QUICK_COUNT], const HostActions& actions) const
    {
        int n = 0;
        auto add = [&](enum quick_action a) -> struct quick_item& {
            memset(&out[n], 0, sizeof out[n]);
            out[n].action = a;
            return out[n++];
        };
        add(QUICK_REPEL);
        if (fieldGame)
        {
            auto& it = add(QUICK_WILD);
            it.off = wildOff;
        }
        {
            auto& it = add(QUICK_HUD);
            it.lit = shell.settings.hud;
        }
        add(QUICK_COMPANION);
        if (actions.getFf && actions.setFf)
        {
            auto& it = add(QUICK_FF);
            it.lit = actions.getFf();
        }
        if (actions.saveState) add(QUICK_SAVE_STATE);
        if (actions.loadState && (!actions.stateExists || actions.stateExists())) add(QUICK_LOAD_STATE);
        return n;
    }

    void RunQuickAction(const struct quick_item& item, uint8_t* mainRam, size_t mainRamSize, const HostActions& actions)
    {
        auto toast = [&](const char* s) { if (s && *s && actions.toast) actions.toast(s); };
        switch (item.action)
        {
        case QUICK_REPEL:
        {
            static constexpr int kRepels[3] = { 79, 76, 77 }; // Repel, Super Repel, Max Repel
            repelCount = 0;
            for (int id : kRepels)
            {
                for (const DsItem& bi : snapshot.bag)
                {
                    if (bi.id == id && bi.pocket == 0 && bi.count > 0)
                    {
                        repelChoices[repelCount] = id;
                        const char* nm = ds_item(id, game);
                        repelLabels[repelCount] = std::string(nm && *nm ? nm : "REPEL") + " x" + std::to_string(bi.count);
                        repelCount++;
                        break;
                    }
                }
            }
            if (repelCount <= 0) { toast("NO REPEL IN BAG"); quickOpen = false; }
            else if (repelCount == 1)
            {
                ApplyRepel(repelChoices[0], mainRam, mainRamSize, actions);
                quickOpen = false;
            }
            else { repelSel = 0; repelList = true; }
            break;
        }
        case QUICK_WILD:
            wildOff = !wildOff;
            toast(wildOff ? "WILD ENCOUNTERS OFF" : "WILD ENCOUNTERS ON");
            quickOpen = false;
            break;
        case QUICK_HUD:
            shell.settings.hud = !shell.settings.hud;
            hudHash = 0;
            shell.save();
            toast(shell.settings.hud ? "HUD ON" : "HUD OFF");
            quickOpen = false;
            break;
        case QUICK_COMPANION:
            quickOpen = false;
            EnsureCartAssets();
            companionOpen = true;
            Refresh(mainRam, mainRamSize, shell.battle.inBattle);
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

    void ApplyRepel(int itemId, uint8_t* mainRam, size_t mainRamSize, const HostActions& actions)
    {
        if (!mainRam || mainRamSize < 0x400000u) return;
        g_ram = mainRam;
        g_ramSize = mainRamSize;
        const DsRepelResult r = ds_use_repel(mainRam, mainRamSize, game, itemId, WriteRam8);
        g_ram = nullptr;
        g_ramSize = 0;
        if (!actions.toast) return;
        if (r == DsRepelResult::Used) actions.toast("REPEL ACTIVE");
        else if (r == DsRepelResult::StillActive) actions.toast("REPEL ALREADY ACTIVE");
        else if (r == DsRepelResult::NoneLeft) actions.toast("NO REPEL LEFT");
        else actions.toast("REPEL NOT AVAILABLE HERE");
    }

    uint32_t ComputeHudHash() const
    {
        uint32_t h = 2166136261u;
        auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
        mix((uint32_t)snapshot.valid);
        mix((uint32_t)snapshot.map);
        mix((uint32_t)snapshot.mapX);
        mix((uint32_t)snapshot.mapY);
        mix((uint32_t)snapshot.repelSteps);
        mix((uint32_t)hudWild);
        mix((uint32_t)shell.settings.hudLook);
        mix((uint32_t)shell.settings.hudLayout);
        mix((uint32_t)shell.settings.hudHidden);
        mix((uint32_t)shell.settings.hudSize);
        mix((uint32_t)shell.hudMinimal);
        mix((uint32_t)snapshot.bag.size());
        for (const DsItem& it : snapshot.bag)
        {
            const bool unovaBall = (it.id >= 1 && it.id <= 16) || (it.id >= 492 && it.id <= 500) || it.id == 576;
            const bool isBall = ds_unova(game) ? unovaBall : (it.pocket == 6);
            if (isBall) { mix((uint32_t)it.id); mix((uint32_t)it.count); }
        }
        mix((uint32_t)snapshot.party.size());
        for (const DsMon& m : snapshot.party)
        {
            mix((uint32_t)m.species);
            mix((uint32_t)m.hp);
            mix((uint32_t)m.maxHp);
            mix((uint32_t)m.level);
            mix(m.status);
        }
        return h ? h : 1u;
    }

    static bool DrawHudItemIcon(struct pd_canvas* c, int id, int x, int y)
    {
        Impl* self = static_cast<Impl*>(g_hudImpl);
        if (!self || !c || id < 0 || (size_t)id >= self->hudItemIcons.size()) return false;
        const auto& art = self->hudItemIcons[(size_t)id];
        if (art.size() != 32u * 32u) return false;
        for (int row = 0; row < PD_ITEM_ICON; ++row)
            for (int col = 0; col < PD_ITEM_ICON; ++col)
            {
                const uint32_t p = art[(size_t)(row * 32 / PD_ITEM_ICON) * 32 + (size_t)(col * 32 / PD_ITEM_ICON)];
                if (p >> 24) pd_fill(c, x + col, y + row, 1, 1, p & 0xFFFFFFu);
            }
        return true;
    }

    static void DrawHudMinimap(struct pd_canvas* c, int x, int y, int w, int h)
    {
        Impl* self = static_cast<Impl*>(g_hudImpl);
        if (!self || !c) return;
        ds_draw_minimap(c, self->snapshot, self->mapArt, x, y, w, h);
    }

    static void DrawHudMonIcon(struct pd_canvas* c, void* ctx, const struct pd_mon* m, int x, int y, bool faded)
    {
        auto* self = static_cast<Impl*>(ctx);
        if (!self || !c || !m || m->species <= 0) return;
        const int idx = m->egg ? ds_egg_icon(self->game) : m->species;
        if (idx < 0 || (size_t)idx >= self->partyIcons.size()) return;
        const auto& icon = self->partyIcons[(size_t)idx];
        if (icon.size() != 32u * 32u) return;
        for (int py = 0; py < 32; py++)
            for (int px = 0; px < 32; px++)
            {
                const uint32_t p = icon[(size_t)py * 32 + px];
                if ((p >> 24) == 0) continue;
                const int dx = x + px, dy = y + py;
                if (dx < c->cx0 || dx >= c->cx1 || dy < c->cy0 || dy >= c->cy1) continue;
                uint32_t col = p & 0xFFFFFFu;
                if (faded)
                {
                    const uint32_t r = ((col >> 16) & 0xFFu) * 140 / 255;
                    const uint32_t g = ((col >> 8) & 0xFFu) * 140 / 255;
                    const uint32_t b = (col & 0xFFu) * 140 / 255;
                    col = (r << 16) | (g << 8) | b;
                }
                c->px[(size_t)dy * c->w + dx] = 0xFF000000u | col;
            }
    }
};

Bridge::Bridge() : impl(new Impl()) {}
Bridge::~Bridge() = default;

bool Bridge::Load(const uint8_t* romData, size_t romLen, const std::string& dataDir, const std::string& langCode)
{
    Unload();
    if (!romData || romLen < 0x200) return false;

    // Hash the in-RAM cartridge image to match PokeDaisy's exact Gen 4 / Gen 5 tables.
    struct pd_sha1 sha{};
    pd_sha1_init(&sha);
    pd_sha1_update(&sha, romData, romLen);
    char shaHex[41] = {};
    pd_sha1_hex(&sha, shaHex);

    const char* canon = ds_open_cart(shaHex);
    impl->game = ds_companion_game(canon ? canon : shaHex);
    impl->supported = (impl->game != DsGame::Unsupported);
    impl->soulSilver = romLen >= 16 && !memcmp(romData + 12, "IPG", 3);
    impl->headerLang = romLen >= 16 ? (char)romData[15] : 0;

    if (!impl->supported)
    {
        impl->game = FallbackGameFromCode(romData, romLen, impl->soulSilver);
        impl->supported = (impl->game != DsGame::Unsupported);
    }
    if (!impl->supported) return false;
    if (romLen >= 0x20)
    {
        char code4[5] = {};
        memcpy(code4, romData + 12, 4);
        ds_select_port_by_code(code4, romData[0x1E]);
    }

    impl->fieldGame = pk_nds_field_game_rom(romData, (uint32_t)romLen);
    if (!impl->fieldGame && romLen >= 0x20)
        impl->fieldGame = pk_nds_field_game(reinterpret_cast<const char*>(romData + 12), romData[0x1E]);
    impl->battleStyle = BattleStyleFor(canon ? canon : shaHex, impl->game, impl->soulSilver);
    impl->battleActive = false;
    impl->battleHold = 0;
    impl->battlePanelArtValid = false;
    impl->battleStrip = DsBattleStrip{};
    impl->drawnBattle = DsBattleView{};
    impl->battleName.clear();

    impl->dir = dataDir + "/pokedaisy-ds";
    pd_mkdir(dataDir.c_str());
    pd_mkdir(impl->dir.c_str());
    impl->shell.init(dataDir, 0, false);
    impl->shell.set_game(impl->game, impl->soulSilver);
    // Keep HUD off by default until enabled so existing DS players see zero surprise clutter.
    SyncLanguage(langCode);

    // Keep a copy only if ROM is <= 256 MB so we can unpack map/icon/dex NARCs lazily on first open.
    if (romLen <= (256u << 20))
        impl->romCopy.assign(romData, romData + romLen);

    impl->loaded = true;
    impl->assetsLoaded = false;
    impl->tickCounter = 0;
    impl->hudHash = 0;
    if (impl->fieldGame)
    {
        const DsWildRates wr = ds_wild_rates(impl->game, 0, impl->soulSilver);
        pk_nds_field_set_rates(wr.triples, wr.count);
        pk_nds_field_reset_wild();
    }
    fprintf(stderr, "[pokedaisy-ds] loaded game=%d field=%s sha1=%.12s\n",
            (int)impl->game, impl->fieldGame ? impl->fieldGame->name : "none", shaHex);
    return true;
}

void Bridge::Unload()
{
    if (!impl) return;
    if (impl->loaded) impl->shell.save();
    impl->FreeTextures();
    impl->loaded = false;
    impl->supported = false;
    impl->assetsLoaded = false;
    impl->companionOpen = false;
    impl->quickOpen = false;
    impl->repelList = false;
    impl->game = DsGame::Unsupported;
    impl->fieldGame = nullptr;
    impl->battleStyle = nullptr;
    impl->battleActive = false;
    impl->battleHold = 0;
    impl->battlePanelArtValid = false;
    std::vector<uint8_t>().swap(impl->romCopy);
}

void Bridge::OnReset()
{
    if (!impl || !impl->loaded) return;
    impl->companionOpen = false;
    impl->quickOpen = false;
    impl->repelList = false;
    impl->battleActive = false;
    impl->battleHold = 0;
    impl->battlePanelArtValid = false;
    impl->battleStrip = DsBattleStrip{};
    impl->drawnBattle = DsBattleView{};
    impl->battleName.clear();
    impl->hudHash = 0;
    pk_nds_field_reset_wild();
}

void Bridge::SyncLanguage(const std::string& langCode)
{
    if (!impl) return;
    const int lang = ToPdLang(langCode);
    if (lang != PD_LANG_AUTO) pd_tr_set_system(lang);
    pd_tr_apply(lang, impl->headerLang);
    impl->shell.settings.language = lang;
}

bool Bridge::Supported() const { return impl && impl->loaded && impl->supported; }
bool Bridge::CompanionOpen() const { return impl && impl->companionOpen; }
bool Bridge::QuickOpen() const { return impl && impl->quickOpen; }
bool Bridge::HudEnabled() const { return impl && impl->loaded && impl->shell.settings.hud; }
bool Bridge::AutoRunEnabled() const { return impl && impl->loaded && impl->shell.autoRun; }
bool Bridge::WildOffEnabled() const { return impl && impl->loaded && impl->wildOff; }

void Bridge::SetHud(bool on)
{
    if (!impl || !impl->loaded) return;
    impl->shell.settings.hud = on;
    impl->hudHash = 0;
    if (on) impl->EnsureCartAssets();
    impl->shell.save();
}

void Bridge::SetAutoRun(bool on)
{
    if (!impl || !impl->loaded) return;
    impl->shell.autoRun = on;
    impl->shell.save();
}

void Bridge::SetWildOff(bool on)
{
    if (!impl || !impl->loaded) return;
    impl->wildOff = on;
}

void Bridge::ToggleCompanion()
{
    if (!impl || !impl->loaded) return;
    impl->quickOpen = false;
    impl->repelList = false;
    impl->companionOpen = !impl->companionOpen;
    if (impl->companionOpen) impl->EnsureCartAssets();
}

void Bridge::ToggleQuickMenu()
{
    if (!impl || !impl->loaded) return;
    impl->companionOpen = false;
    impl->repelList = false;
    impl->quickOpen = !impl->quickOpen;
    if (impl->quickOpen) impl->quickSel = 0;
}

void Bridge::CloseMenus()
{
    if (!impl) return;
    impl->companionOpen = false;
    impl->quickOpen = false;
    impl->repelList = false;
}

void Bridge::Tick(uint8_t* mainRam, size_t mainRamSize, bool inBattle, bool inMenuOrFade, const HostActions& actions)
{
    (void)inMenuOrFade;
    (void)actions;
    if (!impl || !impl->loaded || !mainRam || mainRamSize < 0x400000u) return;

    if (impl->fieldGame && impl->wildOff)
    {
        g_ram = mainRam;
        g_ramSize = mainRamSize;
        pk_nds_field_state(impl->fieldGame, ReadRam32, WriteRam32, 1);
        g_ram = nullptr;
        g_ramSize = 0;
    }

    const bool needRead = impl->companionOpen || impl->quickOpen || impl->shell.settings.hud;
    if (!needRead) return;
    const int period = impl->companionOpen ? 6 : 15;
    if (++impl->tickCounter >= period)
    {
        impl->tickCounter = 0;
        impl->Refresh(mainRam, mainRamSize, inBattle);
    }
}

bool Bridge::BattleStripActive() const
{
    return impl && impl->loaded && impl->battleStyle && (impl->battleActive || impl->battleHold > 0);
}

BattleTouchResult Bridge::TickBattle(uint8_t* mainRam, size_t mainRamSize, const uint32_t* botFb,
                                     bool enabled, int navDx, int navDy, bool accept, bool back)
{
    BattleTouchResult out{};
    if (!impl || !impl->loaded || !enabled || !impl->fieldGame || !impl->battleStyle ||
        !mainRam || mainRamSize < 0x400000u || impl->companionOpen || impl->quickOpen)
    {
        if (impl)
        {
            impl->battleActive = false;
            impl->battleHold = 0;
            impl->pendingTouchAccept = false;
            impl->pendingTouchBack = false;
        }
        return out;
    }

    g_ram = mainRam;
    g_ramSize = mainRamSize;
    const int bits = pk_nds_field_state(impl->fieldGame, ReadRam32, nullptr, 0);
    const DsBattleView view = ds_read_battle(impl->fieldGame, ReadRam32, bits);
    const bool stripWasUp = impl->battleActive;
    impl->battleActive = view.panel != DsBattlePanel::Native;

    if (impl->battleActive)
    {
        impl->EnsureCartAssets();
        const bool changed = impl->drawnBattle.panel != view.panel ||
                             impl->drawnBattle.personality != view.personality ||
                             impl->drawnBattle.partySlot != view.partySlot ||
                             impl->drawnBattle.species != view.species ||
                             (view.panel == DsBattlePanel::Moves &&
                              (impl->drawnBattle.moves != view.moves || impl->drawnBattle.pp != view.pp));
        if (changed) impl->battlePanelArtValid = false;

        DsBattleView nextView = view;
        if (changed || !stripWasUp)
        {
            if (view.panel == DsBattlePanel::Actions)
            {
                const DsCompanion snap = ds_read_companion(mainRam, mainRamSize, impl->game);
                ds_read_battle_balls(impl->fieldGame, ReadRam32, mainRam, mainRamSize, snap, nextView);
            }
            else if (impl->drawnBattle.balls)
            {
                nextView.balls = true;
                nextView.partyBalls = impl->drawnBattle.partyBalls;
                nextView.foeBalls = impl->drawnBattle.foeBalls;
            }
        }
        else if (impl->drawnBattle.balls)
        {
            nextView.balls = true;
            nextView.partyBalls = impl->drawnBattle.partyBalls;
            nextView.foeBalls = impl->drawnBattle.foeBalls;
        }

        if (botFb && !impl->battlePanelArtValid && impl->battleStyle->look == DsBattleLook::Platinum)
        {
            const bool ready = view.panel == DsBattlePanel::Actions
                                   ? ds_platinum_has_command_art(botFb)
                                   : ds_platinum_has_move_art(botFb, view);
            if (ready)
            {
                impl->battlePanelArt.assign(botFb, botFb + 256u * 192u);
                impl->battlePanelArtValid = true;
            }
        }

        if (impl->drawnBattle.personality != view.personality ||
            impl->drawnBattle.partySlot != view.partySlot ||
            impl->drawnBattle.species != view.species || impl->battleName.empty())
        {
            impl->battleName = ds_battle_species(view);
            const DsCompanion snap = ds_read_companion(mainRam, mainRamSize, impl->game);
            const auto& party = snap.party;
            auto mon = party.end();
            if (view.gen5)
            {
                if (view.partySlot >= 0 && (size_t)view.partySlot < party.size())
                    mon = party.begin() + view.partySlot;
            }
            else
            {
                mon = std::find_if(party.begin(), party.end(),
                                   [&](const DsMon& c) { return c.personality == view.personality; });
            }
            if (mon != party.end() && mon->species == view.species && !mon->nickname.empty())
                impl->battleName = mon->nickname;
        }

        impl->drawnBattle = nextView;
        impl->battleHold = 12;

        const bool fireAccept = accept || impl->pendingTouchAccept;
        const bool fireBack = back || impl->pendingTouchBack;
        impl->pendingTouchAccept = false;
        impl->pendingTouchBack = false;
        impl->battleStrip.select(impl->drawnBattle, navDx, navDy, fireAccept, fireBack);
    }
    else if (impl->battleHold > 0)
    {
        --impl->battleHold;
        impl->pendingTouchAccept = false;
        impl->pendingTouchBack = false;
    }

    const DsBattleTap tap = impl->battleStrip.tick();
    out.down = tap.down;
    out.x = tap.x;
    out.y = tap.y;

    g_ram = nullptr;
    g_ramSize = 0;
    return out;
}

bool Bridge::HandleBattleTouch(int sx, int sy, int ow, int oh, bool down)
{
    if (!impl || !impl->loaded || !impl->battleActive || !impl->battleStyle || !down ||
        impl->battleDst.w <= 0 || impl->battleDst.h <= 0 || ow <= 0 || oh <= 0)
        return false;
    if (sx < impl->battleDst.x || sx >= impl->battleDst.x + impl->battleDst.w ||
        sy < impl->battleDst.y || sy >= impl->battleDst.y + impl->battleDst.h)
        return false;

    const bool moves = impl->drawnBattle.panel == DsBattlePanel::Moves;
    const int bandTop = 2 * impl->battleStyle->bandTop - (moves ? 14 : 0);
    const int bandH = 384 - bandTop;
    const int stageW = std::max(512, impl->battleStageW);
    const int right = stageW - 4;
    const int commandLeft = stageW * 270 / 512;
    const int lx = (sx - impl->battleDst.x) * stageW / impl->battleDst.w;
    const int ly = bandTop + (sy - impl->battleDst.y) * bandH / impl->battleDst.h;

    const int top = moves ? bandTop : 2 * impl->battleStyle->textTop;
    const int height = (moves ? 384 : 382) - top;
    const int rowGap = moves ? 2 : 4;
    const int left = moves ? 6 : commandLeft;
    const int end = moves ? (right - 52) : right;
    const int cellW = (end - left - 4) / 2;
    const int cellH = (height - rowGap) / 2;

    if (moves && lx >= end && lx <= right && ly >= top && ly < top + height)
    {
        impl->pendingTouchBack = true;
        return true;
    }
    for (int i = 0; i < 4; ++i)
    {
        const int cx = left + (i % 2) * (cellW + 4);
        const int cy = top + (i / 2) * (cellH + rowGap);
        if (lx >= cx && lx < cx + cellW && ly >= cy && ly < cy + cellH)
        {
            if (!impl->drawnBattle.enabled(i)) return true;
            if (moves) impl->battleStrip.moveFocus = i;
            else impl->battleStrip.actionFocus = i;
            impl->pendingTouchAccept = true;
            return true;
        }
    }
    return true;
}

void Bridge::DrawBattleStrip(SDL_Renderer* ren, int ow, int oh, const uint32_t* topFb)
{
    if (!impl || !impl->loaded || !ren || !impl->battleStyle ||
        (!impl->battleActive && impl->battleHold <= 0) ||
        impl->drawnBattle.panel == DsBattlePanel::Native || ow <= 0 || oh <= 0)
        return;

    const bool moves = impl->drawnBattle.panel == DsBattlePanel::Moves;
    const int bandTop = 2 * impl->battleStyle->bandTop - (moves ? 14 : 0);
    const int bandH = 384 - bandTop;
    if (bandH <= 0 || bandTop < 0 || bandTop >= 384) return;

    const int stageW = std::max(512, ((384 * ow + oh / 2) / oh) & ~1);
    impl->battleStageW = stageW;
    if (impl->battleStage.size() != (size_t)stageW * 384u) impl->battleStage.assign((size_t)stageW * 384u, 0);
    // Copy the game's bottom-band rows from the top DS screen for Gen 4's native message box frame.
    if (topFb && !moves && impl->battleStyle->look != DsBattleLook::Unova &&
        impl->battleStyle->look != DsBattleLook::Unova2)
    {
        for (int y = bandTop; y < 384; ++y)
        {
            const uint32_t* src = topFb + (size_t)(y / 2) * 256u;
            uint32_t* dst = impl->battleStage.data() + (size_t)y * (size_t)stageW;
            for (int x = 0; x < 256; ++x)
            {
                const uint32_t p = 0xFF000000u | (src[x] & 0xFFFFFFu);
                dst[x * 2] = p;
                dst[x * 2 + 1] = p;
            }
        }
    }
    else
    {
        std::fill(impl->battleStage.begin() + (size_t)bandTop * (size_t)stageW, impl->battleStage.end(), 0xFF101418u);
    }

    const bool pressed = impl->battleStrip.tapFrames > 0;
    const int focus = !moves ? impl->battleStrip.actionFocus
                             : (pressed && impl->battleStrip.cancelingMove) ? 4 : impl->battleStrip.moveFocus;
    ds_draw_battle_strip(impl->battleStage.data(), *impl->battleStyle, impl->drawnBattle, impl->battleName,
                         focus, impl->battleActive && !pressed, ++impl->battleTick,
                         impl->battlePanelArtValid ? impl->battlePanelArt.data() : nullptr, pressed, stageW);

    // Ensure opaque alpha across the drawn strip band before uploading.
    uint32_t* bandPtr = impl->battleStage.data() + (size_t)bandTop * (size_t)stageW;
    for (size_t i = 0; i < (size_t)bandH * (size_t)stageW; ++i) bandPtr[i] |= 0xFF000000u;

    Impl::UploadTexture(ren, impl->battleTex, impl->battleTexW, impl->battleTexH, stageW, bandH, bandPtr);
    if (impl->battleTex)
    {
        // Span the full bottom width of the view while keeping square 1:1 stage proportions.
        const int dh = std::max(1, (oh * bandH + 192) / 384);
        impl->battleDst = { 0, oh - dh, ow, dh };
        SDL_RenderCopy(ren, impl->battleTex, nullptr, &impl->battleDst);
    }
}

void Bridge::HandleNav(const NavInput& in, uint8_t* mainRam, size_t mainRamSize, const HostActions& actions)
{
    if (!impl || !impl->loaded) return;
    if (impl->companionOpen)
    {
        unsigned mask = 0;
        if (in.up) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
        if (in.down) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
        if (in.left) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (in.right) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (in.accept) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
        if (in.back) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
        if (in.pageL) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_L;
        if (in.pageR) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_R;
        if (mask && impl->shell.control(mask))
            impl->companionOpen = false;
        return;
    }
    if (impl->quickOpen)
    {
        if (impl->repelList)
        {
            if (in.up && impl->repelSel > 0) impl->repelSel--;
            if (in.down && impl->repelSel + 1 < impl->repelCount) impl->repelSel++;
            if (in.back) { impl->repelList = false; return; }
            if (in.accept && impl->repelSel >= 0 && impl->repelSel < impl->repelCount)
            {
                impl->ApplyRepel(impl->repelChoices[impl->repelSel], mainRam, mainRamSize, actions);
                impl->repelList = false;
                impl->quickOpen = false;
            }
            return;
        }
        struct quick_item qItems[QUICK_COUNT];
        const int qn = impl->BuildQuickItems(qItems, actions);
        if (qn <= 0 || in.back) { impl->quickOpen = false; return; }
        if (impl->quickSel >= qn) impl->quickSel = 0;
        const int dx = (in.right ? 1 : 0) - (in.left ? 1 : 0);
        const int dy = (in.down ? 1 : 0) - (in.up ? 1 : 0);
        if (dx || dy) impl->quickSel = quick_step(qn, impl->quickSel, dx, dy, impl->lastCanvasW, impl->lastCanvasH);
        if (in.accept) impl->RunQuickAction(qItems[impl->quickSel], mainRam, mainRamSize, actions);
    }
}

bool Bridge::HandleTouch(int sx, int sy, int ow, int oh, bool down, uint8_t* mainRam, size_t mainRamSize,
                         const HostActions& actions)
{
    if (!impl || !impl->loaded || ow <= 0 || oh <= 0) return false;
    if (impl->companionOpen && impl->overlayDst.w > 0 && impl->overlayDst.h > 0)
    {
        const int cx = (sx - impl->overlayDst.x) * impl->lastCanvasW / impl->overlayDst.w;
        const int cy = (sy - impl->overlayDst.y) * impl->lastCanvasH / impl->overlayDst.h;
        if (down && (cx < 0 || cx >= impl->lastCanvasW || cy < 0 || cy >= impl->lastCanvasH))
        {
            impl->companionOpen = false;
            return true;
        }
        impl->shell.touch(std::clamp(cx, 0, impl->lastCanvasW - 1), std::clamp(cy, 0, impl->lastCanvasH - 1), down);
        return true;
    }
    if (impl->quickOpen && down && impl->overlayDst.w > 0 && impl->overlayDst.h > 0)
    {
        const int cx = (sx - impl->overlayDst.x) * impl->lastCanvasW / impl->overlayDst.w;
        const int cy = (sy - impl->overlayDst.y) * impl->lastCanvasH / impl->overlayDst.h;
        if (impl->repelList)
        {
            const int hit = quick_list_hit(cx, cy);
            if (hit >= 0 && hit < impl->repelCount)
            {
                impl->ApplyRepel(impl->repelChoices[hit], mainRam, mainRamSize, actions);
                impl->repelList = false;
                impl->quickOpen = false;
            }
            else impl->repelList = false;
            return true;
        }
        const int hit = quick_hit(cx, cy);
        if (hit >= 0)
        {
            struct quick_item qItems[QUICK_COUNT];
            const int qn = impl->BuildQuickItems(qItems, actions);
            if (hit < qn)
            {
                impl->quickSel = hit;
                impl->RunQuickAction(qItems[hit], mainRam, mainRamSize, actions);
            }
        }
        else impl->quickOpen = false;
        return true;
    }
    if (impl->shell.settings.hud && down && impl->hudDst.w > 0 && impl->hudDst.h > 0 &&
        impl->hudTexW > 0 && impl->hudTexH > 0 && impl->hudHits.n > 0)
    {
        const int hx = (sx - impl->hudDst.x) * impl->hudTexW / impl->hudDst.w;
        const int hy = (sy - impl->hudDst.y) * impl->hudTexH / impl->hudDst.h;
        for (int i = 0; i < impl->hudHits.n; i++)
        {
            const auto& r = impl->hudHits.at[i];
            if (hx >= r.x && hx < r.x + r.w && hy >= r.y && hy < r.y + r.h)
            {
                impl->EnsureCartAssets();
                impl->companionOpen = true;
                if (r.id == HUD_BAG) impl->shell.frame.tab = PD_TAB_BAG;
                else if (r.id == HUD_WILD) impl->shell.frame.tab = PD_TAB_GUIDE;
                else if (r.id == HUD_MAP || r.id == HUD_MAP_BUTTON) impl->shell.frame.tab = PD_TAB_MAP;
                else if (r.id >= HUD_SLOT && r.id < HUD_SLOT + 6) impl->shell.frame.tab = PD_TAB_PARTY;
                return true;
            }
        }
    }
    return false;
}

void Bridge::Draw(SDL_Renderer* ren, int ow, int oh, bool onOverworld, const HostActions& actions)
{
    if (!impl || !impl->loaded || !ren || ow <= 0 || oh <= 0) return;

    // 1. Live Field HUD over the 3D overworld (full 16:9 widescreen canvas + live minimap + cart icons).
    if (impl->shell.settings.hud && onOverworld && !impl->companionOpen && !impl->quickOpen && impl->snapshot.valid)
    {
        impl->EnsureCartAssets();
        const int hw = (ow * 9 >= oh * 14) ? 426 : 320;
        const int hh = 240;
        const uint32_t h = impl->ComputeHudHash() ^ ((uint32_t)hw << 16);
        if (h != impl->hudHash || impl->hudTexW != hw || impl->hudTexH != hh)
        {
            impl->hudBuf.assign((size_t)hw * hh, QUICK_CLEAR);
            struct pd_canvas hc{};
            pd_canvas_init(&hc, hw, hh, impl->hudBuf.data());
            struct pd_snapshot pdSnap{};
            pdSnap.valid = impl->snapshot.valid;
            pdSnap.partyCount = std::min((int)impl->snapshot.party.size(), PD_PARTY_SIZE);
            for (int i = 0; i < pdSnap.partyCount; i++)
            {
                const DsMon& sm = impl->snapshot.party[(size_t)i];
                pdSnap.party[i].species = sm.egg ? PD_SPECIES_EGG : sm.species;
                pdSnap.party[i].level = sm.level;
                pdSnap.party[i].hp = sm.hp;
                pdSnap.party[i].maxHp = sm.maxHp;
                pdSnap.party[i].status = sm.status;
                pdSnap.party[i].egg = sm.egg;
                snprintf(pdSnap.party[i].nickname, sizeof pdSnap.party[i].nickname, "%s", sm.nickname.c_str());
            }
            pdSnap.bagOk = impl->snapshot.valid;
            auto& balls = pdSnap.bag[PD_POCKET_BALLS];
            for (const DsItem& item : impl->snapshot.bag)
            {
                const bool unovaBall = (item.id >= 1 && item.id <= 16) ||
                                       (item.id >= 492 && item.id <= 500) || item.id == 576;
                const bool isBall = ds_unova(impl->game) ? unovaBall : (item.pocket == 6);
                if (isBall && balls.count < PD_POCKET_SLOTS)
                    balls.items[balls.count++] = { (uint16_t)item.id, (uint16_t)item.count };
            }
            struct hud_state hs{};
            hs.repel = impl->snapshot.repelSteps;
            if (hs.repel > 0) impl->repelFull = std::max(impl->repelFull, hud_repel_full(hs.repel));
            else impl->repelFull = 0;
            hs.repelFull = impl->repelFull;
            hs.wild = impl->hudWild;
            hs.focus = -1;
            hs.mini = impl->shell.settings.hudSize != PD_HUD_LARGE;
            impl->hudHits.n = 0;
            g_hudImpl = impl.get();
            hud_ds_draw(&hc, &pdSnap, &hs, &impl->shell.settings, impl.get(), Impl::DrawHudMonIcon,
                        Impl::DrawHudItemIcon, 79, impl->shell.hudMinimal, hw, &impl->hudHits,
                        &ds_party_palette(impl->game));
            const auto player = ds_map_position(impl->snapshot);
            if (!impl->shell.hudMinimal && !(impl->shell.settings.hudHidden & (1 << PD_HUD_MAP)) &&
                !impl->mapArt.background.empty() && player.x >= 0 && player.y >= 0)
            {
                hud_ds_minimap(&hc, &impl->shell.settings, ds_place(impl->snapshot.map, impl->game),
                               Impl::DrawHudMinimap, &impl->hudHits);
            }
            g_hudImpl = nullptr;
            quick_finish(&hc);
            for (uint32_t& p : impl->hudBuf)
                if (p == QUICK_CLEAR) p = 0;
            Impl::UploadTexture(ren, impl->hudTex, impl->hudTexW, impl->hudTexH, hw, hh, impl->hudBuf.data());
            impl->hudHash = h;
        }
        if (impl->hudTex)
        {
            const int scale = std::max(1, std::min(ow / impl->hudTexW, oh / impl->hudTexH));
            impl->hudDst = { (ow - impl->hudTexW * scale) / 2, (oh - impl->hudTexH * scale) / 2,
                             impl->hudTexW * scale, impl->hudTexH * scale };
            SDL_RenderCopy(ren, impl->hudTex, nullptr, &impl->hudDst);
        }
    }

    // 2. Modal Companion or Quick Menu overlay.
    if (!impl->companionOpen && !impl->quickOpen) return;
    const int cw = (ow * 9 >= oh * 14) ? 426 : 320;
    const int ch = 240;
    impl->lastCanvasW = cw;
    impl->lastCanvasH = ch;
    impl->overlayBuf.assign((size_t)cw * ch, impl->companionOpen ? 0xFF000000u : QUICK_CLEAR);
    struct pd_canvas c{};
    pd_canvas_init(&c, cw, ch, impl->overlayBuf.data());

    if (impl->companionOpen)
    {
        impl->EnsureCartAssets();
        impl->shell.draw(&c, impl->snapshot, impl->supported, impl->mapArt, impl->partyIcons);
        for (uint32_t& p : impl->overlayBuf) p |= 0xFF000000u;
    }
    else if (impl->quickOpen)
    {
        if (impl->repelList)
        {
            const char* rows[3] = { impl->repelLabels[0].c_str(), impl->repelLabels[1].c_str(), impl->repelLabels[2].c_str() };
            quick_list_draw(&c, "REPEL", rows, impl->repelCount, impl->repelSel, impl->shell.settings.hudLook);
        }
        else
        {
            struct quick_item qItems[QUICK_COUNT];
            const int qn = impl->BuildQuickItems(qItems, actions);
            if (impl->quickSel >= qn) impl->quickSel = 0;
            struct pd_game dummyGame{};
            quick_draw(&c, &dummyGame, qItems, qn, impl->quickSel, impl->shell.settings.hudLook);
        }
        quick_finish(&c);
        for (uint32_t& p : impl->overlayBuf)
            if (p == QUICK_CLEAR) p = 0;
    }

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 6, 10, 18, impl->companionOpen ? 210 : 120);
    SDL_RenderFillRect(ren, nullptr);

    Impl::UploadTexture(ren, impl->overlayTex, impl->overlayTexW, impl->overlayTexH, cw, ch, impl->overlayBuf.data());
    if (impl->overlayTex)
    {
        const int scale = std::max(1, std::min(ow / cw, oh / ch));
        impl->overlayDst = { (ow - cw * scale) / 2, (oh - ch * scale) / 2, cw * scale, ch * scale };
        SDL_RenderCopy(ren, impl->overlayTex, nullptr, &impl->overlayDst);
    }
}

} // namespace PdDs
