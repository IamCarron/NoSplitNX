// PokeDaisy Companion, Field HUD & Quick Menu bridge for NoSplitNX's melonDS backend.
// Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy (GNU GPL version 3).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct SDL_Renderer;

namespace PdDs
{

struct HostActions
{
    std::function<bool()> saveState;
    std::function<bool()> loadState;
    std::function<bool()> stateExists;
    std::function<bool()> getFf;
    std::function<void(bool)> setFf;
    std::function<void(const std::string&)> toast;
};

struct NavInput
{
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false, pageL = false, pageR = false;
};

struct BattleTouchResult
{
    bool down = false;
    int x = 0, y = 0;
};

class Bridge
{
public:
    Bridge();
    ~Bridge();

    // Initialise for the currently loaded DS cartridge (uses melonDS's in-RAM ROM buffer directly).
    bool Load(const uint8_t* romData, size_t romLen, const std::string& dataDir, const std::string& langCode);
    void Unload();
    void OnReset();
    void SyncLanguage(const std::string& langCode);

    bool Supported() const;
    bool CompanionOpen() const;
    bool QuickOpen() const;
    bool AnyMenuOpen() const { return CompanionOpen() || QuickOpen(); }
    bool HudEnabled() const;
    void SetHud(bool on);
    bool AutoRunEnabled() const;
    void SetAutoRun(bool on);
    bool WildOffEnabled() const;
    void SetWildOff(bool on);

    void ToggleCompanion();
    void ToggleQuickMenu();
    void CloseMenus();

    // Called once per emulated DS frame with direct access to melonDS's 4 MB MainRAM.
    void Tick(uint8_t* mainRam, size_t mainRamSize, bool inBattle, bool inMenuOrFade, const HostActions& actions);

    // PokeDaisy DS Battle Strip: reads live battle command/move state, processes D-pad/A/B
    // navigation and returns the DS touch-screen press to feed into melonDS.
    bool BattleStripActive() const;
    BattleTouchResult TickBattle(uint8_t* mainRam, size_t mainRamSize, const uint32_t* botFb,
                                 bool enabled, int navDx, int navDy, bool accept, bool back);
    bool HandleBattleTouch(int sx, int sy, int ow, int oh, bool down);
    void DrawBattleStrip(SDL_Renderer* ren, int ow, int oh, const uint32_t* topFb);

    // Controller / keyboard navigation when Companion or Quick Menu is open.
    void HandleNav(const NavInput& in, uint8_t* mainRam, size_t mainRamSize, const HostActions& actions);

    // Touch tap / drag in renderer output pixels. Returns true if consumed by Companion, Quick Menu or HUD.
    bool HandleTouch(int sx, int sy, int ow, int oh, bool down, uint8_t* mainRam, size_t mainRamSize,
                     const HostActions& actions);

    // Draws the Field HUD (when overworld active), Quick Menu or Companion over the SDL renderer.
    void Draw(SDL_Renderer* ren, int ow, int oh, bool onOverworld, const HostActions& actions);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace PdDs
