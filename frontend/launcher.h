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
enum Platform { DS, GBA, N3DS, PLATFORMS };
inline const char* PlatformName(int p) { return p == DS ? "DS" : p == GBA ? "GBA" : "3DS"; }
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

// Games on their way, in their console's section with COMING SOON over the picture: Pokemon
// Omega Ruby on the 3DS (its picture as any game's: icons/ECRA.jpg, or SSR_PC_ART's ECRA.jpg).
inline void AddComingSoon(std::vector<Game>& games)
{
    Game g;
    g.platform = N3DS;
    g.code = "ECRA";
    g.rom = "coming-soon:ECRA"; // (never a file: so never the last game played, nor started)
    g.name = "POKEMON OMEGA RUBY";
    g.comingSoon = true;
    games.push_back(std::move(g));
}

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
    for (const char* word : { "Firered", "Leafgreen" })
    {
        const size_t at = out.find(word);
        if (at != std::string::npos) out[at + 4] = (char)toupper((unsigned char)out[at + 4]);
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

// Draw the authentic Nintendo DSi / DS firmware micro-mesh background
inline void DrawDsMesh(Canvas& c, int s)
{
    const int step = std::max(6, 6 * s);
    for (int y = 0; y < c.h; y += step)
    {
        const int xOff = ((y / step) % 2) * (step / 2);
        for (int x = xOff; x < c.w; x += step)
            c.FillBlend(x, y, std::max(1, s / 2), std::max(1, s / 2), 0x0AFFFFFF);
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

// Renders the modern Nintendo Switch / Tico Glass Hero Title & Metadata Showcase Banner
inline void DrawDsiTopBanner(Canvas& c, int x, int y, int w, int h, const Game& g, int s, uint32_t accent, int gameIdx = 0, int totalGames = 0)
{
    const bool isGba = (g.platform == GBA);
    const bool is3ds = (g.platform == N3DS);
    const uint32_t platAccent = isGba ? 0xFFA855F7 : is3ds ? 0xFFF43F5E : (accent ? accent : 0xFF00C3FF);

    // Subtle frosted glass showcase container with glowing left accent bar
    const int rad = 4 * s;
    c.ShadowRound(x, y, w, h, rad, s, 70);
    c.FillGlass(x, y, w, h, rad, 0xCC161F30, 0xCC0D131F, 0xFF26344B, 1);
    c.FillRound(x + 2 * s, y + 3 * s, std::max(3, s + 1), h - 6 * s, std::max(1, s / 2), platAccent);

    const int contentX = x + 8 * s;
    std::string title = TitleCase(g.name);
    int titleH = std::max(16, 24 * s / 3);
    while (titleH > 15 && ModernTextWidth(title, titleH) > w - 70 * s) --titleH;

    const int pillH = 8 * s;
    const int metaFontH = std::max(10, 12 * s / 3);
    const int gap = 3 * s;
    const int totalTextH = titleH + gap + pillH;
    const int startY = y + (h - totalTextH) / 2;

    // Line 1: Bold pure white game title with soft shadow
    ModernText(c, contentX, startY, title, titleH, 0xFFFFFFFF, true);

    // Line 2: Structured Metadata Pill Badges (Platform  •  Serial  •  Save Status)
    int curX = contentX;
    const int pillY = startY + titleH + gap;

    auto drawPill = [&](const std::string& label, uint32_t bgCol, uint32_t borderCol, uint32_t textCol) {
        const int tw = ModernTextWidth(label, metaFontH);
        const int pw = tw + 6 * s;
        c.FillRound(curX, pillY, pw, pillH, 2 * s, bgCol);
        c.OutlineRound(curX, pillY, pw, pillH, 2 * s, 1, borderCol);
        ModernText(c, curX + 3 * s, pillY + (pillH - metaFontH) / 2, label, metaFontH, textCol);
        curX += pw + 3 * s;
    };

    const std::string plat = (isGba ? "Game Boy Advance" : is3ds ? "Nintendo 3DS" : "Nintendo DS");
    const uint32_t platBg = isGba ? 0x38A855F7 : is3ds ? 0x38F43F5E : 0x3800C3FF;
    const uint32_t platBd = isGba ? 0x80A855F7 : is3ds ? 0x80F43F5E : 0x8000C3FF;
    const uint32_t platFg = isGba ? 0xFFD8B4FE : is3ds ? 0xFFFDA4AF : 0xFF7DD3FC;
    drawPill(plat, platBg, platBd, platFg);

    const std::string serial = (isGba ? "AGB-" : is3ds ? "CTR-" : "NTR-") + g.code;
    drawPill(serial, 0x441E293B, 0x88334155, 0xFFCBD5E1);

    const std::string rawStatus = g.comingSoon ? UiLang::Tr("COMING SOON") : !g.known ? UiLang::Tr("NOT PLAYABLE") : g.hasSave ? UiLang::Tr("SAVE FOUND") : UiLang::Tr("NEW GAME");
    const std::string status = std::string(1, (char)7) + " " + ToNiceTitle(rawStatus);
    const uint32_t statusFg = !g.known && !g.comingSoon ? 0xFFFF6B6B : g.comingSoon ? 0xFFD8B4FE : g.hasSave ? 0xFF34D399 : 0xFFFBBF24;
    const uint32_t statusBg = !g.known && !g.comingSoon ? 0x33FF453A : g.comingSoon ? 0x33A855F7 : g.hasSave ? 0x3300E676 : 0x33FFB020;
    const uint32_t statusBd = !g.known && !g.comingSoon ? 0x80FF453A : g.comingSoon ? 0x80A855F7 : g.hasSave ? 0x8000E676 : 0x80FFB020;
    drawPill(status, statusBg, statusBd, statusFg);

    // Right side of Hero Banner: Library Counter Badge ("1 / 6")
    if (totalGames > 0 && gameIdx >= 0)
    {
        char countBuf[32];
        snprintf(countBuf, sizeof(countBuf), "%d / %d", gameIdx + 1, totalGames);
        const std::string countStr = countBuf;
        const int countFontH = std::max(12, 14 * s / 3);
        const int cw = ModernTextWidth(countStr, countFontH) + 10 * s;
        const int ch = 11 * s;
        const int cx = x + w - cw - 5 * s;
        const int cy = y + (h - ch) / 2;
        c.FillRound(cx, cy, cw, ch, 3 * s, 0x661E293B);
        c.OutlineRound(cx, cy, cw, ch, 3 * s, 1, 0xFF334155);
        ModernText(c, cx + 5 * s, cy + (ch - countFontH) / 2, countStr, countFontH, 0xFFE2E8F0, true);
    }
}

// Renders a sleek Nintendo DS / GBA / 3DS Software Tile with framed emblem and card title footer
inline void DrawCartridgeCase(Canvas& c, int x, int y, int sz, int platform, const uint32_t* icon32,
                              const std::string& code, const std::string& name, int s, bool hasSave = false, bool sel = false)
{
    const bool isGba = (platform == GBA);
    const bool is3ds = (platform == N3DS);

    // Rich dark gradient surface tailored to console family
    const uint32_t gradTop = isGba ? (sel ? 0xFF2B1C44 : 0xFF201534) : is3ds ? (sel ? 0xFF381928 : 0xFF2A121E) : (sel ? 0xFF1F2D46 : 0xFF162032);
    const uint32_t gradBot = isGba ? 0xFF10091B : is3ds ? 0xFF14070D : 0xFF0C121E;
    const int rad = std::max(4, 5 * s);
    c.FillGradV(x, y, sz, sz, gradTop, gradBot, rad);
    c.OutlineRound(x, y, sz, sz, rad, 1, sel ? 0xFF3B5278 : 0xFF26354C);

    // Subtle upper specular reflection band
    c.FillBlend(x + 2 * s, y + 2 * s, sz - 4 * s, sz / 4, 0x09FFFFFF);

    // Platform pill badge in top-left
    const char* badge = isGba ? "GBA" : is3ds ? "3DS" : "DS";
    const uint32_t badgeBg = isGba ? 0x48A855F7 : is3ds ? 0x48F43F5E : 0x4800C3FF;
    const uint32_t badgeBorder = isGba ? 0x99A855F7 : is3ds ? 0x99F43F5E : 0x9900C3FF;
    const uint32_t badgeCol = isGba ? 0xFFD8B4FE : is3ds ? 0xFFFDA4AF : 0xFF38BDF8;
    const int badgeH = 9 * s;
    const int badgeFontH = std::max(10, 11 * s / 3);
    const int badgeW = ModernTextWidth(badge, badgeFontH) + 6 * s;
    const int bx = x + 4 * s, by = y + 4 * s;
    c.FillRound(bx, by, badgeW, badgeH, 2 * s, badgeBg);
    c.OutlineRound(bx, by, badgeW, badgeH, 2 * s, 1, badgeBorder);
    ModernText(c, bx + 3 * s, by + (badgeH - badgeFontH) / 2, badge, badgeFontH, badgeCol);

    // Save status LED dot in top-right corner when a save file exists
    if (hasSave)
    {
        const int dotR = std::max(2, 2 * s);
        const int dx = x + sz - 5 * s - dotR * 2;
        const int dy = by + (badgeH - dotR * 2) / 2;
        c.FillRound(dx - s, dy - s, (dotR + s) * 2, (dotR + s) * 2, dotR + s, 0x3800E676);
        c.FillRound(dx, dy, dotR * 2, dotR * 2, dotR, 0xFF00E676);
    }

    // Framed inner Emblem Medallion for the 32x32 DS/GBA icon (3x = 96x96 inside a 114x114 recessed glass well)
    const int footerH = 19 * s;
    const int bodyH = sz - footerH;
    const int iconScale = (sz >= 180) ? 3 : 2;
    const int iconSz = 32 * iconScale;
    const int wellPad = 4 * s;
    const int wellSz = iconSz + wellPad * 2;
    const int wx = x + (sz - wellSz) / 2;
    const int wy = y + 10 * s + (bodyH - 10 * s - wellSz) / 2;
    const int wellRad = 4 * s;

    // Ambient radial glow behind the recessed emblem well
    const uint32_t glowCol = isGba ? 0x26A855F7 : is3ds ? 0x26F43F5E : 0x2600C3FF;
    c.GlowRound(wx - 2 * s, wy - 2 * s, wellSz + 4 * s, wellSz + 4 * s, wellRad + 2 * s, 3 * s, glowCol);
    c.FillRound(wx, wy, wellSz, wellSz, wellRad, 0xB8090D16);
    c.OutlineRound(wx, wy, wellSz, wellSz, wellRad, 1, sel ? badgeBorder : 0xFF28364E);

    const int ix = wx + wellPad;
    const int iy = wy + wellPad;
    if (icon32)
    {
        for (int py = 0; py < 32; py++)
        {
            for (int px = 0; px < 32; px++)
            {
                const uint32_t p = icon32[py * 32 + px];
                if ((p >> 24) >= 0x40)
                    c.Fill(ix + px * iconScale, iy + py * iconScale, iconScale, iconScale, p | 0xFF000000u);
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
        ModernText(c, wx + (wellSz - iw) / 2, wy + (wellSz - is) / 2, initials, is, 0xFF8FA0B8, true);
    }

    // Frosted card footer strip with game short title and serial code
    const int fy = y + sz - footerH;
    c.FillBlend(x + s, fy, sz - 2 * s, footerH - s, 0x66060910);
    c.Fill(x + 3 * s, fy, sz - 6 * s, 1, 0x44334155);

    std::string cardTitle = ShortCardTitle(name);
    int nameFontH = std::max(11, 13 * s / 3);
    while (cardTitle.size() > 6 && ModernTextWidth(cardTitle, nameFontH) > sz - 6 * s)
    {
        cardTitle.pop_back();
    }
    const int nw = ModernTextWidth(cardTitle, nameFontH);
    ModernText(c, x + (sz - nw) / 2, fy + 2 * s, cardTitle, nameFontH, sel ? 0xFFFFFFFF : 0xFFD8E2F0, true);

    std::string serial = (isGba ? "AGB-" : is3ds ? "CTR-" : "NTR-") + code;
    const int serH = std::max(9, 10 * s / 3);
    const int swidth = ModernTextWidth(serial, serH);
    ModernText(c, x + (sz - swidth) / 2, fy + 2 * s + nameFontH + s, serial, serH, sel ? 0xFF94A3B8 : 0xFF64748B);
}

// Where the list's parts go on a canvas: the title band, the tabs under it, the DSi showcase
// banner, and the horizontal game carousel (Nintendo Switch Home + Nintendo DSi Showcase).
struct Grid
{
    int w = 0, h = 0;
    int s = 1;
    int tabsY = 0, tabsH = 0;         // Category tabs bar
    int bannerY = 0, bannerH = 0;     // Hero Title & Status Area
    int carouselY = 0;                // Center-stage horizontal carousel Y
    int tile = 0;                     // Square card size (240x240 at 720p)
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
    g.colGap = 9 * g.s;                          // 27px gap
    g.labelH = 8 * g.s;                          // 24px for selection indicator pill
    g.tabsY = 20 * g.s;                          // 60px
    g.tabsH = 11 * g.s;                          // 33px
    g.bannerY = g.tabsY + g.tabsH + 6 * g.s;     // 111px
    g.bannerH = 24 * g.s;                        // 72px Glass Hero Banner
    g.dockH = 18 * g.s;                          // 54px
    g.bottom = c.h - g.dockH;                    // 666px
    g.tile = (g.s == 3) ? 240 : 76 * g.s;        // 240px card size at 720p
    // Vertically balance carousel in the body below the hero banner
    g.carouselY = 232;
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
    const Grid& g = v.g;
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

    const int totalW = (int)mine.size() * g.tile + (int)(mine.size() - 1) * g.colGap;
    const int startX = (totalW <= c.w - 48 * g.s) ? (c.w - totalW) / 2 : 24 * g.s;

    for (size_t k = 0; k < mine.size(); k++)
    {
        const int x = startX + (int)k * (g.tile + g.colGap);
        v.spots.push_back({ mine[k], x, g.carouselY, games[mine[k]].platform });
    }
    v.width = totalW;
    v.height = g.tile + g.labelH;
    return v;
}

// Keeps the chosen game in view by centering it horizontally in the carousel.
inline void FollowSelection(const View& v, int selected, int& scroll, bool follow = true)
{
    if (v.spots.empty()) { scroll = 0; return; }
    const Grid& g = v.g;
    const int screenW = g.w > 0 ? g.w : 1280;
    if (v.width <= screenW - 48 * g.s)
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
        const int maxScroll = v.spots.back().x + g.tile - screenW + 24 * g.s;
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
    if (y < g.carouselY || y >= g.carouselY + g.tile + g.labelH) return -1;
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
    const uint32_t bgBot = 0xFF07090E;
    c.FillGradV(0, 0, c.w, c.h, th.bg, bgBot, 0);
    DrawDsMesh(c, s);
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
    const int topFontH = std::max(13, 5 * s + 1);
    c.ModernText(21 * s, (bandH - topFontH) / 2, title, topFontH, th.title, true);

    // Subtle edition pill badge beside NoSplitNX
    const int titleW = c.ModernTextWidth(title, topFontH);
    const std::string subTag = "WIDESCREEN";
    const int tagFontH = std::max(9, 3 * s + 1);
    const int tagW = c.ModernTextWidth(subTag, tagFontH) + 5 * s;
    const int tagH = 6 * s + 2;
    const int tagX = 21 * s + titleW + 4 * s;
    const int tagY = (bandH - tagH) / 2;
    c.FillRound(tagX, tagY, tagW, tagH, 2 * s, 0x3300C3FF);
    c.OutlineRound(tagX, tagY, tagW, tagH, 2 * s, 1, 0x8000C3FF);
    c.ModernText(tagX + (tagW - c.ModernTextWidth(subTag, tagFontH)) / 2, tagY + (tagH - tagFontH) / 2, subTag, tagFontH, 0xFF7DD3FC);

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
    if (grid.bannerH > 0 && !games.empty() && selected >= 0 && selected < (int)games.size())
    {
        const int spotIdx = v.SpotOf(selected);
        DrawDsiTopBanner(c, 24 * s, grid.bannerY, c.w - 48 * s, grid.bannerH, games[selected], s, th.frame,
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

    const int badgeH = 11 * s;
    c.DrawButtonBadge(segX - badgeH - 4 * s, segY + (segH - badgeH) / 2, "L", s, 0xFF222B3D, 0xFFE0E8F5, 0xFF3D4B66);
    c.DrawButtonBadge(segX + segW + 4 * s, segY + (segH - badgeH) / 2, "R", s, 0xFF222B3D, 0xFFE0E8F5, 0xFF3D4B66);

    for (int t = 0; t < TABS; t++)
    {
        const std::string name = (t == 0 ? UiLang::Tr("ALL") : t == 1 ? "Nintendo DS" : t == 2 ? "GBA" : "3DS");
        const int tabH = 14;
        const int nameW = ModernTextWidth(name, tabH);
        const int cx = TabX(c, grid, t);
        const int x = cx - nameW / 2, y = grid.tabsY + (grid.tabsH - tabH) / 2;
        const int pillW = std::max(nameW + 12 * s, tabStep - 4 * s);
        const int pillX = cx - pillW / 2;
        if (t == tab)
        {
            if (onTabs)
            {
                c.GlowRound(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, 3 * s, th.frame);
                c.FillRound(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, th.frame);
                ModernText(c, x, y, name, tabH, 0xFFFFFFFF, true);
            }
            else
            {
                c.FillGlass(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, 0xFF28364E, 0xFF182232, th.frame, 1);
                ModernText(c, x, y, name, tabH, 0xFFFFFFFF, true);
            }
        }
        else
        {
            ModernText(c, x, y, name, tabH, th.hint);
        }
    }

    FollowSelection(v, selected, scroll, follow);
    const int tile = grid.tile, labelH = grid.labelH;
    const int cardRad = std::max(4, 5 * s);

    // Empty state card if the chosen tab has no ROMs installed
    if (v.spots.empty())
    {
        const std::string none = UiLang::Tr("NO GAMES FOUND") + " (" + PlatformName(tab <= 0 ? DS : tab - 1) + ")";
        const int ew = ModernTextWidth(none, 14) + 24 * s;
        const int eh = 24 * s;
        const int ex = (c.w - ew) / 2, ey = grid.carouselY + (tile - eh) / 2;
        c.FillRound(ex, ey, ew, eh, 4 * s, 0xDD141926);
        c.OutlineRound(ex, ey, ew, eh, 4 * s, 1, 0xFF2A364C);
        ModernText(c, ex + 12 * s, ey + 6 * s, none, 14, th.hint);
    }

    // Ambient focus handled on active card

    // Render horizontal carousel cards
    for (const View::Spot& sp : v.spots)
    {
        const bool sel = (sp.game == selected);
        const int x = sp.x - scroll;
        const int y = sp.y - (sel ? floatY : 0);
        if (x + tile < -20 * s || x > c.w + 20 * s) continue;
        const Game& g = games[sp.game];

        const int cardRad = std::max(4, 5 * s);
        if (sel)
        {
            // Sleek Nintendo Switch Selection Focus: crisp white frame + neon cyan halo + soft shadow
            const uint32_t glowAlpha = (uint32_t)(40 + 30 * pulse) << 24;
            c.ShadowRound(x - 2 * s, y, tile + 4 * s, tile + 4 * s, cardRad + 2 * s, 3 * s, 110);
            c.GlowRound(x - 3 * s, y - 3 * s, tile + 6 * s, tile + 6 * s, cardRad + 3 * s, (int)((3.0f + 2.0f * pulse) * s), (th.frame & 0x00FFFFFF) | glowAlpha);
            c.OutlineRound(x - s, y - s, tile + 2 * s, tile + 2 * s, cardRad + s, s + 1, 0xFFFFFFFF);
            c.OutlineRound(x - 3 * s, y - 3 * s, tile + 6 * s, tile + 6 * s, cardRad + 3 * s, s + 1, th.frame);
        }
        else
        {
            c.ShadowRound(x, y, tile, tile, cardRad, s, 2 * s);
        }

        const uint32_t cardTop = sel ? 0xFF222C3E : 0xFF161B26;
        const uint32_t cardBot = sel ? 0xFF161E2C : 0xFF0E121A;
        const uint32_t cardBorder = sel ? 0xFFFFFFFF : 0xFF283448;
        c.FillGlass(x, y, tile, tile, cardRad, cardTop, cardBot, cardBorder, 1);

        if (!g.art.empty())
        {
            const int artPad = 2 * s;
            const int artX = x + artPad, artY = y + artPad;
            const int artSz = tile - 2 * artPad;
            const int artRad = std::max(2, cardRad - artPad);
            c.FillRound(artX, artY, artSz, artSz, artRad, 0xFF0D1017);
            for (int py = 0; py < artSz; py++)
            {
                const int cy = artY + py;
                if (cy < 0 || cy >= c.h) continue;
                int ix0 = 0, ix1 = artSz;
                Canvas::RoundedSpan(artSz, artSz, artRad, py, ix0, ix1);
                if (ix0 >= ix1) continue;
                const uint32_t* row = &g.art[(size_t)(py * Art / artSz) * Art];
                for (int px = ix0; px < ix1; px++)
                {
                    const int cx = artX + px;
                    if (cx < 0 || cx >= c.w) continue;
                    const uint32_t p = row[px * Art / artSz];
                    if ((p >> 24) >= 0x80) c.px[(size_t)cy * c.w + cx] = p | 0xFF000000u;
                }
            }
            c.FillBlend(artX, artY + artSz - 3 * s, artSz, 3 * s, 0x44000000);
        }
        else
        {
            DrawCartridgeCase(c, x, y, tile, g.platform, g.hasIcon ? g.icon : nullptr, g.code, g.name, s, g.hasSave, sel);
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
            c.FillBlend(x, y, tile, tile, 0xAA080B12);
            const std::string soon = UiLang::Tr("COMING SOON");
            const int soonH = 16;
            const int bannerH = 16 * s;
            const int bannerY = y + (tile - bannerH) / 2;
            c.FillRound(x + 2 * s, bannerY, tile - 4 * s, bannerH, 3 * s, 0xF0151924);
            c.OutlineRound(x + 2 * s, bannerY, tile - 4 * s, bannerH, 3 * s, 1, th.frame);
            ModernText(c, x + (tile - ModernTextWidth(soon, soonH)) / 2, bannerY + 3 * s, soon, soonH, th.title, true);
        }

        // Clean active indicator pill under selected card
        if (sel)
        {
            const int dotW = (int)((24.0f + 6.0f * pulse) * s);
            const int dotH = std::max(2, 2 * s);
            c.FillRound(x + (tile - dotW) / 2, y + tile + 8 * s, dotW, dotH, dotH / 2, 0xFF00E5FF);
        }
    }

    // Nintendo DSi Carousel Navigation Arrow Badges with gentle floating animation
    const int arrowFloat = (int)(2.5f * sinf(t * 4.0f));
    if (scroll > 0)
    {
        const int arrW = 13 * s, arrH = 22 * s;
        const int arrX = 6 * s + arrowFloat, arrY = grid.carouselY + (tile - arrH) / 2;
        c.ShadowRound(arrX, arrY, arrW, arrH, 4 * s, s, 90);
        c.FillGlass(arrX, arrY, arrW, arrH, 4 * s, 0xF01C2538, 0xF0111726, 0xFF3E5070, 1);
        const int chevH = std::max(14, 5 * s);
        const int chevW = ModernTextWidth("<", chevH);
        ModernText(c, arrX + (arrW - chevW) / 2, arrY + (arrH - chevH) / 2, "<", chevH, 0xFFFFFFFF, true);
    }
    if (scroll + c.w < v.width + 24 * s)
    {
        const int arrW = 13 * s, arrH = 22 * s;
        const int arrX = c.w - arrW - 6 * s - arrowFloat, arrY = grid.carouselY + (tile - arrH) / 2;
        c.ShadowRound(arrX, arrY, arrW, arrH, 4 * s, s, 90);
        c.FillGlass(arrX, arrY, arrW, arrH, 4 * s, 0xF01C2538, 0xF0111726, 0xFF3E5070, 1);
        const int chevH = std::max(14, 5 * s);
        const int chevW = ModernTextWidth(">", chevH);
        ModernText(c, arrX + (arrW - chevW) / 2, arrY + (arrH - chevH) / 2, ">", chevH, 0xFFFFFFFF, true);
    }

    // Bottom control dock bar with tactile button badges
    const int dockH = 18 * s;
    c.FillGlass(0, c.h - dockH, c.w, dockH, 0, 0xFA141926, 0xFA0B0E16, 0xFF28344C, 1);

    if (!note.empty())
    {
        int noteH = 14;
        while (noteH > 10 && ModernTextWidth(note, noteH) > c.w - 32 * s) --noteH;
        const int nw = ModernTextWidth(note, noteH) + 16 * s;
        const int nh = 14 * s;
        const int nx = (c.w - nw) / 2, ny = c.h - dockH + (dockH - nh) / 2;
        c.FillRound(nx, ny, nw, nh, 3 * s, 0xEE1E2638);
        c.OutlineRound(nx, ny, nw, nh, 3 * s, 1, th.frame);
        ModernText(c, nx + 8 * s, ny + (nh - noteH) / 2, note, noteH, th.title, true);
    }
    else
    {
        struct DockItem { const char* badge; const char* label; };
        static const DockItem items[] = {
            { "A", "PLAY" }, { "Y", "SETTINGS" }, { "X", "RANDOM" }, { "L/R", "TABS" }, { "+", "QUIT" }
        };
        const int itemCount = 5;
        const int bh = 11 * s;
        const int dockFontH = std::max(12, 14 * s / 3);
        int totalW = 0;
        int widths[itemCount];
        for (int i = 0; i < itemCount; i++)
        {
            const std::string trLabel = (strcmp(items[i].label, "RANDOM") == 0)
                ? (UiLang::lang == 1 ? "Aleatorio" : UiLang::lang == 2 ? "Casuale" : UiLang::lang == 3 ? "Aléatoire" : UiLang::lang == 4 ? "Zufall" : "Random")
                : ToNiceTitle(UiLang::Tr(items[i].label));
            const int bw = (strlen(items[i].badge) > 1) ? bh * 16 / 10 : bh;
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
                const std::string trLabel = (strcmp(items[i].label, "RANDOM") == 0)
                    ? (UiLang::lang == 1 ? "Aleatorio" : UiLang::lang == 2 ? "Casuale" : UiLang::lang == 3 ? "Aléatoire" : UiLang::lang == 4 ? "Zufall" : "Random")
                    : ToNiceTitle(UiLang::Tr(items[i].label));
                const int bw = (strlen(items[i].badge) > 1) ? bh * 16 / 10 : bh;
                c.DrawButtonBadge(dx, by, items[i].badge, s, 0xFF222B3D, 0xFFFFFFFF, 0xFF3D4B66);
                ModernText(c, dx + bw + 5 * s, by + (bh - dockFontH) / 2, trLabel, dockFontH, 0xFFD8E2F0, true);
                dx += widths[i] + itemGap;
            }
        }
        else
        {
            const std::string hint = !hintText.empty() ? hintText : "A: PLAY   Y: SETTINGS   X: RANDOM   L / R: TABS   +: QUIT";
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
    static const char* controls[] = { "start_button", "buttons", "pad_touch" };
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
    std::vector<std::pair<int, int>> rows; // (option, the top of its name line)

    // The option whose part of the page (its name and the lines below it) is at y, or -1.
    int ItemAt(int y) const
    {
        int found = -1;
        for (const auto& r : rows)
            if (r.second - lineH / 4 <= y) found = r.first;
        if (found >= 0 && !rows.empty() && y > rows.back().second + 6 * lineH) return -1;
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

    // Over the whole canvas: the options with their values (the chosen one marked), each with
    // what it does below it, kept in view as the choice moves.
    void Draw(Canvas& c, const std::string& hint, const Theme& th = Theme())
    {
        const int s = std::max(1, std::min(c.w, c.h) / 240);
#ifdef __ANDROID__
        // Phone settings use readable touch targets in either orientation.
        const int ts = std::max(2, s);
#else
        const int ts = std::max(1, (s * 2 + 1) / 3); // the text's scale: 2 at 720 lines, 3 at 1080
#endif
        const int bandH = (c.h > c.w ? 43 : 22) * s;
        c.FillGradV(0, 0, c.w, c.h, th.bg, 0xFF07090E, 0);
        DrawDsMesh(c, s);
        if (th.background)
            if (const uint32_t* bgPx = th.background->Fitted(c.w, c.h)) std::copy(bgPx, bgPx + (size_t)c.w * c.h, c.px.begin());
        c.FillGradV(0, 0, c.w, bandH, 0xFF191F2D, th.band, 0);
        c.Fill(0, bandH, c.w, th.bandLineH * s, th.bandLine);
        c.FillBlend(0, bandH + th.bandLineH * s, c.w, 2 * s, 0x33000000);
        const int logoH = (4 * s + 1) * 2 + std::max(1, s + 1);
        DrawDsLogo(c, 10 * s, (bandH - logoH) / 2, s, th.title);
        const std::string title = group < 0 ? ToNiceTitle(UiLang::Tr("SETTINGS")) : ToNiceTitle(UiLang::Tr("SETTINGS")) + ": " + ToNiceTitle(UiLang::Tr(SettingsGroups()[group].name));
        const int titleH = std::max(14, 5 * s + 1);
        const int tw = c.ModernTextWidth(title, titleH);
        c.ModernText((c.w - tw) / 2, (bandH - titleH) / 2, title, titleH, th.title);
        const int width = std::min(c.w - 48 * s, 860);
        const int x = (c.w - width) / 2;
        struct Line { std::string text, value; int item; bool name; };
        std::vector<Line> out;
        int selFirst = 0, selLast = 0;
        bool infoHeading = false;
        const int nameFontH = std::max(13, 7 * ts);
        const int descFontH = std::max(11, 5 * ts);
        for (int i = 0; i < (int)items.size(); i++)
        {
            const Setting& e = items[i];
            if (!e.key && !infoHeading)
            {
                out.push_back({ "", "", -1, false });
                infoHeading = true;
            }
            if (i == selected) selFirst = (int)out.size();
            const std::string value = e.key ? Shown(e) : "";
            const bool arrows = !value.empty() && i == selected && e.key && strncmp(e.key, "!group", 6);
            const std::string niceVal = ToNiceTitle(UiLang::Tr(value));
            const std::string drawnValue = arrows ? "< " + niceVal + " >" : niceVal;
            const std::string niceName = ToNiceTitle(UiLang::Tr(e.name));
            const bool stacked = c.ModernTextWidth(niceName, nameFontH) + c.ModernTextWidth(drawnValue, nameFontH) + 16 * ts > width;
            out.push_back({ niceName, stacked ? "" : value, i, true });
            if (stacked && !value.empty())
                for (const std::string& l : Wrap(niceVal, width / (6 * ts) - 2)) out.push_back({ l, "", i, false });
            for (const std::string& l : Wrap(ToNiceTitle(UiLang::Tr(e.text)), width / (6 * ts) - 2)) out.push_back({ l, "", i, false });
            if (i == selected) selLast = (int)out.size() - 1;
            out.push_back({ "", "", -1, false });
        }
        int top = (c.h > c.w ? 48 : 30) * s, bottom = c.h - 18 * s;
        lineH = std::max(20, (UiLang::Tall() ? 16 : 11) * ts);
        const int fit = std::max(1, (bottom - top) / lineH);
        if (follow)
        {
            if (selFirst < scroll) scroll = selFirst;
            if (selLast >= scroll + fit) scroll = selLast - fit + 1;
        }
        if (group < 0 && (int)out.size() < fit)
        {
            const int totalH = (int)out.size() * lineH;
            top = top + std::max(0, (bottom - top - totalH) / 2);
        }
        scroll = std::max(0, std::min(scroll, (int)out.size() - fit));
        rows.clear();

        // Draw elevated frosted glass cards behind each visible item group before drawing text.
        const int cardRad = std::max(3, 4 * ts);
        const float t = AnimTime();
        const float pulse = 0.5f + 0.5f * sinf(t * 3.5f);
        for (int i = scroll; i < (int)out.size(); )
        {
            const int y0 = top + (i - scroll) * lineH;
            if (y0 + lineH > bottom) break;
            const int it = out[i].item;
            if (it < 0) { i++; continue; }
            int j = i;
            while (j < (int)out.size() && out[j].item == it && top + (j - scroll + 1) * lineH <= bottom) j++;
            const int cardY = y0 - 2 * ts;
            const int cardH = (j - i) * lineH + ts;
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
            i = j;
        }

        int y = top;
        for (int i = scroll; i < (int)out.size() && y + lineH <= bottom; i++, y += lineH)
        {
            const Line& l = out[i];
            if (l.name) rows.push_back({ l.item, y });
            const bool sel = l.item == selected && l.item >= 0;
            if (l.name)
            {
                const bool info = !items[l.item].key;
                c.ModernText(x + 4 * ts, y + (lineH - nameFontH) / 2, l.text, nameFontH, info ? th.hint : sel ? th.nameSel : th.subSel);
                if (!l.value.empty())
                {
                    const bool group = !strncmp(items[l.item].key, "!group", 6);
                    const std::string v = sel && !group ? "< " + ToNiceTitle(UiLang::Tr(l.value)) + " >" : ToNiceTitle(l.value);
                    const int valW = c.ModernTextWidth(v, nameFontH);
                    c.ModernText(x + width - valW - 4 * ts, y + (lineH - nameFontH) / 2, v, nameFontH, sel ? th.nameSel : th.name);
                }
            }
            else if (!l.text.empty())
            {
                c.ModernText(x + 10 * ts, y + (lineH - descFontH) / 2, l.text, descFontH, sel ? th.name : th.sub);
            }
        }
        if (scroll > 0) c.FillRound(c.w / 2 - 10 * s, top - 4 * s, 20 * s, s + 1, std::max(1, s / 2), th.frame);
        if (scroll + fit < (int)out.size()) c.FillRound(c.w / 2 - 10 * s, bottom + s, 20 * s, s + 1, std::max(1, s / 2), th.frame);
        const int dockH = 18 * s;
        c.FillGlass(0, c.h - dockH, c.w, dockH, 0, 0xFA141926, 0xFA0B0E16, 0xFF28344C, 1);

        struct DockItem { const char* badge; const char* label; };
        static const DockItem dockItems[] = {
            { "A", "CHANGE" }, { "B", "BACK" }
        };
        const int itemCount = 2;
        const int bh = 11 * s;
        const int dockFontH = std::max(11, 4 * s);
        int totalW = 0;
        int widths[itemCount];
        for (int i = 0; i < itemCount; i++)
        {
            const std::string niceLabel = ToNiceTitle(UiLang::Tr(dockItems[i].label));
            widths[i] = bh + c.ModernTextWidth(niceLabel, dockFontH) + 6 * s;
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
                c.ModernText(dx + bh + 4 * s, by + (bh - dockFontH) / 2, niceLabel, dockFontH, th.hint);
                dx += widths[i] + itemGap;
            }
        }
        else
        {
            const int hintH = std::max(11, 4 * s);
            const std::string niceHint = ToNiceTitle(hint);
            const int hw = c.ModernTextWidth(niceHint, hintH);
            c.ModernText((c.w - hw) / 2, c.h - dockH + (dockH - hintH) / 2, niceHint, hintH, th.pokemon ? 0xFFDBE9FAu : th.hint);
        }
    }
};

}
