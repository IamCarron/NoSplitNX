// Pokemon GBA Widescreen built into another program (the all-in-one launcher, Pokemon DS
// Widescreen's: CMake with GBAW_EMBEDDED, the library gbawide): its GBA games and their
// pictures for that program's list, and playing one in a window of its own until QUIT.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace GbaWide
{

struct ListGame
{
    std::string key;     // the game (emerald, ultraviolet, firered, leafgreen): its save is key.sav
    std::string name;    // as the list shows it: POKEMON EMERALD
    std::string file;    // the ROM, or Ultra Violet's .ips, as found: what Play takes
    std::string art;     // its picture's name for Art, "" for none
    bool hasSave = false;
    bool widescreen = false; // false: it plays, without widescreen
    bool hack = false;       // not one of the games as sold: a hack (or another game)
};

// The GBA games in the roms folder (Ultra Violet also as its .ips with a FireRed 1.0 ROM), with
// their saves in saves (key.sav; a .sav beside the ROM is copied in when the game starts). What
// each ROM is is kept in saves/gba-rom-list.txt.
std::vector<ListGame> Find(const std::string& roms, const std::string& saves);

// A game's picture, built in: a square JPEG (512x512). False if there is none.
bool Art(const std::string& name, const unsigned char*& jpeg, size_t& size);

// RetroAchievements, filled in by the program this is built into (null: this program has none).
// The GBA game's own pause menu calls these; the account and the connection stay in that program.
struct AchievementsHost
{
    void* ud = nullptr;
    // Reads `bytes` from RetroAchievements' GBA map into `buffer` (0 when `address` is not in it).
    using Read = uint32_t (*)(void* core, uint32_t address, uint8_t* buffer, uint32_t bytes);
    // The ROM as it is sold (before a widescreen patch) and the running core. False: not in use.
    bool (*begin)(void* ud, const uint8_t* rom, size_t romBytes, void* core, Read read) = nullptr;
    void (*end)(void* ud) = nullptr;
    void (*frame)(void* ud) = nullptr;           // after each emulated frame, including fast forward
    void (*pump)(void* ud, int paused) = nullptr; // once per shown frame
    void (*reset)(void* ud) = nullptr;           // the game was reset or a save state loaded
    void (*setOn)(void* ud, int on) = nullptr;
    void (*login)(void* ud, const char* user, const char* password) = nullptr;
    void (*logout)(void* ud) = nullptr;
    // Each writes a short line into buf (cap includes the NUL) and returns false when there is none.
    bool (*account)(void* ud, char* buf, size_t cap) = nullptr;
    bool (*game)(void* ud, char* buf, size_t cap) = nullptr;
    bool (*detail)(void* ud, char* buf, size_t cap) = nullptr;
    bool (*pop)(void* ud, char* buf, size_t cap) = nullptr; // one message for the player
    bool (*loggedIn)(void* ud) = nullptr;
    bool (*active)(void* ud) = nullptr;
    bool (*busy)(void* ud) = nullptr;
    bool (*on)(void* ud) = nullptr;
    int (*count)(void* ud) = nullptr; // the set, unlocked first
    // title, description and progress are filled in. points and unlocked (1 or 0) too.
    bool (*at)(void* ud, int index, char* title, size_t titleCap, char* description, size_t descriptionCap,
               char* progress, size_t progressCap, unsigned* points, int* unlocked) = nullptr;
    // Any cheat on (1) or none (0): achievements are paused while any is (the game's CHEATS).
    void (*cheats)(void* ud, int anyOn) = nullptr;
};
#define GBAW_HOST_HAS_CHEATS 1

struct MenuHostItem
{
    std::string label, value;
    bool info = false;
    int slider = -1;
};

struct MenuHostState
{
    int boxX = 0, boxY = 0, boxW = 0, boxH = 0;
    int lineH = 0, firstY = 0, shown = 0, scroll = 0;
    int barX = 0, barStep = 0;
    int count = 0;

    int ItemAt(int x, int y) const
    {
        if (boxW <= 0 || x < boxX || x >= boxX + boxW || y < boxY || y >= boxY + boxH) return -2;
        if (lineH <= 0 || y < firstY) return -1;
        const int rel = (y - firstY) / lineH;
        if (rel < 0 || (shown > 0 && rel >= shown)) return -1;
        const int i = scroll + rel;
        return i < count ? i : -1;
    }

    int SliderAt(int x) const
    {
        if (barStep <= 0 || x >= barX + 10 * barStep + barStep / 2) return -1;
        if (x < barX) return x >= barX - 2 * barStep ? 0 : -1;
        return std::min(10, (x - barX) / barStep + 1);
    }
};

// Optional host-rendered overlay (pause menu, toast and FPS counter) using NoSplitNX's Canvas/Menu.
struct UiHost
{
    void (*drawOverlay)(void* ud, std::vector<uint32_t>& outPx, int outW, int outH,
                        bool menuOpen, const std::vector<MenuHostItem>& items, int selected,
                        const std::string& title, const std::string& hint, const std::string& note,
                        const std::string& toast, int toastAlpha, int fpsShown, bool showFps,
                        const std::string& accentName, MenuHostState& drawn) = nullptr;
    void* ud = nullptr;
};

struct Options
{
    std::string roms, saves; // as for Find
    std::string data;        // its settings (gba-settings.ini) and save states (states/)
    bool fullscreen = true;  // else a window of w x h
    int w = 1280, h = 720;
    const AchievementsHost* achievements = nullptr; // null: no RetroAchievements
    const UiHost* ui = nullptr;                     // null: draw GBA's built-in pause menu
    std::string accent;                             // launcher list_colour theme name
};

// Plays the game found as `file` (Find) in a window of its own until QUIT GAME in its pause
// menu, or the window is closed. False if it could not start (the window's or the ROM's error is
// on stderr).
bool Play(const std::string& file, const Options& options);

}
