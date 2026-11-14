#pragma once
// The game picker of the all-games download (one program for every game): finds the players'
// ROMs, reads each one's name and icon from the ROM itself (every DS game carries a 32x32 icon
// and its title in its banner), and draws the list. Kept free of any platform so that it can be
// drawn into a picture on a PC as well; the Switch shows it with switch_platform.cpp before the
// game's window opens.
#include <algorithm>
#include <climits>
#include <filesystem>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <cmath>
#include <ctime>
#include <vector>
#include <dirent.h>
#include "ssr_os.h"
#include "gamecode.h"
#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include "ui.h"
#include "switch_platform.h"
#include "launcher_font.h"
// (JPEG and PNG pictures for the list: the program that draws it defines STB_IMAGE_IMPLEMENTATION
// with STBI_ONLY_JPEG and STBI_ONLY_PNG before including this, in one file.)
#include "thirdparty/stb_image.h"

namespace Launcher
{

inline float AnimTime()
{
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - start).count();
}

inline int ModernTextWidth(const std::string& text, int targetH)
{
    return Canvas::ModernTextWidth(text, targetH);
}

inline void ModernText(Canvas& c, int x, int y, const std::string& text, int targetH, uint32_t colour, bool shadow = false)
{
    c.ModernText(x, y, text, targetH, colour, shadow);
}

// The consoles the list has games of, in the order its sections and tabs come in.
enum Platform { DS, GBA, PLATFORMS };
inline const char* PlatformName(int p) { return p == DS ? "DS" : "GBA"; }
// The tabs along the top: ALL (every section, each under its console's heading), then one per
// console.
constexpr int TABS = 1 + PLATFORMS;
inline const char* TabName(int t) { return t <= 0 ? "ALL" : PlatformName(t - 1); }
inline bool InTab(int platform, int tab) { return tab <= 0 || tab == platform + 1; }

struct Game
{
    int platform = DS;
    std::string rom;      // the ROM's full path
    std::string folder;   // where its save (and a funcs.txt of the player's own) live
    std::string code;     // the game code, e.g. IREO
    std::string name;     // as shown, e.g. POKEMON BLACK 2
    std::string saveName; // e.g. black2.sav
    bool known = false;   // one of the games this program plays
    bool hasSave = false;
    bool hasIcon = false;
    uint32_t icon[32 * 32] = {}; // ARGB, transparent where the icon is
    // A larger picture for the game, if the player has one (FindArt): ARGB, Art x Art pixels.
    std::vector<uint32_t> art;
    // Set when the same game is also in another folder: this one's folder, shown under it.
    std::string where;
    // A game on its way (AddComingSoon): its tile says so and it cannot be played.
    bool comingSoon = false;
};

// The size the larger pictures are kept at (the tiles of the list at 720 lines).
constexpr int Art = 224;

// Games on their way (none now): kept so a tile can say COMING SOON again.
inline void AddComingSoon(std::vector<Game>&) {}

// The games this program plays, in the order the list shows them.
struct KnownGame
{
    const char* code;
    const char* name;
    const char* save;
};
inline const KnownGame* Known(const std::string& code)
{
    static const KnownGame games[] = {
        { "IRBO", "Pokemon Black", "black.sav" },
        { "IRBS", "Pokemon Black (ES)", "black-es.sav" },
        { "IRBF", "Pokemon Black (FR)", "black-fr.sav" },
        { "IRAO", "Pokemon White", "white.sav" },
        { "IRAS", "Pokemon White (ES)", "white-es.sav" },
        { "IRAF", "Pokemon White (FR)", "white-fr.sav" },
        { "IREO", "Pokemon Black 2", "black2.sav" },
        { "IRES", "Pokemon Black 2 (ES)", "black2-es.sav" },
        { "IREF", "Pokemon Black 2 (FR)", "black2-fr.sav" },
        { "IRDO", "Pokemon White 2", "white2.sav" },
        { "IRDS", "Pokemon White 2 (ES)", "white2-es.sav" },
        { "IRDF", "Pokemon White 2 (FR)", "white2-fr.sav" },
        { "IPKE", "Pokemon HeartGold", "heartgold.sav" },
        { "IPKF", "Pokemon HeartGold (FR)", "heartgold-fr.sav" },
        { "IPGE", "Pokemon SoulSilver", "soulsilver.sav" },
        { "IPGF", "Pokemon SoulSilver (FR)", "soulsilver-fr.sav" },
        { "CPUE", "Pokemon Platinum", "platinum.sav" },
        { "ADAE", "Pokemon Diamond (Experimental)", "diamond.sav" },
        { "APAE", "Pokemon Pearl (Experimental)", "pearl.sav" },
        { "CPUS", "Pokemon Platinum (ES)", "platinum-es.sav" },
        { "IPGS", "Pokemon SoulSilver (ES)", "soulsilver-es.sav" },
        { "IPKS", "Pokemon HeartGold (ES)", "heartgold-es.sav" },
        { "ADAS", "Pokemon Diamond (ES, Experimental)", "diamond-es.sav" },
        { "APAF", "Pokemon Pearl (FR, Experimental)", "pearl-fr.sav" },
        { "APAS", "Pokemon Pearl (ES, Experimental)", "pearl-es.sav" },
        { "APAD", "Pokemon Pearl (DE, Experimental)", "pearl-de.sav" },
        { "ADAD", "Pokemon Diamond (DE, Experimental)", "diamond-de.sav" },
        { "ADAI", "Pokemon Diamond (IT, Experimental)", "diamond-it.sav" },
        { "IPKI", "Pokemon HeartGold (IT)", "heartgold-it.sav" },
        { "CPUF", "Pokemon Platinum (FR)", "platinum-fr.sav" },
    };
    for (const KnownGame& g : games)
        if (code == g.code) return &g;
    static const std::vector<KnownGame> regional = [] {
        std::vector<KnownGame> result;
        for (const auto& p : RegionalProfiles::profiles) result.push_back({p.code,p.name,p.save});
        return result;
    }();
    for (const auto& g : regional) if (code == g.code) return &g;
    return nullptr;
}
inline int Order(const std::string& code)
{
    // (DS, then GBA: Emerald, Ultra Violet (UV22: its own code here, as FireRed's is BPRE too),
    // FireRed, LeafGreen.)
    static const char* order[] = { "IRBO", "IRAO", "IREO", "IRDO", "IPKE", "IPGE", "CPUE", "ADAE", "APAE", "BPEE", "UV22", "BPRE", "BPGE" };
    const std::string base = BaseGameCode(code);
    const int n = (int)(sizeof order / sizeof order[0]);
    for (int i = 0; i < n; i++)
        if (base == order[i]) return i;
    return n;
}

// Returns the English base code if a code's first 3 letters match a known DS Pokémon game.
inline const char* HackBaseCode(const std::string& code)
{
    if (code.size() != 4) return nullptr;
    if (code.rfind("IRB", 0) == 0) return "IRBO";
    if (code.rfind("IRA", 0) == 0) return "IRAO";
    if (code.rfind("IRE", 0) == 0) return "IREO";
    if (code.rfind("IRD", 0) == 0) return "IRDO";
    if (code.rfind("IPG", 0) == 0) return "IPGE";
    if (code.rfind("IPK", 0) == 0) return "IPKE";
    if (code.rfind("CPU", 0) == 0) return "CPUE";
    if (code.rfind("ADA", 0) == 0) return "ADAE";
    if (code.rfind("APA", 0) == 0) return "APAE";
    return nullptr;
}

inline bool IsCustomHackTitle(const std::string& name)
{
    std::string upper;
    for (char c : name) upper += (char)toupper((unsigned char)c);
    static const char* const tags[] = {
        "BLAZE", "VOLT", "RENEGADE", "SACRED", "STORM", "REDUX", "RANDOM", "HACK",
        "KAIZO", "VINTAGE", "MOON", "SUN", "SHINING", "LUMINESCENT", "PLUS", "DARK"
    };
    for (const char* tag : tags)
        if (upper.find(tag) != std::string::npos) return true;
    return false;
}

// A name as the list has it (POKEMON EMERALD) as a title: Pokemon Emerald (FireRed and LeafGreen
// as they are spelt).
inline std::string TitleCase(const std::string& name)
{
    std::string out = ToNiceTitle(name);
    struct Fix { const char* word; size_t pos; };
    for (const Fix& f : std::initializer_list<Fix>{
             { "Soulsilver", 4 }, { "Heartgold", 5 }, { "Firered", 4 }, { "Leafgreen", 4 } })
    {
        const size_t at = out.find(f.word);
        if (at != std::string::npos) out[at + f.pos] = (char)toupper((unsigned char)out[at + f.pos]);
    }
    return out;
}

inline bool EndsWithNds(const std::string& name)
{
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".nds";
}

// Reads a ROM's game code, title and icon. False if it is not a DS ROM.
inline bool ReadGame(const std::string& rom, const std::string& folder, Game& g)
{
    FILE* f = fopen(rom.c_str(), "rb");
    if (!f) return false;
    struct stat info;
    if (fstat(fileno(f), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0x200) { fclose(f); return false; }
    uint8_t header[0x70];
    bool ok = fread(header, 1, sizeof header, f) == sizeof header;
    g = Game();
    g.rom = rom;
    g.folder = folder;
    if (ok)
    {
        g.code.assign((const char*)header + 0x0C, 4);
        for (char c : g.code)
            if (c < 0x20 || c > 0x7E) ok = false;
    }
    // The banner: version, CRCs, then the icon's 4-bit pixels (4x4 tiles of 8x8), its 16
    // colours (BGR555, the first transparent) and the title in six languages (UTF-16).
    const uint32_t banner = ok ? header[0x68] | header[0x69] << 8 | header[0x6A] << 16 | (uint32_t)header[0x6B] << 24 : 0;
    uint8_t ban[0x440];
    if (ok && banner >= 0x200 && (uint64_t)banner + sizeof ban <= (uint64_t)info.st_size && fseek(f, (long)banner, SEEK_SET) == 0 && fread(ban, 1, sizeof ban, f) == sizeof ban)
    {
        uint32_t pal[16];
        for (int i = 0; i < 16; i++)
        {
            const uint16_t c = (uint16_t)(ban[0x220 + 2 * i] | ban[0x221 + 2 * i] << 8);
            const uint32_t r = (c & 31) * 255 / 31, gr = (c >> 5 & 31) * 255 / 31, b = (c >> 10 & 31) * 255 / 31;
            pal[i] = (i ? 0xFF000000u : 0u) | r << 16 | gr << 8 | b;
        }
        for (int ty = 0; ty < 4; ty++)
            for (int tx = 0; tx < 4; tx++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                    {
                        const uint8_t byte = ban[0x20 + (ty * 4 + tx) * 32 + y * 4 + x / 2];
                        g.icon[(ty * 8 + y) * 32 + tx * 8 + x] = pal[x & 1 ? byte >> 4 : byte & 15];
                    }
        g.hasIcon = true;
        // The English title's first line, for a game this program does not know.
        for (int i = 0; i < 64; i++)
        {
            const uint16_t ch = (uint16_t)(ban[0x340 + 2 * i] | ban[0x341 + 2 * i] << 8);
            if (ch == 0 || ch == '\n') break;
            g.name += ch == 0xE9 ? 'E' : ch < 0x80 ? (char)ch : ' ';
        }
    }
    fclose(f);
    if (!ok) return false;

    // Check for companion code override file (.code beside the ROM or in the saves folder)
    // allowing hacks with custom IDs (like BB2E, VW2E, etc.) to specify their base game profile:
    const std::string romBase = rom.substr(rom.find_last_of('/') + 1);
    const size_t dot = romBase.find_last_of('.');
    const std::string stem = (dot != std::string::npos) ? romBase.substr(0, dot) : romBase;
    const std::string romDir = (rom.find_last_of('/') != std::string::npos) ? rom.substr(0, rom.find_last_of('/')) : folder;
    for (const std::string& path : { romDir + "/" + stem + ".code", folder + "/" + stem + ".code" })
    {
        FILE* cf = fopen(path.c_str(), "r");
        if (cf)
        {
            char codeBuf[16] = {};
            if (fscanf(cf, "%4s", codeBuf) == 1 && strlen(codeBuf) == 4)
                g.code = codeBuf;
            fclose(cf);
            break;
        }
    }

    const std::string banTitle = g.name;
    const KnownGame* k = Known(g.code);
    if (!k)
    {
        if (const char* base = HackBaseCode(g.code))
            k = Known(base);
    }

    const bool isHack = (!Known(g.code) && HackBaseCode(g.code) != nullptr) ||
                        IsCustomHackTitle(banTitle) || IsCustomHackTitle(stem);

    if (k)
    {
        g.known = true;
        if (isHack && !banTitle.empty() && IsCustomHackTitle(banTitle))
            g.name = banTitle;
        else if (isHack && IsCustomHackTitle(stem))
            g.name = stem;
        else
            g.name = k->name;
        g.saveName = k->save;
    }
    else
    {
        if (g.name.empty()) g.name = stem;
        g.saveName = stem + ".sav";
    }
    for (char& c : g.name) c = (char)toupper((unsigned char)c);

    struct stat st;
    // For ROM hacks or games with existing custom saves, isolate save files so vanilla playthroughs
    // are never overwritten:
    if (stat((folder + "/" + stem + ".sav").c_str(), &st) == 0)
    {
        g.saveName = stem + ".sav";
        g.hasSave = true;
    }
    else if (isHack)
    {
        g.saveName = stem + ".sav";
        g.hasSave = stat((folder + "/" + g.saveName).c_str(), &st) == 0;
    }
    else
    {
        g.hasSave = stat((folder + "/" + g.saveName).c_str(), &st) == 0;
        if (!g.hasSave)
        {
            // If the regional save name does not exist yet, look for common existing alternatives
            // (such as <rom_stem>.sav, the unregionalised name e.g. black2.sav, or game.sav):
            const size_t dash = g.saveName.find('-');
            const std::string baseSave = (dash != std::string::npos) ? g.saveName.substr(0, dash) + ".sav" : "";
            for (const std::string& candidate : { stem + ".sav", baseSave, std::string("game.sav") })
            {
                if (!candidate.empty() && candidate != g.saveName &&
                    stat((folder + "/" + candidate).c_str(), &st) == 0)
                {
                    g.saveName = candidate;
                    g.hasSave = true;
                    break;
                }
            }
        }
    }
    return true;
}

// A JPEG or PNG picture, scaled to Art x Art (each pixel the average of the ones it covers).
inline bool ImageAllowed(const unsigned char* data, size_t size)
{
    int w = 0, h = 0, n = 0;
    return data && size > 0 && size <= (16u << 20) && size <= INT_MAX &&
        stbi_info_from_memory(data, (int)size, &w, &h, &n) && w > 0 && h > 0 &&
        w <= 8192 && h <= 8192 && (uint64_t)w * h <= 16000000;
}

inline bool LoadArt(const unsigned char* data, size_t size, std::vector<uint32_t>& out)
{
    if (!ImageAllowed(data, size)) return false;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
    if (!px) return false;
    out.assign((size_t)Art * Art, 0);
    for (int y = 0; y < Art; y++)
        for (int x = 0; x < Art; x++)
        {
            const int x0 = x * w / Art, x1 = std::max(x0 + 1, (x + 1) * w / Art);
            const int y0 = y * h / Art, y1 = std::max(y0 + 1, (y + 1) * h / Art);
            uint32_t sum[4] = {}, count = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++)
                {
                    const unsigned char* p = px + ((size_t)yy * w + xx) * 4;
                    for (int c = 0; c < 4; c++) sum[c] += p[c];
                    count++;
                }
            out[(size_t)y * Art + x] = (sum[3] / count) << 24 | (sum[0] / count) << 16 | (sum[1] / count) << 8 | sum[2] / count;
        }
    stbi_image_free(px);
    return true;
}

inline bool ReadFile(const std::string& path, std::vector<unsigned char>& data, long offset = 0, long size = -1)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    if (size < 0)
    {
        fseek(f, 0, SEEK_END);
        size = ftell(f) - offset;
    }
    bool ok = size > 0 && size < (16 << 20) && fseek(f, offset, SEEK_SET) == 0;
    if (ok)
    {
        data.resize((size_t)size);
        ok = fread(data.data(), 1, data.size(), f) == data.size();
    }
    fclose(f);
    return ok;
}

// The icon a Switch homebrew program carries (its Homebrew Menu icon, a JPEG), from a .nro file.
inline bool NroIcon(const std::string& nro, std::vector<unsigned char>& jpeg)
{
    std::vector<unsigned char> head;
    if (!ReadFile(nro, head, 0, 0x20) || memcmp(head.data() + 0x10, "NRO0", 4) != 0) return false;
    const long size = head[0x18] | head[0x19] << 8 | head[0x1A] << 16 | (long)head[0x1B] << 24;
    std::vector<unsigned char> aset;
    if (!ReadFile(nro, aset, size, 0x38) || memcmp(aset.data(), "ASET", 4) != 0) return false;
    uint64_t off = 0, len = 0;
    for (int i = 7; i >= 0; i--)
    {
        off = off << 8 | aset[0x08 + i];
        len = len << 8 | aset[0x10 + i];
    }
    return len > 0 && ReadFile(nro, jpeg, size + (long)off, (long)len);
}

// The game's larger picture, from the first of: <list folder>/icons/<game code>.jpg or .png (e.g.
// icons/IREO.jpg), a picture with the ROM's name (Black 2.nds: Black 2.jpg or .png), icon.jpg in
// the game's folder, or the Homebrew Menu icon of a one-game program (.nro) in its folder (not in
// the list's own folder: that one is the all-games program, whose icon is no game's). The
// downloads hold none: these are the player's own.
// Square ARGB pixels (side n) at side m: smooth both ways (bilinear when larger, the average of
// the pixels covered when smaller), with the colours weighted by their opacity.
inline std::vector<uint32_t> ResizeArt(const std::vector<uint32_t>& src, int n, int m)
{
    std::vector<uint32_t> out((size_t)m * m);
    auto at = [&](int x, int y) { return src[(size_t)std::min(n - 1, std::max(0, y)) * n + std::min(n - 1, std::max(0, x))]; };
    for (int y = 0; y < m; y++)
        for (int x = 0; x < m; x++)
        {
            double acc[4] = {}, wsum = 0;
            auto add = [&](uint32_t p, double w) {
                const double al = (p >> 24) / 255.0;
                acc[0] += w * al * ((p >> 16) & 255);
                acc[1] += w * al * ((p >> 8) & 255);
                acc[2] += w * al * (p & 255);
                acc[3] += w * (p >> 24);
                wsum += w;
            };
            if (m >= n)
            {
                const double fx = (x + 0.5) * n / m - 0.5, fy = (y + 0.5) * n / m - 0.5;
                const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                const double tx = fx - x0, ty = fy - y0;
                add(at(x0, y0), (1 - tx) * (1 - ty));
                add(at(x0 + 1, y0), tx * (1 - ty));
                add(at(x0, y0 + 1), (1 - tx) * ty);
                add(at(x0 + 1, y0 + 1), tx * ty);
            }
            else
            {
                const int sx0 = x * n / m, sx1 = std::max(sx0 + 1, (x + 1) * n / m);
                const int sy0 = y * n / m, sy1 = std::max(sy0 + 1, (y + 1) * n / m);
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++) add(at(sx, sy), 1);
            }
            const double alpha = acc[3] / wsum;
            const double k = alpha > 0 ? 255.0 / (alpha * wsum) : 0;
            auto c = [&](double v) { return (uint32_t)std::min(255.0, std::max(0.0, v * k + 0.5)); };
            out[(size_t)y * m + x] = ((uint32_t)std::min(255.0, alpha + 0.5) << 24) | (c(acc[0]) << 16) | (c(acc[1]) << 8) | c(acc[2]);
        }
    return out;
}

// The game's picture at side m, for an icon outside the list (desktop shortcuts, HOME Menu
// entries): its box art or picture, else its DS icon (sharp, as a pixel picture). Empty when it
// has neither.
inline std::vector<uint32_t> GamePicture(const Game& g, int m)
{
    if (!g.art.empty()) return ResizeArt(g.art, Art, m);
    if (!g.hasIcon) return {};
    std::vector<uint32_t> out((size_t)m * m);
    for (int y = 0; y < m; y++)
        for (int x = 0; x < m; x++) out[(size_t)y * m + x] = g.icon[(y * 32 / m) * 32 + x * 32 / m];
    return out;
}

inline void FindArt(Game& g, const std::string& listFolder)
{
    std::vector<unsigned char> data;
    const std::string base = g.rom.substr(0, g.rom.size() - 4);
    for (const std::string& path : { listFolder + "/icons/" + g.code + ".jpg", listFolder + "/icons/" + g.code + ".png",
                                     base + ".jpg", base + ".png", g.folder + "/icon.jpg" })
        if (ReadFile(path, data) && LoadArt(data.data(), data.size(), g.art)) return;
    if (g.folder == listFolder) return;
    if (DIR* d = opendir(g.folder.c_str()))
    {
        std::vector<std::string> nros;
        while (dirent* e = readdir(d))
        {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".nro") == 0) nros.push_back(g.folder + "/" + n);
        }
        closedir(d);
        std::sort(nros.begin(), nros.end());
        for (const std::string& nro : nros)
            if (NroIcon(nro, data) && LoadArt(data.data(), data.size(), g.art)) return;
    }
}

// The ROMs in a folder (and in its roms/ folder, with subfolder); their saves go in the folder
// itself.
inline void ScanFolder(const std::string& folder, std::vector<Game>& out, bool subfolder = true)
{
    for (const std::string& dir : { folder, subfolder ? folder + "/roms" : std::string() })
    {
        if (dir.empty()) continue;
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> names;
        while (dirent* e = readdir(d))
            if (EndsWithNds(e->d_name)) names.push_back(e->d_name);
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names)
        {
            Game g;
            if (ReadGame(dir + "/" + n, folder, g)) out.push_back(g);
        }
    }
}

// By console (the list's sections), then known games first, in the list's order, then the rest
// by name.
inline void Sort(std::vector<Game>& games)
{
    std::stable_sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
        if (a.platform != b.platform) return a.platform < b.platform;
        const int oa = Order(a.code), ob = Order(b.code);
        if (oa != ob) return oa < ob;
        return a.name < b.name;
    });
}

// The list's colours (ARGB): Horizon-inspired dark slate with white text and accent details.
struct Theme
{
    uint32_t bg = 0xFF0D1017, band = 0xFF151924, bandLine = 0xFF0090FF, title = 0xFFFFFFFF;
    uint32_t tile = 0xFF1A202E, frame = 0xFF0090FF;
    uint32_t name = 0xFFC8D0E0, nameSel = 0xFFFFFFFF, sub = 0xFF7E8A9E, subSel = 0xFF0090FF, bad = 0xFFE0242C;
    uint32_t hint = 0xFF94A0B8;
    int bandLineH = 1;       // the band's line, in font pixels
    int frameW = 2;          // the chosen game's frame, in font pixels
    uint32_t selBar = 0;     // a bar under the chosen game's name (0: none)
    bool pokemon = false;
    struct Background* background = nullptr; // a picture behind the list (BACKGROUND: YOUR PICTURE)
};

// The player's own picture behind the list (background.jpg or .png beside the saves folder; on
// the Switch, with the saves): kept as read, and once fitted to the canvas, darkened so the
// names stay readable.
struct Background
{
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    std::vector<uint32_t> fit;
    int fw = 0, fh = 0;
    // The picture covering a w x h canvas (cut at the sides or top and bottom), or nullptr.
    const uint32_t* Fitted(int cw, int ch)
    {
        if (px.empty() || cw <= 0 || ch <= 0) return nullptr;
        if (fw == cw && fh == ch) return fit.data();
        fit.assign((size_t)cw * ch, 0);
        const double sc = std::max((double)cw / w, (double)ch / h);
        const int ox = (int)((w * sc - cw) / 2), oy = (int)((h * sc - ch) / 2);
        for (int y = 0; y < ch; y++)
        {
            const int sy = std::min(h - 1, (int)((y + oy) / sc));
            for (int x = 0; x < cw; x++)
            {
                const uint32_t p = px[(size_t)sy * w + std::min(w - 1, (int)((x + ox) / sc))];
                // (A third of its brightness: the grey names stayed hard to read on a bright one
                // at two fifths.)
                const uint32_t r = (p >> 16 & 0xFF) / 3, g = (p >> 8 & 0xFF) / 3, b = (p & 0xFF) / 3;
                fit[(size_t)y * cw + x] = 0xFF000000u | r << 16 | g << 8 | b;
            }
        }
        fw = cw;
        fh = ch;
        return fit.data();
    }
};

// The accent colour of the list (COLOUR on the settings page): the band's line, the chosen
// game's frame and its save line.
inline Theme ThemeNamed(const std::string& colour)
{
    Theme t;
    t.bandLine = t.frame = t.subSel = AccentNamed(colour); // (ui.h; red when not set)
    return t;
}

// A setting's value in a settings file ("" when it has none).
inline std::string ConfigValue(const std::string& cfg, const char* key)
{
    std::string out;
    if (FILE* f = fopen(cfg.c_str(), "r"))
    {
        char buf[512];
        const size_t n = strlen(key);
        while (fgets(buf, sizeof buf, f))
            if (!strncmp(buf, key, n) && buf[n] == '=')
            {
                out = buf + n + 1;
                while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
            }
        fclose(f);
    }
    return out;
}

// The GBA games (Pokemon GBA Widescreen, built in) take the DS games' settings where both have
// one: written into its gba-settings.ini in `dataDir` as a GBA game starts, the others there (its
// WIDESCREEN shape, FAST FORWARD SPEED) left as they are. SMOOTH SCALING ON is its SMOOTH; OFF
// keeps its SHARP or WHOLE PIXELS, whichever was chosen there (SHARP to start: it fills the screen;
// WHOLE PIXELS, the crispest, leaves a border, which its WIDESCREEN 18:9 keeps to the top and
// bottom at 720p). FPS COUNTER, VOLUME and, but on the Switch, CONTROLLER BUTTONS as they are.
inline void SyncGbaSettings(const std::string& cfg, const std::string& dataDir)
{
    std::vector<std::pair<std::string, std::string>> set;
    const std::string ini = dataDir + "/gba-settings.ini";
    const std::string had = ConfigValue(ini, "scaling");
    set.push_back({ "scaling", ConfigValue(cfg, "smooth") == "1" ? "smooth" : had == "whole" ? "whole" : "sharp" });
    set.push_back({ "show_fps", ConfigValue(cfg, "show_fps") == "1" ? "1" : "0" });
    set.push_back({ "fast_button", ConfigValue(cfg, "fast_button") == "toggle" ? "toggle" : "hold" });
    const std::string fast = ConfigValue(cfg, "fast_lock");
    if (fast == "0" || fast == "1") set.push_back({ "fast_lock", fast });
    const std::string vol = ConfigValue(cfg, "volume");
    set.push_back({ "volume", vol.empty() ? "100" : vol });
#ifndef __SWITCH__
    set.push_back({ "buttons", ConfigValue(cfg, "buttons") == "positions" ? "positions" : "labels" });
#endif
    std::vector<std::string> lines;
    if (FILE* f = fopen(ini.c_str(), "r"))
    {
        char buf[512];
        while (fgets(buf, sizeof buf, f))
        {
            std::string l = buf;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            bool ours = false;
            for (const auto& kv : set) ours |= !l.compare(0, kv.first.size() + 1, kv.first + "=");
            if (!ours && !l.empty()) lines.push_back(l);
        }
        fclose(f);
    }
    for (const auto& kv : set) lines.push_back(kv.first + "=" + kv.second);
    if (FILE* f = fopen(ini.c_str(), "w"))
    {
        for (const std::string& l : lines) fprintf(f, "%s\n", l.c_str());
        fclose(f);
    }
}

// The list's look from the settings file (COLOUR, BACKGROUND): the picture read from `folder`
// (background.jpg or .png), at most 2048 pixels on a side (larger ones are read at a fraction).
inline void LoadLook(const std::string& cfg, const std::string& folder, Theme& t, Background& b)
{
    const std::string language = ConfigValue(cfg, "list_lang");
    UiLang::Set(language.empty() ? ConfigValue(cfg, "language") : language);
    t = ThemeNamed(ConfigValue(cfg, "list_colour"));
    b = Background();
    t.background = nullptr;
    if (ConfigValue(cfg, "list_background") != "picture") return;
    std::vector<unsigned char> data;
    for (const char* name : { "/background.jpg", "/background.png", "/background.jpeg" })
    {
        if (!ReadFile(folder + name, data)) continue;
        if (!ImageAllowed(data.data(), data.size())) continue;
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &n, 4);
        if (!px) continue;
        const int step = std::max(1, (std::max(w, h) + 2047) / 2048);
        b.w = std::max(1, w / step);
        b.h = std::max(1, h / step);
        b.px.resize((size_t)b.w * b.h);
        for (int y = 0; y < b.h; y++)
            for (int x = 0; x < b.w; x++)
            {
                const unsigned char* p = px + ((size_t)y * step * w + (size_t)x * step) * 4;
                b.px[(size_t)y * b.w + x] = 0xFF000000u | p[0] << 16 | p[1] << 8 | p[2];
            }
        stbi_image_free(px);
        t.background = &b;
        return;
    }
}

// Draw the iconic Nintendo DS dual-screen logo [ ⊟ ]
inline void DrawDsLogo(Canvas& c, int x, int y, int s, uint32_t color)
{
    const int sw = 8 * s, sh = 4 * s + 1;
    const int gap = std::max(1, s + 1);
    c.OutlineRound(x, y, sw, sh, s, 1, color);
    c.FillBlend(x + s, y + s, sw - 2 * s, sh - 2 * s, (color & 0x00FFFFFF) | 0x33000000);
    c.OutlineRound(x, y + sh + gap, sw, sh, s, 1, color);
    c.FillBlend(x + s, y + sh + gap + s, sw - 2 * s, sh - 2 * s, (color & 0x00FFFFFF) | 0x66000000);
}

// Draw the iconic Nintendo DS / DSi 3-bar battery meter [ ||| ]
inline void DrawDsBattery(Canvas& c, int x, int y, int s, int level = 3)
{
    const int bw = 15 * s, bh = 8 * s;
    const int nubW = 2 * s, nubH = 4 * s;
    c.OutlineRound(x, y, bw, bh, s, 1, 0xFF7A8A9E);
    c.Fill(x + bw, y + (bh - nubH) / 2, nubW, nubH, 0xFF7A8A9E);
    const uint32_t barColor = (level <= 1) ? 0xFFFF3B30 : 0xFF00E676;
    const int barW = 3 * s, barH = bh - 4 * s;
    for (int i = 0; i < level; i++)
        c.Fill(x + 2 * s + i * (barW + s), y + 2 * s, barW, barH, barColor);
}

// Linearly blend two ARGB colours (tNum / tDen from a toward b) with opaque alpha.
inline uint32_t MixArgb(uint32_t a, uint32_t b, int tNum, int tDen)
{
    const int inv = tDen - tNum;
    const uint32_t r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * tNum) / tDen;
    const uint32_t g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * tNum) / tDen;
    const uint32_t bl = ((a & 0xFF) * inv + (b & 0xFF) * tNum) / tDen;
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

// Draw the authentic Nintendo 3DS / Wii U / DSi atmospheric plaza background with subtle
// radial accent blooms and floating translucent 3D tiles.
inline void DrawDsMesh(Canvas& c, int s, uint32_t accent = 0xFF00C3FF)
{
    const float t = AnimTime();
    const uint32_t accentRgb = (accent ? accent : 0xFF00C3FFu) & 0x00FFFFFFu;

    // Soft concentric ambient glow clouds behind the hero showcase and right plaza corner
    auto drawAura = [&](int cx, int cy, int maxR, uint32_t rgb, int baseAlpha) {
        const int steps = 5;
        for (int i = steps; i >= 1; i--)
        {
            const int rx = (maxR * i) / steps;
            const int ry = (rx * 2) / 3;
            const uint32_t a = (uint32_t)(baseAlpha * (steps - i + 1) / (steps * 2));
            if (a > 0) c.FillRound(cx - rx, cy - ry, rx * 2, ry * 2, ry, (a << 24) | rgb);
        }
    };
    drawAura(c.w * 28 / 100, c.h * 27 / 100, 140 * s, accentRgb, 26);
    drawAura(c.w * 80 / 100, c.h * 66 / 100, 115 * s, 0x0038BDF8u, 18);

    // Crisp staggered dot-matrix plaza texture
    const int step = std::max(8, 8 * s);
    const int dot = std::max(1, s / 2);
    for (int y = 0; y < c.h; y += step)
    {
        const int xOff = ((y / step) % 2) * (step / 2);
        for (int x = xOff; x < c.w; x += step)
            c.FillBlend(x, y, dot, dot, 0x11FFFFFF);
    }

    // Floating translucent 3D-style Wii U / 3DS Plaza squares drifting gently in the background
    struct PlazaCube { int xPct, yPct, szUnits; float phase; uint32_t col; bool tinted; };
    static const PlazaCube cubes[] = {
        {  6, 22, 16, 0.0f, 0x1600C3FF, true },
        { 14, 74, 22, 1.1f, 0x1238BDF8, false },
        { 26, 16, 12, 2.3f, 0x12A855F7, false },
        { 74, 18, 18, 0.7f, 0x1600C3FF, true },
        { 88, 28, 14, 1.9f, 0x12F43F5E, false },
        { 82, 72, 24, 2.8f, 0x1400C3FF, true },
        { 93, 56, 15, 3.6f, 0x1438BDF8, false },
        {  4, 52, 19, 4.2f, 0x126366F1, false },
    };
    for (const auto& cb : cubes)
    {
        const int sz = cb.szUnits * s;
        const int bx = (c.w * cb.xPct) / 100 - sz / 2;
        const int by = (c.h * cb.yPct) / 100 - sz / 2 + (int)(4.0f * s * sinf(t * 1.4f + cb.phase));
        const int rad = std::max(2, 3 * s);
        const uint32_t col = cb.tinted ? ((cb.col & 0xFF000000u) | accentRgb) : cb.col;
        c.FillRound(bx, by, sz, sz, rad, col);
        c.OutlineRound(bx, by, sz, sz, rad, 1, (col & 0x00FFFFFFu) | 0x28000000u);
    }
}

// Draw Nintendo 3DS / Wii U animated corner selection brackets around a focused tile
inline void Draw3dsCornerBrackets(Canvas& c, int x, int y, int w, int h, int s, uint32_t col)
{
    const int len = 7 * s;
    const int thk = std::max(2, s + 1);
    // Top-left
    c.Fill(x, y, len, thk, col);
    c.Fill(x, y, thk, len, col);
    // Top-right
    c.Fill(x + w - len, y, len, thk, col);
    c.Fill(x + w - thk, y, thk, len, col);
    // Bottom-left
    c.Fill(x, y + h - thk, len, thk, col);
    c.Fill(x, y + h - len, thk, len, col);
    // Bottom-right
    c.Fill(x + w - len, y + h - thk, len, thk, col);
    c.Fill(x + w - thk, y + h - len, thk, len, col);
}

// Draw a physical Nintendo DS / GBA / 3DS Game Cartridge with printed sticker frame around the 32x32 ROM icon
inline void DrawPhysicalCartridge(Canvas& c, int cx, int cy, int cw, int ch, int platform, const uint32_t* icon32,
                                  const std::string& code, const std::string& name, int s, int iconScale)
{
    const bool isGba = (platform == GBA);
    const bool is3ds = false; // (no 3DS games: the 3DS tab was removed)
    const int rad = std::max(3, 3 * s);

    // Drop shadow behind cartridge body
    c.ShadowRound(cx, cy, cw, ch, rad, s, 85);

    // Molded cartridge shell (Dark charcoal for DS, Slate purple-grey for GBA, Pearl slate for 3DS)
    const uint32_t shellTop = isGba ? 0xFF302B40 : is3ds ? 0xFF382C36 : 0xFF2B3548;
    const uint32_t shellBot = isGba ? 0xFF1E1A2B : is3ds ? 0xFF241B22 : 0xFF1A2230;
    const uint32_t shellBorder = isGba ? 0xFF564B73 : is3ds ? 0xFF684B58 : 0xFF475874;
    c.FillGradV(cx, cy, cw, ch, shellTop, shellBot, rad);
    c.OutlineRound(cx, cy, cw, ch, rad, 1, shellBorder);

    // Top-left / top-right cartridge bevel notch & side grip ribs
    c.Fill(cx + 3 * s, cy + s, cw - 6 * s, std::max(1, s), 0x28FFFFFF);
    c.Fill(cx + s, cy + 5 * s, std::max(1, s), ch - 10 * s, 0x22000000);
    c.Fill(cx + cw - 2 * s, cy + 5 * s, std::max(1, s), ch - 10 * s, 0x22000000);

    // Gold PCB contact teeth peeking at bottom center of cartridge
    const int pinsW = cw / 2;
    const int pinsX = cx + (cw - pinsW) / 2;
    const int pinsH = std::max(2, 2 * s);
    c.Fill(pinsX, cy + ch - pinsH - s, pinsW, pinsH, 0xFF121722);
    for (int px = pinsX + s; px < pinsX + pinsW - s; px += 2 * s)
        c.Fill(px, cy + ch - pinsH - s, std::max(1, s), pinsH, 0xFFD4AF37);

    // Inner Recessed Cartridge Sticker Label (Crisp silver/white border so dark icons like Black/White 2 pop cleanly!)
    const int padX = 4 * s;
    const int padTop = 3 * s;
    const int padBot = 5 * s;
    const int stX = cx + padX;
    const int stY = cy + padTop;
    const int stW = cw - padX * 2;
    const int stH = ch - padTop - padBot;
    const int stRad = std::max(2, 2 * s);

    // Sticker paper frame (light silver/white rim like a real DS cartridge sticker)
    c.FillRound(stX, stY, stW, stH, stRad, 0xFFE2E8F0);
    c.OutlineRound(stX, stY, stW, stH, stRad, 1, 0xFF94A3B8);

    // Colored console header band at the top of the sticker
    const int hdrH = std::max(6, 5 * s);
    const uint32_t hdrCol = isGba ? 0xFF7C3AED : is3ds ? 0xFFE11D48 : 0xFF0284C7;
    c.FillRound(stX + s, stY + s, stW - 2 * s, hdrH, std::max(1, stRad - 1), hdrCol);
    const char* hdrText = isGba ? "GAME BOY" : is3ds ? "NINTENDO 3DS" : "NINTENDO DS";
    const int hdrFontH = std::max(10, 3 * s + 1);
    const int htw = ModernTextWidth(hdrText, hdrFontH);
    if (htw < stW - 2 * s)
        ModernText(c, stX + (stW - htw) / 2, stY + s + (hdrH - hdrFontH) / 2, hdrText, hdrFontH, 0xFFFFFFFF);

    // Dark inner artwork canvas inside the sticker below the header strip
    const int artX = stX + s;
    const int artY = stY + s + hdrH;
    const int artW = stW - 2 * s;
    const int artH = stH - 2 * s - hdrH;
    c.Fill(artX, artY, artW, artH, 0xFF141B28);

    const int iconSz = 32 * iconScale;
    const int ix = artX + (artW - iconSz) / 2;
    const int iy = artY + (artH - iconSz) / 2;

    if (icon32)
    {
        // Crisp 1px frame around the 32x32 ROM icon inside the sticker
        c.OutlineRound(ix - 1, iy - 1, iconSz + 2, iconSz + 2, 1, 1, 0xFF475569);
        for (int py = 0; py < 32; py++)
        {
            for (int px = 0; px < 32; px++)
            {
                const uint32_t p = icon32[py * 32 + px];
                if ((p >> 24) >= 0x40)
                    c.Fill(ix + px * iconScale, iy + py * iconScale, iconScale, iconScale, p | 0xFF000000u);
                else
                {
                    // Soft silver-slate backing for transparent icon pixels (like DS OS icon background)
                    const uint32_t bg = ((px / 4 + py / 4) & 1) ? 0xFF1E293B : 0xFF172030;
                    c.Fill(ix + px * iconScale, iy + py * iconScale, iconScale, iconScale, bg);
                }
            }
        }
    }
    else
    {
        std::string initials;
        for (char ch : name)
            if (isupper((unsigned char)ch) && initials.size() < 4) initials += ch;
        if (initials.empty() && !code.empty()) initials = code;
        const int is = 18 * s / 3;
        const int iw = ModernTextWidth(initials, is);
        ModernText(c, artX + (artW - iw) / 2, artY + (artH - is) / 2, initials, is, 0xFFCBD5E1, true);
    }
}

// Shorten a game title for compact card footers (e.g. "Pokémon Edición Negra" -> "Edición Negra")
inline std::string ShortCardTitle(const std::string& rawName)
{
    std::string nice = TitleCase(rawName);
    for (const char* pfx : { "Pokémon ", "Pokemon " })
    {
        const size_t n = strlen(pfx);
        if (nice.size() > n + 2 && !nice.compare(0, n, pfx))
        {
            nice = nice.substr(n);
            break;
        }
    }
    return nice;
}

// Renders the Nintendo 3DS / DSi / Wii U Upper Showcase Screen (3D Bevel Display + Cartridge Showcase + Launch Panel)
// Subtle game-specific accent colour so each Pokemon title feels distinctive on its card and banner.
inline uint32_t GameAccentTint(const std::string& name, int platform)
{
    std::string lo;
    lo.reserve(name.size());
    for (unsigned char ch : name) lo += (char)tolower(ch);
    if (lo.find("soulsilver") != std::string::npos || lo.find("plata") != std::string::npos || lo.find("argent") != std::string::npos) return 0xFF94A3B8;
    if (lo.find("heartgold") != std::string::npos || lo.find("oro") != std::string::npos || lo.find("or ") != std::string::npos) return 0xFFF59E0B;
    if (lo.find("platinum") != std::string::npos || lo.find("platino") != std::string::npos || lo.find("platine") != std::string::npos) return 0xFF38BDF8;
    if (lo.find("black") != std::string::npos || lo.find("negr") != std::string::npos || lo.find("noir") != std::string::npos || lo.find("schwarz") != std::string::npos) return 0xFF6366F1;
    if (lo.find("white") != std::string::npos || lo.find("blanc") != std::string::npos || lo.find("weiss") != std::string::npos) return 0xFFE2E8F0;
    if (lo.find("emerald") != std::string::npos || lo.find("esmeralda") != std::string::npos || lo.find("emeraude") != std::string::npos || lo.find("smaragd") != std::string::npos) return 0xFF10B981;
    if (lo.find("firered") != std::string::npos || lo.find("rojo") != std::string::npos || lo.find("rouge") != std::string::npos || lo.find("feuerrot") != std::string::npos || lo.find("ruby") != std::string::npos) return 0xFFEF4444;
    if (lo.find("leafgreen") != std::string::npos || lo.find("hoja") != std::string::npos || lo.find("vert") != std::string::npos || lo.find("blatt") != std::string::npos) return 0xFF22C55E;
    if (lo.find("diamond") != std::string::npos || lo.find("diamant") != std::string::npos) return 0xFF60A5FA;
    if (lo.find("pearl") != std::string::npos || lo.find("perla") != std::string::npos || lo.find("perle") != std::string::npos) return 0xFFF472B6;
    return platform == GBA ? 0xFFA855F7 : 0xFF00C3FF;
}

inline void DrawDsiTopBanner(Canvas& c, int x, int y, int w, int h, const Game& g, int s, uint32_t accent, int gameIdx = 0, int totalGames = 0)
{
    const bool isGba = (g.platform == GBA);
    const bool is3ds = false; // (no 3DS games: the 3DS tab was removed)
    const uint32_t gameTint = GameAccentTint(g.name, g.platform);
    const uint32_t platAccent = gameTint ? gameTint : (isGba ? 0xFFA855F7u : is3ds ? 0xFFF43F5Eu : (accent ? accent : 0xFF00C3FFu));
    const float t = AnimTime();

    // 3DS Stereo Speaker Grille Dots on left and right outside the upper display frame
    for (int side = 0; side < 2; side++)
    {
        const int gx = (side == 0) ? (x - 9 * s) : (x + w + 5 * s);
        const int gy = y + h / 2 - 4 * s;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 2; col++)
                c.FillRound(gx + col * 3 * s, gy + row * 4 * s, std::max(2, s + 1), std::max(2, s + 1), 1, 0xFF28364E);
    }

    // Upper 3DS / Wii U Display Bevel Frame tinted softly to the highlighted title
    const int rad = 5 * s;
    const uint32_t glassTop = (MixArgb(0xFF172338u, platAccent, 22, 100) & 0x00FFFFFFu) | 0xF0000000u;
    const uint32_t glassBot = (MixArgb(0xFF0E1524u, platAccent, 8, 100) & 0x00FFFFFFu) | 0xF0000000u;
    const uint32_t glassBd = MixArgb(0xFF334766u, platAccent, 42, 100);
    c.ShadowRound(x, y, w, h, rad, 2 * s, 95);
    c.GlowRound(x, y, w, h, rad, 2 * s, (platAccent & 0x00FFFFFFu) | 0x26000000u);
    c.FillGlass(x, y, w, h, rad, glassTop, glassBot, glassBd, 1);
    c.OutlineRound(x + s, y + s, w - 2 * s, h - 2 * s, rad - s, 1, 0x36FFFFFF);
    // Glowing game-colored accent bar along left inner edge
    c.FillRound(x + 3 * s, y + 4 * s, std::max(3, s + 1), h - 8 * s, std::max(1, s / 2), platAccent);

    // Left: Floating 3D Game Cartridge / Box Art Medallion inside the Upper Screen
    const int cartH = h - 8 * s;
    const int cartW = cartH - 2 * s;
    const int cartX = x + 8 * s;
    const int cartY = y + 4 * s + (int)(1.5f * sinf(t * 3.0f));
    c.GlowRound(cartX - 2 * s, cartY - 2 * s, cartW + 4 * s, cartH + 4 * s, 4 * s, 3 * s, (platAccent & 0x00FFFFFFu) | 0x36000000u);
    DrawPhysicalCartridge(c, cartX, cartY, cartW, cartH, g.platform, g.hasIcon ? g.icon : nullptr, g.code, g.name, s, 2);

    // Center: Console Family + Bold Game Title + Feature/Serial/Save Pills
    const int contentX = cartX + cartW + 8 * s;
    const int rightPanelW = 76 * s;
    const int rpx = x + w - rightPanelW - 4 * s;
    const int maxTextW = w - (contentX - x) - rightPanelW - 8 * s;

    const std::string subHeader = isGba ? "GAME BOY ADVANCE" : is3ds ? "NINTENDO 3DS" : "NINTENDO DS";
    const int subFontH = std::max(14, 5 * s);

    std::string title = TitleCase(g.name);
    int titleH = std::max(24, 11 * s);
    while (titleH > 18 && ModernTextWidth(title, titleH) > maxTextW) --titleH;

    const int pillH = 12 * s;
    const int metaFontH = std::max(15, 5 * s + 1);
    const int totalTextH = subFontH + 2 * s + titleH + 3 * s + pillH;
    const int startY = y + (h - totalTextH) / 2;

    // Line 0: Console family subheader tinted cleanly to the game accent
    ModernText(c, contentX, startY, subHeader, subFontH, MixArgb(0xFFFFFFFFu, platAccent, 65, 100), true);

    // Line 1: Bold pure white game title
    const int titleY = startY + subFontH + 2 * s;
    ModernText(c, contentX, titleY, title, titleH, 0xFFFFFFFF, true);

    // Line 2: Metadata Pill Badges (Widescreen Mode  •  Serial  •  Save Status)
    int curX = contentX;
    const int pillY = titleY + titleH + 3 * s;

    auto drawPill = [&](const std::string& label, uint32_t bgCol, uint32_t borderCol, uint32_t textCol) {
        const int tw = ModernTextWidth(label, metaFontH);
        const int pw = tw + 8 * s;
        if (curX + pw > rpx - 4 * s) return;
        c.FillRound(curX, pillY, pw, pillH, 2 * s, bgCol);
        c.OutlineRound(curX, pillY, pw, pillH, 2 * s, 1, borderCol);
        ModernText(c, curX + 4 * s, pillY + (pillH - metaFontH) / 2, label, metaFontH, textCol);
        curX += pw + 4 * s;
    };

    const std::string modeTag = ToNiceTitle(UiLang::Tr(isGba ? "WIDESCREEN" : "WIDESCREEN 3D"));
    const uint32_t modeBg = (platAccent & 0x00FFFFFFu) | 0x34000000u;
    const uint32_t modeBd = (platAccent & 0x00FFFFFFu) | 0x98000000u;
    const uint32_t modeFg = MixArgb(0xFFFFFFFFu, platAccent, 38, 100);
    drawPill(modeTag, modeBg, modeBd, modeFg);

    const std::string serial = (isGba ? "AGB-" : is3ds ? "CTR-" : "NTR-") + g.code;
    drawPill(serial, 0x551E293B, 0x99384A66, 0xFFE2E8F0);

    const std::string rawStatus = g.comingSoon ? UiLang::Tr("COMING SOON") : !g.known ? UiLang::Tr("NOT PLAYABLE") : g.hasSave ? UiLang::Tr("SAVE FOUND") : UiLang::Tr("NEW GAME");
    const std::string status = ToNiceTitle(rawStatus);
    const uint32_t statusFg = !g.known && !g.comingSoon ? 0xFFFF6B6B : g.comingSoon ? 0xFFD8B4FE : g.hasSave ? 0xFF34D399 : 0xFFFBBF24;
    const uint32_t statusBg = !g.known && !g.comingSoon ? 0x33FF453A : g.comingSoon ? 0x33A855F7 : g.hasSave ? 0x3300E676 : 0x33FFB020;
    const uint32_t statusBd = !g.known && !g.comingSoon ? 0x80FF453A : g.comingSoon ? 0x80A855F7 : g.hasSave ? 0x8000E676 : 0x80FFB020;
    drawPill(status, statusBg, statusBd, statusFg);

    // Right side of Upper 3DS Screen: Wii U / 3DS Slot & Launch Prompt Panel
    const int rpy = y + 4 * s;
    const int rph = h - 8 * s;
    c.FillRound(rpx, rpy, rightPanelW, rph, 3 * s, 0x700B101Bu);
    c.OutlineRound(rpx, rpy, rightPanelW, rph, 3 * s, 1, MixArgb(0xFF283952u, platAccent, 35, 100));

    if (totalGames > 0 && gameIdx >= 0)
    {
        char countBuf[32];
        snprintf(countBuf, sizeof(countBuf), "%02d / %02d", gameIdx + 1, totalGames);
        const std::string countStr = countBuf;
        const int countFontH = std::max(13, 5 * s);
        const int cw = ModernTextWidth(countStr, countFontH);
        ModernText(c, rpx + (rightPanelW - cw) / 2, rpy + 3 * s, countStr, countFontH, 0xFFCBD5E1, true);
    }

    // Animated 3DS-style sound wave bars in the middle of the right panel
    const int barsCount = 9;
    const int barW = 2 * s, barGap = s;
    const int barsTotalW = barsCount * barW + (barsCount - 1) * barGap;
    const int barsX = rpx + (rightPanelW - barsTotalW) / 2;
    const int barsBaseY = rpy + rph / 2 + 2 * s;
    for (int b = 0; b < barsCount; b++)
    {
        const float wave = 0.45f + 0.55f * fabsf(sinf(t * 3.8f + b * 0.65f));
        const int bh = std::max(2 * s, (int)(wave * 7 * s));
        c.FillRound(barsX + b * (barW + barGap), barsBaseY - bh, barW, bh, 1, platAccent);
    }

    // Bottom of right panel: "[ A ] Jugar" action pill
    const std::string playLabel = "A: " + ToNiceTitle(UiLang::Tr("PLAY"));
    const int playFontH = std::max(16, 5 * s + 2);
    const int playW = ModernTextWidth(playLabel, playFontH) + 12 * s;
    const int playH = 12 * s;
    const int playX = rpx + (rightPanelW - playW) / 2;
    const int playY = rpy + rph - playH - 3 * s;
    c.FillRound(playX, playY, playW, playH, 2 * s, (platAccent & 0x00FFFFFFu) | 0x5C000000u);
    c.OutlineRound(playX, playY, playW, playH, 2 * s, 1, platAccent);
    ModernText(c, playX + 6 * s, playY + (playH - playFontH) / 2, playLabel, playFontH, 0xFFFFFFFF, true);
}

// Renders a glossy Wii U / Nintendo 3DS Channel Plaza Tile with physical cartridge & title footer
inline void DrawCartridgeCase(Canvas& c, int x, int y, int tileW, int tileH, int platform, const uint32_t* icon32,
                              const std::string& code, const std::string& name, int s, bool hasSave = false, bool sel = false)
{
    const bool isGba = (platform == GBA);
    const bool is3ds = false; // (no 3DS games: the 3DS tab was removed)
    const uint32_t accent = GameAccentTint(name, platform);

    // Rich 3DS / Wii U channel tile gradient surface subtly tinted per title
    const uint32_t baseTop = isGba ? (sel ? 0xFF2B1D46u : 0xFF1F1634u) : is3ds ? 0xFF2E1522u : (sel ? 0xFF1F2E48u : 0xFF151F33u);
    const uint32_t baseBot = isGba ? 0xFF120B22u : is3ds ? 0xFF180A12u : 0xFF0C1320u;
    const uint32_t gradTop = MixArgb(baseTop, accent, sel ? 30 : 18, 100);
    const uint32_t gradBot = MixArgb(baseBot, accent, sel ? 10 : 5, 100);
    const int rad = std::max(4, 5 * s);
    c.FillGradV(x, y, tileW, tileH, gradTop, gradBot, rad);
    c.OutlineRound(x, y, tileW, tileH, rad, 1, sel ? MixArgb(0xFFFFFFFFu, accent, 55, 100) : MixArgb(0xFF2E415Eu, accent, 28, 100));

    // Top per-game jewel accent hairline and curved gloss dome reflection
    c.FillRound(x + 2 * s, y + 2 * s, tileW - 4 * s, tileH / 4, std::max(2, rad - s), 0x12FFFFFF);
    c.Fill(x + rad, y + s, tileW - 2 * rad, std::max(1, s / 2 + 1), (accent & 0x00FFFFFFu) | (sel ? 0xB8000000u : 0x60000000u));

    // Platform pill badge in top-left
    const char* badge = isGba ? "GBA" : is3ds ? "3DS" : "NDS";
    const uint32_t badgeBg = isGba ? 0x66A855F7 : is3ds ? 0x66F43F5E : 0x6600C3FF;
    const uint32_t badgeBorder = isGba ? 0xCCA855F7 : is3ds ? 0xCCF43F5E : 0xCC00C3FF;
    const uint32_t badgeCol = isGba ? 0xFFE9D5FF : is3ds ? 0xFFFECDD3 : 0xFFBAE6FD;
    const int badgeH = 12 * s;
    const int badgeFontH = std::max(14, 5 * s + 1);
    const int badgeW = ModernTextWidth(badge, badgeFontH) + 10 * s;
    const int bx = x + 4 * s, by = y + 4 * s;
    c.FillRound(bx, by, badgeW, badgeH, 2 * s, badgeBg);
    c.OutlineRound(bx, by, badgeW, badgeH, 2 * s, 1, badgeBorder);
    ModernText(c, bx + 5 * s, by + (badgeH - badgeFontH) / 2, badge, badgeFontH, badgeCol, true);

    // Save status LED in top-right corner when a save file exists
    if (hasSave)
    {
        const int dotR = std::max(3, 2 * s + 1);
        const int dx = x + tileW - 6 * s - dotR * 2;
        const int dy = by + (badgeH - dotR * 2) / 2;
        c.FillRound(dx - s, dy - s, (dotR + s) * 2, (dotR + s) * 2, dotR + s, 0x5000E676);
        c.FillRound(dx, dy, dotR * 2, dotR * 2, dotR, 0xFF00E676);
        const int spec = std::max(1, dotR / 2);
        c.FillRound(dx + spec, dy + spec, spec + 1, spec + 1, spec, 0xCCFFFFFF);
    }

    // Physical Cartridge Silhouette sized strictly to the clear band between the top badge row
    // and the frosted title footer so the badge and save dot never overlap the shell.
    const int footerH = 26 * s;
    const int topClearY = by + badgeH + 3 * s;
    const int availCartW = std::max(40 * s, tileW - 14 * s);
    const int availCartH = std::max(40 * s, (y + tileH - footerH - 3 * s) - topClearY);
    int iconScale = 5;
    while (iconScale > 2 && (32 * iconScale + 12 * s > availCartW || 32 * iconScale + 18 * s > availCartH))
        --iconScale;
    const int iconSz = 32 * iconScale;
    const int cartW = iconSz + 12 * s;
    const int cartH = iconSz + 18 * s;
    const int cx = x + (tileW - cartW) / 2;
    const int cy = topClearY + std::max(0, (availCartH - cartH) / 2);

    const uint32_t glowCol = (accent & 0x00FFFFFFu) | (sel ? 0x42000000u : 0x26000000u);
    c.GlowRound(cx - 2 * s, cy - 2 * s, cartW + 4 * s, cartH + 4 * s, 4 * s, 3 * s, glowCol);
    DrawPhysicalCartridge(c, cx, cy, cartW, cartH, platform, icon32, code, name, s, iconScale);

    // Frosted Wii U / 3DS Channel Footer Banner with game title and serial code
    const int fy = y + tileH - footerH;
    c.FillBlend(x + s, fy, tileW - 2 * s, footerH - s, 0x8E080D18);
    c.Fill(x + 3 * s, fy, tileW - 6 * s, 1, sel ? accent : ((accent & 0x00FFFFFFu) | 0x55000000u));

    std::string cardTitle = ShortCardTitle(name);
    int nameFontH = std::max(18, (tileW >= 300 ? 9 : 8) * s);
    while (nameFontH > std::max(13, 5 * s) && ModernTextWidth(cardTitle, nameFontH) > tileW - 10 * s)
        --nameFontH;
    if (ModernTextWidth(cardTitle, nameFontH) > tileW - 10 * s)
    {
        while (cardTitle.size() > 4 && ModernTextWidth(cardTitle + "...", nameFontH) > tileW - 10 * s)
        {
            while (!cardTitle.empty() && ((unsigned char)cardTitle.back() & 0xC0) == 0x80) cardTitle.pop_back();
            if (!cardTitle.empty()) cardTitle.pop_back();
        }
        cardTitle += "...";
    }
    const int nw = ModernTextWidth(cardTitle, nameFontH);
    ModernText(c, x + (tileW - nw) / 2, fy + 3 * s, cardTitle, nameFontH, sel ? 0xFFFFFFFF : 0xFFE2E8F0, true);

    std::string serial = (isGba ? "AGB-" : is3ds ? "CTR-" : "NTR-") + code;
    const int serH = std::max(13, 4 * s + 1);
    const int swidth = ModernTextWidth(serial, serH);
    ModernText(c, x + (tileW - swidth) / 2, fy + 3 * s + nameFontH + s, serial, serH, sel ? 0xFF93C5FD : 0xFF64748B);
}

// Where the list's parts go on a canvas: the title band, the tabs under it, the 3DS/DSi upper
// showcase screen, and the dynamic Wii U / 3DS channel plaza grid below.
struct Grid
{
    int w = 0, h = 0;
    int s = 1;
    int tabsY = 0, tabsH = 0;         // Category tabs bar
    int bannerY = 0, bannerH = 0;     // Upper 3DS / DSi Showcase Screen
    int carouselY = 0;                // Lower Wii U / 3DS Plaza Grid Y
    int tile = 0;                     // Card width (dynamically scaled in MakeView)
    int tileH = 0;                    // Card height (dynamically scaled in MakeView)
    int colGap = 0;                   // Gap between cards
    int labelH = 0;                   // Height reserved below card
    int dockH = 0;                    // Bottom controller dock height
    int top = 0, bottom = 0;
};
inline Grid Layout(const Canvas& c)
{
    Grid g;
    g.w = c.w;
    g.h = c.h;
    g.s = std::max(1, std::min(c.w, c.h) / 240); // 3 at 720 lines
    g.colGap = 9 * g.s;                          // default 27px gap
    g.labelH = 8 * g.s;                          // 24px for selection indicator pill
    g.tabsY = 18 * g.s;                          // 54px
    g.tabsH = 15 * g.s;                          // 45px
    g.bannerY = g.tabsY + g.tabsH + 3 * g.s;     // 108px
    g.bannerH = 44 * g.s;                        // 132px Upper 3DS Showcase Display
    g.dockH = 22 * g.s;                          // 66px
    g.bottom = c.h - g.dockH;                    // 654px
    g.tile = 84 * g.s;                           // default 252px card width
    g.tileH = 100 * g.s;                         // default 300px card height
    const int stageTop = g.bannerY + g.bannerH + 3 * g.s;
    const int stageH = g.bottom - stageTop - g.labelH;
    g.carouselY = stageTop + std::max(0, (stageH - g.tileH) / 2);
    g.top = g.carouselY;
    return g;
}

// What one tab shows: the horizontal carousel spots of games belonging to this tab.
struct View
{
    struct Spot { int game, x, y, section; };
    struct Head { int platform, y; bool empty; };
    Grid g;
    int tab = 0;
    std::vector<Spot> spots;
    std::vector<Head> heads;
    int width = 0;
    int height = 0;
    int SpotOf(int game) const
    {
        for (size_t k = 0; k < spots.size(); k++)
            if (spots[k].game == game) return (int)k;
        return -1;
    }
};
inline View MakeView(const Canvas& c, const std::vector<Game>& games, int tab)
{
    View v;
    v.g = Layout(c);
    v.tab = tab;
    Grid& g = v.g;
    std::vector<int> mine;
    for (size_t i = 0; i < games.size(); i++)
        if (InTab(games[i].platform, tab))
            mine.push_back((int)i);

    if (mine.empty())
    {
        v.heads.push_back({ tab <= 0 ? DS : tab - 1, g.carouselY, true });
        v.width = 0;
        v.height = 0;
        return v;
    }

    // Dynamically scale card dimensions so 1-4 games expand to fill the 1280x720 Wii U / 3DS stage!
    const int s = g.s;
    const int stageTop = g.bannerY + g.bannerH + 4 * s;
    const int availH = std::max(60 * s, g.bottom - stageTop - g.labelH - 4 * s);
    const int availW = c.w - 44 * s;
    const int n = (int)mine.size();

    if (n <= 3)
    {
        g.colGap = 12 * s; // 36px at 720p
        g.tile = std::min(116 * s, (availW - (n - 1) * g.colGap) / std::max(1, n)); // 348px at 720p
        g.tileH = std::min(108 * s, availH);                                         // 324px at 720p
    }
    else if (n == 4)
    {
        g.colGap = 8 * s;  // 24px at 720p
        g.tile = (availW - 3 * g.colGap) / 4;                                        // ~268px at 720p
        g.tileH = std::min(104 * s, availH);                                         // 312px at 720p
    }
    else
    {
        g.colGap = 8 * s;
        g.tile = 84 * s;                                                             // 252px at 720p
        g.tileH = std::min(100 * s, availH);                                         // 300px at 720p
    }
    g.carouselY = stageTop + std::max(0, (g.bottom - stageTop - g.labelH - g.tileH) / 2);
    g.top = g.carouselY;

    const int totalW = n * g.tile + (n - 1) * g.colGap;
    const int startX = (totalW <= c.w - 36 * s) ? (c.w - totalW) / 2 : 22 * s;

    for (size_t k = 0; k < mine.size(); k++)
    {
        const int x = startX + (int)k * (g.tile + g.colGap);
        v.spots.push_back({ mine[k], x, g.carouselY, games[mine[k]].platform });
    }
    v.width = totalW;
    v.height = g.tileH + g.labelH;
    return v;
}

// Maximum horizontal carousel scroll offset for a view.
inline int MaxScroll(const View& v)
{
    if (v.spots.empty()) return 0;
    const Grid& g = v.g;
    const int screenW = g.w > 0 ? g.w : 1280;
    if (v.width <= screenW - 48 * g.s) return 0;
    return std::max(0, v.spots.back().x + g.tile - screenW + 24 * g.s);
}

// Keeps the chosen game in view by centering it horizontally in the carousel.
inline void FollowSelection(const View& v, int selected, int& scroll, bool follow = true)
{
    if (v.spots.empty()) { scroll = 0; return; }
    const Grid& g = v.g;
    const int screenW = g.w > 0 ? g.w : 1280;
    const int maxScroll = MaxScroll(v);
    if (maxScroll == 0)
    {
        const int diff = 0 - scroll;
        if (std::abs(diff) <= 2) scroll = 0;
        else scroll += diff / 3;
        return;
    }
    const int k = follow ? v.SpotOf(selected) : -1;
    if (k >= 0)
    {
        const int cardCenterX = v.spots[k].x + g.tile / 2;
        int targetScroll = cardCenterX - screenW / 2;
        targetScroll = std::max(0, std::min(targetScroll, maxScroll));
        const int diff = targetScroll - scroll;
        if (std::abs(diff) <= 2) scroll = targetScroll;
        else scroll += diff / 3;
    }
}

// The game whose card in the horizontal carousel is at x, y on the canvas, or -1.
inline int TileAt(const View& v, int scroll, int x, int y)
{
    const Grid& g = v.g;
    if (y < g.carouselY || y >= g.carouselY + g.tileH + g.labelH) return -1;
    for (const View::Spot& sp : v.spots)
    {
        const int tx = sp.x - scroll;
        if (x >= tx && x < tx + g.tile) return sp.game;
    }
    return -1;
}

// Where the tabs are drawn (TabsDraw) and which one is at x, y, or -1.
inline int TabX(const Canvas& c, const Grid& g, int t)
{
    const int step = 22 * 6 * g.s / 3;
    return c.w / 2 + (t * 2 - (TABS - 1)) * step / 2;
}
inline int TabAt(const Canvas& c, int x, int y)
{
    const Grid g = Layout(c);
    if (y < g.tabsY - 3 * g.s || y >= g.tabsY + g.tabsH + 3 * g.s) return -1;
    const int step = 22 * 6 * g.s / 3;
    for (int t = 0; t < TABS; t++)
        if (std::abs(x - TabX(c, g, t)) < step / 2) return t;
    return -1;
}

inline bool SettingsPillRect(const Canvas& c, int& px, int& py, int& pw, int& ph)
{
    const Grid g = Layout(c);
    const int s = g.s;
    if (c.w < 640) return false;
    const int bandH = (c.h > c.w ? 43 : 16) * s;
    const std::string label = "Y: " + ToNiceTitle(UiLang::Tr("SETTINGS"));
    const int fontH = std::max(12, 5 * s - 1);
    pw = ModernTextWidth(label, fontH) + 10 * s;
    ph = 11 * s;
    const int reservedRight = 72 * s;
    px = c.w - reservedRight - pw - 10 * s;
    py = (bandH - ph) / 2;
    return px >= c.w / 2 - 40 * s;
}

inline bool TapInSettingsPill(const Canvas& c, int x, int y)
{
    int px = 0, py = 0, pw = 0, ph = 0;
    if (!SettingsPillRect(c, px, py, pw, ph)) return false;
    const int s = Layout(c).s;
    return x >= px - 4 * s && x < px + pw + 4 * s && y >= py - 3 * s && y < py + ph + 3 * s;
}

inline bool TapInBannerPlay(const Canvas& c, int x, int y)
{
    const Grid g = Layout(c);
    if (g.bannerH <= 0) return false;
    const int s = g.s;
    const int bx = 24 * s, bw = c.w - 48 * s;
    const int rightPanelW = 76 * s;
    const int rpx = bx + bw - rightPanelW - 4 * s;
    const int rpy = g.bannerY + 4 * s;
    const int rph = g.bannerH - 8 * s;
    return x >= rpx && x < rpx + rightPanelW && y >= rpy && y < rpy + rph;
}

inline int CarouselArrowAt(const Canvas& c, const View& v, int scroll, int x, int y)
{
    const int s = v.g.s;
    const int arrW = 13 * s, arrH = 22 * s;
    const int arrY = v.g.carouselY + (v.g.tileH - arrH) / 2;
    if (y < arrY - 6 * s || y >= arrY + arrH + 6 * s) return 0;
    if (scroll > 0 && x >= 2 * s && x < 22 * s + arrW) return -1;
    if (scroll + c.w < v.width + 24 * s && x >= c.w - arrW - 22 * s && x < c.w - 2 * s) return 1;
    return 0;
}

inline bool SettingsBackPillRect(const Canvas& c, int& px, int& py, int& pw, int& ph)
{
    if (c.w < 640) return false;
    const int s = std::max(1, std::min(c.w, c.h) / 240);
    const int bandH = (c.h > c.w ? 43 : 22) * s;
    const std::string label = "B: " + ToNiceTitle(UiLang::Tr("BACK"));
    const int fontH = std::max(13, 5 * s);
    pw = ModernTextWidth(label, fontH) + 10 * s;
    ph = 12 * s;
    px = 24 * s;
    py = (bandH - ph) / 2;
    return true;
}

inline bool TapInSettingsBack(const Canvas& c, int x, int y)
{
    int px = 0, py = 0, pw = 0, ph = 0;
    if (!SettingsBackPillRect(c, px, py, pw, ph)) return false;
    const int s = std::max(1, std::min(c.w, c.h) / 240);
    return x >= px - 4 * s && x < px + pw + 6 * s && y >= py - 4 * s && y < py + ph + 4 * s;
}

// Step in carousel: Left (1) and Right (2) traverse the games in the carousel;
// Up (3) jumps seamlessly up to the category tabs bar; Down (4) stays on card.
inline int Step(const View& v, int selected, int dir)
{
    const int k = v.SpotOf(selected);
    if (k < 0) return v.spots.empty() ? -1 : v.spots[0].game;
    if (dir == 1) return k > 0 ? v.spots[k - 1].game : selected;
    if (dir == 2) return k + 1 < (int)v.spots.size() ? v.spots[k + 1].game : selected;
    if (dir == 3) return -1; // up to category tabs!
    return selected;
}

// A tab's own first game (when the one chosen is not in it), or -1 when it has none.
inline int FirstIn(const View& v) { return v.spots.empty() ? -1 : v.spots[0].game; }

// The list, drawn over the whole canvas (1280x720 on the Switch): the title band, the tabs, then
// the tab's games as tiles, each the game's picture (or its DS icon, enlarged) with its name and
// whether it has a save, under each console's heading on ALL. selected: -1 for none. scroll is
// kept so the chosen game is in view (follow false: as it is, a finger having scrolled the list).
// hint: the line along the bottom (the Switch's buttons when empty). onTabs: the tabs are chosen
// (up from the first row of games; left and right then change the tab), the shown one framed.
inline void Draw(Canvas& c, const std::vector<Game>& games, int tab, int selected, int& scroll, const std::string& note,
                 const std::string& hintText = std::string(), const Theme& th = Theme(), bool follow = true, bool onTabs = false)
{
    const View v = MakeView(c, games, tab);
    const Grid& grid = v.g;
    const int s = grid.s;
    const bool hasSelected = !games.empty() && selected >= 0 && selected < (int)games.size();
    const uint32_t heroAccent = hasSelected ? GameAccentTint(games[selected].name, games[selected].platform) : th.frame;
    // Blend game accent (80%) with active theme frame (20%) so tabs/cards stay vivid yet cohesive.
    const uint32_t spotAccent = !hasSelected ? th.frame : (0xFF000000u
        | (((((heroAccent >> 16) & 0xFFu) * 4u + ((th.frame >> 16) & 0xFFu)) / 5u) << 16)
        | (((((heroAccent >> 8) & 0xFFu) * 4u + ((th.frame >> 8) & 0xFFu)) / 5u) << 8)
        | (((heroAccent & 0xFFu) * 4u + (th.frame & 0xFFu)) / 5u));
    const uint32_t bgTop = (th.bg == 0xFF0D1017) ? 0xFF141E33 : th.bg;
    const uint32_t bgBot = 0xFF090E1B;
    c.FillGradV(0, 0, c.w, c.h, bgTop, bgBot, 0);
    DrawDsMesh(c, s, heroAccent);
    if (th.background)
        if (const uint32_t* bgPx = th.background->Fitted(c.w, c.h)) std::copy(bgPx, bgPx + (size_t)c.w * c.h, c.px.begin());

    // Nintendo DS / DSi top header bar with twin-screen logo and date / battery widget.
    const int bandH = (c.h > c.w ? 43 : 16) * s;
    c.FillGradV(0, 0, c.w, bandH, 0xFF1A2130, th.band, 0);
    c.Fill(0, bandH, c.w, th.bandLineH * s, th.bandLine);
    c.FillBlend(0, bandH + th.bandLineH * s, c.w, 2 * s, 0x44000000);

    const int logoH = (4 * s + 1) * 2 + std::max(1, s + 1);
    DrawDsLogo(c, 10 * s, (bandH - logoH) / 2, s, th.title);
    const std::string title = "NoSplitNX";
    const int topFontH = std::max(20, 8 * s);
    c.ModernText(21 * s, (bandH - topFontH) / 2, title, topFontH, th.title, true);

    // Edition pill badge beside NoSplitNX
    const int titleW = c.ModernTextWidth(title, topFontH);
    const std::string subTag = UiLang::Tr("WIDESCREEN");
    const int tagFontH = std::max(13, 5 * s);
    const int tagW = c.ModernTextWidth(subTag, tagFontH) + 8 * s;
    const int tagH = 8 * s + 2;
    const int tagX = 21 * s + titleW + 5 * s;
    const int tagY = (bandH - tagH) / 2;
    c.FillRound(tagX, tagY, tagW, tagH, 2 * s, 0x3300C3FF);
    c.OutlineRound(tagX, tagY, tagW, tagH, 2 * s, 1, 0x8000C3FF);
    c.ModernText(tagX + (tagW - c.ModernTextWidth(subTag, tagFontH)) / 2, tagY + (tagH - tagFontH) / 2, subTag, tagFontH, 0xFF7DD3FC);

    // Quick-access Settings pill in top-right beside the Clock widget (touch & visual shortcut)
    {
        int sx = 0, sy = 0, sw = 0, sh = 0;
        if (SettingsPillRect(c, sx, sy, sw, sh) && sx > tagX + tagW + 8 * s)
        {
            const std::string settLabel = "Y: " + ToNiceTitle(UiLang::Tr("SETTINGS"));
            const int settFontH = std::max(12, 5 * s - 1);
            c.FillGlass(sx, sy, sw, sh, 3 * s, 0xEE223048, 0xEE172033, th.frame, 1);
            c.ModernText(sx + 5 * s, sy + (sh - settFontH) / 2, settLabel, settFontH, th.title, true);
        }
    }

    // Right: Date, Time & DS 3-bar battery widget
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    bool haveTime = false;
#ifdef __SWITCH__
    haveTime = SwitchPlatform::LocalTime(yr, mo, dy, hr, mn, sc);
#else
    time_t raw = time(nullptr);
    if (tm* lt = localtime(&raw))
    {
        yr = lt->tm_year + 1900; mo = lt->tm_mon + 1; dy = lt->tm_mday;
        hr = lt->tm_hour; mn = lt->tm_min; sc = lt->tm_sec;
        haveTime = true;
    }
#endif
    (void)yr; (void)sc;
    const float t = AnimTime();
    const float pulse = 0.5f + 0.5f * sinf(t * 3.5f);
    const int floatY = (int)(3.0f * sinf(t * 2.8f));

    if (haveTime)
    {
        char dateBuf[16], timeBuf[16];
        snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d", mo, dy);
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hr, mn);
        const std::string dateStr = dateBuf;
        const std::string timeStr = timeBuf;
        const int dateFontH = std::max(10, 4 * s);
        const int timeFontH = std::max(12, 5 * s - 1);
        const int dateW = ModernTextWidth(dateStr, dateFontH);
        const int timeW = ModernTextWidth(timeStr, timeFontH);
        const int clockH = 11 * s;
        const int clockW = dateW + timeW + 34 * s;
        const int clockX = c.w - clockW - 10 * s;
        const int clockY = (bandH - clockH) / 2;
        c.FillRound(clockX, clockY, clockW, clockH, 3 * s, 0xFF1F2737);
        c.OutlineRound(clockX, clockY, clockW, clockH, 3 * s, 1, 0xFF33425B);
        ModernText(c, clockX + 5 * s, clockY + (clockH - dateFontH) / 2, dateStr, dateFontH, th.hint);
        c.Fill(clockX + 8 * s + dateW, clockY + 2 * s, 1, clockH - 4 * s, 0xFF33425B);
        ModernText(c, clockX + 11 * s + dateW, clockY + (clockH - timeFontH) / 2, timeStr, timeFontH, th.title, true);
        DrawDsBattery(c, clockX + clockW - 19 * s, clockY + (clockH - 8 * s) / 2, s, 3);
    }

    // Nintendo DSi Upper Display Game Showcase Card
    if (grid.bannerH > 0 && hasSelected)
    {
        const int spotIdx = v.SpotOf(selected);
        DrawDsiTopBanner(c, 24 * s, grid.bannerY, c.w - 48 * s, grid.bannerH, games[selected], s, heroAccent,
                         spotIdx >= 0 ? spotIdx : 0, (int)v.spots.size());
    }

    // Segmented pill bar for console tabs (ALL, DS, GBA, 3DS) with tactile L / R bumper badges.
    const int tabStep = 22 * 6 * s / 3;
    const int segW = TABS * tabStep + 8 * s;
    const int segX = (c.w - segW) / 2;
    const int segY = grid.tabsY - s;
    const int segH = grid.tabsH + 2 * s;
    c.ShadowRound(segX, segY, segW, segH, 4 * s, s, std::max(1, s / 2));
    c.FillGlass(segX, segY, segW, segH, 4 * s, 0xDD161C2A, 0xDD0D111A, 0xFF28344C, 1);

    const int badgeH = 13 * s;
    c.DrawButtonBadge(segX - badgeH - 4 * s, segY + (segH - badgeH) / 2, "L", s, 0xFF222B3D, 0xFFE0E8F5, 0xFF3D4B66);
    c.DrawButtonBadge(segX + segW + 4 * s, segY + (segH - badgeH) / 2, "R", s, 0xFF222B3D, 0xFFE0E8F5, 0xFF3D4B66);

    for (int t = 0; t < TABS; t++)
    {
        const std::string name = (t == 0 ? ToNiceTitle(UiLang::Tr("ALL")) : t == 1 ? "Nintendo DS" : "GBA");
        const int tabH = std::max(18, 6 * s);
        const int nameW = ModernTextWidth(name, tabH);
        const int cx = TabX(c, grid, t);
        const int x = cx - nameW / 2, y = grid.tabsY + (grid.tabsH - tabH) / 2;
        const int pillW = std::max(nameW + 16 * s, tabStep - 4 * s);
        const int pillX = cx - pillW / 2;
        if (t == tab)
        {
            if (onTabs)
            {
                c.GlowRound(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, 3 * s, spotAccent);
                c.FillRound(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, spotAccent);
                ModernText(c, x, y, name, tabH, 0xFFFFFFFF, true);
            }
            else
            {
                c.FillGlass(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, 0xFF324565, 0xFF1D2A40,
                            (spotAccent & 0x00FFFFFFu) | 0xCC000000u, 1);
                const int barW = std::max(6 * s, pillW - 8 * s);
                const int barH = std::max(2, s);
                c.FillRound(pillX + (pillW - barW) / 2, grid.tabsY + grid.tabsH - barH - 1, barW, barH, barH / 2, spotAccent);
                ModernText(c, x, y, name, tabH, 0xFFFFFFFF, true);
            }
        }
        else
        {
            ModernText(c, x, y, name, tabH, th.hint);
        }
    }

    FollowSelection(v, selected, scroll, follow);
    const int tile = grid.tile, tileH = grid.tileH;
    const int cardRad = std::max(4, 5 * s);

    // Empty state card if the chosen tab has no ROMs installed
    if (v.spots.empty())
    {
        const std::string none = ToNiceSentence(UiLang::Tr("NO GAMES FOUND")) + " (" + PlatformName(tab <= 0 ? DS : tab - 1) + ")";
        const int ew = ModernTextWidth(none, 14) + 24 * s;
        const int eh = 24 * s;
        const int ex = (c.w - ew) / 2, ey = grid.carouselY + (tileH - eh) / 2;
        c.FillRound(ex, ey, ew, eh, 4 * s, 0xDD141926);
        c.OutlineRound(ex, ey, ew, eh, 4 * s, 1, 0xFF2A364C);
        ModernText(c, ex + 12 * s, ey + 6 * s, none, 14, th.hint);
    }

    // Render Wii U / Nintendo 3DS Channel Plaza Tiles
    for (const View::Spot& sp : v.spots)
    {
        const bool sel = (sp.game == selected);
        const int x = sp.x - scroll;
        const int y = sp.y - (sel ? floatY : 0);
        if (x + tile < -20 * s || x > c.w + 20 * s) continue;
        const Game& g = games[sp.game];

        if (sel)
        {
            // Sleek Nintendo 3DS / Wii U Selection Focus: crisp white inner bevel + neon halo + 3DS corner brackets
            const uint32_t glowAlpha = (uint32_t)(65 + 35 * pulse) << 24;
            c.ShadowRound(x - 2 * s, y, tile + 4 * s, tileH + 4 * s, cardRad + 2 * s, 3 * s, 120);
            c.GlowRound(x - 3 * s, y - 3 * s, tile + 6 * s, tileH + 6 * s, cardRad + 3 * s, (int)((3.0f + 2.0f * pulse) * s), (spotAccent & 0x00FFFFFF) | glowAlpha);
            c.OutlineRound(x - s, y - s, tile + 2 * s, tileH + 2 * s, cardRad + s, s + 1, 0xFFFFFFFF);
            c.OutlineRound(x - 3 * s, y - 3 * s, tile + 6 * s, tileH + 6 * s, cardRad + 3 * s, s + 1, spotAccent);
        }
        else
        {
            c.ShadowRound(x, y, tile, tileH, cardRad, s, 2 * s);
        }

        const uint32_t cardTop = sel ? 0xFF222C3E : 0xFF161B26;
        const uint32_t cardBot = sel ? 0xFF161E2C : 0xFF0E121A;
        const uint32_t cardBorder = sel ? 0xFFFFFFFF : 0xFF283448;
        c.FillGlass(x, y, tile, tileH, cardRad, cardTop, cardBot, cardBorder, 1);

        if (!g.art.empty())
        {
            const int artPad = 2 * s;
            const int artX = x + artPad, artY = y + artPad;
            const int artW = tile - 2 * artPad, artH = tileH - 2 * artPad;
            const int artRad = std::max(2, cardRad - artPad);
            c.FillRound(artX, artY, artW, artH, artRad, 0xFF0D1017);
            for (int py = 0; py < artH; py++)
            {
                const int cy = artY + py;
                if (cy < 0 || cy >= c.h) continue;
                int ix0 = 0, ix1 = artW;
                Canvas::RoundedSpan(artW, artH, artRad, py, ix0, ix1);
                if (ix0 >= ix1) continue;
                const uint32_t* row = &g.art[(size_t)(py * Art / artH) * Art];
                for (int px = ix0; px < ix1; px++)
                {
                    const int cx = artX + px;
                    if (cx < 0 || cx >= c.w) continue;
                    const uint32_t p = row[px * Art / artW];
                    if ((p >> 24) >= 0x80) c.px[(size_t)cy * c.w + cx] = p | 0xFF000000u;
                }
            }
            c.FillBlend(artX, artY + artH - 3 * s, artW, 3 * s, 0x44000000);
        }
        else
        {
            DrawCartridgeCase(c, x, y, tile, tileH, g.platform, g.hasIcon ? g.icon : nullptr, g.code, g.name, s, g.hasSave, sel);
        }

        // Platform badge (NDS / GBA) on top-left of box art
        if (!g.art.empty())
        {
            const char* platText = g.platform == GBA ? "GBA" : "NDS";
            const uint32_t platBg = g.platform == GBA ? 0xEE4E1D88 : 0xEE0B4FA8;
            const uint32_t platBorder = g.platform == GBA ? 0xFF9D4EDD : 0xFF3A86FF;
            const int bW = ModernTextWidth(platText, 11) + 8 * s;
            const int bH = 10 * s;
            const int bx = x + 4 * s, by = y + 4 * s;
            c.FillRound(bx, by, bW, bH, 2 * s, platBg);
            c.OutlineRound(bx, by, bW, bH, 2 * s, 1, platBorder);
            ModernText(c, bx + 4 * s, by + s, platText, 11, 0xFFFFFFFF);
        }

        // COMING SOON overlay if unreleased
        if (g.comingSoon)
        {
            c.FillBlend(x, y, tile, tileH, 0xAA080B12);
            const std::string soon = UiLang::Tr("COMING SOON");
            const int soonH = 16;
            const int bannerH = 16 * s;
            const int bannerY = y + (tileH - bannerH) / 2;
            c.FillRound(x + 2 * s, bannerY, tile - 4 * s, bannerH, 3 * s, 0xF0151924);
            c.OutlineRound(x + 2 * s, bannerY, tile - 4 * s, bannerH, 3 * s, 1, th.frame);
            ModernText(c, x + (tile - ModernTextWidth(soon, soonH)) / 2, bannerY + 3 * s, soon, soonH, th.title, true);
        }

        // Nintendo 3DS / Wii U animated corner brackets & active indicator pill on selected card
        if (sel)
        {
            const int brkPad = (int)((4.0f + 1.5f * pulse) * s);
            Draw3dsCornerBrackets(c, x - brkPad, y - brkPad, tile + brkPad * 2, tileH + brkPad * 2, s, spotAccent);
            const int dotW = (int)((26.0f + 6.0f * pulse) * s);
            const int dotH = std::max(2, 2 * s);
            c.FillRound(x + (tile - dotW) / 2, y + tileH + 5 * s, dotW, dotH, dotH / 2, spotAccent);
        }
    }

    // Nintendo DSi / 3DS Carousel Navigation Arrow Badges with gentle floating animation
    const int arrowFloat = (int)(2.5f * sinf(t * 4.0f));
    if (scroll > 0)
    {
        const int arrW = 13 * s, arrH = 22 * s;
        const int arrX = 6 * s + arrowFloat, arrY = grid.carouselY + (tileH - arrH) / 2;
        c.ShadowRound(arrX, arrY, arrW, arrH, 4 * s, s, 90);
        c.FillGlass(arrX, arrY, arrW, arrH, 4 * s, 0xF01C2538, 0xF0111726, 0xFF3E5070, 1);
        const int chevH = std::max(14, 5 * s);
        const int chevW = ModernTextWidth("<", chevH);
        ModernText(c, arrX + (arrW - chevW) / 2, arrY + (arrH - chevH) / 2, "<", chevH, 0xFFFFFFFF, true);
    }
    if (scroll + c.w < v.width + 24 * s)
    {
        const int arrW = 13 * s, arrH = 22 * s;
        const int arrX = c.w - arrW - 6 * s - arrowFloat, arrY = grid.carouselY + (tileH - arrH) / 2;
        c.ShadowRound(arrX, arrY, arrW, arrH, 4 * s, s, 90);
        c.FillGlass(arrX, arrY, arrW, arrH, 4 * s, 0xF01C2538, 0xF0111726, 0xFF3E5070, 1);
        const int chevH = std::max(14, 5 * s);
        const int chevW = ModernTextWidth(">", chevH);
        ModernText(c, arrX + (arrW - chevW) / 2, arrY + (arrH - chevH) / 2, ">", chevH, 0xFFFFFFFF, true);
    }

    // Bottom control dock bar with tactile button badges
    const int dockH = 18 * s;
    c.FillGlass(0, c.h - dockH, c.w, dockH, 0, 0xFA141926, 0xFA0B0E16, 0xFF28344C, 1);
    c.FillBlend(0, c.h - dockH, c.w, 1, (spotAccent & 0x00FFFFFFu) | 0x80000000u);

    if (!note.empty())
    {
        const std::string trNote = ToNiceSentence(UiLang::Tr(note));
        int noteH = 14;
        while (noteH > 10 && ModernTextWidth(trNote, noteH) > c.w - 32 * s) --noteH;
        const int nw = ModernTextWidth(trNote, noteH) + 16 * s;
        const int nh = 14 * s;
        const int nx = (c.w - nw) / 2, ny = c.h - dockH + (dockH - nh) / 2;
        c.FillRound(nx, ny, nw, nh, 3 * s, 0xEE1E2638);
        c.OutlineRound(nx, ny, nw, nh, 3 * s, 1, th.frame);
        ModernText(c, nx + 8 * s, ny + (nh - noteH) / 2, trNote, noteH, th.title, true);
    }
    else
    {
        struct DockItem { const char* badge; const char* label; };
        static const DockItem items[] = {
            { "A", "PLAY" }, { "Y", "SETTINGS" }, { "X", "RANDOM" }, { "L/R", "TABS" }, { "+", "QUIT" }
        };
        const int itemCount = 5;
        const int bh = 12 * s;
        const int dockFontH = std::max(16, 5 * s + 2);
        int totalW = 0;
        int widths[itemCount];
        for (int i = 0; i < itemCount; i++)
        {
            const std::string trLabel = ToNiceTitle(UiLang::Tr(items[i].label));
            const int bw = c.ButtonBadgeWidth(items[i].badge, s);
            widths[i] = bw + ModernTextWidth(trLabel, dockFontH) + 6 * s;
            totalW += widths[i];
        }
        const int itemGap = 14 * s;
        totalW += (itemCount - 1) * itemGap;

        if (totalW <= c.w - 16 * s)
        {
            int dx = (c.w - totalW) / 2;
            const int by = c.h - dockH + (dockH - bh) / 2;
            for (int i = 0; i < itemCount; i++)
            {
                const std::string trLabel = ToNiceTitle(UiLang::Tr(items[i].label));
                const int bw = c.ButtonBadgeWidth(items[i].badge, s);
                c.DrawButtonBadge(dx, by, items[i].badge, s, 0xFF222B3D, 0xFFFFFFFF, 0xFF3D4B66);
                ModernText(c, dx + bw + 5 * s, by + (bh - dockFontH) / 2, trLabel, dockFontH, 0xFFD8E2F0, true);
                dx += widths[i] + itemGap;
            }
        }
        else
        {
            const std::string fallbackHint = "A: " + ToNiceTitle(UiLang::Tr("PLAY"))
                + "   Y: " + ToNiceTitle(UiLang::Tr("SETTINGS"))
                + "   X: " + ToNiceTitle(UiLang::Tr("RANDOM"))
                + "   L / R: " + ToNiceTitle(UiLang::Tr("TABS"))
                + "   +: " + ToNiceTitle(UiLang::Tr("QUIT"));
            const std::string hint = !hintText.empty() ? ToNiceSentence(UiLang::Tr(hintText)) : fallbackHint;
            int hintH = 13;
            while (hintH > 9 && ModernTextWidth(hint, hintH) > c.w - 16 * s) --hintH;
            ModernText(c, (c.w - ModernTextWidth(hint, hintH)) / 2, c.h - 13 * s, hint, hintH, th.hint);
        }
    }
}

// The settings page (Y on the Switch's list, H or a controller's Y on a PC's): the pause menu's
// options with what each does, set here before playing. They are written to the same settings
// file the games read (main.cpp, Settings: keys, values and defaults must stay in step with it),
// a line at a time, leaving the others as they are. Entries without a key are what can only be
// done in the pause menu. The list's font has capitals, digits and : - + . / < > ! ( ) only.
struct Setting
{
    const char* key;  // nullptr: told about, not set here; "!...": something to do, not stored
    const char* name;
    std::vector<std::pair<std::string, std::string>> values; // stored, shown
    std::string def;  // the stored value when the file has none ("": the game decides, AUTO)
    const char* text;
};
inline std::vector<Setting> SettingsList()
{
    const std::vector<std::pair<std::string, std::string>> onOff = { { "1", "ON" }, { "0", "OFF" } };
    std::vector<Setting> l;
#ifdef __ANDROID__
    l.push_back({ "!updates", "CHECK FOR UPDATES", { { "", "CHECK" } }, "",
                  "CHECKS GITHUB FOR A NEW VERSION. INSTALL DOWNLOADS IT: IT IS CHECKED AGAINST THE RELEASE SHA256SUMS.TXT AND ANDROID ASKS "
                  "BEFORE INSTALLING. THE OLD VERSION IS KEPT. YOUR ROMS / SAVES AND SETTINGS STAY AS THEY ARE." });
#elif defined(__APPLE__)
    l.push_back({ "!updates", "CHECK FOR UPDATES", { { "", "CHECK" } }, "",
                  "CHECKS GITHUB FOR A NEW VERSION. WHEN THERE IS ONE: CHOOSE AGAIN TO OPEN ITS PAGE AND DOWNLOAD IT THERE." });
#elif defined(__SWITCH__) && defined(SSR_ALL_GAMES)
    l.push_back({ "!updates", "CHECK FOR UPDATES", { { "", "CHECK" } }, "",
                  "CHECKS GITHUB FOR A NEW VERSION. CHOOSE AGAIN TO INSTALL IT: IT IS CHECKED AGAINST THE RELEASE SHA256SUMS.TXT "
                  "AND THE OLD .NRO IS KEPT IN THE UPDATE FOLDER. YOUR ROMS / SAVES AND SETTINGS STAY AS THEY ARE." });
#elif !defined(__SWITCH__)
    l.push_back({ "!updates", "CHECK FOR UPDATES", { { "", "CHECK" } }, "",
                  "CHECKS GITHUB FOR A NEW VERSION. CHOOSE AGAIN TO INSTALL IT: IT IS CHECKED AGAINST THE RELEASE SHA256SUMS.TXT "
                  "AND THE OLD VERSION IS KEPT IN THE UPDATE FOLDER. YOUR ROMS / SAVES AND SETTINGS STAY AS THEY ARE." });
#endif

#ifdef __SWITCH__
    // (The first start's 60 FPS and WORLD DETAIL depend on the game: SoulSilver and HeartGold
    // start lighter. Until either is set, AUTO.)
    l.push_back({ "fps60", "60 FPS", onOff, "", "ON: THE 3D WORLD GETS IN-BETWEEN FRAMES AND MOVES AT 60 FRAMES A SECOND (THE GAMES DRAW IT AT 30). OFF: 30 AS ON A DS." });
#else
    // (A PC, and the Android app: no BORDERLESS FULLSCREEN, WORLD ON GPU or STICK TOUCH CURSOR
    // there, which is always full screen, draws the world with the processor and has a touch
    // screen.)
    l.push_back({ "view", "SCREEN LAYOUT", { { "merged", "MERGED" }, { "hybrid", "HYBRID" }, { "side", "SIDE" }, { "vertical", "VERTICAL" }, { "top", "TOP" }, { "bottom", "BOTTOM" } }, "merged",
                  "MERGED: THE ONE WIDESCREEN PICTURE. THE OTHERS SHOW THE TWO DS SCREENS LAID OUT AS ON OTHER EMULATORS." });
    l.push_back({ "large_screen", "LARGE SCREEN", { { "auto", "AUTO" }, { "top", "TOP" }, { "bottom", "BOTTOM" } }, "auto",
                  "FOR THE LAYOUTS WITH A LARGE AND A SMALL SCREEN: WHICH ONE IS LARGE. AUTO FOLLOWS WHAT THE GAME IS DOING." });
    l.push_back({ "fps60", "60 FPS", onOff, "1", "ON: THE 3D WORLD GETS IN-BETWEEN FRAMES AND MOVES AT 60 FRAMES A SECOND (THE GAMES DRAW IT AT 30). OFF: 30 AS ON A DS." });
#ifndef SSR_PHONE
    l.push_back({ "fullscreen", "BORDERLESS FULLSCREEN", onOff, "1", "ON: FILLS YOUR SCREEN AS A BORDERLESS WINDOW AT ITS OWN RESOLUTION. SWITCHING TO OTHER WINDOWS IS INSTANT. F11 DOES THE SAME." });
#endif
#endif
    l.push_back({ "smooth", "SMOOTH SCALING", onOff, "0", "ON: THE PICTURE IS SOFTENED AS IT IS SCALED UP TO YOUR SCREEN. OFF: SHARP PIXELS." });
    l.push_back({ "fast_button", "FAST FORWARD BUTTON", { { "hold", "HOLD" }, { "toggle", "TOGGLE" } }, "hold",
                  "HOLD: RELEASE TO STOP. TOGGLE: PRESS ONCE TO START AND AGAIN TO STOP. APPLIES TO KEYBOARD, CONTROLLER AND ON-SCREEN FAST FORWARD." });
#ifdef __SWITCH__
    l.push_back({ "switch_ff", "FAST FORWARD (EXPERIMENTAL)", onOff, "0",
                  "EXPERIMENTAL ON SWITCH: TURNS ON FAST FORWARD (HOLD OR TOGGLE ZR BY DEFAULT). WHILE FAST FORWARD IS ACTIVE, WIDESCREEN 3D TEMPORARILY SWITCHES TO STANDARD DS RESOLUTION SO SPEED STAYS SMOOTH WITHOUT RAISING CONSOLE CLOCKS." });
#endif
    l.push_back({ "wide", "WIDESCREEN WORLD", onOff, "1", "ON: THE 3D WORLD IS DRAWN OUT TO THE SIDES OF THE 16:9 SCREEN. OFF: ONLY AS WIDE AS A DS SCREEN." });
#ifdef __SWITCH__
    l.push_back({ "wide_lines", "WORLD DETAIL", { { "192", "192" }, { "288", "288" }, { "384", "384" }, { "540", "540" }, { "720", "720" } }, "",
                  "HOW MANY LINES THE WIDE 3D WORLD IS DRAWN WITH. 720 IS FULL SHARPNESS ON THE SWITCH. LOWER IS LIGHTER ON THE CONSOLE." });
#else
#ifdef SSR_PHONE
    l.push_back({ "wide_lines", "3D RESOLUTION", { { "192", "192P (DS)" }, { "288", "288P" }, { "384", "384P" }, { "540", "540P" }, { "720", "720P" }, { "1080", "1080P" } }, "720",
                  "HOW SHARP THE 3D WORLD IS. 720P SUITS MOST PHONES. LOWER IF YOURS CANNOT KEEP UP OR TO SAVE BATTERY. SPRITES AND TEXT KEEP THEIR DS PIXELS." });
#else
    l.push_back({ "wide_lines", "3D RESOLUTION", { { "192", "192P (DS)" }, { "288", "288P" }, { "384", "384P" }, { "540", "540P" }, { "720", "720P" }, { "1080", "1080P" }, { "1440", "1440P (2K)" }, { "2160", "2160P (4K)" } },
#ifdef __APPLE__
                  "720", // (drawn by the processor on a Mac)
#else
                  "1080",
#endif

                  "HOW SHARP THE 3D WORLD IS. PICK YOUR SCREEN RESOLUTION OR LOWER IF YOUR PC CANNOT KEEP UP. SPRITES AND TEXT KEEP THEIR DS PIXELS." });
#ifndef __APPLE__ // (a Mac's OpenGL stops at 4.1: the processor draws the world)
    l.push_back({ "wide_gpu", "WORLD ON GPU", onOff, "1", "ON: THE WIDE 3D WORLD IS DRAWN BY YOUR GRAPHICS CARD (OPENGL 4.3). TURN IT OFF IF THE WORLD LOOKS WRONG OR THE GAME WILL NOT START: THE PROCESSOR THEN DRAWS IT (THAT WANTS A FAST PC)." });
#endif
#endif
#endif
    l.push_back({ "show_fps", "FPS COUNTER", onOff, "0", "SHOWS THE FRAMES A SECOND IN A CORNER." });
    l.push_back({ "volume", "VOLUME", { { "0", "OFF" }, { "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } }, "100",
                  "THE GAME'S SOUND AND MUSIC. OFF FOR NONE. ALSO IN THE PAUSE MENU WHILE YOU PLAY." });
#if defined(__SWITCH__) && defined(SSR_ALL_GAMES)
    // (frontend/switch_forwarder.cpp, made by the game list: switch_platform.cpp.)
    l.push_back({ "!forwarders", "HOME MENU ICONS", { { "", "INSTALL" } }, "",
                  "PUTS AN ICON FOR EACH GAME IN THIS LIST ON THE HOME MENU WITH ITS PICTURE. IT STARTS THAT GAME STRAIGHT AWAY. MADE ON THIS CONSOLE: NO PC OR KEYS FILE NEEDED. LIKE ANY FORWARDER THE ICONS NEED SIGPATCHES TO START. INSTALL AGAIN AFTER ADDING GAMES. REMOVE ONE IN THE HOME MENU (OPTIONS / DELETE SOFTWARE)." });
#endif
    // The interface's language (UiLang, ui_lang.h), shown first among DISPLAY's options.
    l.insert(l.begin(), { "list_lang", "LANGUAGE", UiLang::Choices(), "auto",
                          "THE LANGUAGE OF THE GAME LIST, THE SETTINGS AND THE PAUSE MENU (NOT OF THE GAMES)." });
    // The game list's own look (GAME LIST: ThemeNamed, Background).
    l.push_back({ "list_colour", "COLOUR", { { "red", "RED" }, { "blue", "BLUE" }, { "green", "GREEN" }, { "purple", "PURPLE" },
                  { "gold", "GOLD" }, { "pink", "PINK" }, { "teal", "TEAL" } }, "red",
                  "THE GAME LIST'S COLOUR: ITS LINES, THE FRAME ROUND THE CHOSEN GAME AND THE TABS." });
#ifdef __SWITCH__
    l.push_back({ "list_background", "BACKGROUND", { { "plain", "PLAIN" }, { "picture", "YOUR PICTURE" } }, "plain",
                  "YOUR PICTURE: PUT A PICTURE NAMED BACKGROUND.JPG OR BACKGROUND.PNG IN /SWITCH/POKEMON-DS-WIDE/ (WITH YOUR SAVES). BEST AT 1280 X 720 OR 1920 X 1080 (16:9); ANY SIZE WORKS, CUT TO FILL THE SCREEN. IT IS SHOWN DARKENED BEHIND THE GAMES." });
#else
    l.push_back({ "list_background", "BACKGROUND", { { "plain", "PLAIN" }, { "picture", "YOUR PICTURE" } }, "plain",
                  "YOUR PICTURE: PUT A PICTURE NAMED BACKGROUND.JPG OR BACKGROUND.PNG BESIDE YOUR SAVES FOLDER. BEST AT 1920 X 1080 (16:9); ANY SIZE WORKS, CUT TO FILL THE SCREEN. IT IS SHOWN DARKENED BEHIND THE GAMES." });
#endif
#if !defined(__SWITCH__)
    // RetroAchievements from the game list too (pc_launcher.cpp: the login, as the pause menu's).
    l.push_back({ "ra", "RETROACHIEVEMENTS", onOff, "0",
                  "ON: YOUR RETROACHIEVEMENTS ACCOUNT FOLLOWS THE GAMES YOU PLAY AND UNLOCKS POP UP AS YOU PLAY (CASUAL MODE: SAVE STATES AND FAST FORWARD STILL WORK; PAUSED WHILE A CHEAT IS ON)." });
    l.push_back({ "!ra_login", "ACCOUNT", { { "", "LOG IN" } }, "",
                  "LOG IN WITH YOUR RETROACHIEVEMENTS USER NAME AND PASSWORD (A FREE ACCOUNT FROM RETROACHIEVEMENTS.ORG). THE PASSWORD IS SENT ONCE AND NOT KEPT. CHOSEN AGAIN WHEN LOGGED IN: LOG OUT." });
#endif
#ifdef __SWITCH__
    l.push_back({ "!keys", "KEYS AND BUTTONS", { { "", "CHANGE" } }, "",
                  "GIVE ANY DS BUTTON / THE PAUSE MENU / FAST FORWARD / SWAP SCREENS / SAVE STATE / LOAD STATE ANOTHER BUTTON (ANY CONTROLLER: PRO / JOY-CON OR ONE ADDED WITH SYS-CON OR MISSIONCONTROL). GIVING ONE A BUTTON ANOTHER ONE HAD SWAPS THE TWO. SAVE STATE AND LOAD STATE START NOT SET. ALSO IN THE PAUSE MENU (CONTROLS). THIS LIST FOLLOWS THE ONES YOU CHANGE." });
#else
    l.push_back({ "!keys", "KEYS AND BUTTONS", { { "", "CHANGE" } }, "",
                  "GIVE ANY DS BUTTON / THE PAUSE MENU / FAST FORWARD / SWAP SCREENS / SAVE STATE / LOAD STATE ANOTHER KEY OR CONTROLLER BUTTON. GIVING ONE A KEY OR BUTTON ANOTHER ONE HAD SWAPS THE TWO. SAVE STATE STARTS AS F5 AND LOAD STATE AS F8 (NOT SET ON A CONTROLLER). ALSO IN THE PAUSE MENU (CONTROLS). THIS LIST FOLLOWS THE ONES YOU CHANGE (A PLAYS / B GOES BACK / Y OPENS THESE SETTINGS)." });
#endif
    return l;
}

// Words to lines of at most `chars` characters.
inline std::vector<std::string> Wrap(const std::string& text, int chars)
{
    return UiLang::Wrap(text, chars * 6);
}

struct SettingsGroup
{
    const char* key;  // its row in the list of groups (a "!" key: chosen, not stored)
    const char* name;
    const char* text;
};
inline const std::vector<SettingsGroup>& SettingsGroups()
{
    static const std::vector<SettingsGroup> g = {
        // (What each holds is listed under it: its options' names, ShowGroups.)
        { "!group0", "DISPLAY", "" },
        { "!group1", "SOUND", "" },
        { "!group2", "GRAPHICS", "" },
        { "!group3", "CONTROLS", "" },
        { "!group4", "SHORTCUTS", "" },
        { "!group5", "GAME LIST", "" },
        { "!group6", "RETROACHIEVEMENTS", "" },
        { "!group7", "HELP", "" },
    };
    return g;
}
// Which group an option is in (SettingsGroups).
inline int GroupOf(const Setting& s)
{
    if (s.key && !strcmp(s.key, "!updates")) return 7;
    if (!s.key) return !strcmp(s.name, "KEYS AND BUTTONS") ? 3 : 7;
    if (!strcmp(s.key, "list_lang")) return 0;
    if (!strncmp(s.key, "list_", 5)) return 5;
    if (!strcmp(s.key, "ra") || !strcmp(s.key, "!ra_login")) return 6;
    static const char* display[] = { "view", "large_screen", "fullscreen", "smooth", "battle_buttons", "show_fps" };
    static const char* controls[] = { "start_button", "buttons", "pad_touch", "fast_button" };
    for (const char* k : display)
        if (!strcmp(s.key, k)) return 0;
    if (!strcmp(s.key, "volume")) return 1;
    for (const char* k : controls)
        if (!strcmp(s.key, k)) return 3;
    if (!strcmp(s.key, "!keys")) return 3;
    if (s.key[0] == '!') return 4; // (DESKTOP SHORTCUTS, HOME MENU ICONS)
    return 2;
}

struct SettingsPage
{
    std::string path;
    std::vector<Setting> all = SettingsList();
    std::vector<Setting> items; // what is shown: the groups, or one group's options
    int group = -1;             // the group shown, -1: the list of groups
    std::string groupText[8];   // (the groups' lines: their options' names)
    std::vector<std::string> lines; // the file, as it is
    int selected = 0, scroll = 0;
    // Draw keeps the chosen option in view while follow is set (the keys and the D-pad); a
    // finger scrolling the page clears it. Draw also notes, for taps (ItemAt), where each
    // option's name line is and how tall a line is.
    bool follow = true;
    int lineH = 20;
    int lastCardBottom = 0;
    std::vector<std::pair<int, int>> rows; // (option, the top of its name line)

    // The option whose part of the page (its name and the lines below it) is at y, or -1.
    int ItemAt(int y) const
    {
        int found = -1;
        for (const auto& r : rows)
            if (r.second - lineH / 4 <= y) found = r.first;
        const int bound = lastCardBottom > 0 ? lastCardBottom : (rows.empty() ? 0 : rows.back().second + 6 * lineH);
        if (found >= 0 && !rows.empty() && y > bound) return -1;
        return found;
    }

    void Open(const std::string& cfgPath, bool keepChoice = false)
    {
        path = cfgPath;
        lines.clear();
        if (FILE* f = fopen(path.c_str(), "r"))
        {
            char buf[1024];
            while (fgets(buf, sizeof buf, f))
            {
                std::string l = buf;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                lines.push_back(l);
            }
            fclose(f);
        }
        if (keepChoice) return;
        ShowGroups(0);
    }
    // The list of groups, with group `sel` chosen.
    void ShowGroups(int sel)
    {
        group = -1;
        items.clear();
        int row = 0;
        for (int g = 0; g < (int)SettingsGroups().size(); g++)
        {
            bool any = false;
            for (const Setting& s : all) any |= GroupOf(s) == g;
            if (!any) continue; // (a platform without any of it: no SHORTCUTS on a Mac)
            if (g == sel) row = (int)items.size();
            const SettingsGroup& sg = SettingsGroups()[g];
            std::string names;
            int count = 0;
            for (const Setting& s : all)
            {
                if (GroupOf(s) == g && count < 2)
                {
                    std::string trName = ToNiceTitle(UiLang::Tr(s.name));
                    names += (names.empty() ? "" : ", ") + trName;
                    count++;
                }
            }
            if (!names.empty()) names += "...";
            groupText[g] = names;
            items.push_back({ sg.key, sg.name, { { "", ">" } }, "", groupText[g].c_str() });
        }
        selected = row;
        scroll = 0;
        follow = true;
    }
    // Group g's options.
    void Enter(int g)
    {
        group = g;
        items.clear();
        for (const Setting& s : all)
            if (GroupOf(s) == g) items.push_back(s);
        selected = 0;
        scroll = 0;
        follow = true;
    }
    // B: from a group back to the list of groups; false when already there (the page closes).
    bool Back()
    {
        if (group < 0) return false;
        ShowGroups(group);
        return true;
    }
    std::string Stored(const std::string& key) const
    {
        for (const std::string& l : lines)
            if (l.size() > key.size() && !l.compare(0, key.size(), key) && l[key.size()] == '=') return l.substr(key.size() + 1);
        return "";
    }
    // The index of the shown value (-1: AUTO).
    int ValueIndex(const Setting& s) const
    {
        if (s.key[0] == '!') return 0; // (something to do: its one value is what it says)
        std::string v = Stored(s.key);
        if (v.empty()) v = s.def;
        if (v.empty()) return -1;
        for (size_t i = 0; i < s.values.size(); i++)
            if (s.values[i].first == v) return (int)i;
        // Values the file may hold in other spellings: numbers between the choices (the
        // nearest below), ON/OFF as anything but "0" or "1".
        if (!strcmp(s.key, "wide_lines"))
        {
            int best = 0;
            for (size_t i = 0; i < s.values.size(); i++)
                if (atoi(s.values[i].first.c_str()) <= atoi(v.c_str())) best = (int)i;
            return best;
        }
        for (size_t i = 0; i < s.values.size(); i++)
            if (s.values[i].first == s.def) return (int)i;
        return 0;
    }
    std::string Shown(const Setting& s) const
    {
        const int i = ValueIndex(s);
        return i < 0 ? "AUTO" : s.values[i].second;
    }
    void Move(int d)
    {
        follow = true;
        if (selected + d >= 0 && selected + d < (int)items.size()) selected += d;
    }
    // The chosen option's next (d 1) or previous (d -1) value, written to the file. Returns its
    // key, or nullptr.
    const char* Change(int d)
    {
        if (selected < 0 || selected >= (int)items.size() || !items[selected].key) return nullptr;
        const Setting& s = items[selected];
        if (!strncmp(s.key, "!group", 6))
        {
            if (d > 0) Enter(atoi(s.key + 6));
            return nullptr;
        }
        if (s.key[0] == '!') return s.key; // (done by the caller)
        const int n = (int)s.values.size();
        const int i = ValueIndex(s);
        const int next = i < 0 ? (d > 0 ? 0 : n - 1) : (i + d + n) % n;
        const std::string line = std::string(s.key) + "=" + s.values[next].first;
        bool found = false;
        for (std::string& l : lines)
            if (l.size() > strlen(s.key) && !l.compare(0, strlen(s.key), s.key) && l[strlen(s.key)] == '=')
            {
                l = line;
                found = true;
            }
        if (!found) lines.push_back(line);
        if (!strcmp(s.key, "wide_lines"))
        {
            // Match the in-game menu: choosing a manual resolution disables automatic
            // lowering, so starting the game honours that choice.
            bool haveBusy = false;
            for (std::string& l : lines)
                if (l.rfind("busy_detail=", 0) == 0) { l = "busy_detail=0"; haveBusy = true; }
            if (!haveBusy) lines.push_back("busy_detail=0");
        }
        const std::string tmp = path + ".tmp";
        if (FILE* f = fopen(tmp.c_str(), "w"))
        {
            bool ok = true;
            for (const std::string& l : lines) if (fprintf(f, "%s\n", l.c_str()) < 0) ok = false;
            ok = fclose(f) == 0 && ok;
            std::error_code ec;
            if (ok)
            {
                std::remove(path.c_str());
                std::filesystem::rename(tmp, path, ec);
                if (ec)
                {
                    if (FILE* f2 = fopen(path.c_str(), "w"))
                    {
                        for (const std::string& l : lines) fprintf(f2, "%s\n", l.c_str());
                        fclose(f2);
                        ec.clear();
                    }
                }
            }
            if (!ok || ec) fprintf(stderr, "[launcher] cannot save settings: %s\n", path.c_str());
        }
        return s.key;
    }

    static std::vector<std::string> WrapModern(const Canvas& c, const std::string& text, int fontH, int maxPx)
    {
        std::vector<std::string> out;
        if (text.empty()) return out;
        size_t pos = 0;
        std::string cur;
        auto pushCur = [&]() {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        };
        while (pos < text.size())
        {
            while (pos < text.size() && text[pos] == ' ') pos++;
            if (pos >= text.size()) break;
            size_t end = pos;
            while (end < text.size() && text[end] != ' ') end++;
            const std::string word = text.substr(pos, end - pos);
            pos = end;
            if (cur.empty())
            {
                if (c.ModernTextWidth(word, fontH) <= maxPx) cur = word;
                else
                {
                    std::string chunk;
                    for (size_t k = 0; k < word.size(); )
                    {
                        size_t next = k + 1;
                        while (next < word.size() && ((unsigned char)word[next] & 0xC0) == 0x80) next++;
                        const std::string ch = word.substr(k, next - k);
                        if (!chunk.empty() && c.ModernTextWidth(chunk + ch, fontH) > maxPx)
                        {
                            out.push_back(chunk);
                            chunk.clear();
                        }
                        chunk += ch;
                        k = next;
                    }
                    cur = chunk;
                }
            }
            else if (c.ModernTextWidth(cur + " " + word, fontH) <= maxPx)
            {
                cur += " " + word;
            }
            else
            {
                pushCur();
                if (c.ModernTextWidth(word, fontH) <= maxPx) cur = word;
                else out.push_back(word);
            }
        }
        pushCur();
        return out;
    }

    // Over the whole canvas: the options with their values (the chosen one marked), each with
    // what it does below it, kept in view as the choice moves.
    void Draw(Canvas& c, const std::string& hint, const Theme& th = Theme())
    {
        const int s = std::max(1, std::min(c.w, c.h) / 240);
#ifdef __ANDROID__
        // Phone settings use readable touch targets in either orientation.
        const int ts = std::max(2, s);
#else
        const int ts = s; // full scale on handheld/console (3 at 720 lines, 4 at 1080)
#endif
        const int bandH = (c.h > c.w ? 43 : 22) * s;
        const uint32_t bgTop = (th.bg == 0xFF0D1017) ? 0xFF141E33 : th.bg;
        c.FillGradV(0, 0, c.w, c.h, bgTop, 0xFF090E1B, 0);
        DrawDsMesh(c, s);
        if (th.background)
            if (const uint32_t* bgPx = th.background->Fitted(c.w, c.h)) std::copy(bgPx, bgPx + (size_t)c.w * c.h, c.px.begin());
        c.FillGradV(0, 0, c.w, bandH, 0xFF191F2D, th.band, 0);
        c.Fill(0, bandH, c.w, th.bandLineH * s, th.bandLine);
        c.FillBlend(0, bandH + th.bandLineH * s, c.w, 2 * s, 0x33000000);
        const int logoH = (4 * s + 1) * 2 + std::max(1, s + 1);
        DrawDsLogo(c, 10 * s, (bandH - logoH) / 2, s, th.title);
        const std::string title = group < 0 ? ToNiceTitle(UiLang::Tr("SETTINGS")) : ToNiceTitle(UiLang::Tr("SETTINGS")) + ": " + ToNiceTitle(UiLang::Tr(SettingsGroups()[group].name));
        const int titleH = std::max(22, 8 * s);
        const int tw = c.ModernTextWidth(title, titleH);
        c.ModernText((c.w - tw) / 2, (bandH - titleH) / 2, title, titleH, th.title);
        {
            int bx = 0, by = 0, bw = 0, bh = 0;
            if (SettingsBackPillRect(c, bx, by, bw, bh) && bx + bw + 8 * s < (c.w - tw) / 2)
            {
                const std::string backLbl = "B: " + ToNiceTitle(UiLang::Tr("BACK"));
                const int backFontH = std::max(13, 5 * s);
                c.FillGlass(bx, by, bw, bh, 3 * s, 0xEE223048, 0xEE172033, th.frame, 1);
                c.ModernText(bx + 5 * s, by + (bh - backFontH) / 2, backLbl, backFontH, th.title, true);
            }
        }
        if (!items.empty() && c.w >= 480)
        {
            const std::string countStr = std::to_string(selected + 1) + " / " + std::to_string(items.size());
            const int cntFontH = std::max(14, 5 * s + 1);
            const int cntW = c.ModernTextWidth(countStr, cntFontH);
            const int pillW = cntW + 10 * s;
            const int pillH = 12 * s;
            const int px = c.w - pillW - 10 * s;
            const int py = (bandH - pillH) / 2;
            if (px > (c.w + tw) / 2 + 8 * s)
            {
                c.FillRound(px, py, pillW, pillH, pillH / 2, 0xCC1E283C);
                c.OutlineRound(px, py, pillW, pillH, pillH / 2, 1, 0xFF394966);
                c.ModernText(px + 5 * s, py + (pillH - cntFontH) / 2, countStr, cntFontH, th.subSel, false);
            }
        }
        const int width = std::min(c.w - 24 * s, 1080);
        const int x = (c.w - width) / 2;
        struct Line { std::string text, value; int item; bool name; int h; };
        std::vector<Line> out;
        int selFirst = 0, selLast = 0;
        bool infoHeading = false;
        const int nameFontH = std::max(20, 9 * ts);
        const int descFontH = std::max(15, 6 * ts + 1);
        lineH = std::max(28, (UiLang::Tall() ? 18 : 16) * ts);
        const int descLineH = descFontH + std::max(4, 2 * ts);
        const int cardPadBot = std::max(3, 2 * ts);
        const int gapH = std::max(8, 5 * ts);
        const int descMaxPx = std::max(80, width - 22 * ts);
        for (int i = 0; i < (int)items.size(); i++)
        {
            const Setting& e = items[i];
            if (!e.key && !infoHeading)
            {
                out.push_back({ "", "", -1, false, gapH });
                infoHeading = true;
            }
            if (i == selected) selFirst = (int)out.size();
            const std::string value = e.key ? Shown(e) : "";
            const bool arrows = !value.empty() && i == selected && e.key && strncmp(e.key, "!group", 6);
            const std::string niceVal = ToNiceTitle(UiLang::Tr(value));
            const std::string drawnValue = arrows ? "< " + niceVal + " >" : niceVal;
            const std::string niceName = ToNiceTitle(UiLang::Tr(e.name));
            const int valReserve = value.empty() ? 0 : (c.ModernTextWidth(drawnValue, nameFontH) + 28 * ts);
            const bool stacked = c.ModernTextWidth(niceName, nameFontH) + valReserve + 14 * ts > width;
            out.push_back({ niceName, stacked ? "" : value, i, true, lineH });
            if (stacked && !value.empty())
                for (const std::string& l : WrapModern(c, niceVal, descFontH, descMaxPx))
                    out.push_back({ l, "", i, false, descLineH });
            const std::string descTr = UiLang::Tr(e.text);
            const std::string niceDesc = group < 0 ? ToNiceTitle(descTr) : ToNiceSentence(descTr);
            for (const std::string& l : WrapModern(c, niceDesc, descFontH, descMaxPx))
                out.push_back({ l, "", i, false, descLineH });
            out.push_back({ "", "", i, false, cardPadBot });
            if (i == selected) selLast = (int)out.size() - 1;
            if (i + 1 < (int)items.size())
                out.push_back({ "", "", -1, false, gapH });
        }
        int top = (c.h > c.w ? 48 : 30) * s;
        const int dockH = 18 * s;
        const int bottom = c.h - dockH - 3 * s;
        const int availH = std::max(lineH, bottom - top);
        if (follow && !out.empty())
        {
            if (selFirst < scroll) scroll = selFirst;
            auto spanH = [&](int a, int b) {
                int sum = 0;
                for (int k = a; k <= b && k < (int)out.size(); k++) sum += out[k].h;
                return sum;
            };
            while (scroll < selFirst && spanH(scroll, selLast) > availH)
            {
                const int curIt = out[scroll].item;
                while (scroll < selFirst && out[scroll].item == curIt) scroll++;
                while (scroll < selFirst && out[scroll].item < 0) scroll++;
            }
            if (spanH(scroll, selLast) > availH && selFirst > scroll) scroll = selFirst;
        }
        int totalH = 0;
        for (const Line& l : out) totalH += l.h;
        if (group < 0 && totalH < availH)
        {
            top = top + std::max(0, (availH - totalH) / 2);
            scroll = 0;
        }
        else if (!out.empty())
        {
            scroll = std::clamp(scroll, 0, (int)out.size() - 1);
            // Pull scroll up if the tail leaves unused space at the bottom.
            while (scroll > 0)
            {
                int prev = scroll - 1;
                while (prev > 0 && out[prev].item < 0) prev--;
                const int prevIt = out[prev].item;
                while (prev > 0 && out[prev - 1].item == prevIt) prev--;
                int rem = 0;
                for (int k = prev; k < (int)out.size(); k++) rem += out[k].h;
                if (rem <= availH) scroll = prev;
                else break;
            }
        }
        rows.clear();
        lastCardBottom = 0;

        // Draw elevated frosted glass cards behind each visible item group before drawing text.
        const int cardRad = std::max(3, 4 * ts);
        const float t = AnimTime();
        const float pulse = 0.5f + 0.5f * sinf(t * 3.5f);
        int scanY = top;
        int endIdx = scroll;
        for (int i = scroll; i < (int)out.size(); )
        {
            if (out[i].item < 0)
            {
                if (scanY + out[i].h > bottom) break;
                scanY += out[i].h;
                i++;
                endIdx = i;
                continue;
            }
            const int it = out[i].item;
            int j = i;
            int cardTextH = 0;
            while (j < (int)out.size() && out[j].item == it)
            {
                cardTextH += out[j].h;
                j++;
            }
            if (scanY + cardTextH > bottom && i > scroll) break;
            if (scanY + out[i].h > bottom) break;
            int visJ = i;
            int visH = 0;
            while (visJ < j && scanY + visH + out[visJ].h <= bottom)
            {
                visH += out[visJ].h;
                visJ++;
            }
            const int cardY = scanY - 2 * ts;
            const int cardH = visH + 2 * ts;
            const bool sel = it == selected;
            if (sel)
            {
                const uint32_t glowCol = (th.frame & 0x00FFFFFF) | ((uint32_t)(40 + 35 * pulse) << 24);
                c.GlowRound(x - 6 * ts, cardY, width + 12 * ts, cardH, cardRad, 3 * ts, glowCol);
                c.FillGlass(x - 6 * ts, cardY, width + 12 * ts, cardH, cardRad, 0xFF242E44, 0xFF192030, th.frame, 1);
                c.FillRound(x - 4 * ts, cardY + 2 * ts, std::max(2, 2 * ts), cardH - 4 * ts, ts, 0xFF00E5FF);
            }
            else
            {
                c.FillGlass(x - 6 * ts, cardY, width + 12 * ts, cardH, cardRad, 0xC0171D2A, 0xC010141F, 0xFF242C3F, 1);
            }
            lastCardBottom = cardY + cardH;
            scanY += visH;
            endIdx = visJ;
            i = j;
            if (visJ < j) break;
        }

        int y = top;
        for (int i = scroll; i < endIdx; y += out[i].h, i++)
        {
            const Line& l = out[i];
            if (l.name) rows.push_back({ l.item, y });
            const bool sel = l.item == selected && l.item >= 0;
            if (l.name)
            {
                const bool info = !items[l.item].key;
                c.ModernText(x + 6 * ts, y + (l.h - nameFontH) / 2, l.text, nameFontH, info ? th.hint : sel ? th.nameSel : th.subSel);
                if (!l.value.empty())
                {
                    const bool isGroup = !strncmp(items[l.item].key, "!group", 6);
                    const std::string v = sel && !isGroup ? "< " + ToNiceTitle(UiLang::Tr(l.value)) + " >" : ToNiceTitle(UiLang::Tr(l.value));
                    const int valW = c.ModernTextWidth(v, nameFontH);
                    const int pillPadX = 6 * ts;
                    const int pillW = valW + 2 * pillPadX;
                    const int pillH = std::max(nameFontH + 4 * ts, l.h - 4 * ts);
                    const int pillX = x + width - pillW - 2 * ts;
                    const int pillY = y + (l.h - pillH) / 2;
                    const int pillRad = std::max(3, pillH / 3);
                    const uint32_t pillBg = sel ? 0xFF2A4268 : (isGroup ? 0xB01C263B : 0xA8162033);
                    const uint32_t pillBd = sel ? 0xB000E5FF : 0x80324464;
                    c.FillRound(pillX, pillY, pillW, pillH, pillRad, pillBg);
                    c.OutlineRound(pillX, pillY, pillW, pillH, pillRad, 1, pillBd);
                    c.ModernText(pillX + pillPadX, y + (l.h - nameFontH) / 2, v, nameFontH, sel ? 0xFFFFFFFF : th.name);
                }
            }
            else if (!l.text.empty())
            {
                c.ModernText(x + 12 * ts, y + (l.h - descFontH) / 2, l.text, descFontH, sel ? 0xFFD8E4F8 : th.sub);
            }
        }
        if (scroll > 0) c.FillRound(c.w / 2 - 12 * s, top - 4 * s, 24 * s, s + 1, std::max(1, s / 2), th.frame);
        if (endIdx < (int)out.size()) c.FillRound(c.w / 2 - 12 * s, bottom + s, 24 * s, s + 1, std::max(1, s / 2), th.frame);
        c.FillGlass(0, c.h - dockH, c.w, dockH, 0, 0xFA141926, 0xFA0B0E16, 0xFF28344C, 1);

        struct DockItem { const char* badge; const char* label; };
        const DockItem dockItems[] = {
            { "A", group < 0 ? "OPEN" : "CHANGE" }, { "B", "BACK" }
        };
        const int itemCount = 2;
        const int bh = 12 * s;
        const int dockFontH = std::max(16, 5 * s + 2);
        int totalW = 0;
        int widths[itemCount];
        for (int i = 0; i < itemCount; i++)
        {
            const std::string niceLabel = ToNiceTitle(UiLang::Tr(dockItems[i].label));
            const int bw = c.ButtonBadgeWidth(dockItems[i].badge, s);
            widths[i] = bw + c.ModernTextWidth(niceLabel, dockFontH) + 6 * s;
            totalW += widths[i];
        }
        const int itemGap = 16 * s;
        totalW += (itemCount - 1) * itemGap;

        if (totalW <= c.w - 16 * s)
        {
            int dx = (c.w - totalW) / 2;
            const int by = c.h - dockH + (dockH - bh) / 2;
            for (int i = 0; i < itemCount; i++)
            {
                c.DrawButtonBadge(dx, by, dockItems[i].badge, s, 0xFF222B3D, 0xFFFFFFFF, 0xFF3D4B66);
                const std::string niceLabel = ToNiceTitle(UiLang::Tr(dockItems[i].label));
                const int bw = c.ButtonBadgeWidth(dockItems[i].badge, s);
                c.ModernText(dx + bw + 5 * s, by + (bh - dockFontH) / 2, niceLabel, dockFontH, 0xFFD8E2F0);
                dx += widths[i] + itemGap;
            }
        }
        else
        {
            const int hintH = std::max(11, 4 * s);
            const std::string niceHint = ToNiceTitle(UiLang::Tr(hint));
            const int hw = c.ModernTextWidth(niceHint, hintH);
            c.ModernText((c.w - hw) / 2, c.h - dockH + (dockH - hintH) / 2, niceHint, hintH, th.pokemon ? 0xFFDBE9FAu : th.hint);
        }
    }
};

// Centered frosted-glass modal dialog drawn over a full-screen scrim so synchronous status
// updates (e.g. checking/downloading/installing updates or icons) always appear cleanly in front.
inline void DrawStatusModal(Canvas& c, const std::string& title, const std::string& message,
                            const std::string& detail = std::string(), const std::string& hint = std::string(),
                            bool busy = false, int progressPct = -1, const Theme& th = Theme())
{
    const int s = std::max(1, std::min(c.w, c.h) / 240);
    c.FillBlend(0, 0, c.w, c.h, 0xC8050812);

    const int mw = std::min(c.w - 28 * s, 210 * s);
    const int innerW = std::max(120, mw - 24 * s);
    const int titleH = std::max(18, 7 * s);
    const int msgH = std::max(16, 6 * s);
    const int detH = std::max(14, 5 * s);
    const int hintH = std::max(13, 5 * s);

    const std::string trTitle = title.empty() ? std::string() : ToNiceTitle(UiLang::Tr(title));
    const std::string trMsg = message.empty() ? std::string() : ToNiceSentence(UiLang::Tr(message));
    const std::string trDet = detail.empty() ? std::string() : ToNiceSentence(UiLang::Tr(detail));
    const std::string trHint = (busy || hint.empty()) ? std::string() : ToNiceTitle(UiLang::Tr(hint));

    const int charMax = std::max(24, innerW / std::max(6, msgH / 2));
    const std::vector<std::string> msgLines = trMsg.empty() ? std::vector<std::string>{} : Wrap(trMsg, charMax);
    const std::vector<std::string> detLines = trDet.empty() ? std::vector<std::string>{} : Wrap(trDet, charMax + 6);
    int usedMsgH = msgH;
    for (const std::string& line : msgLines)
        while (usedMsgH > 12 && ModernTextWidth(line, usedMsgH) > innerW) --usedMsgH;
    int usedDetH = detH;
    for (const std::string& line : detLines)
        while (usedDetH > 10 && ModernTextWidth(line, usedDetH) > innerW) --usedDetH;

    int mh = 18 * s;
    if (!trTitle.empty()) mh += titleH + 8 * s;
    if (!msgLines.empty()) mh += (int)msgLines.size() * (usedMsgH + 4 * s) + 4 * s;
    if (busy || progressPct >= 0) mh += 10 * s;
    if (!detLines.empty()) mh += (int)detLines.size() * (usedDetH + 3 * s) + 4 * s;
    if (!trHint.empty()) mh += hintH + 10 * s;
    mh = std::min(c.h - 24 * s, std::max(mh, 54 * s));

    const int mx = (c.w - mw) / 2;
    const int my = (c.h - mh) / 2;
    const int rad = std::max(6, 5 * s);

    c.ShadowRound(mx, my, mw, mh, rad, 3 * s, 140);
    c.GlowRound(mx, my, mw, mh, rad, 2 * s, (th.frame & 0x00FFFFFFu) | 0x44000000u);
    c.FillGlass(mx, my, mw, mh, rad, 0xFC1E2A3F, 0xFC101726, th.frame, std::max(1, s / 2 + 1));
    c.FillRound(mx + 6 * s, my + s + 1, mw - 12 * s, std::max(2, s), std::max(1, s / 2), th.frame);

    int y = my + 10 * s;
    if (!trTitle.empty())
    {
        const int tw = ModernTextWidth(trTitle, titleH);
        ModernText(c, mx + (mw - tw) / 2, y, trTitle, titleH, th.title, true);
        y += titleH + 6 * s;
        c.Fill(mx + 14 * s, y - 3 * s, mw - 28 * s, 1, 0x44FFFFFF);
    }

    for (const std::string& line : msgLines)
    {
        const int lw = ModernTextWidth(line, usedMsgH);
        ModernText(c, mx + std::max(12 * s, (mw - lw) / 2), y, line, usedMsgH, 0xFFFFFFFF, true);
        y += usedMsgH + 4 * s;
    }

    if (progressPct >= 0)
    {
        const int pct = std::min(100, std::max(0, progressPct));
        const int barW = mw - 32 * s;
        const int barH = std::max(6, 3 * s);
        const int barX = mx + (mw - barW) / 2;
        const int barY = y + 2 * s;
        c.FillRound(barX, barY, barW, barH, barH / 2, 0xFF0E1524);
        c.OutlineRound(barX, barY, barW, barH, barH / 2, 1, 0xFF334766);
        const int fillW = barW * pct / 100;
        if (fillW > barH / 2)
            c.FillRound(barX, barY, fillW, barH, barH / 2, th.frame);
        y += barH + 7 * s;
    }
    else if (busy)
    {
        const float t = AnimTime();
        const int dotR = std::max(3, s + 1);
        const int dotGap = 7 * s;
        const int dotsW = 3 * dotR * 2 + 2 * (dotGap - dotR * 2);
        const int dx0 = mx + (mw - dotsW) / 2;
        const int dy = y + 2 * s;
        for (int i = 0; i < 3; i++)
        {
            const float ph = 0.45f + 0.55f * fabsf(sinf(t * 4.2f - i * 0.7f));
            const uint8_t a = (uint8_t)(90 + ph * 165);
            const uint32_t col = (th.frame & 0x00FFFFFFu) | ((uint32_t)a << 24);
            c.FillRound(dx0 + i * dotGap, dy, dotR * 2, dotR * 2, dotR, col);
        }
        y += dotR * 2 + 7 * s;
    }

    for (const std::string& line : detLines)
    {
        const int lw = ModernTextWidth(line, usedDetH);
        ModernText(c, mx + std::max(12 * s, (mw - lw) / 2), y, line, usedDetH, th.sub, false);
        y += usedDetH + 3 * s;
    }

    if (!trHint.empty())
    {
        const int pillH = hintH + 6 * s;
        const int pillW = ModernTextWidth(trHint, hintH) + 14 * s;
        const int px = mx + (mw - pillW) / 2;
        const int py = my + mh - pillH - 7 * s;
        c.FillRound(px, py, pillW, pillH, pillH / 2, 0xFF1A253B);
        c.OutlineRound(px, py, pillW, pillH, pillH / 2, 1, 0xFF3E5278);
        ModernText(c, px + 7 * s, py + (pillH - hintH) / 2, trHint, hintH, th.title, false);
    }
}

}
