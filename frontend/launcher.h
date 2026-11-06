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
#include <vector>
#include <dirent.h>
#include "ssr_os.h"
#include "gamecode.h"
#include <sys/stat.h>

#include "ui.h"
#include "switch_platform.h"
// (JPEG and PNG pictures for the list: the program that draws it defines STB_IMAGE_IMPLEMENTATION
// with STBI_ONLY_JPEG and STBI_ONLY_PNG before including this, in one file.)
#include "thirdparty/stb_image.h"

namespace Launcher
{

// Keep the established launcher font while the replacement design is unfinished.
inline void ModernText(Canvas& c, int x, int y, const std::string& text, int scale, uint32_t colour)
{
    c.Text(x, y, text, scale, colour);
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
    std::string out;
    bool start = true;
    for (char c : name)
    {
        out += start ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
        start = c == ' ';
    }
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
    uint32_t bg = 0xFF0D1017, band = 0xFF151924, bandLine = 0xFFE0242C, title = 0xFFFFFFFF;
    uint32_t tile = 0xFF1A202E, frame = 0xFFE0242C;
    uint32_t name = 0xFFC8D0E0, nameSel = 0xFFFFFFFF, sub = 0xFF7E8A9E, subSel = 0xFFE0242C, bad = 0xFFE0242C;
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

// Renders a tactile physical cartridge (Nintendo DS or GBA) when high-res box art is not present.
inline void DrawCartridgeCase(Canvas& c, int x, int y, int sz, int platform, const uint32_t* icon32,
                              const std::string& code, const std::string& name, int s)
{
    const bool isGba = (platform == GBA);
    const int cw = isGba ? sz * 92 / 100 : sz * 86 / 100;
    const int ch = isGba ? sz * 78 / 100 : sz * 88 / 100;
    const int cx = x + (sz - cw) / 2;
    const int cy = y + (sz - ch) / 2;
    const int rad = std::max(2, 4 * s);

    c.ShadowRound(cx, cy, cw, ch, rad, s, std::max(1, s / 2));

    const uint32_t shellTop = isGba ? 0xFF2A2F3B : 0xFF242730;
    const uint32_t shellBot = isGba ? 0xFF191C24 : 0xFF15181F;
    const uint32_t shellBorder = isGba ? 0xFF3C4354 : 0xFF363B48;
    c.FillGlass(cx, cy, cw, ch, rad, shellTop, shellBot, shellBorder, 1);

    if (isGba)
    {
        const int lipW = cw * 70 / 100;
        const int lipH = std::max(2, 3 * s);
        const int lipX = cx + (cw - lipW) / 2;
        c.FillRound(lipX, cy + s, lipW, lipH, s, 0x55000000);
        c.OutlineRound(lipX, cy + s, lipW, lipH, s, 1, 0x44FFFFFF);
    }
    else
    {
        const int notchW = std::max(2, 4 * s);
        const int notchH = std::max(3, 6 * s);
        c.Fill(cx + cw - notchW, cy, notchW, notchH, 0xFF11141A);
        const int arrowW = 6 * s, arrowH = 2 * s;
        c.FillRound(cx + (cw - arrowW) / 2, cy + ch - arrowH - s, arrowW, arrowH, 1, 0x44000000);
    }

    const int stickerPad = std::max(3, 4 * s);
    const int sx = cx + stickerPad;
    const int sy = cy + (isGba ? 6 * s : 4 * s);
    const int sw = cw - 2 * stickerPad;
    const int sh = ch - (isGba ? 9 * s : 8 * s);
    const int sRad = std::max(2, 2 * s);

    const uint32_t labelTop = isGba ? 0xFF261D3A : 0xFFE2E6EC;
    const uint32_t labelBot = isGba ? 0xFF140E20 : 0xFFCBD2DA;
    c.FillGradV(sx, sy, sw, sh, labelTop, labelBot, sRad);
    c.OutlineRound(sx, sy, sw, sh, sRad, 1, isGba ? 0xFF4A3870 : 0xFF9CA7B4);

    const int stripH = std::max(3, 5 * s);
    if (isGba)
    {
        c.FillRound(sx + s, sy + s, sw - 2 * s, stripH, 1, 0xFF4D1D88);
        const int bs = std::max(1, s - 1);
        c.Text(sx + 3 * s, sy + s + (stripH - 7 * bs) / 2, "GAME BOY ADVANCE", bs, 0xFFE6D6FF);
    }
    else
    {
        c.FillRound(sx + s, sy + s, sw - 2 * s, stripH, 1, 0xFF163E78);
        const int bs = std::max(1, s - 1);
        c.Text(sx + 3 * s, sy + s + (stripH - 7 * bs) / 2, "NINTENDO DS", bs, 0xFFFFFFFF);
    }

    if (icon32)
    {
        const int maxArtH = sh - stripH - (isGba ? 8 * s : 7 * s);
        const int iconScale = std::max(1, std::min(sw - 4 * s, maxArtH) / 32);
        const int iconSz = 32 * iconScale;
        const int ix = sx + (sw - iconSz) / 2;
        const int iy = sy + stripH + (maxArtH - iconSz) / 2 + s;

        c.FillRound(ix - s, iy - s, iconSz + 2 * s, iconSz + 2 * s, 2, isGba ? 0x66000000 : 0x22000000);
        for (int py = 0; py < 32; py++)
            for (int px = 0; px < 32; px++)
            {
                const uint32_t p = icon32[py * 32 + px];
                if ((p >> 24) >= 0x40)
                    c.Fill(ix + px * iconScale, iy + py * iconScale, iconScale, iconScale, p | 0xFF000000u);
            }
    }
    else
    {
        std::string initials;
        for (char ch : name)
            if (isupper((unsigned char)ch) && initials.size() < 4) initials += ch;
        if (initials.empty() && !code.empty()) initials = code;
        const int is = std::max(1, s);
        const int iw = Canvas::TextWidth(initials, is);
        c.Text(sx + (sw - iw) / 2, sy + (sh - 7 * is) / 2, initials, is, isGba ? 0xFFFFFFFF : 0xFF1E2634);
    }

    std::string serial = (isGba ? "AGB-" : "NTR-") + code;
    const int ss = std::max(1, s / 2);
    const int swidth = Canvas::TextWidth(serial, ss);
    if (swidth + 4 * ss <= sw)
    {
        const int serY = sy + sh - 7 * ss - 2 * ss;
        c.Text(sx + (sw - swidth) / 2, serY, serial, ss, isGba ? 0x99D0C0F0 : 0xFF5A6678);
    }

    for (int gy = 0; gy < sh / 2; gy++)
    {
        const int gw = std::max(0, sw - gy * 2);
        if (gw > 0)
            c.FillBlend(sx + s, sy + s + gy, gw, 1, 0x14FFFFFF);
    }
}

// Where the list's parts go on a canvas: the title band, the tabs under it, then the games'
// tiles (each the game's picture with its name below), in an adaptive grid that scales
// gracefully across orientations and high resolutions.
struct Grid
{
    int s, gap, colGap, rowGap, labelH, tabsY, tabsH, headH, top, bottom, tile, cols, gx;
    int panelX = 0, panelW = 0;
    int rowH() const { return tile + labelH + rowGap; }
};
inline Grid Layout(const Canvas& c)
{
    Grid g;
    g.s = std::max(1, std::min(c.w, c.h) / 240); // the font's scale: 3 at 720 lines
    g.colGap = 7 * g.s;
    g.rowGap = 7 * g.s;
    g.gap = g.rowGap;
    g.labelH = (UiLang::Tall() ? 32 : 20) * g.s;
    g.tabsY = (c.h > c.w ? 46 : 25) * g.s;
    g.tabsH = 13 * g.s;
    g.headH = 13 * g.s;
    g.top = g.tabsY + g.tabsH + 5 * g.s;
    g.bottom = c.h - 16 * g.s;
    const int availH = g.bottom - g.top - g.headH;
    if (c.h > c.w) // portrait / vertical screen
    {
        g.cols = 2;
        g.tile = std::max(32, (c.w - 3 * g.colGap) / 2);
    }
    else // landscape widescreen
    {
        g.tile = std::max(32, std::min(Art * c.h / 720, (availH - g.rowGap) / 2 - g.labelH - g.rowGap));
        const int maxCols = c.w >= 2560 ? 7 : c.w >= 1920 ? 6 : c.w >= 1400 ? 5 : 4;
        g.cols = std::max(2, std::min(maxCols, (c.w - 4 * g.colGap) / (g.tile + g.colGap)));
    }
    const int gridW = g.cols * g.tile + (g.cols - 1) * g.colGap;
    g.gx = std::max(0, (c.w - gridW) / 2);
    return g;
}

// What one tab shows: each game's tile and each console's heading (on ALL) or the line saying it
// has none yet, in content coordinates.
struct View
{
    struct Spot { int game, x, y, section; };
    struct Head { int platform, y; bool empty; };
    Grid g;
    int tab = 0;
    std::vector<Spot> spots;
    std::vector<Head> heads;
    int height = 0;
    // The spot of game i, or -1.
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
    const int emptyH = 16 * g.s;
    int y = 0;
    for (int p = 0; p < PLATFORMS; p++)
    {
        if (!InTab(p, tab)) continue;
        std::vector<int> mine;
        for (size_t i = 0; i < games.size(); i++)
            if (games[i].platform == p) mine.push_back((int)i);
        // (ALL: every console under its heading, also one without games yet.)
        if (tab == 0)
        {
            v.heads.push_back({ p, y, false });
            y += g.headH;
        }
        if (mine.empty())
        {
            v.heads.push_back({ p, y, true });
            y += emptyH + g.rowGap;
            continue;
        }
        for (size_t k = 0; k < mine.size(); k++)
            v.spots.push_back({ mine[k], g.gx + (int)(k % g.cols) * (g.tile + g.colGap), y + (int)(k / g.cols) * g.rowH(), p });
        y += (int)((mine.size() + g.cols - 1) / g.cols) * g.rowH() + g.rowGap;
    }
    v.height = y;
    return v;
}

// Keeps the chosen game in view (with its console's heading when it is in the first row under
// it): scroll is how far down the content the screen is. follow false: only kept within the
// content (a finger scrolling the list).
inline void FollowSelection(const View& v, int selected, int& scroll, bool follow = true)
{
    const Grid& g = v.g;
    const int viewH = g.bottom - g.top;
    const int k = follow ? v.SpotOf(selected) : -1;
    if (k >= 0)
    {
        int top = v.spots[k].y;
        for (const View::Head& h : v.heads)
            if (!h.empty && h.platform == v.spots[k].section && top - h.y <= g.headH) top = h.y;
        const int bottom = v.spots[k].y + g.tile + g.labelH;
        if (top < scroll) scroll = top;
        if (bottom > scroll + viewH) scroll = bottom - viewH;
    }
    scroll = std::max(0, std::min(scroll, std::max(0, v.height - viewH)));
}

// The game whose tile (with its name below it) is at x, y on the canvas, or -1.
inline int TileAt(const View& v, int scroll, int x, int y)
{
    const Grid& g = v.g;
    if (y < g.top || y >= g.bottom) return -1;
    for (const View::Spot& sp : v.spots)
    {
        const int ty = g.top + sp.y - scroll;
        if (x >= sp.x && x < sp.x + g.tile && y >= ty && y < ty + g.tile + g.labelH) return sp.game;
    }
    return -1;
}

// Where the tabs are drawn (TabsDraw) and which one is at x, y, or -1.
inline int TabX(const Canvas& c, const Grid& g, int t)
{
    const int step = 22 * 6 * g.s / 3; // (each tab's room, at the font's scale)
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

// The next game from `selected` in direction dir (1 left, 2 right, 3 up, 4 down): left and right
// go through the tab's games in order, up and down to the nearest tile in the row above or below
// (across the headings). -1 when there is nothing that way (up from the first row: the caller's
// title band; down from the last: stay).
inline int Step(const View& v, int selected, int dir)
{
    const int k = v.SpotOf(selected);
    if (k < 0) return v.spots.empty() ? -1 : v.spots[0].game;
    if (dir == 1) return k > 0 ? v.spots[k - 1].game : selected;
    if (dir == 2) return k + 1 < (int)v.spots.size() ? v.spots[k + 1].game : selected;
    // The row above or below: the nearest y beyond this one, then the nearest x in it.
    const int y = v.spots[k].y, x = v.spots[k].x;
    int rowY = -1;
    for (const View::Spot& sp : v.spots)
        if ((dir == 3 ? sp.y < y : sp.y > y) && (rowY < 0 || (dir == 3 ? sp.y > rowY : sp.y < rowY))) rowY = sp.y;
    if (rowY < 0) return dir == 3 ? -1 : selected;
    int best = -1, bestD = 0;
    for (const View::Spot& sp : v.spots)
        if (sp.y == rowY && (best < 0 || std::abs(sp.x - x) < bestD))
        {
            best = sp.game;
            bestD = std::abs(sp.x - x);
        }
    return best;
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
    if (th.background)
        if (const uint32_t* bgPx = th.background->Fitted(c.w, c.h)) std::copy(bgPx, bgPx + (size_t)c.w * c.h, c.px.begin());

    // Modern top header bar with accent underline.
    const int bandH = (c.h > c.w ? 43 : 22) * s;
    c.FillGradV(0, 0, c.w, bandH, 0xFF191F2D, th.band, 0);
    c.Fill(0, bandH, c.w, th.bandLineH * s, th.bandLine);
    const std::string title = "NOSPLIT NX";
    c.Text((c.w - Canvas::TextWidth(title, s + 1)) / 2, 6 * s, title, s + 1, th.title);
#ifdef __SWITCH__
    // Placed on the top-left of the header bar so it never collides with SETTINGS (Y) on the right.
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    if (SwitchPlatform::LocalTime(yr, mo, dy, hr, mn, sc))
    {
        char timeBuf[16];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hr, mn);
        const std::string clockStr = timeBuf;
        const std::string modeStr = SwitchPlatform::IsHandheld() ? "HANDHELD" : "DOCKED";
        const int modeScale = std::max(1, s - 1);
        const int modeW = Canvas::TextWidth(modeStr, modeScale) + 6 * s;
        const int pillH = 11 * s;
        const int pillY = 5 * s;
        c.FillRound(8 * s, pillY, modeW, pillH, 3 * s, 0xFF22293A);
        c.OutlineRound(8 * s, pillY, modeW, pillH, 3 * s, 1, 0xFF333D54);
        c.Text(8 * s + 3 * s, pillY + (pillH - 7 * modeScale) / 2, modeStr, modeScale, th.hint);
        const int clockX = 8 * s + modeW + 4 * s;
        const int clockW = Canvas::TextWidth(clockStr, s) + 6 * s;
        c.FillRound(clockX, pillY, clockW, pillH, 3 * s, 0xFF22293A);
        c.Text(clockX + 3 * s, 6 * s, clockStr, s, th.title);
    }
#endif

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
        const std::string name = TabName(t);
        const int nameW = Canvas::TextWidth(name, s);
        const int cx = TabX(c, grid, t);
        const int x = cx - nameW / 2, y = grid.tabsY + 3 * s;
        const int pillW = std::max(nameW + 10 * s, tabStep - 4 * s);
        const int pillX = cx - pillW / 2;
        if (t == tab)
        {
            if (onTabs)
            {
                c.GlowRound(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, 3 * s, th.frame);
                c.FillRound(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, th.frame);
                c.Text(x, y, name, s, th.title);
            }
            else
            {
                c.FillGlass(pillX, grid.tabsY, pillW, grid.tabsH, 3 * s, 0xFF263146, 0xFF19202E, th.frame, 1);
                c.Text(x, y, name, s, th.title);
            }
        }
        else
        {
            c.Text(x, y, name, s, th.hint);
        }
    }

    FollowSelection(v, selected, scroll, follow);
    const int top = grid.top, bottom = grid.bottom, tile = grid.tile, labelH = grid.labelH;

    // Console section headers on ALL tab, or empty-state cards when a console has no ROMs.
    for (const View::Head& h : v.heads)
    {
        const int y = top + h.y - scroll;
        if (h.empty)
        {
            const std::string none = UiLang::Tr("NO GAMES FOUND") + " (" + PlatformName(h.platform) + ")";
            if (tab != 0)
            {
                const int ew = Canvas::TextWidth(none, s) + 16 * s;
                const int eh = 18 * s;
                const int ex = (c.w - ew) / 2, ey = (top + bottom - eh) / 2;
                c.FillRound(ex, ey, ew, eh, 4 * s, 0xCC141926);
                c.OutlineRound(ex, ey, ew, eh, 4 * s, 1, 0xFF283248);
                c.Text(ex + 8 * s, ey + 5 * s, none, s, th.hint);
            }
            else if (y >= top && y + 12 * s <= bottom)
            {
                const int ew = Canvas::TextWidth(none, s) + 10 * s;
                c.FillRound(grid.gx, y, ew, 12 * s, 3 * s, 0x99141926);
                c.Text(grid.gx + 5 * s, y + 2 * s, none, s, th.hint);
            }
            continue;
        }
        if (y < top || y + 10 * s > bottom) continue;
        const std::string name = PlatformName(h.platform);
        c.FillRound(grid.gx, y + s, 2 * s, 8 * s, s, th.frame);
        c.Text(grid.gx + 4 * s, y + s, name, s + 1, th.title);
        const int lx = grid.gx + 4 * s + Canvas::TextWidth(name, s + 1) + 5 * s;
        const int rx = grid.gx + grid.cols * tile + (grid.cols - 1) * grid.colGap;
        if (rx > lx)
        {
            c.Fill(lx, y + 5 * s, std::min(24 * s, rx - lx), std::max(1, s / 2), th.frame);
            if (rx > lx + 24 * s)
                c.Fill(lx + 24 * s, y + 5 * s, rx - lx - 24 * s, std::max(1, s / 2), 0xFF252D40);
        }
    }

    // Elevated rounded game cards with cover art, cartridge icon well, platform badge, and save status pill.
    const int cardRad = std::max(4, 5 * s);
    for (const View::Spot& sp : v.spots)
    {
        const int x = sp.x, y = top + sp.y - scroll;
        const int cardH = tile + labelH - 2 * s;
        if (y < top || y + cardH > bottom) continue;
        const Game& g = games[sp.game];
        const bool sel = sp.game == selected;

        if (sel)
            c.GlowRound(x, y, tile, cardH, cardRad, 5 * s, th.frame);
        else
            c.ShadowRound(x, y, tile, cardH, cardRad, s, 2 * s);

        const uint32_t cardTop = sel ? 0xFF263248 : 0xFF181E2C;
        const uint32_t cardBot = sel ? 0xFF1A2234 : 0xFF111520;
        const uint32_t cardBorder = sel ? th.frame : 0xFF28344C;
        c.FillGlass(x, y, tile, cardH, cardRad, cardTop, cardBot, cardBorder, 1);

        // Cover art area at top of card
        const int artPad = 2 * s;
        const int artX = x + artPad, artY = y + artPad;
        const int artSz = tile - 2 * artPad;

        if (!g.art.empty())
        {
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
            DrawCartridgeCase(c, artX, artY, artSz, g.platform, g.hasIcon ? g.icon : nullptr, g.code, g.name, s);
        }

        // Platform badge (NDS / GBA) on top-left of box art
        if (!g.art.empty())
        {
            const char* platText = g.platform == GBA ? "GBA" : "NDS";
            const uint32_t platBg = g.platform == GBA ? 0xEE4E1D88 : 0xEE0B4FA8;
            const uint32_t platBorder = g.platform == GBA ? 0xFF9D4EDD : 0xFF3A86FF;
            const int bScale = std::max(1, s - 1);
            const int bW = Canvas::TextWidth(platText, bScale) + 6 * bScale;
            const int bH = 9 * bScale;
            const int bx = artX + 3 * s, by = artY + 3 * s;
            c.FillRound(bx, by, bW, bH, 2 * bScale, platBg);
            c.OutlineRound(bx, by, bW, bH, 2 * bScale, 1, platBorder);
            c.Text(bx + 3 * bScale, by + bScale, platText, bScale, 0xFFFFFFFF);
        }

        // COMING SOON overlay if unreleased
        if (g.comingSoon)
        {
            c.FillBlend(artX, artY, artSz, artSz, 0xAA080B12);
            const std::string soon = "COMING SOON";
            const int bs = Canvas::TextWidth(soon, s) + 8 * s <= artSz ? s : std::max(1, s - 1);
            const int bannerH = 14 * bs;
            const int bannerY = artY + (artSz - bannerH) / 2;
            c.FillRound(artX + 2 * s, bannerY, artSz - 4 * s, bannerH, 3 * bs, 0xF0151924);
            c.OutlineRound(artX + 2 * s, bannerY, artSz - 4 * s, bannerH, 3 * bs, 1, th.frame);
            c.Text(artX + (artSz - Canvas::TextWidth(soon, bs)) / 2, bannerY + 3 * bs, soon, bs, th.title);
        }

        // Focused double selection ring
        if (sel)
        {
            const int fw = th.frameW * s;
            c.OutlineRound(x - fw, y - fw, tile + 2 * fw, cardH + 2 * fw, cardRad + fw, fw, th.frame);
            c.OutlineRound(x - fw - s, y - fw - s, tile + 2 * (fw + s), cardH + 2 * (fw + s), cardRad + fw + s, s, (th.frame & 0x00FFFFFF) | 0x44000000u);
        }

        // Game title
        std::string name = g.name;
        if (name.rfind("POKEMON ", 0) == 0) name = name.substr(8);
        int ns = s;
        while (ns > 1 && Canvas::TextWidth(name, ns) > tile - 6 * s) --ns;
        c.Text(x + (tile - Canvas::TextWidth(name, ns)) / 2, y + tile + 2 * s, name, ns, sel ? th.nameSel : th.name);

        // Status string & hardware LED dot
        std::string sub = g.comingSoon ? "COMING SOON" : !g.known ? "NOT PLAYABLE" : g.hasSave ? "SAVE FOUND" : "NEW GAME";
        if (!g.where.empty() && g.known) sub = (g.hasSave ? "SAVE " : "NEW ") + g.where;
        const int ss = std::max(1, s * 2 / 3);
        const uint32_t ledCol = !g.known && !g.comingSoon ? 0xFFFF453A
                              : g.comingSoon ? 0xFFA855F7
                              : g.hasSave ? 0xFF00E676
                              : 0xFFFFB020;
        const uint32_t pillBg = !g.known && !g.comingSoon ? 0xDD3B141C
                              : g.comingSoon ? 0xDD251A38
                              : g.hasSave ? 0xDD123624
                              : 0xDD3A2B12;
        const uint32_t pillBorder = !g.known && !g.comingSoon ? 0x88FF453A
                                  : g.comingSoon ? 0x88A855F7
                                  : g.hasSave ? 0x8800E676
                                  : 0x88FFB020;

        const int dotR = std::max(2, 2 * ss);
        const int textW = Canvas::TextWidth(sub, ss);
        const int pillW = textW + dotR * 2 + 10 * ss;
        const int pillH = 10 * ss;
        const int pillX = x + (tile - pillW) / 2;
        const int pillY = y + tile + (UiLang::Tall() ? 19 : 12) * s;

        c.FillRound(pillX, pillY - ss, pillW, pillH, 3 * ss, pillBg);
        c.OutlineRound(pillX, pillY - ss, pillW, pillH, 3 * ss, 1, pillBorder);

        const int dotX = pillX + 4 * ss + dotR;
        const int dotY = pillY - ss + pillH / 2;
        c.FillRound(dotX - dotR - 1, dotY - dotR - 1, (dotR + 1) * 2, (dotR + 1) * 2, dotR + 1, (ledCol & 0x00FFFFFF) | 0x44000000u);
        c.FillRound(dotX - dotR, dotY - dotR, dotR * 2, dotR * 2, dotR, ledCol);
        c.Fill(dotX - 1, dotY - 1, 2, 2, 0xFFFFFFFF);

        c.Text(dotX + dotR + 3 * ss, pillY, sub, ss, ledCol | 0xFF000000u);
    }

    // Scroll indicator pill when more games lie below.
    if (scroll + (bottom - top) < v.height - grid.gap)
        c.FillRound(c.w / 2 - 10 * s, bottom - 2 * s, 20 * s, s + 1, std::max(1, s / 2), th.frame);

    // Bottom control dock bar with tactile button badges
    const int dockH = 18 * s;
    c.FillGlass(0, c.h - dockH, c.w, dockH, 0, 0xFA141926, 0xFA0B0E16, 0xFF28344C, 1);

    if (!note.empty())
    {
        int noteScale = s;
        while (noteScale > 1 && Canvas::TextWidth(note, noteScale) > c.w - 32 * s) --noteScale;
        const int nw = Canvas::TextWidth(note, noteScale) + 16 * noteScale;
        const int nh = 12 * noteScale;
        const int nx = (c.w - nw) / 2, ny = c.h - dockH + (dockH - nh) / 2;
        c.FillRound(nx, ny, nw, nh, 3 * noteScale, 0xEE1E2638);
        c.OutlineRound(nx, ny, nw, nh, 3 * noteScale, 1, th.frame);
        c.Text(nx + 8 * noteScale, ny + 2 * noteScale, note, noteScale, th.title);
    }
    else
    {
        struct DockItem { const char* badge; const char* label; };
        static const DockItem items[] = {
            { "A", "PLAY" }, { "Y", "SETTINGS" }, { "X", "RANDOM" }, { "L", "TABS" }, { "+", "QUIT" }
        };
        const int itemCount = 5;
        const int bh = 11 * s;
        int totalW = 0;
        int widths[itemCount];
        for (int i = 0; i < itemCount; i++)
        {
            widths[i] = bh + Canvas::TextWidth(items[i].label, s) + 5 * s;
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
                c.DrawButtonBadge(dx, by, items[i].badge, s, 0xFF222B3D, 0xFFFFFFFF, 0xFF3D4B66);
                c.Text(dx + bh + 4 * s, by + 2 * s, items[i].label, s, th.hint);
                dx += widths[i] + itemGap;
            }
        }
        else
        {
            const std::string hint = !hintText.empty() ? hintText : "A: PLAY   Y: SETTINGS   X: RANDOM   L / R: TABS   +: QUIT";
            int hintScale = s;
            while (hintScale > 1 && Canvas::TextWidth(hint, hintScale) > c.w - 16 * s) --hintScale;
            c.Text((c.w - Canvas::TextWidth(hint, hintScale)) / 2, c.h - 11 * s, hint, hintScale, th.hint);
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
    l.push_back({ "wide_map", "WIDE MAP LOADING", onOff, "1", "BLACK / WHITE / BLACK 2 / WHITE 2: LOADS MORE OF THE MAP AROUND YOU SO THE WIDE VIEW HAS LAND AT ITS EDGES." });
    l.push_back({ "battle_buttons", "BATTLE BUTTONS", { { "corner", "CORNER" }, { "panel", "PANEL" }, { "strip", "STRIP" } }, "strip",
                  "WHERE THE BATTLE MENU GOES. CORNER: SMALL BUTTONS IN A CORNER. PANEL: THE FIGHT / BAG / POKEMON / RUN PANEL FROM THE GAME BESIDE THE BATTLE. "
                  "STRIP: LARGE BUTTONS IN THE MESSAGE BOX IN BLACK AND WHITE (CORNER IN THE OTHERS). SOULSILVER / HEARTGOLD / PLATINUM HAVE THEIR OWN." });
#ifdef __SWITCH__
    l.push_back({ "wide_lines", "WORLD DETAIL", { { "192", "192" }, { "288", "288" }, { "384", "384" }, { "540", "540" }, { "720", "720" } }, "",
                  "HOW MANY LINES THE WIDE 3D WORLD IS DRAWN WITH. 720 IS FULL SHARPNESS ON THE SWITCH. LOWER IS LIGHTER ON THE CONSOLE." });
    l.push_back({ "busy_detail", "LOWER DETAIL WHEN BUSY", onOff, "0", "ON: DROPS THE WORLD DETAIL FOR A MOMENT WHEN DRAWING CANNOT KEEP UP (A MOVE FULL OF EFFECTS) INSTEAD OF SLOWING THE GAME DOWN." });
#else
#ifdef SSR_PHONE
    l.push_back({ "wide_lines", "3D RESOLUTION", { { "192", "192P (DS)" }, { "288", "288P" }, { "384", "384P" }, { "540", "540P" }, { "720", "720P" }, { "1080", "1080P" } }, "720",
                  "HOW SHARP THE 3D WORLD IS. 720P SUITS MOST PHONES. LOWER IF YOURS CANNOT KEEP UP OR TO SAVE BATTERY. SPRITES AND TEXT KEEP THEIR DS PIXELS." });
    l.push_back({ "busy_detail", "LOWER RES WHEN BUSY", onOff, "0", "ON: DROPS THE 3D RESOLUTION FOR A MOMENT WHEN THE PHONE CANNOT KEEP UP (A MOVE FULL OF EFFECTS) INSTEAD OF SLOWING THE GAME DOWN." });
#else
    l.push_back({ "wide_lines", "3D RESOLUTION", { { "192", "192P (DS)" }, { "288", "288P" }, { "384", "384P" }, { "540", "540P" }, { "720", "720P" }, { "1080", "1080P" }, { "1440", "1440P (2K)" }, { "2160", "2160P (4K)" } },
#ifdef __APPLE__
                  "720", // (drawn by the processor on a Mac)
#else
                  "1080",
#endif

                  "HOW SHARP THE 3D WORLD IS. PICK YOUR SCREEN RESOLUTION OR LOWER IF YOUR PC CANNOT KEEP UP. SPRITES AND TEXT KEEP THEIR DS PIXELS." });
    l.push_back({ "busy_detail", "LOWER RES WHEN BUSY", onOff, "0", "ON: DROPS THE 3D RESOLUTION FOR A MOMENT WHEN YOUR PC CANNOT KEEP UP (A MOVE FULL OF EFFECTS) INSTEAD OF SLOWING THE GAME DOWN." });
    l.push_back({ "ff_low_res", "FAST FORWARD LOW RES", onOff, "0", "ON: THE 3D WORLD DROPS TO 360P WHILE YOU FAST FORWARD FOR MORE FPS (A FASTER FAST FORWARD IF YOUR PC CANNOT KEEP UP). OFF: YOUR 3D RESOLUTION STAYS." });
#ifndef __APPLE__ // (a Mac's OpenGL stops at 4.1: the processor draws the world)
    l.push_back({ "wide_gpu", "WORLD ON GPU", onOff, "1", "ON: THE WIDE 3D WORLD IS DRAWN BY YOUR GRAPHICS CARD (OPENGL 4.3). TURN IT OFF IF THE WORLD LOOKS WRONG OR THE GAME WILL NOT START: THE PROCESSOR THEN DRAWS IT (THAT WANTS A FAST PC)." });
#endif
#endif
#endif
    l.push_back({ "show_fps", "FPS COUNTER", onOff, "0", "SHOWS THE FRAMES A SECOND IN A CORNER." });
    l.push_back({ "volume", "VOLUME", { { "0", "OFF" }, { "10", "10%" }, { "20", "20%" }, { "30", "30%" }, { "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } }, "100",
                  "THE GAME'S SOUND AND MUSIC. OFF FOR NONE. ALSO IN THE PAUSE MENU WHILE YOU PLAY." });
#ifdef __SWITCH__
    l.push_back({ "start_button", "START BUTTON", { { "menu", "PAUSE MENU" }, { "ds", "DS START" } }, "menu",
                  "PAUSE MENU: + OPENS THE PAUSE MENU WHILE PLAYING. DS START: + IS THE DS START BUTTON (THE GAMES HARDLY USE IT) AND + AND - TOGETHER OPEN THE MENU." });
#elif defined(SSR_PHONE)
    l.push_back({ "start_button", "START BUTTON", { { "menu", "PAUSE MENU" }, { "ds", "DS START" } }, "menu",
                  "PAUSE MENU: START ON A CONTROLLER OPENS THE PAUSE MENU WHILE PLAYING. DS START: IT IS THE DS START BUTTON (THE GAMES HARDLY USE IT). THE START ON THE SCREEN IS ALWAYS THE DS START." });
#else
    l.push_back({ "start_button", "START BUTTON", { { "menu", "PAUSE MENU" }, { "ds", "DS START" } }, "ds",
                  "DS START: START ON A CONTROLLER IS THE DS START; THE XBOX / PS BUTTON, OR START AND BACK TOGETHER, OPEN THE PAUSE MENU. PAUSE MENU: START (MENU ON XBOX / OPTIONS ON PS) OPENS IT. ENTER IS ALWAYS THE DS START." });
#endif
    l.push_back({ "pad_touch", "STICK TOUCH CURSOR", onOff, "1",
                  "ON: THE RIGHT STICK MOVES A CURSOR ON THE TOUCH SCREEN AND THE LEFT TRIGGER PRESSES IT (RECOMMENDED FOR TV / DOCKED MODE)." });
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
    // Only in the pause menu.
#ifdef __SWITCH__
    l.push_back({ nullptr, "THE PAUSE MENU", {}, "", "PRESS + WHILE PLAYING (+ AND - TOGETHER WITH START BUTTON ON DS START). EVERYTHING ABOVE IS THERE TOO AND CAN BE CHANGED AS YOU PLAY." });
    l.push_back({ nullptr, "FAST FORWARD", {}, "", "HOLD ZR (UP TO 4X). FAST FORWARD IN THE PAUSE MENU KEEPS IT ON WITHOUT HOLDING. THE BUTTON IS CHANGED IN KEYS AND BUTTONS." });
#elif defined(SSR_PHONE)
    l.push_back({ nullptr, "THE PAUSE MENU", {}, "", "THE MENU BUTTON AT THE TOP OF THE SCREEN WHILE PLAYING OR START OR GUIDE ON A CONTROLLER. EVERYTHING ABOVE IS THERE TOO." });
    l.push_back({ nullptr, "FAST FORWARD", {}, "", "HOLD THE >> BUTTON BESIDE R AT THE TOP, OR THE RIGHT TRIGGER OF A CONTROLLER (UP TO 4X). FAST FORWARD IN THE PAUSE MENU KEEPS IT ON WITHOUT HOLDING. THE BUTTON IS CHANGED IN KEYS AND BUTTONS." });
#else
    l.push_back({ nullptr, "THE PAUSE MENU", {}, "", "ESC ON THE KEYBOARD. ON A CONTROLLER: START (MENU ON XBOX / OPTIONS ON PS) OR ITS GUIDE BUTTON. EVERYTHING ABOVE IS THERE TOO." });
    l.push_back({ nullptr, "FAST FORWARD", {}, "", "HOLD THE RIGHT TRIGGER OR LEFT SHIFT (UP TO 4X). FAST FORWARD IN THE PAUSE MENU KEEPS IT ON WITHOUT HOLDING. THE KEY AND BUTTON ARE CHANGED IN KEYS AND BUTTONS." });
#endif
#ifdef __SWITCH__
    l.push_back({ "!keys", "KEYS AND BUTTONS", { { "", "CHANGE" } }, "",
                  "GIVE ANY DS BUTTON / THE PAUSE MENU / FAST FORWARD / SWAP SCREENS / SAVE STATE / LOAD STATE ANOTHER BUTTON (ANY CONTROLLER: PRO / JOY-CON OR ONE ADDED WITH SYS-CON OR MISSIONCONTROL). GIVING ONE A BUTTON ANOTHER ONE HAD SWAPS THE TWO. SAVE STATE AND LOAD STATE START NOT SET. ALSO IN THE PAUSE MENU (CONTROLS). THIS LIST FOLLOWS THE ONES YOU CHANGE." });
#else
    l.push_back({ "!keys", "KEYS AND BUTTONS", { { "", "CHANGE" } }, "",
                  "GIVE ANY DS BUTTON / THE PAUSE MENU / FAST FORWARD / SWAP SCREENS / SAVE STATE / LOAD STATE ANOTHER KEY OR CONTROLLER BUTTON. GIVING ONE A KEY OR BUTTON ANOTHER ONE HAD SWAPS THE TWO. SAVE STATE STARTS AS F5 AND LOAD STATE AS F8 (NOT SET ON A CONTROLLER). ALSO IN THE PAUSE MENU (CONTROLS). THIS LIST FOLLOWS THE ONES YOU CHANGE (A PLAYS / B GOES BACK / Y OPENS THESE SETTINGS)." });
#endif
#ifdef __SWITCH__
    l.push_back({ nullptr, "SAVE STATE / LOAD STATE", {}, "", "ONE QUICK SLOT PER GAME, FROM THE PAUSE MENU. CHOOSE A BUTTON FOR EACH IN KEYS AND BUTTONS (THEY START NOT SET). YOUR SAVE IN THE GAME IS SEPARATE AND STILL WORKS AS NORMAL." });
#else
    l.push_back({ nullptr, "SAVE STATE / LOAD STATE", {}, "", "ONE QUICK SLOT PER GAME. F5 SAVES AND F8 LOADS UNTIL YOU CHANGE THEM IN KEYS AND BUTTONS. A CONTROLLER BUTTON IS NOT SET UNTIL YOU CHOOSE ONE. YOUR SAVE IN THE GAME IS SEPARATE AND STILL WORKS AS NORMAL." });
#endif
    l.push_back({ nullptr, "RESET GAME / QUIT", {}, "", "IN THE PAUSE MENU: START THE GAME AGAIN FROM ITS TITLE SCREEN / BACK TO THIS GAME LIST." });
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
            for (const Setting& s : all)
                if (GroupOf(s) == g) names += (names.empty() ? "" : " / ") + std::string(s.name);
            groupText[g] = names + ".";
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
        if (th.background)
            if (const uint32_t* bgPx = th.background->Fitted(c.w, c.h)) std::copy(bgPx, bgPx + (size_t)c.w * c.h, c.px.begin());
        c.FillGradV(0, 0, c.w, bandH, 0xFF191F2D, th.band, 0);
        c.Fill(0, bandH, c.w, th.bandLineH * s, th.bandLine);
        const std::string title = group < 0 ? "SETTINGS" : std::string("SETTINGS: ") + UiLang::Tr(SettingsGroups()[group].name);
        int titleScale = s + 1;
        while (titleScale > 1 && Canvas::TextWidth(title, titleScale) > c.w - 8) --titleScale;
        c.Text((c.w - Canvas::TextWidth(title, titleScale)) / 2, 6 * s, title, titleScale, th.title);
        const int width = std::min(c.w - 24 * s, 100 * 6 * ts);
        const int x = (c.w - width) / 2;
        struct Line { std::string text, value; int item; bool name; };
        std::vector<Line> out;
        int selFirst = 0, selLast = 0;
        bool infoHeading = false;
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
            const std::string drawnValue = arrows ? "< " + UiLang::Tr(value) + " >" : UiLang::Tr(value);
            const bool stacked = Canvas::TextWidth(e.name, ts) + Canvas::TextWidth(drawnValue, ts) + 8 * ts > width;
            out.push_back({ e.name, stacked ? "" : value, i, true });
            if (stacked && !value.empty())
                for (const std::string& l : Wrap(UiLang::Tr(value), width / (6 * ts) - 2)) out.push_back({ l, "", i, false });
            for (const std::string& l : Wrap(UiLang::Tr(e.text), width / (6 * ts) - 2)) out.push_back({ l, "", i, false });
            if (i == selected) selLast = (int)out.size() - 1;
            out.push_back({ "", "", -1, false });
        }
        const int top = (c.h > c.w ? 48 : 30) * s, bottom = c.h - 18 * s;
        lineH = (UiLang::Tall() ? 16 : 10) * ts;
        const int fit = std::max(1, (bottom - top) / lineH);
        if (follow)
        {
            if (selFirst < scroll) scroll = selFirst;
            if (selLast >= scroll + fit) scroll = selLast - fit + 1;
        }
        scroll = std::max(0, std::min(scroll, (int)out.size() - fit));
        rows.clear();

        // Draw elevated frosted glass cards behind each visible item group before drawing text.
        const int cardRad = std::max(3, 4 * ts);
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
                c.GlowRound(x - 6 * ts, cardY, width + 12 * ts, cardH, cardRad, 3 * ts, th.frame);
                c.FillGlass(x - 6 * ts, cardY, width + 12 * ts, cardH, cardRad, 0xFF242E44, 0xFF192030, th.frame, 1);
                c.FillRound(x - 4 * ts, cardY + 2 * ts, std::max(2, 2 * ts), cardH - 4 * ts, ts, th.frame);
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
                c.Text(x, y, l.text, ts, info ? th.hint : sel ? th.nameSel : th.subSel);
                if (!l.value.empty())
                {
                    const bool group = !strncmp(items[l.item].key, "!group", 6);
                    const std::string v = sel && !group ? "< " + UiLang::Tr(l.value) + " >" : l.value;
                    c.Text(x + width - Canvas::TextWidth(v, ts), y, v, ts, sel ? th.nameSel : th.name);
                }
            }
            else if (!l.text.empty())
            {
                c.TextRaw(x + 2 * 6 * ts, y, l.text, ts, sel ? th.name : th.sub);
            }
        }
        if (scroll > 0) c.FillRound(c.w / 2 - 10 * s, top - 4 * s, 20 * s, s + 1, std::max(1, s / 2), th.frame);
        if (scroll + fit < (int)out.size()) c.FillRound(c.w / 2 - 10 * s, bottom + s, 20 * s, s + 1, std::max(1, s / 2), th.frame);
        const int dockH = 18 * s;
        c.FillGlass(0, c.h - dockH, c.w, dockH, 0, 0xFA141926, 0xFA0B0E16, 0xFF28344C, 1);

        struct DockItem { const char* badge; const char* label; };
        static const DockItem items[] = {
            { "A", "CHANGE" }, { "B", "BACK" }
        };
        const int itemCount = 2;
        const int bh = 11 * s;
        int totalW = 0;
        int widths[itemCount];
        for (int i = 0; i < itemCount; i++)
        {
            widths[i] = bh + Canvas::TextWidth(items[i].label, s) + 5 * s;
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
                c.DrawButtonBadge(dx, by, items[i].badge, s, 0xFF222B3D, 0xFFFFFFFF, 0xFF3D4B66);
                c.Text(dx + bh + 4 * s, by + 2 * s, items[i].label, s, th.hint);
                dx += widths[i] + itemGap;
            }
        }
        else
        {
            int hintScale = s;
            while (hintScale > 1 && Canvas::TextWidth(hint, hintScale) > c.w - 16 * s) --hintScale;
            c.Text((c.w - Canvas::TextWidth(hint, hintScale)) / 2, c.h - 11 * s, hint, hintScale, th.pokemon ? 0xFFDBE9FAu : th.hint);
        }
    }
};

}
