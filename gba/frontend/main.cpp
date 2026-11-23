// Pokemon GBA Widescreen: the player program. A game list of the player's ROMs, then the game
// on mGBA (with this project's wide renderer) and the widescreen patch put on it as it starts:
// one picture as wide as the screen, 60 frames a second, scaled to any size from 720p to 4K.
//
//   pokemon-gba-wide                    the game list
//   pokemon-gba-wide --game emerald     straight into a game (emerald, ultraviolet, firered, leafgreen)
//   pokemon-gba-wide --rom FILE         straight into any ROM
// Tests: --window WxH (a window of that size), --script FILE (key presses and screenshots by
// frame, for headless runs with SDL_VIDEODRIVER=offscreen; with GBAW_TEST_REALTIME set, at the
// game's real pace on the graphics card, reporting how steadily the frames came), --data DIR
// (the folder for roms, saves and settings).
#include <mgba/flags.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>

#include <SDL.h>

#include "art.h"
#include "fast_forward_button.h"
#include "fast_forward_audio.h"
#include "state_file.h"
#include "hack_names.h"
extern "C" {
#include "pokedaisy/pk_wide.h"
}
#include "pokedaisy/wide_camera.h"
#include "pokedaisy/gba_companion.h"
#ifdef GBAW_EMBEDDED
#include "embed.h"
#include "auto_state.h"
// Cheats (CHEATS in the pause menu): Fulldex's, from its frontend folder (on the include path when
// built into it). All off until the player turns one on.
#if __has_include("gba_cheats.h")
#include "gba_cheats.h"
#define GBAW_CHEATS 1
#endif
// (The launcher has stb_image too: this one's kept to this file.)
#define STB_IMAGE_STATIC
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "third_party/stb_image.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include "third_party/xbrz/xbrz.h"
#if defined(__SWITCH__)
#include <switch.h>
#endif

#include "follower_hgss.h"
#include "gameplay.h"
#include "games.h"
#include "ui.h"

using namespace gbaw;

namespace
{

constexpr int GbaW = 240, GbaH = 160;
constexpr int MaxExtra = 112;                 // the most the game patches support a side
constexpr int MaxW = GbaW + 2 * MaxExtra;     // 464
constexpr double Fps = 60.0;                  // the GBA's 59.73 is run at the screen's 60

// GBA keys, as mGBA numbers them.
enum GbaKey { K_A, K_B, K_SELECT, K_START, K_RIGHT, K_LEFT, K_UP, K_DOWN, K_R, K_L };

void QuietLog(struct mLogger*, int, enum mLogLevel level, const char* format, va_list args)
{
    if (level & mLOG_FATAL)
    {
        vfprintf(stderr, format, args);
        fputc('\n', stderr);
    }
}
struct mLogger gQuiet = { QuietLog, nullptr };

// The window's icon: the same picture as the program's (tools/pc/make_icon.py, keep them alike).
uint32_t IconPixel(int x, int y, int s)
{
    const double fx = (x + 0.5) / s, fy = (y + 0.5) / s, r = 0.17;
    const double cx = std::min(std::max(fx, r), 1 - r), cy = std::min(std::max(fy, r), 1 - r);
    if ((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy) > r * r) return 0;
    if (fx >= 0.08 && fx <= 0.92 && fy >= 0.27 && fy <= 0.73)
    {
        if (!(fx >= 0.12 && fx <= 0.88 && fy >= 0.31 && fy <= 0.69)) return 0xFFF0F0F0;
        const double d = fabs(fx - 0.5) / 0.14 + fabs(fy - 0.5) / 0.15;
        if (d <= 1) return fy < 0.5 ? 0xFFFFFFFF : 0xFFC8D0E0;
        return 0xFF0B0F18;
    }
    const int top[3] = { 0x1E, 0x8C, 0x55 }, bottom[3] = { 0x6A, 0x3F, 0xC0 };
    uint32_t c = 0xFF000000;
    for (int i = 0; i < 3; i++) c |= (uint32_t)(int)(top[i] + (bottom[i] - top[i]) * fy) << (16 - 8 * i);
    return c;
}

// ---------------------------------------------------------------------------------------------
// Settings: settings.ini beside the saves, one "key=value" per line.

struct Setting
{
    const char* key;
    const char* name;
    std::vector<std::pair<std::string, std::string>> values; // stored, shown
    const char* def;
    const char* text;
};

const std::vector<Setting>& SettingsList()
{
    static const std::vector<std::pair<std::string, std::string>> onOff = { { "0", "OFF" }, { "1", "ON" } };
    static const std::vector<Setting> l = {
        { "widescreen", "ASPECT RATIO",
          { { "auto", "AUTO" }, { "16:9", "16:9" }, { "16:10", "16:10" }, { "3:2", "3:2" }, { "4:3", "4:3" } },
          "auto",
          "AUTO: AS WIDE AS YOUR SCREEN. 3:2 AND 4:3: THE GBA'S OWN SHAPE, WITH BLACK AT THE SIDES. THE WORLD AND "
          "BATTLES FILL THE WIDTH; MENUS AND THE BAG STAY IN THE MIDDLE." },
#ifndef __SWITCH__
        { "fullscreen", "BORDERLESS FULLSCREEN", onOff, "1",
          "ON: FILLS YOUR SCREEN AS A BORDERLESS WINDOW AT ITS OWN RESOLUTION. F11 SWITCHES BETWEEN IT AND A WINDOW." },
#endif
        { "scaling", "SCALING", { { "sharp", "SHARP" }, { "smooth", "SMOOTH" }, { "whole", "PIXEL PERFECT" } }, "sharp",
          "HOW THE GBA'S SMALL PICTURE IS ENLARGED TO YOUR SCREEN. SHARP: CRISP SQUARE PIXELS THAT FILL THE SCREEN. "
          "SMOOTH: THE PIXELS' EDGES SOFTENED. PIXEL PERFECT: ONLY WHOLE-NUMBER ZOOMS (4X, 5X, NEVER 4.5X), SO EVERY "
          "PIXEL IS EXACTLY THE SAME SIZE, WITH A THIN BLACK BORDER." },
        { "hd_filter", "HD FILTER", onOff, "0",
          "ON: THE PIXEL ART IS REDRAWN WITH SMOOTH EDGES AND CURVES (XBRZ). OFF: THE GAME'S OWN PIXELS." },
        { "lcd", "LCD EFFECT", onOff, "0", "ON: A FAINT GRID BETWEEN THE PIXELS, LIKE A GBA'S OWN SCREEN." },
        { "colour", "COLOUR CORRECTION", onOff, "0",
          "ON: THE COLOURS AS THEY LOOKED ON A GBA'S SCREEN (THE GAMES WERE MADE EXTRA BRIGHT FOR IT). "
          "OFF: THE GAME'S OWN COLOURS." },
        { "zoom", "ZOOM AND LOOK AROUND", onOff, "1",
          "CLICK THE RIGHT STICK OR PRESS C ON THE OVERWORLD TO STEP THE ZOOM OUT; MOVE THE RIGHT STICK TO LOOK AROUND." },
        { "zoom_text", "AUTO ZOOM IN", onOff, "1",
          "PULL IN TO 1X ON THE PLAYER WHILE TEXT BOXES ARE UP ON THE OVERWORLD." },
        { "day_night", "DAY AND NIGHT", onOff, "0",
          "ON: OUTDOORS FOLLOWS YOUR CLOCK: A WARM DAWN, PLAIN DAY, AN ORANGE DUSK AND A BLUE NIGHT. ONLY THE COLOURS CHANGE." },
        { "follower", "FOLLOWING POKEMON", onOff, "0",
          "ON: YOUR FIRST POKEMON WALKS BEHIND YOU USING SPRITES FROM YOUR OWN HEARTGOLD OR SOULSILVER IN THE ROMS FOLDER." },
        { "show_fps", "FPS COUNTER", onOff, "0", "SHOWS THE FRAMES A SECOND IN A CORNER." },
        { "instant_text", "INSTANT TEXT", onOff, "0",
          "ON: A TEXT BOX'S WORDS APPEAR AT ONCE, WHATEVER THE GAME'S OWN TEXT SPEED." },
        { "reusable_tms", "REUSABLE TMS", onOff, "0",
          "ON: EVERY TM STAYS IN THE BAG AFTER TEACHING ITS MOVE, AS AN HM DOES." },
        { "run_indoors", "RUN INDOORS", onOff, "0",
          "ON: THE RUNNING SHOES WORK ON EVERY MAP, INDOORS TOO (EMERALD, FIRERED AND LEAFGREEN)." },
        { "wild_encounters", "WILD ENCOUNTERS",
          { { "100", "NORMAL" }, { "75", "75%" }, { "50", "50%" }, { "25", "25%" }, { "0", "OFF" } }, "100",
          "HOW MANY WILD POKEMON APPEAR IN GRASS, CAVES AND WATER. FISHING, SWEET SCENT AND SCRIPTED BATTLES STAY AS THEY ARE." },
        { "shiny_odds", "SHINY ODDS",
          { { "8192", "NORMAL" }, { "4096", "1 IN 4096" }, { "1024", "1 IN 1024" }, { "512", "1 IN 512" }, { "128", "1 IN 128" } }, "8192",
          "HOW OFTEN A WILD POKEMON APPEARS SHINY. POKEMON ALREADY CAUGHT DO NOT CHANGE." },
        { "battle_speed", "BATTLE SPEED", { { "1", "NORMAL" }, { "2", "2X" }, { "3", "3X" }, { "4", "4X" } }, "1",
          "HOW FAST BATTLES RUN WITHOUT FAST FORWARD." },
        { "text_speed", "TEXT SPEED", { { "1", "NORMAL" }, { "2", "2X" }, { "3", "3X" }, { "4", "4X" } }, "1",
          "HOW FAST THE GAME RUNS WHILE A TEXT BOX IS UP OUTSIDE BATTLES." },
        { "bike_speed", "BIKE SPEED", { { "1", "NORMAL" }, { "2", "2X" }, { "3", "3X" }, { "4", "4X" } }, "1",
          "HOW FAST THE GAME RUNS WHILE RIDING A BIKE." },
        { "surf_speed", "SURF SPEED", { { "1", "NORMAL" }, { "2", "2X" }, { "3", "3X" }, { "4", "4X" } }, "1",
          "HOW FAST THE GAME RUNS WHILE SURFING OR DIVING." },
        { "volume", "VOLUME",
          { { "0", "OFF" }, { "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" },
            { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } },
          "100", "THE GAME'S SOUND." },
        { "ff_mute", "FAST FORWARD SOUND", { { "0", "ON" }, { "1", "OFF" } }, "0",
          "ON: THE MUSIC CARRIES ON AT ITS NORMAL SPEED WHILE THE GAME FAST FORWARDS. OFF: FAST FORWARD IS SILENT." },
        { "gba_start", "START BUTTON", { { "start", "GBA START" }, { "menu", "PAUSE MENU" } },
#if defined(GBAW_EMBEDDED) || defined(__SWITCH__)
          "menu",
#else
          "start",
#endif
#if defined(__SWITCH__)
          "PAUSE MENU: + OPENS THIS MENU, AND + AND - TOGETHER ARE THE GBA'S START. GBA START: + IS THE GBA'S START, "
          "AND + AND - TOGETHER OPEN THIS MENU." },
#else
          "PAUSE MENU: A CONTROLLER'S START OPENS THIS MENU, AND START AND SELECT TOGETHER ARE THE GBA'S START. "
          "GBA START: START IS THE GBA'S START, AND START AND SELECT TOGETHER OPEN THIS MENU." },
#endif
        { "fast_button", "FAST FORWARD BUTTON", { { "hold", "HOLD" }, { "toggle", "TOGGLE" } }, "hold",
          "HOLD: RELEASE TO STOP. TOGGLE: PRESS ONCE TO START AND AGAIN TO STOP. FOR THE KEYBOARD AND A CONTROLLER; THE >> BUTTON ON THE SCREEN IS ALWAYS A TAP ON, A TAP OFF." },
#ifdef GBAW_EMBEDDED
        { "auto_state", "AUTO SAVE STATE", onOff, "0",
          "KEEP A SEPARATE STATE EVERY MINUTE, WHEN BACKGROUNDED AND ON EXIT. MANUAL SLOTS STAY AS THEY ARE. KEEP SAVING IN THE GAME TOO." },
        { "auto_resume", "RESUME ON START", onOff, "0",
          "RESUME THIS ROM'S AUTOMATIC STATE ON THE NEXT START, KEEPING ITS NORMAL SAVE. TURN OFF TO START NORMALLY." },
#endif
        { "ff_speed", "FAST FORWARD SPEED", { { "2", "2X" }, { "3", "3X" }, { "4", "4X" }, { "8", "8X" } }, "3",
#ifdef __SWITCH__
          "HOW FAST FAST FORWARD GOES (HOLD ZR)." },
#else
          "HOW FAST FAST FORWARD GOES (HOLD LEFT SHIFT, OR A CONTROLLER'S RIGHT TRIGGER)." },
#endif
#if !defined(__SWITCH__)
        // (Not on the Switch: its buttons are where their labels say, as on a GBA.)
        { "buttons", "CONTROLLER BUTTONS", { { "labels", "BY LABEL" }, { "positions", "BY POSITION" } }, "labels",
          "BY LABEL: THE BUTTON PRINTED A IS THE GBA'S A. BY POSITION: THE RIGHT-HAND BUTTON IS A AND THE BOTTOM ONE IS "
          "B, AS ON A GBA, WHATEVER THEY SAY." },
#endif
    };
    return l;
}

struct Settings
{
    std::string path;
    std::map<std::string, std::string> values;

    void Load(const std::string& p)
    {
        path = p;
        values.clear();
        std::vector<uint8_t> data;
        if (Games::ReadFile(path, data, 1 << 20))
        {
            std::string line;
            data.push_back('\n');
            for (uint8_t c : data)
            {
                if (c == '\n' || c == '\r')
                {
                    const size_t eq = line.find('=');
                    if (eq != std::string::npos) values[line.substr(0, eq)] = line.substr(eq + 1);
                    line.clear();
                }
                else
                    line += (char)c;
            }
        }
        std::string lang = values.count("language") ? values["language"] : "";
        if (lang.empty())
        {
            auto h = values.find("host_settings");
            if (h != values.end() && !h->second.empty())
            {
                std::vector<uint8_t> hd;
                if (Games::ReadFile(h->second, hd, 1 << 20))
                {
                    std::string line, fallback;
                    hd.push_back('\n');
                    for (uint8_t c : hd)
                    {
                        if (c == '\n' || c == '\r')
                        {
                            if (!line.compare(0, 10, "list_lang=")) lang = line.substr(10);
                            else if (!line.compare(0, 9, "language=")) fallback = line.substr(9);
                            line.clear();
                        }
                        else line += (char)c;
                    }
                    if (lang.empty()) lang = fallback;
                }
            }
        }
        if (!lang.empty()) values["language"] = lang;
        ApplyUiLanguage(lang);
    }
    std::string HostValue(const char* key) const
    {
        auto h = values.find("host_settings");
        if (h == values.end() || h->second.empty() || !key) return "";
        std::vector<uint8_t> hd;
        if (!Games::ReadFile(h->second, hd, 1 << 20)) return "";
        const std::string prefix = std::string(key) + "=";
        std::string line;
        hd.push_back('\n');
        for (uint8_t c : hd)
        {
            if (c == '\n' || c == '\r')
            {
                if (!line.compare(0, prefix.size(), prefix)) return line.substr(prefix.size());
                line.clear();
            }
            else line += (char)c;
        }
        return "";
    }
    void Save() const
    {
        std::string s;
        for (const auto& kv : values) s += kv.first + "=" + kv.second + "\n";
        Games::WriteFile(path, std::vector<uint8_t>(s.begin(), s.end()));
    }
    std::string Get(const char* key) const
    {
        auto it = values.find(key);
        if (it != values.end()) return it->second;
        for (const Setting& s : SettingsList())
            if (!strcmp(s.key, key)) return s.def;
        if (!strcmp(key, "fullscreen")) return "1";
        return "";
    }
    int Int(const char* key) const { return atoi(Get(key).c_str()); }
    // The next (d 1) or previous (d -1) value of a setting.
    void Step(const Setting& s, int d)
    {
        const std::string v = Get(s.key);
        int i = 0;
        for (size_t j = 0; j < s.values.size(); j++)
            if (s.values[j].first == v) i = (int)j;
        const int n = (int)s.values.size();
        values[s.key] = s.values[(i + d + n) % n].first;
        Save();
        ToHost(s.key);
    }
    // The same setting in Fulldex's own settings file (its names and values), if Fulldex gave one.
    void ToHost(const char* key) const
    {
        auto h = values.find("host_settings");
        if (h == values.end() || h->second.empty()) return;
        const std::string v = Get(key);
        std::string hk, hv;
        if (!strcmp(key, "scaling")) { hk = "smooth"; hv = v == "smooth" ? "1" : "0"; }
        else if (!strcmp(key, "show_fps") || !strcmp(key, "volume") || !strcmp(key, "buttons") || !strcmp(key, "ff_mute") ||
                 !strcmp(key, "fast_lock") || !strcmp(key, "fast_button") || !strcmp(key, "zoom") || !strcmp(key, "zoom_text") ||
                 !strcmp(key, "auto_state") || !strcmp(key, "auto_resume")) { hk = key; hv = v; }
        else if (!strcmp(key, "lcd")) { hk = "gba_lcd"; hv = v; }
        else if (!strcmp(key, "gba_start")) { hk = "start_button"; hv = v == "start" ? "ds" : "menu"; }
        else if (!strcmp(key, "widescreen")) { hk = "aspect"; hv = v == "off" ? "3:2" : v; }
        else if (!strcmp(key, "fullscreen")) { hk = key; hv = v; }
        else return;
        std::vector<uint8_t> data;
        Games::ReadFile(h->second, data, 1 << 20);
        std::string out, line;
        bool done = false;
        data.push_back('\n');
        for (size_t i = 0; i < data.size(); i++)
        {
            const char c = (char)data[i];
            if (c != '\n' && c != '\r') { line += c; continue; }
            if (c == '\r' && i + 1 < data.size() && data[i + 1] == '\n') continue;
            if (!line.compare(0, hk.size() + 1, hk + "=")) { line = hk + "=" + hv; done = true; }
            if (!line.empty()) out += line + "\n";
            line.clear();
        }
        if (!done) out += hk + "=" + hv + "\n";
        Games::WriteFile(h->second, std::vector<uint8_t>(out.begin(), out.end()));
    }
    std::string Shown(const Setting& s) const
    {
        const std::string v = Get(s.key);
        for (const auto& kv : s.values)
            if (kv.first == v) return kv.second;
        return v;
    }
};

// ---------------------------------------------------------------------------------------------
// Input: the keyboard, any number of controllers, and test scripts (which push key events).

// What the menus understand, edge-triggered.
struct MenuIn
{
    bool up = false, down = false, left = false, right = false, accept = false, back = false;
    bool menu = false, settings = false, folder = false, escape = false, erase = false;
    bool pageL = false, pageR = false, toggleCompanion = false, toggleQuick = false;
};

// A controller button as printed on it: SDL_CONTROLLER_BUTTON_A is the button labelled A. On a PC
// SDL does that for Nintendo controllers itself; SDL on the Switch numbers the Switch's own by
// Xbox positions (its "A" is the bottom button, labelled B; its "X" the left one, labelled Y), so
// those two pairs are swapped back there, as Pokemon DS Widescreen does. (Its own inverse.)
inline int Labelled(int b)
{
#if defined(__SWITCH__)
    switch (b)
    {
    case SDL_CONTROLLER_BUTTON_A: return SDL_CONTROLLER_BUTTON_B;
    case SDL_CONTROLLER_BUTTON_B: return SDL_CONTROLLER_BUTTON_A;
    case SDL_CONTROLLER_BUTTON_X: return SDL_CONTROLLER_BUTTON_Y;
    case SDL_CONTROLLER_BUTTON_Y: return SDL_CONTROLLER_BUTTON_X;
    default: break;
    }
#endif
    return b;
}

struct Pads
{
    std::vector<SDL_GameController*> list;
    // (Held directions on any controller, with auto-repeat for the menus.)
    int heldDir = -1;
    Uint32 heldSince = 0, lastRepeat = 0;

    void Open(int index)
    {
        if (!SDL_IsGameController(index)) return;
        if (SDL_GameController* c = SDL_GameControllerOpen(index)) list.push_back(c);
    }
    void CloseAll()
    {
        for (SDL_GameController* c : list) SDL_GameControllerClose(c);
        list.clear();
    }
    void Close(SDL_JoystickID id)
    {
        for (size_t i = 0; i < list.size(); i++)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(list[i])) == id)
            {
                SDL_GameControllerClose(list[i]);
                list.erase(list.begin() + (long)i);
                return;
            }
    }
    // (b: as labelled, see Labelled.)
    bool Button(SDL_GameControllerButton b) const
    {
        for (SDL_GameController* c : list)
            if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)Labelled(b))) return true;
        return false;
    }
    int Axis(SDL_GameControllerAxis a) const
    {
        int best = 0;
        for (SDL_GameController* c : list)
        {
            const int v = SDL_GameControllerGetAxis(c, a);
            if (abs(v) > abs(best)) best = v;
        }
        return best;
    }
    // Directions on the D-pads and left sticks: bits up, down, left, right.
    int Dirs() const
    {
        int d = 0;
        const int dead = 16000;
        if (Button(SDL_CONTROLLER_BUTTON_DPAD_UP) || Axis(SDL_CONTROLLER_AXIS_LEFTY) < -dead) d |= 1;
        if (Button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || Axis(SDL_CONTROLLER_AXIS_LEFTY) > dead) d |= 2;
        if (Button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || Axis(SDL_CONTROLLER_AXIS_LEFTX) < -dead) d |= 4;
        if (Button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || Axis(SDL_CONTROLLER_AXIS_LEFTX) > dead) d |= 8;
        return d;
    }
    // The menus' directions from the controllers, repeating while held.
    void MenuDirs(MenuIn& in)
    {
        const int d = Dirs();
        int dir = (d & 1) ? 0 : (d & 2) ? 1 : (d & 4) ? 2 : (d & 8) ? 3 : -1;
        const Uint32 now = SDL_GetTicks();
        bool fire = false;
        if (dir != heldDir)
        {
            heldDir = dir;
            heldSince = lastRepeat = now;
            fire = dir >= 0;
        }
        else if (dir >= 0 && now - heldSince > 400 && now - lastRepeat > 90)
        {
            lastRepeat = now;
            fire = true;
        }
        if (!fire) return;
        if (dir == 0) in.up = true;
        if (dir == 1) in.down = true;
        if (dir == 2) in.left = true;
        if (dir == 3) in.right = true;
    }
};

// A note for whoever is looking: stderr, or Android's log (logcat) there.
void Note(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf);
}

// A test script: "key FRAME NAME [HOLD]", "touch FRAME X,Y [HOLD]" (0 to 1 across the window),
// "shot FRAME FILE.bmp", "quit FRAME". Frames are the program's own, one GBA frame each; in
// real-time runs (GBAW_TEST_REALTIME) they are the game's frames from its start, which come at
// the screen's pace.
struct ScriptLine
{
    int frame;
    std::string what, arg;
    int hold;
    int done = 0; // 1: pressed (or done), 2: let go
};

// ---------------------------------------------------------------------------------------------

// RetroAchievements' map of a GBA: 32 KB of internal work RAM from 0, 256 KB of external work
// RAM from 0x8000, and 64 KB of the save RAM currently mapped from 0x48000.
#ifdef GBAW_CHEATS
// The running game for the cheats: mGBA's own reads and writes (a write to the ROM is a patch).
struct CheatBus : GbaCheats::Bus
{
    struct mCore* core = nullptr;
    uint8_t R8(uint32_t a) override { return (uint8_t)core->rawRead8(core, a, -1); }
    uint16_t R16(uint32_t a) override { return (uint16_t)core->rawRead16(core, a, -1); }
    uint32_t R32(uint32_t a) override { return core->rawRead32(core, a, -1); }
    void W8(uint32_t a, uint8_t v) override { core->rawWrite8(core, a, -1, v); }
    void W16(uint32_t a, uint16_t v) override { core->rawWrite16(core, a, -1, v); }
    void W32(uint32_t a, uint32_t v) override { core->rawWrite32(core, a, -1, v); }
};
#endif

uint32_t ReadRa(void* p, uint32_t address, uint8_t* buffer, uint32_t bytes)
{
    auto* core = (struct mCore*)p;
    if (!core || !core->board || !buffer) return 0;
    auto* gba = (struct GBA*)core->board;
    const uint8_t* iw = (const uint8_t*)gba->memory.iwram;
    const uint8_t* ew = (const uint8_t*)gba->memory.wram;
    const uint8_t* save = nullptr;
    size_t saveN = 0;
    if (gba->memory.savedata.currentBank)
    {
        // Flash: the 64 KB bank the bus shows (Pokemon's save is 128 KB of flash, one bank at a time).
        save = gba->memory.savedata.currentBank;
        saveN = 0x10000;
    }
    else if (gba->memory.savedata.data)
    {
        save = gba->memory.savedata.data;
        const size_t n = GBASavedataSize(&gba->memory.savedata);
        saveN = n < 0x10000 ? n : 0x10000;
    }
    uint32_t done = 0;
    for (; done < bytes; done++)
    {
        const uint32_t a = address + done;
        if (a < 0x8000)
        {
            if (!iw) break;
            buffer[done] = iw[a];
        }
        else if (a < 0x48000)
        {
            if (!ew) break;
            buffer[done] = ew[a - 0x8000];
        }
        else if (a < 0x58000)
        {
            const uint32_t o = a - 0x48000;
            if (!save || o >= saveN) break;
            buffer[done] = save[o];
        }
        else
            break;
    }
    return done;
}

struct App
{
    // Where things are
    std::string data, roms, saves, states;
    Settings set;

    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* gameTex = nullptr;   // the GBA picture, MaxW x 160
    SDL_Texture* zoomTex = nullptr;   // ZOOM / LOOK AROUND / dynamic WIDE: the rendered window
    SDL_Texture* zoomHdTex = nullptr; // ... scaled through xBRZ
    int zoomHdW = 0, zoomHdH = 0;
    SDL_Texture* bigTex = nullptr;    // the same scaled up by a whole number (SCALING: SHARP)
    SDL_Texture* hdTex = nullptr;     // HD FILTER: the picture redrawn by xBRZ, HdFactor times as big
    SDL_Texture* lcdTex = nullptr;    // LCD EFFECT: the grid, the size the picture is drawn at
    int lcdW = 0, lcdH = 0, lcdCols = 0, lcdRows = 0;
    std::vector<uint32_t> fxSrc, fxOut, colourLut;
    // Helpers for HD FILTER: xBRZ shares the picture out among the processor's cores by rows.
    struct Workers
    {
        std::vector<std::thread> threads;
        std::mutex m;
        std::condition_variable cv, done;
        std::function<void(int)> job;
        int jobs = 0, next = 0, left = 0;
        bool quit = false;
        void Start(int n)
        {
            for (int i = 0; i < n; i++) threads.emplace_back([this, i] { Loop(i); });
        }
        void Loop(int index)
        {
#if defined(__SWITCH__)
            // (A thread starts on the program's own core there: each helper to a core of its own.)
            svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1 + index % 2, 1u << (1 + index % 2));
#else
            (void)index;
#endif
            std::unique_lock<std::mutex> l(m);
            for (;;)
            {
                cv.wait(l, [&] { return quit || next < jobs; });
                if (quit) return;
                const int i = next++;
                l.unlock();
                job(i);
                l.lock();
                if (--left == 0) done.notify_all();
            }
        }
        void Run(int n, const std::function<void(int)>& f)
        {
            std::unique_lock<std::mutex> l(m);
            job = f;
            jobs = n;
            next = 0;
            left = n;
            cv.notify_all();
            while (next < jobs)
            {
                const int i = next++;
                l.unlock();
                job(i);
                l.lock();
                if (--left == 0) done.notify_all();
            }
            done.wait(l, [&] { return left == 0; });
            jobs = next = 0;
        }
        ~Workers()
        {
            {
                std::lock_guard<std::mutex> l(m);
                quit = true;
            }
            cv.notify_all();
            for (std::thread& t : threads) t.join();
        }
    } workers;
    bool workersStarted = false;
    int bigW = 0, bigH = 0;
    SDL_Texture* uiTex = nullptr;
    int uiTexW = 0, uiTexH = 0;
    Canvas ui;
    bool testMode = false;
    // Built into the all-in-one launcher (embed.h): no list of its own; QUIT GAME ends Run.
    bool embedded = false;
    std::string listCache;   // what each ROM is (Games::Scan): saves/rom-list.txt
    bool realTime = true;  // paced and drawn as for play (tests: only with GBAW_TEST_REALTIME)
    int maxTex = 8192;
    unsigned long long gameFrames = 0;
    bool shotTaken = false;

    enum Screen { LIST, SETTINGS, PLAY } screen = LIST;
    Screen settingsBack = LIST;
    std::vector<Games::Game> games;
    int listSel = 0;
    int listCols = 3; // (the game list's columns, as last drawn: up and down move by a row)
    int settingsSel = 0;
#if defined(__SWITCH__)
    bool padLast = true; // the last input was a controller's (the hints follow it)
#else
    bool padLast = false;
#endif
    bool leftTrigger = false;
    int uiScale = 1, uiOffX = 0, uiOffY = 0;
    std::vector<std::pair<UiRect, int>> tapRects; // (where, which item)
    // The games' art on the list: drawn at the screen's own resolution over the menus' canvas.
    struct ArtSpot { UiRect at; std::string code; bool chosen; };
    std::vector<ArtSpot> artSpots;
    std::map<std::string, SDL_Texture*> artTex;
    UiRect backRect, settingsRect;

    // The game
    struct mCore* core = nullptr;
    Games::Game game;
    std::vector<uint32_t> video = std::vector<uint32_t>((size_t)MaxW * GbaH);
    // PokeDaisy WIDE (pokedaisy/): dynamic widescreen rebuild, GB/GBC wide, Rewind, Companion, HUD & Quick Menu.
    bool pkWide = false;
    // GBA Gameplay options (gameplay.h), Day/Night tint and Following Pokemon (follower_hgss.h).
    std::vector<GbaGameplay::Write> gpText, gpTms;
    bool gpTextOn = false, gpTmsOn = false, gpField = false;
    uint32_t gpWild = 0;
    int gpState = 0;
    std::string gpGameCode;
    GbaGameplay::ShinyRoller shiny;
    float tintNow[3] = { 256, 256, 256 };
    int tintSent[3] = { 256, 256, 256 }, tintFrames = 0;
    std::unique_ptr<HgssFollow::Source> follow;
    std::thread followFind;
    std::atomic<int> followState{ 0 }; // 0 not searched, 1 searching, 2 found, 3 none
    int followSpecies = -1, followTick = 0;
    bool followShiny = false, followShown = false, followToldNone = false;
    PkWideCamera::Camera camera;
    GbaCompanion companion;
    std::vector<uint32_t> frame240 = std::vector<uint32_t>((size_t)GbaW * GbaH);
    std::vector<uint32_t> zoomBuf = std::vector<uint32_t>((size_t)PkWideCamera::Camera::MaxW * PkWideCamera::Camera::MaxH);
    PkWideCamera::Window zoomWin;
    bool zoomShown = false;
    bool zoomRefresh = false;
    int CoreW() const { return game.isGb ? 160 : GbaW; }
    int CoreH() const { return game.isGb ? 144 : GbaH; }
    GbaCompanion::HostActions CompanionActions()
    {
        GbaCompanion::HostActions a;
        a.saveState = [this]() { SaveState(); return true; };
        a.loadState = [this]() { LoadState(); return true; };
        a.stateExists = [this]() { return Games::Exists(states + "/" + game.key + ".ss"); };
        a.getFf = [this]() { return fastToggle; };
        a.setFf = [this](bool on) {
            fastToggle = on;
            set.values["fast_lock"] = fastToggle ? "1" : "0";
            set.Save(); set.ToHost("fast_lock");
        };
        if (!game.isGb && gpField && gpWild != 0)
        {
            a.getWildOff = [this]() { return WildKeep() == 0; };
            a.setWildOff = [this](bool off) {
                set.values["wild_encounters"] = off ? "0" : "100";
                set.Save(); set.ToHost("wild_encounters");
            };
        }
        a.toast = [this](const std::string& m) { Toast(m, 2600); };
        return a;
    }
    int wideExtra = -1;
    bool paused = false;
    Menu pause;
    bool pauseTouchDown = false, pauseTouchMoved = false;
    SDL_FingerID pauseTouchId = 0;
    int pauseTouchX0 = 0, pauseTouchY0 = 0, pauseTouchLastY = 0;
    bool fastToggle = false;
    FastForwardButton fastButton;
    bool fastInputFocused = true;
    uint32_t tapDirs = 0;
    Uint32 tapSince = 0;
    bool plusHeld = false, plusCombo = false; // START (+) held, and SELECT (-) pressed with it
    bool comboOn = false;                     // START and SELECT pressed together, till both are let go
    Uint32 plusAt = 0, minusAt = 0;           // when START, SELECT went down
    Uint32 plusPulse = 0, minusPulse = 0;     // a short tap of one alone: given to the game till then
#ifdef GBAW_EMBEDDED
    // RetroAchievements, from the program this is built into (embed.h). None in the standalone game.
    const GbaWide::AchievementsHost* ra = nullptr;
    bool raReady = false;
    enum PausePage { P_ROOT, P_SETTINGS, P_RA, P_LIST, P_LOGIN, P_CHEATS, P_DISPLAY, P_SOUND, P_GAMEPLAY, P_CONTROLS } pausePage = P_ROOT;
    PausePage pauseTouchPage = P_ROOT;
    std::string autoStatePath;
    bool autoStateBlocked = false, autoStatePlayed = false;
    AutoState::Timer autoStateTimer;
    std::vector<const Setting*> pageSettings; // the settings on the DISPLAY, SOUND or CONTROLS page, row by row
    bool typing = false;
    int raField = 0; // 0 the user name, 1 the password
    std::string raUser, raPass;
    std::vector<std::string> achDesc; // the achievements list's descriptions, beside its rows
    const GbaWide::UiHost* uiHost = nullptr;
    std::string hostAccent;
    GbaWide::MenuHostState hostMenu{};
    std::vector<uint32_t> hostPx;
    SDL_Texture* hostTex = nullptr;
    int hostTexW = 0, hostTexH = 0;
    uint64_t builtHostSig = 0;
    bool usingHostMenu = false;
#endif
#ifdef GBAW_CHEATS
    GbaCheats::Engine cheats;
    CheatBus cheatBus;
    std::string cheatsFile; // which are on: <cheats folder>/<game>-on.txt
#endif
    // After a menu closes, the game gets no keys until all are let go (Enter or A choosing in
    // the pause menu would otherwise press the game's START or A).
    bool holdKeys = false;

    // Sound
    SDL_AudioDeviceID audio = 0;
    SDL_AudioSpec have{};
    struct mAudioResampler resampler{};
    struct mAudioBuffer audioOut{};
    std::vector<int16_t> audioTmp;

    // Input
    Pads pads;
    bool key[SDL_NUM_SCANCODES] = {};

    // Messages and the FPS counter
    std::string toast;
    Uint32 toastUntil = 0;
    int fpsFrames = 0, fpsShown = 0;
    Uint32 fpsSince = 0;

    // Tests
    std::vector<ScriptLine> script;
    int frame = 0;
    bool quit = false;

    void Toast(const std::string& t, Uint32 ms = 2500)
    {
        toast = t;
        toastUntil = SDL_GetTicks() + ms;
        fprintf(stderr, "%s\n", t.c_str());
    }

    // -----------------------------------------------------------------------------------------
    // Folders

    void Folders(const std::string& dataArg)
    {
#if defined(__SWITCH__)
        data = "/switch/pokemon-gba-wide";
#else
        if (!dataArg.empty())
            data = dataArg;
        else
        {
            char* base = SDL_GetBasePath();
            data = base ? base : ".";
            SDL_free(base);
        }
#endif
        if (!dataArg.empty()) data = dataArg;
        while (data.size() > 1 && (data.back() == '/' || data.back() == '\\')) data.pop_back();
        roms = data + "/roms";
        saves = data + "/saves";
        states = data + "/states";
        Games::MakeDir(data);
        Games::MakeDir(roms);
        Games::MakeDir(saves);
        Games::MakeDir(states);
        set.Load(data + "/settings.ini");
        listCache = saves + "/rom-list.txt";
    }

    // The launcher's folders (embed.h): its roms and saves, and this one's settings and save
    // states in data, named so as not to be taken for the launcher's own.
    void UseFolders(const std::string& romsDir, const std::string& savesDir, const std::string& dataDir)
    {
        data = dataDir;
        roms = romsDir;
        saves = savesDir;
        states = data + "/states";
        Games::MakeDir(saves);
        Games::MakeDir(states);
        set.Load(data + "/gba-settings.ini");
        listCache = saves + "/gba-rom-list.txt";
    }

    // -----------------------------------------------------------------------------------------
    // The window

    // fullscreen: -1 as the settings say (or as the window size: none, the settings'), else as given.
    bool OpenWindow(int w, int h, int fullscreen = -1)
    {
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __SWITCH__
        flags |= SDL_WINDOW_FULLSCREEN;
        (void)fullscreen;
#else
        if (fullscreen > 0 || (fullscreen < 0 && !w && set.Int("fullscreen"))) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
        if (!w)
        {
            w = 1280;
            h = 720;
        }
        win = SDL_CreateWindow(embedded ? "Pokemon Widescreen" : "Pokemon GBA Widescreen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (!win) return false;
#ifndef __SWITCH__
        if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888))
        {
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++) ((uint32_t*)((uint8_t*)icon->pixels + y * icon->pitch))[x] = IconPixel(x, y, 64);
            SDL_SetWindowIcon(win, icon);
            SDL_FreeSurface(icon);
        }
#endif
        if (!realTime)
            // (Tests draw in software, unless GBAW_TEST_GPU asks for the graphics card's path.)
            ren = SDL_CreateRenderer(win, -1, (getenv("GBAW_TEST_GPU") ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE) | SDL_RENDERER_TARGETTEXTURE);
        else
        {
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
            if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
            if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        }
        if (!ren) return false;
        SDL_RendererInfo info;
        if (!SDL_GetRendererInfo(ren, &info) && info.max_texture_width > 0)
            maxTex = std::min(info.max_texture_width, info.max_texture_height);
        gameTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, MaxW, GbaH);
        if (!gameTex) return false;
        SDL_SetTextureScaleMode(gameTex, SDL_ScaleModeNearest);
        SDL_ShowCursor(SDL_ENABLE);
        return true;
    }

    void ToggleFullscreen()
    {
#ifndef __SWITCH__
        const bool full = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(win, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        set.values["fullscreen"] = full ? "0" : "1";
        set.Save();
#endif
    }

    // -----------------------------------------------------------------------------------------
    // Sound: the game's samples, made at 60 frames a second, resampled to the device's rate,
    // with the rate nudged to keep about four frames queued, or two of the device's own buffers
    // if those are bigger (the Switch plays sound in larger pieces than a PC: with only four
    // frames queued it ran dry between them, and anything over ten frames was thrown away,
    // both heard as crackling).

    void OpenAudio()
    {
        SDL_AudioSpec want{};
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
#if defined(__SWITCH__)
        // (The Switch: with 512-sample buffers its sound thread ran dry between them and played
        // about 41,000 samples a second instead of 48,000, so sound piled up and was thrown away:
        // crackly and sped up. The DS games use 1024.)
        want.samples = 2048;
#else
        want.samples = 512;
#endif
        audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (!audio) return;
        fprintf(stderr, "[gba] sound: %d Hz, %d samples a buffer\n", have.freq, (int)have.samples);
        mAudioBufferInit(&audioOut, 16384, 2);
        audioTmp.resize(16384 * 2);
    }

    bool ffWas = false;
    FastForwardAudio ffAudio; // sound made while fast forwarding, played at its own speed
    void Sound(bool queue, bool ff = false)
    {
        if (!core) return;
        struct mAudioBuffer* buf = core->getAudioBuffer(core);
        if (!audio)
        {
            mAudioBufferClear(buf);
            return;
        }
        const double ratio = mCoreCalculateFramerateRatio(core, Fps); // (59.73 / 60)
        const double frameBytes = have.freq / Fps * 4;
        const double queued = SDL_GetQueuedAudioSize(audio);
        const double deviceBytes = have.samples * 4.0;
        const double target = std::max(frameBytes * 4, deviceBytes * 2);
        if (ff != ffWas)
        {
            // (Letting go drops what waited to be heard; the short device queue plays on.)
            ffAudio.Clear();
            if (ff && set.Get("ff_mute") == "1") SDL_ClearQueuedAudio(audio);
            ffWas = ff;
        }
        if (ff && set.Get("ff_mute") == "1")
        {
            mAudioBufferClear(buf);
            return;
        }
        double nudge = ff ? 0 : (target - queued) / target * 0.005;
        nudge = std::max(-0.005, std::min(0.005, nudge));
        mAudioResamplerSetSource(&resampler, buf, core->audioSampleRate(core) / ratio, true);
        mAudioResamplerSetDestination(&resampler, &audioOut, have.freq * (1 + nudge));
        mAudioResamplerProcess(&resampler);
        size_t n = mAudioBufferRead(&audioOut, audioTmp.data(), audioTmp.size() / 2);
        if (ff)
        {
            // Fast forward: the sound at its own speed and pitch, a stretch at a time, never more
            // than about two seconds behind the picture (FastForwardAudio). The device's queue is
            // kept topped up from it, so that queue is the clock.
            if (queue) ffAudio.Push(audioTmp.data(), (int)n, have.freq);
            const int want = (int)std::max(0.0, (target * 1.5 - queued) / 4);
            n = queue ? (size_t)ffAudio.PullFrames(audioTmp.data(), std::min(want, (int)(audioTmp.size() / 2)), have.freq) : 0;
        }
        const double room = std::max(frameBytes * 10, target * 3);
        if (queue && n && queued < room) SDL_QueueAudio(audio, audioTmp.data(), (Uint32)(n * 4));
    }

    // -----------------------------------------------------------------------------------------
    // The game

    bool Start(const Games::Game& g)
    {
        std::vector<uint8_t> rom, sold;
        std::string error;
#ifdef GBAW_EMBEDDED
        const bool wantRa = ra && ra->begin;
#else
        const bool wantRa = false;
#endif
        // (A hack whose widescreen patch doesn't fit: tried anyway, unless the GBA's own shape
        // was chosen, which is how to play it as it is when it glitches.)
        const std::string shape = set.Get("widescreen");
        const bool ownShape = shape == "off" || shape == "3:2" || shape == "4:3";
        // Always keep the pre-widescreen ROM copy for PokeDaisy WIDE CRC lookup and RetroAchievements.
        if (!Games::Prepare(g, rom, error, &sold, !ownShape))
        {
            Toast(error, 5000);
            return false;
        }
        core = mCoreCreate(g.isGb ? mPLATFORM_GB : mPLATFORM_GBA);
        if (!core || !core->init(core))
        {
            Toast("CAN'T START THE EMULATOR");
            core = nullptr;
            return false;
        }
        mCoreInitConfig(core, nullptr);
        struct mCoreOptions opts{};
        opts.useBios = false;
        opts.skipBios = true;
        opts.volume = 0x100;
        opts.sampleRate = 48000;
        opts.audioBuffers = 4096;
        mCoreConfigLoadDefaults(&core->config, &opts);
        mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "detect");
        if (g.isGb)
        {
            // Keep GB/GBC at native 160x144 (no SGB border) so PokeDaisy pk_wide_gb expands cleanly.
            wideExtra = 0;
            mCoreConfigSetIntValue(&core->config, "gb.sgb.borders", 0);
        }
        else
        {
            // Without a static IPS widescreen patch, keep mGBA at 240x160 so pk_wide rebuilds wide views cleanly.
            wideExtra = !g.patch.empty() ? WantedExtra() : 0;
            mCoreConfigSetIntValue(&core->config, "gba.wideExtra", wideExtra);
        }
        mCoreLoadConfig(core);
        core->setVideoBuffer(core, video.data(), MaxW);
        core->setAudioBufferSize(core, 4096);
        struct VFile* vf = VFileMemChunk(rom.data(), rom.size());
        if (!vf || !core->loadROM(core, vf))
        {
            Toast("CAN'T LOAD THE ROM");
            core->deinit(core);
            core = nullptr;
            return false;
        }
        // The save: saves/<game>.sav, or one put beside the ROM, copied in the first time.
        const std::string save = saves + "/" + g.key + ".sav";
        if (!Games::Exists(save) && Games::Exists(Games::SaveBeside(g)))
        {
            std::vector<uint8_t> s;
            if (Games::ReadFile(Games::SaveBeside(g), s, 1 << 20)) Games::WriteFile(save, s);
        }
        if (struct VFile* sv = VFileOpen(save.c_str(), O_CREAT | O_RDWR)) core->loadSave(core, sv);
        core->reset(core);
        const uint32_t soldCrc = doCrc32(sold.data(), sold.size());
        pkWide = pk_wide_configure(soldCrc) != 0;
        camera.frameW = g.isGb ? 160 : GbaW;
        camera.frameH = g.isGb ? 144 : GbaH;
        camera.Reset();
        camera.level = 0;
        zoomShown = false;
        zoomRefresh = false;
        companion.Start(core, sold.data(), sold.size(), soldCrc, g.isGb, data, set.Get("language"), set.Get("rewind") != "0");
        gpText.clear();
        gpTms.clear();
        gpTextOn = gpTmsOn = false;
        gpWild = 0;
        gpState = 0;
        gpField = false;
        gpGameCode = g.isGb ? std::string() : Games::Header(sold, 0xAC, 4);
        shiny = {};
        tintNow[0] = tintNow[1] = tintNow[2] = 256.0f;
        tintSent[0] = tintSent[1] = tintSent[2] = 256;
        tintFrames = 0;
        followSpecies = -1;
        followShiny = false;
        followShown = false;
        followToldNone = false;
        followTick = 0;
        pk_follower_set(nullptr, nullptr);
        if (!g.isGb)
        {
            gpText = GbaGameplay::InstantText(rom);
            gpTms = GbaGameplay::ReusableTms(rom);
            gpWild = GbaGameplay::WildSwitch(rom);
            gpField = pk_field_configure(soldCrc) != 0;
            if (gpField && gpWild) pk_field_wild(gpWild, WildKeep() < 100);
            ApplyGameplay();
            fprintf(stderr, "[gba] gameplay: text=%zu tms=%zu wild=%08X field=%d\n",
                    gpText.size(), gpTms.size(), gpWild, (int)gpField);
        }
        fprintf(stderr, "[gba] pk_wide: %s (platform=%s, patch='%s', crc=%08X)\n",
                pkWide ? "active" : "none", g.isGb ? "GB/GBC" : "GBA", g.patch.c_str(), (unsigned)soldCrc);
#ifdef GBAW_CHEATS
        cheatBus.core = core;
        cheatsFile = GbaCheats::FolderFor(saves) + "/" + g.key + "-on.txt";
        // (Test scripts play test builds of the games, which no one else has: taken as known.)
        if (cheats.Start(cheatBus, Games::Header(rom, 0xAC, 4), rom.size() > 0xBC ? rom[0xBC] : 0, g.known || testMode))
        {
            cheats.Load(cheatsFile);
            fprintf(stderr, "[gba] cheats: %s, %d on\n", cheats.GameName(), cheats.OnCount());
        }
        CheatsToHost();
#endif
        SetVolume();
        game = g;
        screen = PLAY;
        paused = false;
        fastToggle = set.Get("fast_lock") == "1";
        holdKeys = true;
        mAudioResamplerInit(&resampler, mINTERPOLATOR_SINC);
        if (audio)
        {
            SDL_ClearQueuedAudio(audio);
            SDL_PauseAudioDevice(audio, 0);
        }
        if (g.patch.empty() && !pkWide) Toast("NO WIDESCREEN FOR THIS ROM: " + g.why, 4000);
        else if (g.forced && !ownShape) Toast("ROM HACK: GLITCHY SIDES? TRY ASPECT RATIO 3:2", 6000);
#ifdef GBAW_EMBEDDED
        autoStatePath = AutoState::Path(states + "/" + g.key + ".ss", doCrc32(rom.data(), rom.size()), rom.size());
        autoStateBlocked = false;
        autoStatePlayed = false;
        autoStateTimer = {};
        if (!ResumeAutomatic()) { Stop(); return false; }
        // The ROM RetroAchievements knows is the one sold (sold), not the widescreen patch.
        if (wantRa)
        {
            const std::vector<uint8_t>& id = sold.empty() ? rom : sold;
            raReady = ra->begin(ra->ud, id.data(), id.size(), core, ReadRa);
        }
#endif
        return true;
    }

    void Stop()
    {
        if (!core) return;
        CancelPauseTouch();
#ifdef GBAW_EMBEDDED
        SaveAutomatic();
        if (raReady && ra->end) ra->end(ra->ud);
        raReady = false;
        if (typing) SDL_StopTextInput();
        typing = false;
#endif
        if (followFind.joinable()) followFind.join();
        follow.reset();
        followState = 0;
        pk_follower_set(nullptr, nullptr);
        pk_tint_set(core, 256, 256, 256);
        gpText.clear();
        gpTms.clear();
        gpTextOn = gpTmsOn = gpField = false;
        gpWild = 0;
        gpState = 0;
        companion.Shutdown(core);
        pk_wide_reset();
        pkWide = false;
        zoomShown = false;
        if (audio)
        {
            SDL_PauseAudioDevice(audio, 1);
            SDL_ClearQueuedAudio(audio);
        }
        mAudioResamplerDeinit(&resampler);
        core->deinit(core); // (writes the save)
        core = nullptr;
        if (embedded)
        {
            quit = true; // (back to the launcher's list)
            return;
        }
        screen = LIST;
        Rescan();
    }

    // A test script (--script, or GBAW_TEST_SCRIPT when built into the launcher): test mode.
    bool LoadScript(const std::string& path)
    {
        testMode = true;
        realTime = getenv("GBAW_TEST_REALTIME") != nullptr;
        std::vector<uint8_t> s;
        if (!Games::ReadFile(path, s, 1 << 20))
        {
            fprintf(stderr, "can't read %s\n", path.c_str());
            return false;
        }
        std::string text(s.begin(), s.end());
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            char what[16] = {}, arg[512] = {};
            int f = 0, hold = 2;
            const int got = sscanf(text.substr(pos, end - pos).c_str(), "%15s %d %511s %d", what, &f, arg, &hold);
            if (got >= 2 && what[0] != '#') script.push_back({ f, what, arg, got >= 4 ? hold : 2 });
            pos = end + 1;
        }
        return true;
    }

    // Everything SDL made for this, let go (embedded: the launcher's list comes next, and maybe
    // another game).
    void Close()
    {
        if (core) Stop();
        for (auto& kv : artTex)
            if (kv.second) SDL_DestroyTexture(kv.second);
        artTex.clear();
        for (SDL_Texture** t : { &uiTex, &bigTex, &gameTex, &hdTex, &lcdTex, &zoomTex, &zoomHdTex
#ifdef GBAW_EMBEDDED
                                 , &hostTex
#endif
                               })
            if (*t)
            {
                SDL_DestroyTexture(*t);
                *t = nullptr;
            }
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        ren = nullptr;
        win = nullptr;
        if (audio) SDL_CloseAudioDevice(audio);
        audio = 0;
        pads.CloseAll();
    }

    // A controller's A is the button labelled A (CONTROLLER BUTTONS), always on the Switch.
    bool ByLabel() const
    {
#if defined(__SWITCH__)
        return true;
#else
        return set.Get("buttons") != "positions";
#endif
    }

    void SetVolume()
    {
        if (!core) return;
        mCoreConfigSetIntValue(&core->config, "volume", set.Int("volume") * 0x100 / 100);
        core->reloadConfigOption(core, "volume", nullptr);
    }

    void CancelPauseTouch()
    {
        pauseTouchDown = false;
        pauseTouchMoved = false;
    }
    std::string StatePath(int slot = 0) const
    {
        return states + "/" + game.key + (slot > 0 ? "." + std::to_string(slot + 1) : std::string()) + ".ss";
    }
    void SaveState(int slot = 0)
    {
        const bool ok = core && GbaStateFile::Save(StatePath(slot), [&](const std::string& pending) {
            struct VFile* vf = VFileOpen(pending.c_str(), O_CREAT | O_TRUNC | O_RDWR);
            const bool saved = vf && mCoreSaveStateNamed(core, vf, SAVESTATE_RTC);
            const bool closed = vf ? vf->close(vf) : false;
            return saved && closed;
        });
        Toast(ok ? "STATE SAVED" : "COULDN'T SAVE THE STATE");
    }
    void LoadState(int slot = 0)
    {
        struct VFile* vf = VFileOpen(StatePath(slot).c_str(), O_RDONLY);
        if (!vf)
        {
            Toast("NO SAVED STATE FOR THIS GAME YET");
            return;
        }
        const bool ok = mCoreLoadStateNamed(core, vf, SAVESTATE_RTC);
        vf->close(vf);
        if (ok) companion.OnReset(core);
        if (ok && (DynamicWide1x() || ZoomOn()))
        {
            camera.Reset();
            pk_wide_jump(core, frame240.data());
            zoomShown = false;
        }
#ifdef GBAW_CHEATS
        if (ok) cheats.Reset();
#endif
        Toast(ok ? "STATE LOADED" : "COULDN'T LOAD THE STATE");
#ifdef GBAW_EMBEDDED
        if (ok && raReady && ra->reset) ra->reset(ra->ud);
#endif
    }
#ifdef GBAW_EMBEDDED
    void SaveAutomatic()
    {
        if (!core || autoStatePath.empty() || set.Get("auto_state") != "1" || autoStateBlocked || !autoStatePlayed) return;
        const bool ok = GbaStateFile::Save(autoStatePath, [&](const std::string& pending) {
            struct VFile* vf = VFileOpen(pending.c_str(), O_CREAT | O_TRUNC | O_RDWR);
            const bool saved = vf && mCoreSaveStateNamed(core, vf, SAVESTATE_RTC);
            const bool closed = vf ? vf->close(vf) : false;
            return saved && closed;
        });
        autoStateTimer.Saved(SDL_GetTicks());
        fprintf(stderr, "[auto-state gba] %s frame=%llu\n", ok ? "saved" : "failed; previous kept", gameFrames);
        if (!ok)
        {
            autoStateBlocked = true;
            Toast("AUTO STATE FAILED - PREVIOUS STATE KEPT");
        }
    }
    bool ResumeAutomatic()
    {
        if (set.Get("auto_resume") != "1" || autoStatePath.empty() || !AutoState::Present(autoStatePath)) return true;
        VFile* before = VFileMemChunk(nullptr, 0);
        if (!before || !mCoreSaveStateNamed(core, before, SAVESTATE_RTC))
        {
            if (before) before->close(before);
            autoStateBlocked = true;
            Toast("AUTO RESUME SKIPPED - COULD NOT KEEP RECOVERY STATE");
            return true;
        }
        VFile* state = VFileOpen(autoStatePath.c_str(), O_RDONLY);
        // RTC only: leave the normal SRAM/flash save intact, as manual LoadState does.
        const bool loaded = state && mCoreLoadStateNamed(core, state, SAVESTATE_RTC);
        if (state) state->close(state);
        if (!loaded)
        {
            autoStateBlocked = true;
            const bool recovered = before->seek(before, 0, SEEK_SET) == 0 && mCoreLoadStateNamed(core, before, SAVESTATE_RTC);
            before->close(before);
            fprintf(stderr, "[auto-state gba] invalid automatic state; recovery %s\n", recovered ? "succeeded" : "failed");
            Toast("INVALID AUTO STATE - STARTING NORMALLY; AUTO SAVE PAUSED");
            return recovered;
        }
        before->close(before);
        autoStatePlayed = true;
        camera.Reset();
        companion.OnReset(core);
        if (DynamicWide1x() || ZoomOn()) { pk_wide_jump(core, frame240.data()); zoomShown = false; }
#ifdef GBAW_CHEATS
        cheats.Reset();
#endif
        fprintf(stderr, "[auto-state gba] resumed; normal save kept\n");
        Toast("AUTO STATE RESUMED - IN-GAME SAVE KEPT");
        return true;
    }
#endif

    // Target aspect ratio when ASPECT RATIO is wider than GBA's 3:2.
    double WantedAspect() const
    {
        const std::string w = set.Get("widescreen");
        if (w == "off" || w == "3:2" || w == "4:3") return 0.0;
        if (w == "auto")
        {
            int ow = 16, oh = 9;
            if (ren) SDL_GetRendererOutputSize(ren, &ow, &oh);
            return oh > 0 ? (double)ow / oh : 16.0 / 9;
        }
        const size_t c = w.find(':');
        return c == std::string::npos ? 16.0 / 9 : atof(w.c_str()) / std::max(0.001, atof(w.c_str() + c + 1));
    }

    // How many columns the picture gets on each side: the window's shape, or WIDESCREEN.
    int WantedExtra() const
    {
        const double aspect = WantedAspect();
        if (aspect <= 0.0) return 0;
        const int e = (int)lround((GbaH * aspect - GbaW) / 2);
        return std::max(0, std::min(MaxExtra, e));
    }

    bool DynamicWide1x() const { return game.patch.empty() && pkWide && WantedExtra() > 0; }
    bool ZoomOn() const { return pkWide && set.Get("zoom") == "1"; }

    // ZOOM: the next step out (back to 1x after the last), said in a toast.
    void ZoomCycle()
    {
        if (!ZoomOn())
        {
#ifdef GBAW_EMBEDDED
            Toast(!pkWide ? "ZOOM ISN'T AVAILABLE FOR THIS GAME YET"
                          : (uiHost ? "ZOOM AND LOOK AROUND IS OFF (GRAPHICS)" : "ZOOM AND LOOK AROUND IS OFF (DISPLAY)"));
#else
            Toast(!pkWide ? "ZOOM ISN'T AVAILABLE FOR THIS GAME YET" : "ZOOM AND LOOK AROUND IS OFF (DISPLAY)");
#endif
            return;
        }
        const int l = camera.Cycle();
        char t[48];
        snprintf(t, sizeof t, "ZOOM %.1fX", camera.Factor(l));
        Toast(l == 0 ? "ZOOM 1X" : t, 1200);
    }

    // PokeDaisy WIDE frame: handles Zoom/Look-Around for all known games plus dynamic 1x
    // widescreen for unpatched hacks (e.g. SoulGold v1.2.1) when a wide aspect is active.
    void ZoomFrame(bool shown)
    {
        zoomShown = false;
        const bool dyn1x = DynamicWide1x();
        const bool zoomOn = ZoomOn();
        if (!dyn1x && !zoomOn) return;
        if (!shown)
        {
            pk_wide_skip(core, frame240.data());
            return;
        }
        const int cw = CoreW(), ch = CoreH();
        const int e = std::max(0, wideExtra);
        for (int y = 0; y < ch; y++)
            memcpy(frame240.data() + (size_t)y * cw, video.data() + (size_t)y * MaxW + e, (size_t)cw * 4);
        int ow = 0, oh = 0;
        if (ren) SDL_GetRendererOutputSize(ren, &ow, &oh);
        if (!zoomOn)
        {
            camera.level = 0;
            camera.lookAround = false;
            camera.zoomOnText = false;
            camera.walking = false;
            camera.stickX = camera.stickY = 0;
        }
        else
        {
            camera.lookAround = true;
            camera.zoomOnText = set.Get("zoom_text") == "1";
            const int dirs = pads.Dirs();
            camera.walking = !paused && !companion.TakesInput() && ((GameKeys() & 0xF0) != 0 || dirs);
            float sx = pads.Axis(SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f, sy = pads.Axis(SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
            if (key[SDL_SCANCODE_J]) sx = -1;
            if (key[SDL_SCANCODE_L]) sx = 1;
            if (key[SDL_SCANCODE_I]) sy = -1;
            if (key[SDL_SCANCODE_K]) sy = 1;
            camera.stickX = (paused || companion.TakesInput()) ? 0 : sx;
            camera.stickY = (paused || companion.TakesInput()) ? 0 : sy;
        }
        const bool panIdle = camera.level == 0 && std::fabs(camera.stickX) < 0.2f && std::fabs(camera.stickY) < 0.2f;
        const double aspect = WantedAspect();
        const int capW = (dyn1x && panIdle && aspect > 0.0 && oh > 0)
            ? std::min(ow, std::max(1, (int)lround(oh * aspect))) : ow;
        camera.viewW = capW;
        camera.viewH = oh;
        PkWideCamera::Window w = camera.Frame(pk_wide_scene(),
            (int64_t)SDL_GetPerformanceCounter() * 1000000000LL / (int64_t)SDL_GetPerformanceFrequency());
        if (capW < ow) w.offX += (ow - capW) * 0.5f;
        const bool inside = w.valid && w.x >= -e && w.y >= 0 && w.x + w.w <= cw + e && w.y + w.h <= ch;
        if (!shown || !w.valid || (!dyn1x && panIdle && camera.Plain()))
        {
            pk_wide_skip(core, frame240.data());
            return;
        }
        if (!dyn1x && (camera.Plain() || inside))
        {
            uint32_t px = 0;
            struct PkWideView one = { 0, 0, 1, 1 };
            pk_wide_view(core, frame240.data(), &px, one, nullptr);
            return;
        }
        struct PkWideView v = { w.x, w.y, w.w, w.h };
        pk_wide_view(core, frame240.data(), zoomBuf.data(), v, nullptr);
        if (e > 0)
        {
            const int trim = std::min(8, e);
            const int x0 = std::max(w.x, -e + trim), x1 = std::min(w.x + w.w, cw + e - trim);
            const int y0 = std::max(w.y, 0), y1 = std::min(w.y + w.h, ch);
            if (x1 > x0)
                for (int y = y0; y < y1; y++)
                    memcpy(zoomBuf.data() + (size_t)(y - w.y) * w.w + (x0 - w.x),
                           video.data() + (size_t)y * MaxW + x0 + e, (size_t)(x1 - x0) * 4);
        }
        zoomWin = w;
        zoomShown = true;
    }

    // Keys held for the game.
    uint32_t GameKeys() const
    {
        uint32_t k = 0;
        auto on = [&](GbaKey g) { k |= 1u << g; };
        if (key[SDL_SCANCODE_X]) on(K_A);
        if (key[SDL_SCANCODE_Z]) on(K_B);
        if (key[SDL_SCANCODE_BACKSPACE] || key[SDL_SCANCODE_RSHIFT]) on(K_SELECT);
        if (key[SDL_SCANCODE_RETURN] || key[SDL_SCANCODE_KP_ENTER]) on(K_START);
        if (key[SDL_SCANCODE_RIGHT]) on(K_RIGHT);
        if (key[SDL_SCANCODE_LEFT]) on(K_LEFT);
        if (key[SDL_SCANCODE_UP]) on(K_UP);
        if (key[SDL_SCANCODE_DOWN]) on(K_DOWN);
        if (key[SDL_SCANCODE_W] || key[SDL_SCANCODE_S]) on(K_R);
        if (key[SDL_SCANCODE_Q] || key[SDL_SCANCODE_A]) on(K_L);
        const bool byLabel = ByLabel();
        if (pads.Button(byLabel ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B)) on(K_A);
        if (pads.Button(byLabel ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A)) on(K_B);
        {
            // START BUTTON: PAUSE MENU (start) or GBA START. The other is START and SELECT together.
            const bool st = pads.Button(SDL_CONTROLLER_BUTTON_START), sel = pads.Button(SDL_CONTROLLER_BUTTON_BACK);
            const Uint32 now = SDL_GetTicks();
            const bool stAlone = (st && !sel && !comboOn && now - plusAt >= 150) || (int32_t)(plusPulse - now) > 0;
            const bool selAlone = (sel && !st && !comboOn && now - minusAt >= 150) || (int32_t)(minusPulse - now) > 0;
            if (set.Get("gba_start") == "start")
            {
                if (stAlone) on(K_START);
                if (selAlone) on(K_SELECT);
            }
            else
            {
                if (st && sel) on(K_START);
                else if (selAlone) on(K_SELECT);
            }
        }
        if (pads.Button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) on(K_L);
        if (pads.Button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) on(K_R);
        const int d = pads.Dirs();
        if (d & 1) on(K_UP);
        if (d & 2) on(K_DOWN);
        if (d & 4) on(K_LEFT);
        if (d & 8) on(K_RIGHT);
        return k;
    }
    bool FastDown() const
    {
        return key[SDL_SCANCODE_LSHIFT] || pads.Axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
    }
    bool FastForward() const
    {
        return fastButton.Requested(set.Get("fast_button") == "toggle", FastDown(), fastToggle);
    }

    bool GameplayOffered(const char* key) const
    {
        if (game.isGb)
        {
            if (!strcmp(key, "day_night") || !strcmp(key, "follower") ||
                !strcmp(key, "instant_text") || !strcmp(key, "reusable_tms") ||
                !strcmp(key, "run_indoors") || !strcmp(key, "wild_encounters") ||
                !strcmp(key, "shiny_odds") || !strcmp(key, "battle_speed") ||
                !strcmp(key, "text_speed") || !strcmp(key, "bike_speed") || !strcmp(key, "surf_speed"))
                return false;
            return true;
        }
        if (!strcmp(key, "instant_text")) return !gpText.empty();
        if (!strcmp(key, "reusable_tms")) return !gpTms.empty();
        if (!strcmp(key, "run_indoors")) return gpField && gpGameCode.size() == 4 && gpGameCode[0] == 'B';
        if (!strcmp(key, "wild_encounters")) return gpField && gpWild != 0;
        if (!strcmp(key, "shiny_odds"))
        {
            uint32_t e = 0, sb2 = 0, inb = 0; bool st = false;
            return companion.ShinyAddrs(gpGameCode, e, sb2, st, inb);
        }
        if (!strcmp(key, "day_night")) return pkWide && gpField;
        if (!strcmp(key, "follower")) return pkWide && gpField && companion.active;
        if (!strcmp(key, "battle_speed") || !strcmp(key, "text_speed") ||
            !strcmp(key, "bike_speed") || !strcmp(key, "surf_speed"))
            return gpField;
        return true;
    }
    bool AnyGameplayOffered() const
    {
        static const char* const keys[] = { "instant_text", "reusable_tms", "run_indoors", "wild_encounters",
                                            "shiny_odds", "battle_speed", "text_speed", "bike_speed", "surf_speed" };
        for (const char* k : keys)
            if (GameplayOffered(k)) return true;
        return false;
    }
    int WildKeep() const { return gpWild ? std::max(0, std::min(100, atoi(set.Get("wild_encounters").c_str()))) : 100; }
    void ApplyGameplay()
    {
        if (!core || game.isGb) return;
        auto put = [&](const std::vector<GbaGameplay::Write>& w, bool on, bool& was) {
            if (on == was) return;
            for (const auto& x : w)
            {
                const auto& b = on ? x.value : x.orig;
                for (size_t k = 0; k < b.size(); k++) core->rawWrite8(core, x.addr + (uint32_t)k, -1, b[k]);
            }
            was = on;
        };
        put(gpText, set.Get("instant_text") == "1", gpTextOn);
        put(gpTms, set.Get("reusable_tms") == "1", gpTmsOn);
    }
    void ShinyFrame()
    {
        if (game.isGb) return;
        const int odds = atoi(set.Get("shiny_odds").c_str());
        uint32_t enemy = 0, sb2 = 0, inb = 0;
        bool staticBlocks = false;
        if (odds <= 0 || odds >= 8192 || !companion.ShinyAddrs(gpGameCode, enemy, sb2, staticBlocks, inb)) return;
        shiny.Tick(odds, enemy, sb2, staticBlocks, inb,
                   [&](uint32_t a, uint8_t* b, int n) {
                       for (int i = 0; i < n; i++) b[i] = (uint8_t)core->busRead8(core, a + (uint32_t)i);
                       return true;
                   },
                   [&](uint32_t a, const uint8_t* b, int n) {
                       for (int i = 0; i < n; i++) core->busWrite8(core, a + (uint32_t)i, b[i]);
                       return true;
                   });
    }
    int SceneSpeed() const
    {
        if (game.isGb || !gpField || !(gpState & PK_FIELD_KNOWN)) return 1;
        const char* key = (gpState & PK_FIELD_BATTLE) ? "battle_speed"
                        : (gpState & (PK_FIELD_TEXT | PK_FIELD_WINDOW)) ? "text_speed"
                        : (gpState & PK_FIELD_BIKE) ? "bike_speed"
                        : (gpState & PK_FIELD_SURF) ? "surf_speed" : nullptr;
        return key ? std::max(1, std::min(4, set.Int(key))) : 1;
    }
    static void DayNightAt(float hour, int out[3])
    {
        static const struct { float h; int c[3]; } keys[] = {
            { 0.0f, { 135, 150, 225 } }, { 4.5f, { 135, 150, 225 } }, { 6.0f, { 245, 220, 220 } },
            { 7.5f, { 256, 256, 256 } }, { 16.5f, { 256, 256, 256 } }, { 18.0f, { 255, 200, 165 } },
            { 19.5f, { 135, 150, 225 } }, { 24.0f, { 135, 150, 225 } }
        };
        int i = 0;
        while (i < 6 && keys[i + 1].h <= hour) i++;
        const float t = (hour - keys[i].h) / (keys[i + 1].h - keys[i].h);
        for (int c = 0; c < 3; c++)
            out[c] = (int)(keys[i].c[c] + (keys[i + 1].c[c] - keys[i].c[c]) * std::max(0.0f, std::min(1.0f, t)));
    }
    bool DayNightOn() const { return !game.isGb && set.Get("day_night") == "1" && pkWide && gpField; }
    bool FollowerOn() const { return !game.isGb && set.Get("follower") == "1" && pkWide && gpField && companion.active; }
    void FollowFind()
    {
        followState = 1;
        const std::string dir = roms;
        followFind = std::thread([this, dir] {
            namespace fs = std::filesystem;
            std::error_code ec;
            auto src = std::make_unique<HgssFollow::Source>();
            bool ok = false;
            for (const fs::path& top : { fs::path(dir), fs::path(dir).parent_path() })
            {
                if (top.empty()) continue;
                ec.clear();
                for (auto it = fs::recursive_directory_iterator(top, fs::directory_options::skip_permission_denied, ec);
                     !ok && !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
                    std::string ext = it->path().extension().string();
                    for (char& c : ext) c = (char)tolower((unsigned char)c);
                    if (ext == ".nds" && it->is_regular_file(ec)) ok = src->Open(it->path().string());
                }
                if (ok) break;
            }
            if (ok) follow = std::move(src);
            followState = ok ? 2 : 3;
        });
    }
    void FollowerFrame()
    {
        const bool want = FollowerOn();
        if (want && followState == 0) FollowFind();
        if (want && followState == 3 && !followToldNone)
        {
            followToldNone = true;
            Toast("FOLLOWING POKEMON NEEDS HEARTGOLD OR SOULSILVER IN THE ROMS FOLDER", 4000);
        }
        bool show = false;
        if (want && followState == 2 && follow)
        {
            if (followTick++ % 30 == 0)
            {
                int national = 0;
                bool isShiny = false;
                if (!companion.LeadInfo(national, isShiny)) national = 0;
                if (national != followSpecies || isShiny != followShiny)
                {
                    uint8_t frames[6 * 512], pal[32];
                    if (national > 0 && follow->Mon(national, isShiny, frames, pal)) pk_follower_set(frames, pal);
                    else pk_follower_set(nullptr, nullptr);
                    followSpecies = national;
                    followShiny = isShiny;
                }
            }
            const int scene = pk_wide_scene();
            show = followSpecies > 0 && (scene == PK_WIDE_SCENE_FIELD || scene == PK_WIDE_SCENE_TEXT) &&
                   !(gpState & (PK_FIELD_BATTLE | PK_FIELD_SURF));
        }
        if (show != followShown || (show && followTick % 60 == 0))
        {
            pk_follower_show(core, show ? 1 : 0);
            followShown = show;
        }
    }
    void DayNightFrame()
    {
        if (game.isGb) return;
        int want[3] = { 256, 256, 256 };
        if (DayNightOn())
        {
            const int scene = pk_wide_scene(), mt = pk_field_map_type(core);
            const bool field = (scene == PK_WIDE_SCENE_FIELD || scene == PK_WIDE_SCENE_TEXT) && !(gpState & PK_FIELD_BATTLE);
            const bool outdoors = mt < 0 || mt == 1 || mt == 2 || mt == 3 || mt == 6;
            if (field && outdoors)
            {
                const time_t now = time(nullptr);
                const struct tm* lt = localtime(&now);
                const char* fh = getenv("FULLDEX_HOUR");
                if (fh) DayNightAt((float)atof(fh), want);
                else if (lt) DayNightAt(lt->tm_hour + lt->tm_min / 60.0f, want);
            }
        }
        bool changed = false;
        for (int c = 0; c < 3; c++)
        {
            const float d = want[c] - tintNow[c];
            tintNow[c] = std::fabs(d) < 8 ? (float)want[c] : tintNow[c] + (d > 0 ? 8 : -8);
            if ((int)tintNow[c] != tintSent[c]) { tintSent[c] = (int)tintNow[c]; changed = true; }
        }
        const bool tinted = tintSent[0] != 256 || tintSent[1] != 256 || tintSent[2] != 256;
        if (changed || (tinted && ++tintFrames % 60 == 0)) pk_tint_set(core, tintSent[0], tintSent[1], tintSent[2]);
    }

    // Runs one GBA/GB frame, with sound if `sound` and dynamic wide/zoom work if `shown`.
    void RunFrame(bool sound, bool ff = false, bool shown = true)
    {
        if (!game.isGb)
        {
            const int e = !game.patch.empty() ? WantedExtra() : 0;
            if (e != wideExtra)
            {
                wideExtra = e;
                mCoreConfigSetIntValue(&core->config, "gba.wideExtra", wideExtra);
                core->reloadConfigOption(core, "gba.wideExtra", nullptr);
            }
        }
        uint32_t keys = (paused || companion.TakesInput()) ? 0 : GameKeys();
        if (holdKeys)
        {
            if (keys == 0) holdKeys = false;
            keys = 0;
        }
        const bool rewindHeld = !paused && !companion.TakesInput() &&
            (key[SDL_SCANCODE_R] || (leftTrigger && (pads.Button(SDL_CONTROLLER_BUTTON_BACK) || pads.Button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))));
        if (companion.PreFrame(core, keys, rewindHeld))
        {
            ZoomFrame(shown);
            return;
        }
        core->setKeys(core, keys);
        core->runFrame(core);
        if (!game.isGb && gpField)
            gpState = pk_field_state(core, set.Get("run_indoors") == "1" ? 1 : 0, WildKeep());
        if (!game.isGb) ShinyFrame();
        ZoomFrame(shown);
        if (!game.isGb && shown)
        {
            DayNightFrame();
            FollowerFrame();
        }
        companion.PostFrame(core, CompanionActions(), shown);
#ifdef GBAW_CHEATS
        cheats.Frame(cheatBus, keys);
#endif
#ifdef GBAW_EMBEDDED
        if (raReady && ra->frame) ra->frame(ra->ud);
        autoStatePlayed = true;
#endif
        Sound(sound, ff);
        fpsFrames++;
        gameFrames++;
    }

    // -----------------------------------------------------------------------------------------
    // Menus

    void Rescan()
    {
        games = Games::Scan(roms, saves, listCache);
        listSel = std::max(0, std::min(listSel, (int)games.size() - 1));
    }

    // The pause menu's rows for the page that is open (the root, or RetroAchievements' pages).
    void BuildPause()
    {
        pause.items.clear();
#ifdef GBAW_EMBEDDED
        auto line = [&](bool (*fn)(void*, char*, size_t)) {
            char buf[96] = {};
            if (ra && fn) fn(ra->ud, buf, sizeof buf);
            return std::string(buf[0] ? buf : "-");
        };
        if (pausePage == P_LOGIN)
        {
            std::string u = raUser.size() > 18 ? raUser.substr(0, 15) + "..." : raUser;
            if (u.empty()) u = "...";
            const bool busy = ra && ra->busy && ra->busy(ra->ud);
            pause.items.push_back({ "USER NAME", u + (raField == 0 ? "_" : ""), true });
            pause.items.push_back({ "PASSWORD", std::to_string(raPass.size()) + " CHARACTERS" + (raField == 1 ? "_" : ""), true });
            pause.items.push_back({ busy ? "LOGGING IN..." : "LOG IN", "" });
            pause.items.push_back({ "CANCEL", "" });
            return;
        }
        if (pausePage == P_LIST)
        {
            achDesc.clear();
            const int n = ra && ra->count ? ra->count(ra->ud) : 0;
            if (n <= 0)
            {
                pause.items.push_back({ "NONE YET", "", true });
                return;
            }
            for (int i = 0; i < n; i++)
            {
                char title[80] = {}, desc[160] = {}, prog[32] = {};
                unsigned points = 0;
                int unlocked = 0;
                if (ra->at) ra->at(ra->ud, i, title, sizeof title, desc, sizeof desc, prog, sizeof prog, &points, &unlocked);
                std::string name = title[0] ? title : "ACHIEVEMENT";
                if (name.size() > 28) name = name.substr(0, 25) + "...";
                const std::string value = unlocked ? "DONE" : prog[0] ? std::string(prog) : std::to_string(points);
                pause.items.push_back({ name, value, true });
                std::string d = desc;
                if (d.size() > 52) d = d.substr(0, 49) + "...";
                achDesc.push_back(d);
            }
            return;
        }
#ifdef GBAW_CHEATS
        if (pausePage == P_CHEATS)
        {
            for (int i = 0; i < GbaCheats::COUNT; i++)
            {
                if (cheats.Has(i)) pause.items.push_back({ GbaCheats::About(i).name, cheats.On(i) ? "ON" : "OFF" });
                else pause.items.push_back({ GbaCheats::About(i).name, "-", true });
            }
            pause.items.push_back({ "BACK", "" });
            return;
        }
#endif
        if (pausePage == P_DISPLAY || pausePage == P_SOUND || pausePage == P_GAMEPLAY || pausePage == P_CONTROLS)
        {
            static const char* const display[] = { "widescreen", "fullscreen", "scaling", "hd_filter", "lcd", "colour", "zoom", "zoom_text", "day_night", "follower", "show_fps" };
            static const char* const sound[] = { "volume", "ff_mute" };
            static const char* const gameplay[] = { "instant_text", "reusable_tms", "run_indoors", "wild_encounters", "shiny_odds", "battle_speed", "text_speed", "bike_speed", "surf_speed" };
            static const char* const controls[] = { "gba_start", "buttons", "ff_speed", "fast_button", "auto_state", "auto_resume" };
            const char* const* keys = pausePage == P_DISPLAY ? display : pausePage == P_SOUND ? sound : pausePage == P_GAMEPLAY ? gameplay : controls;
            const int count = pausePage == P_DISPLAY ? (int)(sizeof(display) / sizeof(*display))
                            : pausePage == P_SOUND ? (int)(sizeof(sound) / sizeof(*sound))
                            : pausePage == P_GAMEPLAY ? (int)(sizeof(gameplay) / sizeof(*gameplay))
                            : (int)(sizeof(controls) / sizeof(*controls));
            pageSettings.clear();
            for (int i = 0; i < count; i++)
                for (const Setting& s : SettingsList())
                    if (!strcmp(s.key, keys[i]) && GameplayOffered(s.key))
                    {
                        pause.items.push_back({ s.name, set.Shown(s) });
                        pageSettings.push_back(&s);
                    }
#ifdef GBAW_POKEDAISY
            if (pausePage == P_DISPLAY && companion.active)
                pause.items.push_back({ "FIELD HUD", companion.HudEnabled() ? "ON" : "OFF" });
            if (pausePage == P_GAMEPLAY && companion.active)
                pause.items.push_back({ "AUTO-RUN", companion.AutoRunEnabled() ? "ON" : "OFF" });
#endif
            if (pausePage == P_GAMEPLAY)
                pause.items.push_back({ "REWIND", companion.rewindEnabled ? "ON" : "OFF" });
            pause.items.push_back({ "BACK", "" });
            return;
        }
        if (pausePage == P_SETTINGS)
        {
            const bool on = ra && ra->on && ra->on(ra->ud);
            pause.items.push_back({ "GRAPHICS", "" });
            pause.items.push_back({ "SOUND", "" });
            pause.items.push_back({ "GAMEPLAY", "" });
            pause.items.push_back({ "CONTROLS", "" });
            if (raReady) pause.items.push_back({ "RETROACHIEVEMENTS", on ? "ON" : "OFF" });
            pause.items.push_back({ "BACK", "" });
            return;
        }
        if (pausePage == P_RA)
        {
            const bool on = ra && ra->on && ra->on(ra->ud);
            const bool in = ra && ra->loggedIn && ra->loggedIn(ra->ud);
            pause.items.push_back({ "RETROACHIEVEMENTS", on ? "ON" : "OFF" });
            pause.items.push_back({ "ACCOUNT", line(ra ? ra->account : nullptr), true });
            pause.items.push_back({ "THIS GAME", on ? line(ra ? ra->game : nullptr) : "-", true });
            if (on && ra->active && ra->active(ra->ud)) pause.items.push_back({ "ACHIEVEMENTS", "" });
            pause.items.push_back({ in ? "LOG OUT" : "LOG IN", "" });
            pause.items.push_back({ "BACK", "" });
            return;
        }
#endif
        pause.items.push_back({ "RESUME", "" });
#ifdef GBAW_POKEDAISY
        if (companion.active)
            pause.items.push_back({ "COMPANION", "" });
#endif
        pause.items.push_back({ "SAVE STATE", "" });
        pause.items.push_back({ "LOAD STATE", "" });
        pause.items.push_back({ "FAST FORWARD", fastToggle ? "ON" : "OFF" });
#ifdef GBAW_EMBEDDED
        // Match the DS pause menu: host UI groups display/sound/controls/RA under SETTINGS.
        if (uiHost) pause.items.push_back({ "SETTINGS", "" });
        else
        {
            pause.items.push_back({ "DISPLAY", "" });
            pause.items.push_back({ "SOUND", "" });
            pause.items.push_back({ "GAMEPLAY", "" });
            pause.items.push_back({ "CONTROLS", "" });
        }
#else
        pause.items.push_back({ "SETTINGS", "" });
#endif
#ifdef GBAW_CHEATS
        if (cheats.Active())
        {
#ifdef GBAW_EMBEDDED
            if (uiHost)
            {
                const int nOn = cheats.OnCount();
                const std::string val = nOn == 1 ? std::string("1 ENABLED") : nOn ? std::to_string(nOn) + " ACTIVE" : "";
                pause.items.push_back({ "CHEATS", val, true });
            }
            else
#endif
                pause.items.push_back({ "CHEATS", cheats.OnCount() ? std::to_string(cheats.OnCount()) + " ON" : "" });
        }
#endif
#ifdef GBAW_EMBEDDED
        if (raReady && !uiHost) pause.items.push_back({ "RETROACHIEVEMENTS", "" });
#endif
        pause.items.push_back({ "RESET GAME", "" });
#ifdef GBAW_EMBEDDED
        pause.items.push_back({ "QUIT", "" });
#else
        pause.items.push_back({ "QUIT GAME", "" });
#endif
    }

    void OpenPause()
    {
        paused = true;
        CancelPauseTouch();
#ifdef GBAW_EMBEDDED
        pausePage = P_ROOT;
        hostMenu.scroll = 0;
        builtHostSig = 0;
#endif
        pause.selected = 0;
        BuildPause();
        if (audio)
        {
            SDL_PauseAudioDevice(audio, 1);
            SDL_ClearQueuedAudio(audio);
        }
    }
    void Resume()
    {
        CancelPauseTouch();
#ifdef GBAW_EMBEDDED
        if (typing) SDL_StopTextInput();
        typing = false;
#endif
        paused = false;
        holdKeys = true;
        if (audio) SDL_PauseAudioDevice(audio, 0);
    }

#ifdef GBAW_EMBEDDED
    void PopChar(std::string& s)
    {
        if (s.empty()) return;
        s.pop_back();
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    }
    void ForgetPass()
    {
        std::fill(raPass.begin(), raPass.end(), '\0');
        raPass.clear();
    }
    void LoginInput(const MenuIn& in)
    {
        const int n = (int)pause.items.size();
        if (n <= 0) return;
        if (in.erase)
        {
            PopChar(raField == 0 ? raUser : raPass);
            return;
        }
        if (in.menu)
        {
            ForgetPass();
            Resume();
            return;
        }
        if (in.back)
        {
            ForgetPass();
            if (typing) SDL_StopTextInput();
            typing = false;
            pausePage = P_RA;
            pause.selected = 0;
            return;
        }
        if (in.up) pause.selected = (pause.selected + n - 1) % n;
        if (in.down) pause.selected = (pause.selected + 1) % n;
        if (pause.selected <= 1) raField = pause.selected;
        if (!in.accept) return;
        if (pause.selected <= 1) raField = pause.selected;
        else if (pause.selected == 3)
        {
            ForgetPass();
            if (typing) SDL_StopTextInput();
            typing = false;
            pausePage = P_RA;
            pause.selected = 0;
        }
        else if (ra && ra->login)
        {
            if (ra->busy && ra->busy(ra->ud)) return;
            if (raUser.empty())
            {
                raField = 0;
                pause.selected = 0;
                Toast("TYPE YOUR USER NAME");
                return;
            }
            if (raPass.empty())
            {
                raField = 1;
                pause.selected = 1;
                Toast("TYPE YOUR PASSWORD");
                return;
            }
            ra->login(ra->ud, raUser.c_str(), raPass.c_str());
            ForgetPass();
            if (typing) SDL_StopTextInput();
            typing = false;
            pausePage = P_RA;
            pause.selected = 0;
        }
    }
#endif

#ifdef GBAW_EMBEDDED
    void ReturnToPausePage(PausePage page, const char* selectLabel = nullptr)
    {
        CancelPauseTouch();
        pausePage = page;
        hostMenu.scroll = 0;
        BuildPause();
        pause.selected = 0;
        if (selectLabel)
            for (size_t j = 0; j < pause.items.size(); j++)
                if (pause.items[j].label == selectLabel) { pause.selected = (int)j; break; }
    }
#endif

#ifdef GBAW_CHEATS
    // The achievements host is told whether any cheat is on (achievements pause while one is).
    void CheatsToHost()
    {
        if (ra && ra->cheats) ra->cheats(ra->ud, cheats.OnCount() > 0 ? 1 : 0);
    }
    // CHEATS: a row a cheat (A, or left and right, turns it on or off: kept at once), then BACK.
    void CheatsInput(const MenuIn& in)
    {
        const int n = (int)pause.items.size();
        if (n <= 0) return;
        if (in.up) pause.selected = (pause.selected + n - 1) % n;
        if (in.down) pause.selected = (pause.selected + 1) % n;
        if (in.menu)
        {
            Resume();
            return;
        }
        if (in.back || (in.accept && pause.items[pause.selected].label == "BACK"))
        {
            ReturnToPausePage(P_ROOT, "CHEATS");
            return;
        }
        if (!in.accept && !in.left && !in.right) return;
        const int i = pause.selected;
        if (i < 0 || i >= GbaCheats::COUNT || !cheats.Has(i)) return;
        cheats.Set(i, !cheats.On(i));
        if (!cheats.Save(cheatsFile)) Toast("COULDN'T SAVE THE CHEATS");
        CheatsToHost();
        fprintf(stderr, "[gba] cheat %s %s\n", GbaCheats::About(i).name, cheats.On(i) ? "on" : "off");
    }
#endif

#ifdef GBAW_EMBEDDED
    // DISPLAY, SOUND and CONTROLS: a row a setting (left and right, or A, change it: kept at once,
    // in Fulldex's settings too), then BACK to SETTINGS (or root in standalone), selecting its row.
    void SettingsPageInput(const MenuIn& in)
    {
        const int n = (int)pause.items.size();
        if (n <= 0) return;
        if (in.up) pause.selected = (pause.selected + n - 1) % n;
        if (in.down) pause.selected = (pause.selected + 1) % n;
        if (in.menu)
        {
            Resume();
            return;
        }
        const int i = pause.selected;
        if (in.back || (in.accept && pause.items[i].label == "BACK"))
        {
            if (uiHost)
            {
                const char* from = pausePage == P_DISPLAY ? "GRAPHICS" : pausePage == P_SOUND ? "SOUND" : pausePage == P_GAMEPLAY ? "GAMEPLAY" : "CONTROLS";
                ReturnToPausePage(P_SETTINGS, from);
            }
            else
            {
                const char* from = pausePage == P_DISPLAY ? "DISPLAY" : pausePage == P_SOUND ? "SOUND" : pausePage == P_GAMEPLAY ? "GAMEPLAY" : "CONTROLS";
                ReturnToPausePage(P_ROOT, from);
            }
            return;
        }
        if (in.left || in.right || in.accept)
        {
            const std::string& item = pause.items[i].label;
            if (item == "REWIND")
            {
                companion.SetRewind(core, !companion.rewindEnabled);
                pause.items[i].value = companion.rewindEnabled ? "ON" : "OFF";
                set.values["rewind"] = companion.rewindEnabled ? "1" : "0";
                set.Save();
                return;
            }
#ifdef GBAW_POKEDAISY
            if (item == "FIELD HUD")
            {
                companion.SetHud(!companion.HudEnabled());
                pause.items[i].value = companion.HudEnabled() ? "ON" : "OFF";
                return;
            }
            if (item == "AUTO-RUN")
            {
                companion.SetAutoRun(!companion.AutoRunEnabled());
                pause.items[i].value = companion.AutoRunEnabled() ? "ON" : "OFF";
                return;
            }
#endif
        }
        if (!(in.left || in.right || in.accept) || i < 0 || i >= (int)pageSettings.size()) return;
        const Setting& s = *pageSettings[i];
        set.Step(s, in.left ? -1 : 1);
        if (!strcmp(s.key, "fullscreen"))
        {
            const bool want = set.Int("fullscreen");
            const bool full = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            if (want != full) SDL_SetWindowFullscreen(win, want ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        if (!strcmp(s.key, "volume")) SetVolume();
        if (!strcmp(s.key, "instant_text") || !strcmp(s.key, "reusable_tms")) ApplyGameplay();
        if (!strcmp(s.key, "zoom") && !ZoomOn()) { camera.Reset(); camera.level = 0; }
        if (!strcmp(s.key, "widescreen") || !strcmp(s.key, "zoom") || !strcmp(s.key, "zoom_text")) zoomRefresh = true;
        pause.items[i].value = set.Shown(s);
    }
#endif

    void PauseInput(const MenuIn& in)
    {
#ifdef GBAW_EMBEDDED
        if (pausePage == P_LOGIN)
        {
            LoginInput(in);
            return;
        }
#ifdef GBAW_CHEATS
        if (pausePage == P_CHEATS)
        {
            CheatsInput(in);
            return;
        }
#endif
        if (pausePage == P_DISPLAY || pausePage == P_SOUND || pausePage == P_GAMEPLAY || pausePage == P_CONTROLS)
        {
            SettingsPageInput(in);
            return;
        }
        if (pausePage != P_ROOT)
        {
            const int n = (int)pause.items.size();
            if (n <= 0) return;
            if (in.up) pause.selected = (pause.selected + n - 1) % n;
            if (in.down) pause.selected = (pause.selected + 1) % n;
            if (in.menu)
            {
                Resume();
                return;
            }
            if (in.back)
            {
                if (pausePage == P_LIST) ReturnToPausePage(P_RA, "ACHIEVEMENTS");
                else if (pausePage == P_RA && uiHost) ReturnToPausePage(P_SETTINGS, "RETROACHIEVEMENTS");
                else if (pausePage == P_SETTINGS) ReturnToPausePage(P_ROOT, "SETTINGS");
                else ReturnToPausePage(P_ROOT);
                return;
            }
            const bool turn = (pausePage == P_RA || pausePage == P_SETTINGS) && (in.left || in.right);
            if (!in.accept && !turn) return;
            if (pause.selected < 0 || pause.selected >= n) return;
            const std::string item = pause.items[pause.selected].label;
            if (turn && item == "RETROACHIEVEMENTS" && ra && ra->setOn && ra->on)
            {
                ra->setOn(ra->ud, ra->on(ra->ud) ? 0 : 1);
                return;
            }
            if (!in.accept) return;
            if (pausePage == P_SETTINGS)
            {
                if (item == "GRAPHICS") ReturnToPausePage(P_DISPLAY);
                else if (item == "SOUND") ReturnToPausePage(P_SOUND);
                else if (item == "GAMEPLAY") ReturnToPausePage(P_GAMEPLAY);
                else if (item == "CONTROLS") ReturnToPausePage(P_CONTROLS);
                else if (item == "RETROACHIEVEMENTS") ReturnToPausePage(P_RA);
                else if (item == "BACK") ReturnToPausePage(P_ROOT, "SETTINGS");
                return;
            }
            if (item == "RETROACHIEVEMENTS" && ra && ra->setOn && ra->on)
                ra->setOn(ra->ud, ra->on(ra->ud) ? 0 : 1);
            else if (item == "ACHIEVEMENTS") ReturnToPausePage(P_LIST);
            else if (item == "LOG IN")
            {
                pausePage = P_LOGIN;
                pause.selected = raUser.empty() ? 0 : 1;
                raField = pause.selected;
                ForgetPass();
                typing = true;
                SDL_StartTextInput();
            }
            else if (item == "LOG OUT")
            {
                if (ra && ra->logout) ra->logout(ra->ud);
            }
            else if (item == "BACK")
                ReturnToPausePage(uiHost ? P_SETTINGS : P_ROOT, "RETROACHIEVEMENTS");
            return;
        }
#endif
        const int n = (int)pause.items.size();
        if (n <= 0) return;
        if (in.up) pause.selected = (pause.selected + n - 1) % n;
        if (in.down) pause.selected = (pause.selected + 1) % n;
        if (in.back || in.menu)
        {
            Resume();
            return;
        }
        if (pause.selected < 0 || pause.selected >= n) return;
        const std::string& item = pause.items[pause.selected].label;
        if (item == "FAST FORWARD" && (in.left || in.right || in.accept))
        {
            fastToggle = !fastToggle;
            pause.items[pause.selected].value = fastToggle ? "ON" : "OFF";
            set.values["fast_lock"] = fastToggle ? "1" : "0";
            set.Save(); set.ToHost("fast_lock");
            return;
        }
        if (!in.accept) return;
        if (item == "RESUME") Resume();
#ifdef GBAW_POKEDAISY
        else if (item == "COMPANION") { Resume(); companion.ToggleCompanion(core); }
#endif
        else if (item == "SAVE STATE")
        {
            SaveState();
            Resume();
        }
        else if (item == "LOAD STATE")
        {
            LoadState();
            Resume();
        }
        else if (item == "SETTINGS")
        {
#ifdef GBAW_EMBEDDED
            if (uiHost) ReturnToPausePage(P_SETTINGS);
            else
#endif
            {
                settingsBack = PLAY;
                screen = SETTINGS;
                settingsSel = 0;
            }
        }
#ifdef GBAW_EMBEDDED
        else if (item == "DISPLAY" || item == "SOUND" || item == "GAMEPLAY" || item == "CONTROLS")
            ReturnToPausePage(item == "DISPLAY" ? P_DISPLAY : item == "SOUND" ? P_SOUND : item == "GAMEPLAY" ? P_GAMEPLAY : P_CONTROLS);
        else if (item == "RETROACHIEVEMENTS") ReturnToPausePage(P_RA);
#endif
#if defined(GBAW_CHEATS) && defined(GBAW_EMBEDDED)
        else if (item == "CHEATS") ReturnToPausePage(P_CHEATS);
#endif
        else if (item == "RESET GAME")
        {
            core->reset(core);
            camera.Reset();
            companion.OnReset(core);
            if (DynamicWide1x() || ZoomOn()) { pk_wide_jump(core, frame240.data()); zoomShown = false; }
#ifdef GBAW_CHEATS
            cheats.Reset();
#endif
#ifdef GBAW_EMBEDDED
            if (raReady && ra && ra->reset) ra->reset(ra->ud);
#endif
            Resume();
        }
        else if (item == "QUIT GAME" || item == "QUIT")
            Stop();
    }

    void ListInput(const MenuIn& in)
    {
        const int n = (int)games.size(), cols = std::max(1, listCols);
        if (n)
        {
            if (in.left && listSel > 0) listSel--;
            if (in.right && listSel < n - 1) listSel++;
            if (in.up && listSel >= cols) listSel -= cols;
            if (in.down && listSel + cols < n) listSel += cols;
            if (in.accept) Start(games[listSel]);
        }
        if (in.settings)
        {
            settingsBack = LIST;
            screen = SETTINGS;
            settingsSel = 0;
        }
        if (in.folder) OpenFolder();
        if (in.escape) quit = true;
    }

    void OpenFolder()
    {
#if !defined(__SWITCH__)
        std::string url = "file://" + roms;
        SDL_OpenURL(url.c_str());
#endif
    }

    void SettingsInput(const MenuIn& in)
    {
        const auto& l = SettingsList();
        const int n = (int)l.size();
        if (in.up) settingsSel = (settingsSel + n - 1) % n;
        if (in.down) settingsSel = (settingsSel + 1) % n;
        if (in.left || in.right || in.accept)
        {
            const Setting& s = l[settingsSel];
            set.Step(s, in.left ? -1 : 1);
            if (!strcmp(s.key, "fullscreen"))
            {
                const bool want = set.Int("fullscreen");
                const bool full = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                if (want != full) SDL_SetWindowFullscreen(win, want ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            if (!strcmp(s.key, "volume")) SetVolume();
        }
        if (in.back || in.menu || in.settings) screen = settingsBack;
    }

    // -----------------------------------------------------------------------------------------
    // Drawing

    // COLOUR CORRECTION: each of the GBA's 32768 colours as a GBA's screen showed it (its dark,
    // washed-out screen, which the games' bright colours were made for): the mix of the GBA colour
    // shaders by Pokefan531, in linear light. In and out as the picture's own ABGR.
    void BuildColourLut()
    {
        colourLut.resize(32768);
        for (int i = 0; i < 32768; i++)
        {
            const double r = std::pow((i & 31) / 31.0, 2.2), g = std::pow(((i >> 5) & 31) / 31.0, 2.2),
                         b = std::pow(((i >> 10) & 31) / 31.0, 2.2);
            const double R = 0.82 * r + 0.24 * g - 0.06 * b, G = 0.125 * r + 0.665 * g + 0.21 * b, B = 0.195 * r + 0.075 * g + 0.73 * b;
            auto out = [](double v) { return (uint32_t)std::lround(std::pow(std::min(1.0, std::max(0.0, v * 0.94)), 1 / 2.2) * 255); };
            colourLut[i] = 0xFF000000u | out(B) << 16 | out(G) << 8 | out(R);
        }
    }

    // LCD EFFECT: a grid along the edges of the GBA's pixels, at the size the picture is drawn
    // (so its lines fall exactly between them), laid over the picture to darken.
    void DrawLcd(const SDL_Rect& dst, int w, int h = GbaH)
    {
        const double sx = (double)dst.w / w, sy = (double)dst.h / h;
        if (std::min(sx, sy) < 2) return; // (too small for a grid)
        if (!lcdTex || lcdW != dst.w || lcdH != dst.h || lcdCols != w || lcdRows != h)
        {
            if (lcdTex) SDL_DestroyTexture(lcdTex);
            lcdW = dst.w;
            lcdH = dst.h;
            lcdCols = w;
            lcdRows = h;
            const int t = std::max(1, (int)std::lround(std::min(sx, sy) / 5));
            std::vector<uint8_t> colEdge(lcdW), rowEdge(lcdH);
            for (int x = 0; x < lcdW; x++) colEdge[x] = (int)((int)(x / sx + 1e-9) * sx + sx) - x <= t;
            for (int y = 0; y < lcdH; y++) rowEdge[y] = (int)((int)(y / sy + 1e-9) * sy + sy) - y <= t;
            std::vector<uint32_t> px((size_t)lcdW * lcdH);
            for (int y = 0; y < lcdH; y++)
                for (int x = 0; x < lcdW; x++)
                {
                    const uint32_t v = rowEdge[y] || colEdge[x] ? 0xD4 : 0xFF;
                    px[(size_t)y * lcdW + x] = 0xFF000000u | v << 16 | v << 8 | v;
                }
            lcdTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, lcdW, lcdH);
            if (!lcdTex) return;
            SDL_UpdateTexture(lcdTex, nullptr, px.data(), lcdW * 4);
            SDL_SetTextureBlendMode(lcdTex, SDL_BLENDMODE_MOD);
            SDL_SetTextureScaleMode(lcdTex, SDL_ScaleModeNearest);
        }
        SDL_RenderCopy(ren, lcdTex, nullptr, &dst);
    }

    // The game's picture, as large as fits (SMOOTH SCALING, HD FILTER, LCD EFFECT and COLOUR
    // CORRECTION decide how).
#if defined(__SWITCH__)
    static constexpr int HdFactor = 3; // (720p or 1080p: 3 is plenty, and a third quicker)
#else
    static constexpr int HdFactor = 4;
#endif
    void DrawZoom()
    {
        const PkWideCamera::Window& w = zoomWin;
        const bool hd = set.Get("hd_filter") == "1", colour = set.Get("colour") == "1", lcd = set.Get("lcd") == "1";
        const SDL_Rect src = { 0, 0, w.w, w.h };
        const SDL_Rect dst = { (int)std::floor(w.offX), (int)std::floor(w.offY), (int)std::ceil(w.w * w.scale), (int)std::ceil(w.h * w.scale) };
        if (hd || colour)
        {
            if (colour && colourLut.empty()) BuildColourLut();
            fxSrc.resize((size_t)w.w * w.h);
            for (size_t i = 0; i < fxSrc.size(); i++)
            {
                uint32_t p = zoomBuf[i]; // (ABGR: R in the low byte)
                if (colour) p = colourLut[((p >> 3) & 31) | (((p >> 11) & 31) << 5) | (((p >> 19) & 31) << 10)];
                if (hd) p = (p & 0xFF00FF00u) | (p & 0xFF) << 16 | ((p >> 16) & 0xFF);
                fxSrc[i] = p;
            }
        }
        if (hd)
        {
            const int f = std::max(2, std::min(HdFactor, (int)std::ceil(w.scale - 0.01)));
            const int ow = w.w * f, oh = w.h * f;
            if (!workersStarted)
            {
                workersStarted = true;
#if defined(__SWITCH__)
                workers.Start(2);
#else
                const unsigned cores = std::thread::hardware_concurrency();
                workers.Start((int)std::min(3u, cores > 1 ? cores - 1 : 0u));
#endif
            }
            fxOut.resize((size_t)ow * oh);
            const int slices = 8;
            workers.Run(slices, [&](int i) {
                xbrz::scale(f, fxSrc.data(), fxOut.data(), w.w, w.h, xbrz::ColorFormat::RGB, xbrz::ScalerCfg(),
                            w.h * i / slices, w.h * (i + 1) / slices);
            });
            if (!zoomHdTex || zoomHdW < ow || zoomHdH < oh)
            {
                if (zoomHdTex) SDL_DestroyTexture(zoomHdTex);
                zoomHdW = std::max(zoomHdW, ow);
                zoomHdH = std::max(zoomHdH, oh);
                zoomHdTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, zoomHdW, zoomHdH);
                if (zoomHdTex) SDL_SetTextureScaleMode(zoomHdTex, SDL_ScaleModeLinear);
            }
            if (zoomHdTex)
            {
                const SDL_Rect r = { 0, 0, ow, oh };
                SDL_UpdateTexture(zoomHdTex, &r, fxOut.data(), ow * 4);
                SDL_RenderCopy(ren, zoomHdTex, &r, &dst);
                if (lcd) DrawLcd(dst, w.w, w.h);
                return;
            }
        }
        if (!zoomTex)
        {
            zoomTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                        PkWideCamera::Camera::MaxW, PkWideCamera::Camera::MaxH);
            if (!zoomTex) return;
        }
        SDL_UpdateTexture(zoomTex, &src, colour ? fxSrc.data() : zoomBuf.data(), w.w * 4);
        SDL_SetTextureScaleMode(zoomTex, set.Get("scaling") == "smooth" ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        SDL_RenderCopy(ren, zoomTex, &src, &dst);
        if (lcd) DrawLcd(dst, w.w, w.h);
    }

    void DrawGame()
    {
        if (zoomRefresh)
        {
            zoomRefresh = false;
            ZoomFrame(true);
        }
        if (zoomShown && zoomWin.valid)
        {
            DrawZoom();
            return;
        }
        const int cw = CoreW(), ch = CoreH();
        const int w = cw + 2 * (game.isGb ? 0 : wideExtra);
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        const std::string scaling = set.Get("scaling");
        const bool hd = set.Get("hd_filter") == "1", colour = set.Get("colour") == "1", lcd = set.Get("lcd") == "1";
        double scale = std::min((double)ow / w, (double)oh / ch);
        if (scaling == "whole") scale = std::max(1.0, std::floor(scale));
        const int dw = (int)lround(w * scale), dh = (int)lround(ch * scale);
        const SDL_Rect dst = { (ow - dw) / 2, (oh - dh) / 2, dw, dh };
        const SDL_Rect src = { 0, 0, w, ch };
        if (hd || colour)
        {
            // The picture's w columns, colour corrected; for xBRZ as ARGB (it weighs the channels).
            if (colour && colourLut.empty()) BuildColourLut();
            fxSrc.resize((size_t)w * ch);
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < w; x++)
                {
                    uint32_t p = video[(size_t)y * MaxW + x];
                    if (colour) p = colourLut[((p >> 3) & 31) | (((p >> 11) & 31) << 5) | (((p >> 19) & 31) << 10)];
                    if (hd) p = (p & 0xFF00FF00u) | (p & 0xFF) << 16 | ((p >> 16) & 0xFF);
                    fxSrc[(size_t)y * w + x] = p;
                }
        }
        if (hd)
        {
            if (!workersStarted)
            {
                workersStarted = true;
#if defined(__SWITCH__)
                workers.Start(2); // (on cores 1 and 2: see Loop)
#else
                const unsigned cores = std::thread::hardware_concurrency();
                workers.Start((int)std::min(3u, cores > 1 ? cores - 1 : 0u));
#endif
            }
            fxOut.resize((size_t)w * HdFactor * ch * HdFactor);
            const int slices = 8;
            workers.Run(slices, [&](int i) {
                xbrz::scale(HdFactor, fxSrc.data(), fxOut.data(), w, ch, xbrz::ColorFormat::RGB, xbrz::ScalerCfg(),
                            ch * i / slices, ch * (i + 1) / slices);
            });
            if (!hdTex)
            {
                hdTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, MaxW * HdFactor, GbaH * HdFactor);
                if (hdTex) SDL_SetTextureScaleMode(hdTex, SDL_ScaleModeLinear);
            }
            if (hdTex)
            {
                const SDL_Rect r = { 0, 0, w * HdFactor, ch * HdFactor };
                SDL_UpdateTexture(hdTex, &r, fxOut.data(), w * HdFactor * 4);
                SDL_RenderCopy(ren, hdTex, &r, &dst);
                if (lcd) DrawLcd(dst, w, ch);
                return;
            }
        }
        if (colour) SDL_UpdateTexture(gameTex, &src, fxSrc.data(), w * 4);
        else SDL_UpdateTexture(gameTex, nullptr, video.data(), MaxW * 4);
        if (scaling == "sharp" && scale > 1)
        {
            // Sharp: first a whole-number zoom with square pixels, then smoothly to the screen,
            // so every pixel is the same size and the edges stay crisp.
            int n = (int)std::ceil(scale);
            while (n > 1 && MaxW * n > maxTex) n--;
            if (!bigTex || bigW != MaxW * n || bigH != GbaH * n)
            {
                if (bigTex) SDL_DestroyTexture(bigTex);
                bigW = MaxW * n;
                bigH = GbaH * n;
                bigTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, bigW, bigH);
                if (bigTex) SDL_SetTextureScaleMode(bigTex, SDL_ScaleModeLinear);
            }
            if (bigTex)
            {
                SDL_SetRenderTarget(ren, bigTex);
                const SDL_Rect bd = { 0, 0, w * n, ch * n };
                SDL_RenderCopy(ren, gameTex, &src, &bd);
                SDL_SetRenderTarget(ren, nullptr);
                SDL_RenderCopy(ren, bigTex, &bd, &dst);
                if (lcd) DrawLcd(dst, w, ch);
                return;
            }
        }
        SDL_SetTextureScaleMode(gameTex, scaling == "smooth" ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        SDL_RenderCopy(ren, gameTex, &src, &dst);
        if (lcd) DrawLcd(dst, w);
    }

    std::string Hint(const char* keys, const char* pad, const char* touch = nullptr) const
    {
        (void)touch;
        return padLast ? pad : keys;
    }

    void DrawList(Canvas& c)
    {
        c.Fill(0, 0, c.w, c.h, Colour::Back);
        c.Fill(0, 0, c.w, 26, Colour::Band);
        c.Fill(0, 26, c.w, 1, Colour::BandLine);
        c.TextCentred(c.w / 2, 6, "POKEMON GBA WIDESCREEN", 2, Colour::Title);
        if (games.empty())
        {
            const std::vector<std::string> lines = {
                "NO GAMES YET",
                "",
                "PUT YOUR ROMS IN THE ROMS FOLDER (ANY NAMES):",
                "  POKEMON EMERALD (US)",
                "  POKEMON FIRERED OR LEAFGREEN (US, VERSION 1.0 OR 1.1)",
                "  POKEMON ULTRA VIOLET 1.22: ITS .GBA, OR ITS .IPS",
                "  WITH FIRERED 1.0 BESIDE IT",
                "",
                "THEN PRESS R (OR A CONTROLLER'S X) TO LOOK AGAIN.",
            };
            int y = 60;
            for (const std::string& l : lines)
            {
                c.Text(40, y, l, 1, l == "NO GAMES YET" ? Colour::Bright : Colour::Text);
                y += 12;
            }
            c.Text(40, y + 8, roms.size() > 90 ? "..." + roms.substr(roms.size() - 87) : roms, 1, Colour::Hint);
        }
        else
        {
            // (Square art, as the DS launcher's; as many across as fit: four on a 16:9 screen.)
            const int tileW = 140, artH = 140, tileH = artH + 52, gap = 12;
            const int cols = std::max(1, std::min((int)games.size(), (c.w - 4 + gap) / (tileW + gap)));
            listCols = cols;
            const int rowsFit = std::max(1, (c.h - 80) / (tileH + gap));
            const int row = listSel / cols;
            const int firstRow = std::max(0, row - rowsFit + 1);
            const int shownCols = std::min(cols, (int)games.size());
            const int x0 = (c.w - (shownCols * tileW + (shownCols - 1) * gap)) / 2;
            // (In the middle of the page when they all fit.)
            const int rows = std::min(rowsFit, ((int)games.size() + cols - 1) / cols);
            const int y0 = std::max(40, 27 + (c.h - 27 - 40 - (rows * tileH + (rows - 1) * gap)) / 2);
            for (int i = firstRow * cols; i < (int)games.size() && i / cols < firstRow + rowsFit; i++)
            {
                const Games::Game& g = games[i];
                const int x = x0 + (i % cols) * (tileW + gap), y = y0 + (i / cols - firstRow) * (tileH + gap);
                const bool sel = i == listSel;
                if (sel) c.Frame(x, y, tileW, tileH, 2, Colour::Bright);
                tapRects.push_back({ { x, y, tileW, tileH }, i });
                c.Fill(x, y, tileW, tileH, sel ? 0xFF22304C : 0xFF141C2C);
                // The game's art (art/, drawn over the canvas at the screen's resolution), or its
                // colour with a gem in it.
                const std::string art = g.patch.empty() ? "" : Games::ArtCode(g.sha1);
                if (!art.empty() && Art(art))
                    artSpots.push_back({ { x, y, tileW, artH }, art, sel });
                else
                {
                    const uint32_t col = g.patch.empty() ? 0xFF505A70 : g.colour;
                    c.Fill(x, y, tileW, artH, col);
                    for (int r = 0; r < 14; r++)
                    {
                        const int half = r < 7 ? r + 1 : 14 - r;
                        c.Fill(x + tileW / 2 - half * 4, y + artH / 2 - 28 + r * 4, half * 8, 4, r < 7 ? 0xC0FFFFFF : 0x80FFFFFF);
                    }
                }
                std::vector<std::string> name = Wrap(g.name, (tileW - 12) / 6);
                if (name.size() > 1) name.resize(1);
                int ty = y + artH + 8;
                for (const std::string& l : name)
                {
                    c.TextCentred(x + tileW / 2, ty, l, 1, sel ? Colour::Bright : Colour::Text);
                    ty += 10;
                }
                std::string status = g.patch.empty() ? "NO WIDESCREEN" : g.forced ? "WIDESCREEN UNTESTED" : g.hasSave ? "SAVE FOUND" : "NEW GAME";
                c.TextCentred(x + tileW / 2, y + tileH - 30, status, 1, g.patch.empty() || g.forced ? Colour::Warn : g.hasSave ? 0xFF7EE08A : Colour::Hint);
                std::string file = g.fileName;
                if ((int)file.size() * 6 > tileW - 8) file = file.substr(0, (tileW - 8) / 6 - 3) + "...";
                c.TextCentred(x + tileW / 2, y + tileH - 16, file, 1, 0xFF5E6C88);
            }
            // (More games above or below the ones shown.)
            const int allRows = ((int)games.size() + cols - 1) / cols;
            if (firstRow > 0) c.TextCentred(c.w / 2, y0 - 12, "MORE ABOVE", 1, Colour::Hint);
            if (firstRow + rowsFit < allRows) c.TextCentred(c.w / 2, y0 + rows * (tileH + gap) - gap + 4, "MORE BELOW", 1, Colour::Hint);
            if (games[listSel].patch.empty() && !games[listSel].why.empty())
                c.TextCentred(c.w / 2, c.h - 30, "NO WIDESCREEN: " + games[listSel].why, 1, Colour::Warn);
            else if (games[listSel].forced)
                c.TextCentred(c.w / 2, c.h - 30, "ROM HACK: WIDESCREEN TRIED ANYWAY (" + games[listSel].why + ")", 1, Colour::Warn);
        }
        c.TextCentred(c.w / 2, c.h - 14,
                      Hint("ENTER: PLAY   H: SETTINGS   O: ROMS FOLDER   R: LOOK AGAIN   F11: WINDOW   ESC: QUIT",
#if defined(__SWITCH__)
                           "A: PLAY   Y: SETTINGS   X: LOOK AGAIN   +: QUIT",
#else
                           "A: PLAY   Y: SETTINGS   X: LOOK AGAIN",
#endif
                           "TAP A GAME TO PLAY IT"),
                      1, Colour::Hint);
    }

    void DrawSettings(Canvas& c)
    {
        c.Fill(0, 0, c.w, c.h, settingsBack == PLAY ? 0xE00B0F18 : Colour::Back);
        c.Fill(0, 0, c.w, 26, Colour::Band);
        c.Fill(0, 26, c.w, 1, Colour::BandLine);
        c.TextCentred(c.w / 2, 6, "SETTINGS", 2, Colour::Title);
        const int width = std::min(c.w - 40, 560), x = (c.w - width) / 2;
        const auto& l = SettingsList();
        // (The chosen option and the lines under it are kept on the page.)
        struct Line { std::string text, value; int item; bool name; };
        std::vector<Line> out;
        int selFirst = 0, selLast = 0;
        for (int i = 0; i < (int)l.size(); i++)
        {
            if (i == settingsSel) selFirst = (int)out.size();
            out.push_back({ l[i].name, set.Shown(l[i]), i, true });
            for (const std::string& t : Wrap(l[i].text, width / 6 - 4)) out.push_back({ t, "", i, false });
            if (i == settingsSel) selLast = (int)out.size() - 1;
            out.push_back({ "", "", -1, false });
        }
        const int top = 36, bottom = c.h - 22, lineH = 10;
        const int fit = std::max(1, (bottom - top) / lineH);
        static int scroll = 0;
        if (selFirst < scroll) scroll = selFirst;
        if (selLast >= scroll + fit) scroll = selLast - fit + 1;
        scroll = std::max(0, std::min(scroll, (int)out.size() - fit));
        int y = top;
        for (int i = scroll; i < (int)out.size() && y + lineH <= bottom; i++, y += lineH)
        {
            const Line& ln = out[i];
            const bool sel = ln.item == settingsSel;
            if (ln.item >= 0) tapRects.push_back({ { x - 4, y - 2, width + 8, lineH }, ln.item });
            if (ln.name)
            {
                if (sel) c.Fill(x - 4, y - 2, width + 8, lineH, Colour::Chosen);
                c.Text(x, y, ln.text, 1, sel ? Colour::Bright : Colour::Text);
                const std::string v = sel ? "< " + ln.value + " >" : ln.value;
                c.Text(x + width - Canvas::TextWidth(v, 1), y, v, 1, sel ? Colour::Bright : Colour::Title);
            }
            else if (!ln.text.empty())
                c.Text(x + 12, y, ln.text, 1, sel ? Colour::Text : Colour::Hint);
        }
        c.TextCentred(c.w / 2, c.h - 14, Hint("UP/DOWN: CHOOSE   LEFT/RIGHT: CHANGE   ESC: BACK", "D-PAD: CHOOSE AND CHANGE   B: BACK", "TAP TO CHOOSE, AGAIN TO CHANGE"), 1, Colour::Hint);
    }

    void Draw()
    {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (core) DrawGame();

        // The menus, at about 360 lines whatever the window's size (and at least 600 wide, for
        // the game list); on a phone about 300, so they are bigger under a finger.
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        if (core && screen == PLAY && !paused)
            companion.Draw(ren, ow, oh, CompanionActions());
        int lines = 360;
        if (testMode && getenv("GBAW_TEST_LINES")) lines = std::max(100, atoi(getenv("GBAW_TEST_LINES"))); // (a phone's, on a PC)
        const int k = std::max(1, std::min(oh / lines, ow / 600));
        ui.Resize(std::max(1, ow / k), std::max(1, oh / k));
        ui.Clear();
        uiScale = k;
        uiOffX = (ow - ui.w * k) / 2;
        uiOffY = (oh - ui.h * k) / 2;
        tapRects.clear();
        artSpots.clear();
        backRect = UiRect();
        settingsRect = UiRect();
        bool any = true;
        const Uint32 now = SDL_GetTicks();
        const bool wantFps = set.Int("show_fps") && screen == PLAY && !paused;
        if (wantFps && now - fpsSince >= 1000)
        {
            fpsShown = (int)lround(fpsFrames * 1000.0 / std::max<Uint32>(1, now - fpsSince));
            fpsFrames = 0;
            fpsSince = now;
        }
        const bool showToast = !toast.empty() && now < toastUntil;
#ifdef GBAW_EMBEDDED
        usingHostMenu = false;
        const bool useHost = uiHost && uiHost->drawOverlay && screen == PLAY && (paused || showToast || wantFps);
        if (screen == LIST) DrawList(ui);
        else if (screen == SETTINGS) DrawSettings(ui);
        else if (useHost && paused) usingHostMenu = true;
        else if (paused)
#else
        if (screen == LIST) DrawList(ui);
        else if (screen == SETTINGS) DrawSettings(ui);
        else if (paused)
#endif
        {
            std::string title = game.name;
            std::string hint = Hint("ENTER: CHOOSE   ESC: BACK TO THE GAME", "A: CHOOSE   B: BACK TO THE GAME", "TAP TO CHOOSE, OUTSIDE TO GO BACK");
#ifdef GBAW_EMBEDDED
            if (pausePage == P_SETTINGS)
                title = "SETTINGS";
            else if (pausePage == P_RA)
            {
                title = "RETROACHIEVEMENTS";
                char detail[80] = {};
                if (ra && ra->detail && ra->detail(ra->ud, detail, sizeof detail) && detail[0]) hint = detail;
                else hint = Hint("LEFT/RIGHT: ON OR OFF   ESC: BACK", "LEFT/RIGHT: ON OR OFF   B: BACK", "TAP TO CHANGE");
            }
            else if (pausePage == P_LIST)
            {
                title = "ACHIEVEMENTS";
                if (pause.selected >= 0 && pause.selected < (int)achDesc.size() && !achDesc[pause.selected].empty())
                    hint = achDesc[pause.selected];
            }
#ifdef GBAW_CHEATS
            else if (pausePage == P_CHEATS)
            {
                title = "CHEATS";
                const int i = pause.selected;
                if (i >= 0 && i < GbaCheats::COUNT)
                    hint = cheats.Has(i) ? GbaCheats::About(i).note : "NOT FOR THIS VERSION OF THE GAME";
            }
#endif
            else if (pausePage == P_DISPLAY || pausePage == P_SOUND || pausePage == P_GAMEPLAY || pausePage == P_CONTROLS)
            {
                title = pausePage == P_DISPLAY ? "GRAPHICS" : pausePage == P_SOUND ? "SOUND" : pausePage == P_GAMEPLAY ? "GAMEPLAY" : "CONTROLS";
                hint = Hint("ARROWS: CHANGE   ESC: BACK", "D-PAD: CHOOSE AND CHANGE   B: BACK", "TAP TO CHANGE");
            }
            else if (pausePage == P_LOGIN)
            {
                title = "LOG IN";
                hint = Hint("TYPE, THEN ENTER   ESC: CANCEL", "TYPE WITH THE KEYBOARD   B: CANCEL", "TYPE, THEN TAP LOG IN");
            }
#endif
            pause.Draw(ui, title, hint, false);
#ifdef GBAW_EMBEDDED
            // The chosen setting's explanation, under the menu (as the settings screen has it).
            const int sel = pause.selected;
            if ((pausePage == P_DISPLAY || pausePage == P_SOUND || pausePage == P_GAMEPLAY || pausePage == P_CONTROLS) && sel >= 0 && sel < (int)pageSettings.size())
            {
                const UiRect& b = pause.box;
                const std::vector<std::string> lines = Wrap(pageSettings[sel]->text, std::max(10, (b.w - 12) / 6));
                const int h = (int)lines.size() * 10 + 6;
                const int y = std::min(b.y + b.h + 4, ui.h - h - 2);
                ui.Frame(b.x, y, b.w, h, 1, Colour::BoxEdge);
                ui.Fill(b.x, y, b.w, h, Colour::Box);
                for (size_t l = 0; l < lines.size(); l++) ui.TextCentred(b.x + b.w / 2, y + 4 + (int)l * 10, lines[l], 1, Colour::Hint);
            }
#endif
        }
        else any = false;
#ifdef GBAW_EMBEDDED
        if (useHost)
        {
            if (!paused) any = false;
            std::string title = "NoSplitNX";
            std::string hint = padLast ? "A: SELECT   B: BACK" : "ENTER: SELECT   ESC: BACK";
            std::string note;
            if (paused)
            {
                const int sel = pause.selected;
                const std::string selLabel = (sel >= 0 && sel < (int)pause.items.size()) ? pause.items[sel].label : "";
                if (pausePage == P_ROOT)
                {
                    if (selLabel == "SETTINGS") hint = "AUDIO, VIDEO AND SYSTEM OPTIONS";
                    else if (selLabel == "CHEATS") hint = "ACTION REPLAY CODES";
                }
                else if (pausePage == P_SETTINGS)
                {
                    title = "SETTINGS";
                    if (selLabel == "RETROACHIEVEMENTS") hint = "FROM RETROACHIEVEMENTS.ORG";
                }
                else if (pausePage == P_DISPLAY || pausePage == P_SOUND || pausePage == P_GAMEPLAY || pausePage == P_CONTROLS)
                {
                    title = pausePage == P_DISPLAY ? "GRAPHICS" : pausePage == P_SOUND ? "SOUND" : pausePage == P_GAMEPLAY ? "GAMEPLAY" : "CONTROLS";
                    hint = padLast ? "A: CHANGE   B: BACK" : "ENTER: CHANGE   ESC: BACK";
                    if (sel >= 0 && sel < (int)pageSettings.size()) note = pageSettings[sel]->text;
                }
#ifdef GBAW_CHEATS
                else if (pausePage == P_CHEATS)
                {
                    title = "CHEATS";
                    hint = padLast ? "A: ON / OFF   B: BACK" : "ENTER: ON / OFF   ESC: BACK";
                    if (sel >= 0 && sel < GbaCheats::COUNT)
                        note = cheats.Has(sel) ? GbaCheats::About(sel).note : "NOT FOR THIS VERSION OF THE GAME";
                }
#endif
                else if (pausePage == P_RA)
                {
                    title = "RETROACHIEVEMENTS";
                    char detail[80] = {};
                    if (ra && ra->detail && ra->detail(ra->ud, detail, sizeof detail) && detail[0]) hint = detail;
                }
                else if (pausePage == P_LIST)
                {
                    title = "ACHIEVEMENTS";
                    if (sel >= 0 && sel < (int)achDesc.size()) note = achDesc[sel];
                }
                else if (pausePage == P_LOGIN)
                {
                    title = "LOG IN";
                    hint = padLast ? "B: CANCEL" : "ENTER: NEXT   ESC: CANCEL";
                }
            }
            const int toastAlpha = !showToast ? 0 : (toastUntil - now >= 400 ? 255 : (int)((toastUntil - now) * 255 / 400));
            const std::string accent = !hostAccent.empty() ? hostAccent : set.HostValue("list_colour");
            const std::string langVal = set.Get("language");
            std::vector<GbaWide::MenuHostItem> hostItems;
            if (paused)
            {
                hostItems.reserve(pause.items.size());
                for (const MenuItem& it : pause.items)
                {
                    int sl = -1;
                    if (pausePage == P_SOUND && it.label == "VOLUME")
                        sl = std::max(0, std::min(10, set.Int("volume") / 10));
                    hostItems.push_back({ it.label, it.value, it.fixed, sl });
                }
            }
            uint64_t sig = 14695981039346656037ULL;
            auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ULL; };
            auto mixStr = [&](const std::string& s) {
                mix((uint64_t)s.size());
                for (unsigned char ch : s) mix(ch);
            };
            mix((uint64_t)(unsigned)ow);
            mix((uint64_t)(unsigned)oh);
            mix(paused ? 1ULL : 0ULL);
            mix((uint64_t)pausePage);
            mix((uint64_t)pause.selected);
            mix((uint64_t)hostMenu.scroll);
            mix((uint64_t)toastAlpha);
            mix(wantFps ? (uint64_t)(unsigned)(fpsShown + 1) : 0ULL);
            mixStr(accent);
            mixStr(langVal);
            mixStr(showToast ? toast : std::string());
            if (paused)
            {
                mixStr(title);
                mixStr(hint);
                mixStr(note);
                mix((uint64_t)hostItems.size());
                for (const GbaWide::MenuHostItem& it : hostItems)
                {
                    mixStr(it.label);
                    mixStr(it.value);
                    mix((it.info ? 0x100ULL : 0ULL) | (uint64_t)(unsigned)(it.slider + 2));
                }
            }
            if (!sig) sig = 1;
            if (sig != builtHostSig || hostTexW != ow || hostTexH != oh)
            {
                uiHost->drawOverlay(uiHost->ud, hostPx, ow, oh, paused, hostItems, pause.selected,
                                    title, hint, note, showToast ? toast : std::string(), toastAlpha,
                                    fpsShown, wantFps, accent, hostMenu);
                if (!hostTex || hostTexW != ow || hostTexH != oh)
                {
                    if (hostTex) SDL_DestroyTexture(hostTex);
                    hostTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ow, oh);
                    hostTexW = ow;
                    hostTexH = oh;
                    if (hostTex) SDL_SetTextureBlendMode(hostTex, SDL_BLENDMODE_BLEND);
                }
                if (hostTex && hostPx.size() == (size_t)ow * (size_t)oh)
                    SDL_UpdateTexture(hostTex, nullptr, hostPx.data(), ow * 4);
                builtHostSig = sig;
            }
        }
        else builtHostSig = 0;
#endif
        if (wantFps
#ifdef GBAW_EMBEDDED
            && !useHost
#endif
        )
        {
            const std::string f = std::to_string(fpsShown) + " FPS";
            int fpsX = 2, fpsY = 2;
            ui.Fill(fpsX, fpsY, Canvas::TextWidth(f, 1) + 6, 11, 0xA0000000);
            ui.Text(fpsX + 3, fpsY + 2, f, 1, Colour::Bright);
            any = true;
        }
        if (showToast
#ifdef GBAW_EMBEDDED
            && !useHost
#endif
        )
        {
            const std::vector<std::string> lines = Wrap(toast, std::max(10, (ui.w - 40) / 6));
            int tw = 0;
            for (const std::string& l : lines) tw = std::max(tw, Canvas::TextWidth(l, 1));
            const int th = (int)lines.size() * 10 + 8, ty = ui.h - th - 28;
            ui.Fill((ui.w - tw) / 2 - 6, ty, tw + 12, th, 0xE0101828);
            int y = ty + 5;
            for (const std::string& l : lines)
            {
                ui.TextCentred(ui.w / 2, y, l, 1, Colour::Bright);
                y += 10;
            }
            any = true;
        }
        if (any)
        {
            if (!uiTex || uiTexW != ui.w || uiTexH != ui.h)
            {
                if (uiTex) SDL_DestroyTexture(uiTex);
                uiTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ui.w, ui.h);
                uiTexW = ui.w;
                uiTexH = ui.h;
                if (uiTex)
                {
                    SDL_SetTextureBlendMode(uiTex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(uiTex, SDL_ScaleModeNearest);
                }
            }
            if (uiTex)
            {
                SDL_UpdateTexture(uiTex, nullptr, ui.px.data(), ui.w * 4);
                const SDL_Rect dst = { (ow - ui.w * k) / 2, (oh - ui.h * k) / 2, ui.w * k, ui.h * k };
                SDL_RenderCopy(ren, uiTex, nullptr, &dst);
            }
            for (const ArtSpot& a : artSpots)
                if (SDL_Texture* t = Art(a.code))
                {
                    // (The chosen game's at full brightness, the others a little dimmer.)
                    const Uint8 m = a.chosen ? 255 : 170;
                    SDL_SetTextureColorMod(t, m, m, m);
                    const SDL_Rect dst = { uiOffX + a.at.x * k, uiOffY + a.at.y * k, a.at.w * k, a.at.h * k };
                    SDL_RenderCopy(ren, t, nullptr, &dst);
                }
        }
#ifdef GBAW_EMBEDDED
        if (useHost && hostTex) SDL_RenderCopy(ren, hostTex, nullptr, nullptr);
#endif
    }

    // A game's art as a texture, read from the built-in JPEG the first time (nullptr if none).
    SDL_Texture* Art(const std::string& code)
    {
        if (code.empty()) return nullptr;
        auto it = artTex.find(code);
        if (it != artTex.end()) return it->second;
        SDL_Texture* t = nullptr;
        for (const WideArt& a : kWideArt)
            if (a.jpg && code == a.code)
            {
                int w = 0, h = 0, n = 0;
                if (unsigned char* px = stbi_load_from_memory(a.jpg, (int)a.size, &w, &h, &n, 4))
                {
                    t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
                    if (t)
                    {
                        SDL_UpdateTexture(t, nullptr, px, w * 4);
                        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
                    }
                    stbi_image_free(px);
                }
            }
        artTex[code] = t;
        return t;
    }

    void Shot(const std::string& file)
    {
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!s) return;
        if (!SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch)) SDL_SaveBMP(s, file.c_str());
        SDL_FreeSurface(s);
        printf("shot %lld %s %dx%d extra %d\n", ScriptClock(), file.c_str(), ow, oh, wideExtra);
    }

    // -----------------------------------------------------------------------------------------
    // Events

    void Events(MenuIn& in)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
#ifdef GBAW_EMBEDDED
            case SDL_APP_WILLENTERBACKGROUND:
            case SDL_APP_TERMINATING:
                SaveAutomatic();
                break;
#endif
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                const bool down = e.type == SDL_KEYDOWN;
                const SDL_Scancode sc = e.key.keysym.scancode;
                if (sc < SDL_NUM_SCANCODES) key[sc] = down;
                if (!down) break;
                padLast = false;
                switch (sc)
                {
                case SDL_SCANCODE_AC_BACK: // (Android's back button or gesture)
                    if (!e.key.repeat) (screen == LIST ? in.escape : screen == PLAY && !paused ? in.menu : in.back) = true;
                    break;
                case SDL_SCANCODE_UP: in.up = true; break;
                case SDL_SCANCODE_DOWN: in.down = true; break;
                case SDL_SCANCODE_LEFT: in.left = true; break;
                case SDL_SCANCODE_RIGHT: in.right = true; break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                case SDL_SCANCODE_X:
                    if (!e.key.repeat) in.accept = true;
                    break;
                case SDL_SCANCODE_Z:
                    if (!e.key.repeat) in.back = true;
                    break;
                case SDL_SCANCODE_BACKSPACE:
                    if (!e.key.repeat)
                    {
#ifdef GBAW_EMBEDDED
                        if (typing) in.erase = true;
                        else
#endif
                            in.back = true;
                    }
                    break;
                case SDL_SCANCODE_ESCAPE:
                    if (!e.key.repeat) (screen == LIST ? in.escape : in.menu) = true;
                    break;
                case SDL_SCANCODE_H: in.settings = true; break;
                case SDL_SCANCODE_O: in.folder = true; break;
                case SDL_SCANCODE_R:
                    if (screen == LIST) Rescan();
                    break;
                case SDL_SCANCODE_F11: ToggleFullscreen(); break;
                case SDL_SCANCODE_C:
                    if (!e.key.repeat && screen == PLAY && !paused && !companion.TakesInput()) ZoomCycle();
                    break;
                case SDL_SCANCODE_TAB:
                    if (!e.key.repeat && screen == PLAY && !paused) in.toggleCompanion = true;
                    break;
                case SDL_SCANCODE_F:
                    if (!e.key.repeat && screen == PLAY && !paused) in.toggleQuick = true;
                    break;
                case SDL_SCANCODE_Q:
                    if (!e.key.repeat && companion.TakesInput()) in.pageL = true;
                    break;
                case SDL_SCANCODE_E:
                    if (!e.key.repeat && companion.TakesInput()) in.pageR = true;
                    break;
                default: break;
                }
                break;
            }
            case SDL_TEXTINPUT:
#ifdef GBAW_EMBEDDED
                if (typing)
                {
                    std::string& field = raField == 0 ? raUser : raPass;
                    field += e.text.text;
                    if (field.size() > 128) field.resize(128);
                }
#endif
                break;
            case SDL_CONTROLLERDEVICEADDED:
                pads.Open(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                pads.Close(e.cdevice.which);
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP:
                Finger(e.tfinger, in);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID &&
                    (screen != PLAY || paused))
                {
                    int ww = 0, wh = 0;
                    SDL_GetWindowSize(win, &ww, &wh);
                    if (ww > 0 && wh > 0)
                    {
                        SDL_TouchFingerEvent tap{};
                        tap.type = SDL_FINGERDOWN;
                        tap.x = float(e.button.x) / ww;
                        tap.y = float(e.button.y) / wh;
                        Finger(tap, in, false);
                    }
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            {
                padLast = true;
                const bool byLabel = ByLabel();
                const int b = Labelled(e.cbutton.button);
                if (b == (byLabel ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B)) in.accept = true;
                if (b == (byLabel ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A)) in.back = true;
                if (b == SDL_CONTROLLER_BUTTON_Y && screen == LIST) in.settings = true;
                if (b == SDL_CONTROLLER_BUTTON_X && screen == LIST) Rescan();
#if defined(__SWITCH__)
                // (+ leaves the program from the game list, as Switch homebrew does.)
                if (b == SDL_CONTROLLER_BUTTON_START && screen == LIST) in.escape = true;
#endif
                if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER && companion.TakesInput()) in.pageL = true;
                if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && companion.TakesInput()) in.pageR = true;
                // L3 opens PokeDaisy Quick Menu; R3 cycles Zoom/Look-Around; Guide opens Pause.
                if (b == SDL_CONTROLLER_BUTTON_LEFTSTICK && screen == PLAY && !paused && companion.active)
                    in.toggleQuick = true;
                else if (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK && screen == PLAY && !paused && !companion.TakesInput() && ZoomOn())
                    ZoomCycle();
                else if (b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_LEFTSTICK || b == SDL_CONTROLLER_BUTTON_RIGHTSTICK)
                    in.menu = true;
                if (screen == PLAY)
                {
                    const bool startIsGba = set.Get("gba_start") == "start";
                    const Uint32 now = SDL_GetTicks();
                    if (b == SDL_CONTROLLER_BUTTON_START)
                    {
                        plusHeld = true;
                        plusAt = now;
                    }
                    if (b == SDL_CONTROLLER_BUTTON_BACK) minusAt = now;
                    if ((b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK) && !comboOn &&
                        pads.Button(SDL_CONTROLLER_BUTTON_START) && pads.Button(SDL_CONTROLLER_BUTTON_BACK))
                    {
                        comboOn = plusCombo = true;
                        if (startIsGba) in.menu = true;
                    }
                }
                break;
            }
            case SDL_CONTROLLERBUTTONUP:
            {
                const int b = Labelled(e.cbutton.button);
                const Uint32 now = SDL_GetTicks();
                if (b == SDL_CONTROLLER_BUTTON_START && plusHeld)
                {
                    if (screen == PLAY && !plusCombo && set.Get("gba_start") != "start") in.menu = true;
                    if (!comboOn && now - plusAt < 150) plusPulse = now + 150;
                    plusHeld = plusCombo = false;
                }
                if (b == SDL_CONTROLLER_BUTTON_BACK && !comboOn && now - minusAt < 150) minusPulse = now + 150;
                if ((b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_BACK) &&
                    !pads.Button(SDL_CONTROLLER_BUTTON_START) && !pads.Button(SDL_CONTROLLER_BUTTON_BACK))
                    comboOn = false;
                break;
            }
            case SDL_CONTROLLERAXISMOTION:
                // ZL toggles PokeDaisy Companion (while SELECT+ZL or L+ZL holds Rewind); falls back to Pause if Companion inactive.
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
                {
                    const bool held = e.caxis.value > 16000;
                    if (held && !leftTrigger)
                    {
                        padLast = true;
                        const bool rewindCombo = pads.Button(SDL_CONTROLLER_BUTTON_BACK) || pads.Button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
                        if (!rewindCombo)
                        {
                            if (screen == PLAY && !paused && companion.active) in.toggleCompanion = true;
                            else in.menu = true;
                        }
                    }
                    leftTrigger = held;
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                { std::fill(std::begin(key), std::end(key), false); fastInputFocused = false; fastButton.Block(); CancelPauseTouch(); }
                else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) fastInputFocused = true;
                break;
            default:
                break;
            }
        }
        pads.MenuDirs(in);
    }

    // A finger on the screen: companion and pause menu taps.
    void Finger(const SDL_TouchFingerEvent& f, MenuIn& in, bool fromFinger = true)
    {
        (void)fromFinger;
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        const float x = (f.x * ow - uiOffX) / uiScale, y = (f.y * oh - uiOffY) / uiScale;
        padLast = false;
        if (screen == PLAY && !paused)
        {
            if (companion.active && (f.type == SDL_FINGERDOWN || f.type == SDL_FINGERUP || f.type == SDL_FINGERMOTION))
            {
                const int px = (int)(f.x * ow), py = (int)(f.y * oh);
                if (companion.HandleTouch(core, px, py, ow, oh, f.type != SDL_FINGERUP, CompanionActions()))
                    return;
            }
            return;
        }
        if (screen == PLAY && paused)
        {
            const int px = (int)(f.x * ow), py = (int)(f.y * oh);
            auto activateAt = [&](int tx, int ty, float cx, float cy) {
#ifdef GBAW_EMBEDDED
                if (usingHostMenu)
                {
                    const int hit = hostMenu.ItemAt(tx, ty);
                    if (hit == -2) in.back = true;
                    else if (hit >= 0 && hit < (int)pause.items.size())
                    {
                        pause.selected = hit;
                        if (pausePage == P_SOUND && pause.items[hit].label == "VOLUME")
                        {
                            const int sl = hostMenu.SliderAt(tx);
                            if (sl >= 0)
                            {
                                set.values["volume"] = std::to_string(sl * 10);
                                set.Save();
                                set.ToHost("volume");
                                SetVolume();
                                pause.items[hit].value = sl == 0 ? "OFF" : std::to_string(sl * 10) + "%";
                                return;
                            }
                        }
                        in.accept = true;
                    }
                    return;
                }
#endif
                for (int i = 0; i < (int)pause.rows.size(); i++)
                    if (pause.rows[i].Has(cx, cy))
                    {
                        pause.selected = i < (int)pause.rowItem.size() ? pause.rowItem[i] : i;
                        in.accept = true;
                        return;
                    }
                if (!pause.box.Has(cx, cy)) in.back = true;
            };
            if (!fromFinger)
            {
                if (f.type == SDL_FINGERDOWN) activateAt(px, py, x, y);
                return;
            }
            if (f.type == SDL_FINGERDOWN)
            {
                if (!pauseTouchDown)
                {
                    pauseTouchDown = true;
                    pauseTouchMoved = false;
                    pauseTouchId = f.fingerId;
                    pauseTouchX0 = px;
                    pauseTouchY0 = pauseTouchLastY = py;
#ifdef GBAW_EMBEDDED
                    pauseTouchPage = pausePage;
#endif
                }
                return;
            }
            if (!pauseTouchDown || f.fingerId != pauseTouchId) return;
            if (f.type == SDL_FINGERMOTION)
            {
                const double dist = std::hypot((double)(px - pauseTouchX0), (double)(py - pauseTouchY0));
                if (!pauseTouchMoved && dist > 12.0) pauseTouchMoved = true;
#ifdef GBAW_EMBEDDED
                if (pauseTouchMoved && usingHostMenu && hostMenu.lineH > 0 && hostMenu.shown > 0 && hostMenu.count > hostMenu.shown)
                {
                    const int stepPx = std::max(8, hostMenu.lineH);
                    const int maxScroll = std::max(0, hostMenu.count - hostMenu.shown);
                    while (pauseTouchLastY - py >= stepPx)
                    {
                        pauseTouchLastY -= stepPx;
                        if (hostMenu.scroll < maxScroll)
                        {
                            hostMenu.scroll++;
                            pause.selected = std::clamp(pause.selected, hostMenu.scroll, hostMenu.scroll + hostMenu.shown - 1);
                        }
                    }
                    while (py - pauseTouchLastY >= stepPx)
                    {
                        pauseTouchLastY += stepPx;
                        if (hostMenu.scroll > 0)
                        {
                            hostMenu.scroll--;
                            pause.selected = std::clamp(pause.selected, hostMenu.scroll, hostMenu.scroll + hostMenu.shown - 1);
                        }
                    }
                }
#endif
                return;
            }
            if (f.type == SDL_FINGERUP)
            {
                bool tap = !pauseTouchMoved;
#ifdef GBAW_EMBEDDED
                tap = tap && pauseTouchPage == pausePage;
#endif
                CancelPauseTouch();
                if (tap) activateAt(px, py, x, y);
            }
            return;
        }
        if (f.type != SDL_FINGERDOWN) return;
        if (backRect.Has(x, y))
        {
            in.back = true;
            return;
        }
        if (settingsRect.Has(x, y))
        {
            in.settings = true;
            return;
        }
        for (const auto& r : tapRects)
            if (r.first.Has(x, y))
            {
                if (screen == LIST)
                {
                    listSel = r.second;
                    in.accept = true;
                }
                else if (screen == SETTINGS)
                {
                    if (settingsSel == r.second) in.right = true;
                    settingsSel = r.second;
                }
                return;
            }
        // (The list with no games, or no folder yet: a tap anywhere chooses the folder.)
        if (screen == LIST && games.empty()) in.accept = true;
    }

    // Test scripts push their key presses as events, so they go the same way as real ones.
    long long ScriptClock() const { return realTime ? (long long)gameFrames : frame; }
    void Script()
    {
        const long long now = ScriptClock();
        for (ScriptLine& l : script)
        {
            const bool down = l.done == 0 && now >= l.frame, up = l.done == 1 && now >= l.frame + l.hold;
            if (l.what == "key" && (down || up))
            {
                SDL_Event e{};
                e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
                e.key.keysym.scancode = SDL_GetScancodeFromName(l.arg.c_str());
                e.key.keysym.sym = SDL_GetKeyFromScancode(e.key.keysym.scancode);
                SDL_PushEvent(&e);
                l.done++;
            }
            if (l.what == "click" && (down || up))
            {
                // "click FRAME X,Y [HOLD]": a mouse click, without synthetic finger events.
                int ww = 0, wh = 0;
                float x = 0, y = 0;
                SDL_GetWindowSize(win, &ww, &wh);
                sscanf(l.arg.c_str(), "%f,%f", &x, &y);
                SDL_Event e{};
                e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
                e.button.x = int(x * ww);
                e.button.y = int(y * wh);
                SDL_PushEvent(&e);
                l.done++;
            }
            if (l.what == "touch" && (down || up))
            {
                // "touch FRAME X,Y [HOLD]": a finger at X, Y (0 to 1 across the window)
                SDL_Event e{};
                e.type = down ? SDL_FINGERDOWN : SDL_FINGERUP;
                e.tfinger.fingerId = 1;
                sscanf(l.arg.c_str(), "%f,%f", &e.tfinger.x, &e.tfinger.y);
                SDL_PushEvent(&e);
                l.done++;
            }
#ifdef GBAW_CHEATS
            if (l.what == "cheat" && down && core)
            {
                std::string name = l.arg;
                for (char& c : name) if (c == '_') c = ' ';
                for (int i = 0; i < GbaCheats::COUNT; i++)
                    if (name == GbaCheats::About(i).name) cheats.Set(i, !cheats.On(i));
                fprintf(stderr, "[gba] test: cheat %s, %d on\n", name.c_str(), cheats.OnCount());
                l.done = 2;
            }
            if (l.what == "give" && down && core)
            {
                unsigned item = 0, qty = 0;
                if (sscanf(l.arg.c_str(), "%u:%u", &item, &qty) == 2) cheats.Give(cheatBus, (uint16_t)item, (uint16_t)qty);
                l.done = 2;
            }
            if (l.what == "cheatreport" && down && core)
            {
                fprintf(stderr, "[gba] report %lld: %s\n", now, cheats.Report(cheatBus).c_str());
                l.done = 2;
            }
#endif
            if (l.what == "quit" && down)
            {
                quit = true;
                l.done = 2;
            }
        }
    }
    void ScriptShots()
    {
        const long long now = ScriptClock();
        for (ScriptLine& l : script)
            if (l.what == "shot" && l.done == 0 && now >= l.frame)
            {
                Shot(l.arg);
                l.done = 2;
                shotTaken = true;
            }
    }

    // -----------------------------------------------------------------------------------------

    int Run()
    {
        fpsSince = SDL_GetTicks();
        // Frame pacing: when the pictures come 60, 120, 180 or 240 times a second (vsync on a
        // screen at that rate), one GBA frame every 1, 2, 3 or 4 of them, so each is shown for the
        // same time; otherwise (144 Hz, no vsync, or the game not keeping up) by the clock, 60 a
        // second. The rate is measured every 120 pictures rather than taken from the screen's
        // mode: Android can hold an app to 60 on a 120 Hz screen, and vsync may not really be on.
        // A rate counts when it is within 2% of one of those twice running (pictures that only
        // happen to take about a 60th of a second each, with no vsync, wander more than that),
        // and holds until it is 10% off. By the clock until then.
        int refresh = 0;
        SDL_DisplayMode mode;
        if (!SDL_GetWindowDisplayMode(win, &mode)) refresh = mode.refresh_rate;
        int perFrame = 1; // pictures for each GBA frame
        const bool canLock = realTime && !getenv("GBAW_TEST_CLOCK");
        bool lockToVsync = false;
        int vsyncCount = 0, lastEven = 0;
        if (realTime) Note("screen %d Hz", refresh);
        Uint64 vsyncCheckStart = SDL_GetPerformanceCounter();
        int vsyncCheckFrames = 0;
        const Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 last = SDL_GetPerformanceCounter();
        double due = 0;
        // (For GBAW_TEST_REALTIME: how steady the pictures came, from the game's start to the
        // first screenshot, which takes long enough to show as a gap itself.)
        // (Also how evenly the game's frames were spaced: each one should come 1/60 s after the
        // one before; an uneven one shows as a judder.)
        Uint64 playStart = 0, playEnd = 0, lastGameFrameAt = 0;
        unsigned long long playFrames0 = 0, playFrames1 = 0;
        int pictures = 0, gaps = 0, spaced = 0, uneven = 0;
        double longest = 0;
        while (!quit)
        {
            if (testMode) Script();
            MenuIn in;
            Events(in);
            if (screen == LIST) ListInput(in);
            else if (screen == SETTINGS) SettingsInput(in);
            else if (paused)
            {
                BuildPause();
                if (pause.items.empty()) pause.selected = 0;
                else if (pause.selected < 0 || pause.selected >= (int)pause.items.size())
                    pause.selected = std::max(0, std::min(pause.selected, (int)pause.items.size() - 1));
                PauseInput(in);
                BuildPause();
                if (!pause.items.empty() && (pause.selected < 0 || pause.selected >= (int)pause.items.size()))
                    pause.selected = (int)pause.items.size() - 1;
            }
            else if (in.toggleCompanion && core && screen == PLAY && !paused) companion.ToggleCompanion(core);
            else if (in.toggleQuick && core && screen == PLAY && !paused) companion.ToggleQuickMenu(core);
            else if (companion.TakesInput() && core && screen == PLAY && !paused)
            {
                if (in.menu) { companion.companionOpen = companion.quickOpen = false; OpenPause(); }
                else companion.HandleNav(core, in.up, in.down, in.left, in.right, in.accept, in.back, in.pageL, in.pageR, CompanionActions());
            }
            else if (in.menu) OpenPause();

            if (fastButton.Update(set.Get("fast_button") == "toggle", FastDown(), fastInputFocused && screen == PLAY && !paused, fastToggle))
            {
                set.values["fast_lock"] = fastToggle ? "1" : "0";
                set.Save(); set.ToHost("fast_lock");
            }
            const Uint64 now = SDL_GetPerformanceCounter();
            const double dt = (double)(now - last) / freq;
            last = now;
            const unsigned long long framesBefore = gameFrames;
            if (core && screen == PLAY && !paused && !companion.companionOpen)
            {
                int run = 1;
                if (lockToVsync)
                    run = ++vsyncCount >= perFrame ? 1 : 0;
                if (run && lockToVsync) vsyncCount = 0;
                if (realTime && !lockToVsync)
                {
                    due += dt * Fps;
                    run = (int)due;
                    due -= run;
                    if (run > 4)
                    {
                        run = 4; // (after a stall: carry on rather than racing to catch up)
                        due = 0;
                    }
                }
                int speed = FastForward() ? std::max(2, set.Int("ff_speed")) : SceneSpeed();
                const bool ffAudioNow = FastForward();
                {
                    const uint32_t dirMask = (1u << K_UP) | (1u << K_DOWN) | (1u << K_LEFT) | (1u << K_RIGHT);
                    const uint32_t dirs = GameKeys() & dirMask;
                    if (dirs & ~tapDirs) tapSince = SDL_GetTicks();
                    tapDirs = dirs;
                    if (ffAudioNow && speed > 1 && dirs && SDL_GetTicks() - tapSince < 250) speed = 1;
                }
                if (speed > 1)
                {
                    run = 1;
                    due = 0;
                    const Uint64 batchStart = SDL_GetPerformanceCounter();
                    for (int i = 0; i < speed; i++)
                    {
                        const bool budgetExpired = (i >= 1 && ((double)(SDL_GetPerformanceCounter() - batchStart) / freq) >= 0.0135);
                        const bool isLast = (i == speed - 1) || budgetExpired;
                        RunFrame(true, true, isLast);
                        if (isLast) break;
                    }
                }
                else
                {
                    for (int i = 0; i < run; i++) RunFrame(true, false, i == run - 1);
                }
            }
            else
                due = 0;
#ifdef GBAW_EMBEDDED
            if (raReady && ra && ra->pump) ra->pump(ra->ud, paused || screen != PLAY);
            if (autoStateTimer.Due(SDL_GetTicks(), core && screen == PLAY && !autoStateBlocked && set.Get("auto_state") == "1", paused)) SaveAutomatic();
            if (raReady && ra && ra->pop)
            {
                char msg[128] = {};
                if (ra->pop(ra->ud, msg, sizeof msg) && msg[0]) Toast(msg, 4000);
            }
#endif
            Draw();
            if (testMode) ScriptShots();
            SDL_RenderPresent(ren);
            if (canLock && ++vsyncCheckFrames == 120)
            {
                const double secs = (double)(SDL_GetPerformanceCounter() - vsyncCheckStart) / SDL_GetPerformanceFrequency();
                const double rate = 120 / std::max(secs, 0.001);
                const int m = (int)lround(rate / 60);
                const bool even = m >= 1 && m <= 6 && std::fabs(rate - 60.0 * m) < 1.2 * m;
                const bool twice = even && m == lastEven;
                lastEven = even ? m : 0;
                if (twice && (!lockToVsync || m != perFrame))
                {
                    Note("pictures at %.1f a second: a frame every %d of them", rate, m);
                    lockToVsync = true;
                    perFrame = m;
                    vsyncCount = 0;
                }
                else if (lockToVsync && std::fabs(rate - 60.0 * perFrame) > 6.0 * perFrame)
                {
                    // (Off by more than 10%: not a one-off slow picture but the screen's rate
                    // changing, or the game not keeping up.)
                    Note("pictures at %.1f a second: pacing by the clock", rate);
                    lockToVsync = false;
                    due = 0;
                }
                vsyncCheckStart = SDL_GetPerformanceCounter();
                vsyncCheckFrames = 0;
            }
            if (testMode && realTime && core && screen == PLAY && !shotTaken)
            {
                if (!playStart)
                {
                    playStart = now;
                    playFrames0 = gameFrames;
                }
                else
                {
                    pictures++;
                    longest = std::max(longest, dt);
                    if (dt > 0.025) gaps++;
                }
                if (gameFrames != framesBefore)
                {
                    if (lastGameFrameAt)
                    {
                        spaced++;
                        if (std::fabs((double)(now - lastGameFrameAt) / freq * Fps - 1) > 0.15) uneven++;
                    }
                    lastGameFrameAt = now;
                }
                playEnd = now;
                playFrames1 = gameFrames;
            }
            if (realTime && !lockToVsync)
            {
                // (No vsync, or not at 60 Hz: don't spin faster than needed.)
                const double spent = (double)(SDL_GetPerformanceCounter() - now) / freq;
                if (spent < 0.002) SDL_Delay(1);
            }
            frame++;
        }
        if (testMode && realTime && playStart)
        {
            SDL_RendererInfo info{};
            SDL_GetRendererInfo(ren, &info);
            int ww = 0, wh = 0;
            SDL_GetRendererOutputSize(ren, &ww, &wh);
            const double secs = (double)(playEnd - playStart) / freq;
            const unsigned long long n = playFrames1 - playFrames0;
            char how[64];
            if (lockToVsync) snprintf(how, sizeof how, "a frame every %d vsync%s", perFrame, perFrame > 1 ? "s" : "");
            else snprintf(how, sizeof how, "by the clock");
            printf("realtime: %s, %dx%d, screen %d Hz, %s: %llu game frames in %.2f s (%.2f a second), "
                   "%d pictures, %d gaps over 25 ms, longest %.1f ms, %d of %d frames unevenly spaced (%.1f%%)\n",
                   info.name ? info.name : "?", ww, wh, refresh, how, n, secs, secs > 0 ? n / secs : 0.0, pictures, gaps,
                   longest * 1000, uneven, spaced, spaced ? 100.0 * uneven / spaced : 0.0);
            fflush(stdout);
        }
        if (core) Stop();
        return 0;
    }
};

// SDL started as the player wants it (the launcher's list sets some of these its own way).
bool InitSdl()
{
    mLogSetDefaultLogger(&gQuiet);
    // (Controllers keep working while another window has the focus, as in the DS project.)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef _WIN32
    // The program's own icon (resource 1) for its window.
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

} // namespace

#ifdef GBAW_EMBEDDED
namespace GbaWide
{

std::vector<ListGame> Find(const std::string& roms, const std::string& saves)
{
    std::vector<ListGame> out;
    Games::MakeDir(saves);
    // One cache per roms folder. The launcher looks at roms and at the app folder in turn;
    // a single shared cache would drop the first folder's results when the second was saved.
    unsigned long h = 2166136261u;
    for (unsigned char c : roms) h = (h ^ c) * 16777619u;
    char cache[64];
    snprintf(cache, sizeof cache, "/gba-rom-list-%08lx.txt", h);
    for (const Games::Game& g : Games::Scan(roms, saves, saves + cache))
    {
        ListGame l;
        l.key = g.key;
        l.name = g.name;
        l.file = g.ips.empty() ? g.rom : g.ips;
        const unsigned char* own = nullptr;
        size_t ownSize = 0;
        const HackNames::Hack* hack = HackNames::ForPatch(g.patch);
        const bool thatHack = hack && HackNames::IsThatHack(*hack, g.sha1, g.fileName);
        const std::string baseArt = !g.patch.compare(0, 7, "firered") || g.patch == "radicalred" || g.patch == "unbound" ? "firered"
                                  : !g.patch.compare(0, 7, "emerald") || g.patch == "inclement" ? "emerald"
                                  : !g.patch.compare(0, 9, "leafgreen") ? "leafgreen"
                                  : !g.patch.compare(0, 4, "ruby") ? "ruby"
                                  : !g.patch.compare(0, 8, "sapphire") ? "sapphire" : std::string();
        l.art = g.patch.empty() ? Games::ArtCode(g.sha1)
              : (!hack || thatHack) && Art(g.patch, own, ownSize) ? g.patch
              : !Games::ArtCode(g.sha1).empty() ? Games::ArtCode(g.sha1)
              : (!baseArt.empty() && Art(baseArt, own, ownSize)) ? baseArt : std::string();
        if (thatHack) l.name = hack->name;
        l.hasSave = g.hasSave;
        l.widescreen = !g.patch.empty() || pk_wide_knows(g.crc);
        l.hack = !g.known;
        l.isGb = g.isGb;
        out.push_back(l);
    }
    return out;
}

bool Art(const std::string& name, const unsigned char*& jpeg, size_t& size)
{
    for (const WideArt& a : kWideArt)
        if (a.jpg && name == a.code)
        {
            jpeg = a.jpg;
            size = a.size;
            return true;
        }
    return false;
}

// On a computer the game list closed SDL before the game and starts it again after: SDL is closed
// here too, as the list left it. (Not on a phone or the Switch: SDL stays up there between the
// list and the game.)
static void LeaveSdl()
{
#if !defined(__SWITCH__)
    SDL_Quit();
#endif
}

bool Play(const std::string& file, const Options& o)
{
    if (!InitSdl()) return false;
    std::unique_ptr<App> app(new App());
    app->embedded = true;
    app->ra = o.achievements;
    app->uiHost = o.ui;
    app->hostAccent = o.accent;
    app->UseFolders(o.roms, o.saves, o.data);
    if (const char* script = getenv("GBAW_TEST_SCRIPT"))
        if (*script) app->LoadScript(script);
    if (!app->OpenWindow(o.fullscreen ? 0 : o.w, o.fullscreen ? 0 : o.h, o.fullscreen ? 1 : 0))
    {
        fprintf(stderr, "[gba] can't open a window: %s\n", SDL_GetError());
        app->Close();
        LeaveSdl();
        return false;
    }
    app->OpenAudio();
    for (int i = 0; i < SDL_NumJoysticks(); i++) app->pads.Open(i);
    app->Rescan();
    bool started = false;
    for (const Games::Game& g : app->games)
        if (g.rom == file || g.ips == file)
        {
            fprintf(stderr, "[gba] %s (%s, widescreen patch %s%s%s)\n", file.c_str(), g.key.c_str(),
                    g.patch.empty() ? "none" : g.patch.c_str(),
                    g.patch.empty() && !g.why.empty() ? ": " : "",
                    g.patch.empty() ? g.why.c_str() : "");
            started = app->Start(g);
            break;
        }
    if (started) app->Run();
    else fprintf(stderr, "[gba] %s: not a GBA game in %s\n", file.c_str(), o.roms.c_str());
    app->Close();
    LeaveSdl();
    return started;
}

}
#else
int main(int argc, char** argv)
{
    std::string dataDir, romArg, gameArg, scriptArg;
    int winW = 0, winH = 0;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--data") dataDir = next();
        else if (a == "--rom") romArg = next();
        else if (a == "--game") gameArg = next();
        else if (a == "--script") scriptArg = next();
        else if (a == "--window")
        {
            const std::string s = next();
            sscanf(s.c_str(), "%dx%d", &winW, &winH);
        }
        else
        {
            fprintf(stderr, "usage: %s [--game emerald|ultraviolet|firered|leafgreen] [--rom FILE] [--data DIR] [--window WxH] [--script FILE]\n", argv[0]);
            return 1;
        }
    }
    if (!InitSdl()) return 1;
    static App app;
    app.Folders(dataDir);
    if (!scriptArg.empty() && !app.LoadScript(scriptArg)) return 1;
    if (!app.OpenWindow(winW, winH))
    {
        fprintf(stderr, "Can't open a window: %s\n", SDL_GetError());
        return 1;
    }
    app.OpenAudio();
    for (int i = 0; i < SDL_NumJoysticks(); i++) app.pads.Open(i);
    app.Rescan();
    if (!romArg.empty())
    {
        Games::Game g;
        g.rom = romArg;
        g.fileName = romArg.substr(romArg.find_last_of("/\\") + 1);
        std::vector<uint8_t> rom;
        if (Games::ReadFile(romArg, rom) && Games::LooksLikeGba(rom))
        {
            Games::Identify(g, rom);
            g.hasSave = true;
            app.Start(g);
        }
        else
            app.Toast("NOT A GBA ROM: " + romArg);
    }
    else if (!gameArg.empty())
    {
        bool found = false;
        for (const Games::Game& g : app.games)
            if (g.key == gameArg && !found)
            {
                found = true;
                app.Start(g);
            }
        if (!found) app.Toast("NOT IN THE ROMS FOLDER: " + gameArg);
    }
    const int rc = app.Run();
    SDL_Quit();
    return rc;
}
#endif
