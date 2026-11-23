// The games: finding the player's ROMs, telling which game each one is, and putting the
// widescreen patch on it as it starts. Nothing of the games comes with the program: it holds
// only this project's patches (our own code, tools/hookpatch.py) and the checks that make sure
// a ROM has what they need.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include <SDL.h>
#include <mgba-util/crc32.h>
#include <mgba-util/sha1.h>

#include "patches.h" // made at build time (tools/embed_patches.py): kWidePatches
// ROM hacks as UPS, BPS and xdelta patches too (Fulldex's romhacks.h, when this is built into it);
// on its own, IPS patches only.
#if __has_include("romhacks.h")
#include "romhacks.h"
#define GBAW_ROMHACKS 1
#endif

namespace gbaw
{

namespace Games
{

inline std::string Lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
inline bool EndsWith(const std::string& s, const char* end)
{
    const size_t n = strlen(end);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == end;
}
inline bool Exists(const std::string& path)
{
    if (SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb"))
    {
        SDL_RWclose(f);
        return true;
    }
    return false;
}
inline void MakeDir(const std::string& path)
{
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}
// (Through SDL, which takes UTF-8 names on every platform.)
inline bool ReadFile(const std::string& path, std::vector<uint8_t>& out, size_t limit = 64 << 20)
{
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) return false;
    const Sint64 size = SDL_RWsize(f);
    bool ok = size >= 0 && (size_t)size <= limit;
    if (ok)
    {
        out.resize((size_t)size);
        ok = size == 0 || SDL_RWread(f, out.data(), 1, (size_t)size) == (size_t)size;
    }
    SDL_RWclose(f);
    return ok;
}
inline bool WriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = data.empty() || SDL_RWwrite(f, data.data(), 1, data.size()) == data.size();
    return SDL_RWclose(f) == 0 && ok;
}
inline std::string Sha1(const std::vector<uint8_t>& data)
{
    uint8_t d[20];
    sha1Buffer(data.data(), data.size(), d);
    char hex[41];
    for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    return hex;
}

// An IPS patch onto data (strictly: false if it is cut short or not an IPS patch).
// IPS32: the same with 4-byte offsets ("IPS32" ... "EEOF"), for patches placed past 16 MB in a
// 32 MB hack that has no free space below (Inclement Emerald, Monomyth).
inline bool ApplyIps32(std::vector<uint8_t>& rom, const uint8_t* p, size_t n)
{
    size_t i = 5;
    while (i + 4 <= n)
    {
        if (!memcmp(p + i, "EEOF", 4) && i + 4 == n) return true;
        const size_t off = (size_t)p[i] << 24 | (size_t)p[i + 1] << 16 | (size_t)p[i + 2] << 8 | p[i + 3];
        i += 4;
        if (i + 2 > n) return false;
        size_t len = (size_t)(p[i] << 8 | p[i + 1]);
        i += 2;
        if (len)
        {
            if (i + len > n || off + len > (64u << 20)) return false;
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memcpy(rom.data() + off, p + i, len);
            i += len;
        }
        else
        {
            if (i + 3 > n) return false;
            len = (size_t)(p[i] << 8 | p[i + 1]);
            if (off + len > (64u << 20)) return false;
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memset(rom.data() + off, p[i + 2], len);
            i += 3;
        }
    }
    return false;
}
inline bool ApplyIps(std::vector<uint8_t>& rom, const uint8_t* p, size_t n)
{
    if (n >= 9 && !memcmp(p, "IPS32", 5)) return ApplyIps32(rom, p, n);
    if (n < 8 || memcmp(p, "PATCH", 5)) return false;
    size_t i = 5;
    while (i + 3 <= n)
    {
        if (!memcmp(p + i, "EOF", 3) && (i + 3 == n || i + 6 == n))
        {
            if (i + 6 == n) rom.resize((size_t)(p[i + 3] << 16 | p[i + 4] << 8 | p[i + 5])); // (Lunar IPS: cut to this size)
            return true;
        }
        const size_t off = (size_t)(p[i] << 16 | p[i + 1] << 8 | p[i + 2]);
        i += 3;
        if (i + 2 > n) return false;
        size_t len = (size_t)(p[i] << 8 | p[i + 1]);
        i += 2;
        if (len)
        {
            if (i + len > n) return false;
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memcpy(rom.data() + off, p + i, len);
            i += len;
        }
        else
        {
            if (i + 3 > n) return false;
            len = (size_t)(p[i] << 8 | p[i + 1]);
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memset(rom.data() + off, p[i + 2], len);
            i += 3;
        }
    }
    return false;
}

// A hack's patch (IPS, or UPS, BPS and xdelta with romhacks.h) onto the ROM it is for.
inline bool ApplyPatch(std::vector<uint8_t>& rom, const std::vector<uint8_t>& p, std::string* error = nullptr)
{
#ifdef GBAW_ROMHACKS
    if (RomHacks::FormatOf(p.data(), p.size()) != RomHacks::Format::Ips)
    {
        RomHacks::MemReader r(rom);
        RomHacks::MemWriter w;
        std::string e;
        if (!RomHacks::Apply(p, r, w, e))
        {
            if (error) *error = e;
            return false;
        }
        rom.swap(w.data);
        return true;
    }
#endif
    if (!ApplyIps(rom, p.data(), p.size()))
    {
        if (error) *error = "THE IPS PATCH IS DAMAGED";
        return false;
    }
    return true;
}
inline bool FormatIsIps(const std::vector<uint8_t>& p) { return p.size() >= 5 && (!memcmp(p.data(), "PATCH", 5) || !memcmp(p.data(), "IPS32", 5)); }
inline bool IsPatchFile(const std::string& n)
{
#ifdef GBAW_ROMHACKS
    return RomHacks::IsPatchName(n);
#else
    return EndsWith(n, ".ips");
#endif
}

// Whether a ROM has everything a widescreen patch needs (its .safety list): the functions it
// replaces and the tables it reads exactly as in the original game, the functions it calls
// still there, and its space free. If not, `why` says what is in the way.
inline bool PatchFits(const std::vector<uint8_t>& rom, const WidePatch& patch, std::string& why)
{
    const char* s = patch.safety;
    while (*s)
    {
        const char* end = strchr(s, '\n');
        const std::string line(s, end ? end - s : strlen(s));
        s = end ? end + 1 : s + line.size();
        char kind[8] = {}, name[96] = {};
        unsigned off = 0, size = 0, crc = 0;
        const int got = sscanf(line.c_str(), "%7s %x %x %x %95s", kind, &off, &size, &crc, name);
        if (got < 3) continue;
        if (!strcmp(kind, "free"))
        {
            // (Past the end of a smaller ROM (Japanese Ruby and Sapphire are 8 MB) is free too:
            // the patch makes the ROM longer.)
            for (unsigned i = 0; i < size && off + i < rom.size(); i++)
                if (rom[off + i] != 0xFF)
                {
                    why = "ITS FREE SPACE IS IN USE";
                    return false;
                }
        }
        else if ((size_t)off + size > rom.size())
        {
            why = "TOO SMALL";
            return false;
        }
        else if (got >= 4 && doCrc32(rom.data() + off, size) != crc)
        {
            why = std::string("IT CHANGES ") + name;
            return false;
        }
    }
    return true;
}

// Why a hack can't take its game's widescreen patch even when tried anyway, or "" if it can be
// tried: the patch's code calls the game's own functions where they are in the original (its
// .safety list's call lines), and is written into space left free (its free lines). A hack
// that moved those functions (one rebuilt from the decompilation, as Emerald Legacy is) or put
// something of its own in that space would have its code or data overwritten, or the patch
// would call into the middle of something else: Emerald Legacy froze after the professor's
// introduction, its sound stuck (#163). A hack that only changed functions the patch replaces
// (its same lines) is still tried.
inline std::string PatchCantTry(const std::vector<uint8_t>& rom, const WidePatch& patch)
{
    const char* s = patch.safety;
    while (*s)
    {
        const char* end = strchr(s, '\n');
        const std::string line(s, end ? end - s : strlen(s));
        s = end ? end + 1 : s + line.size();
        char kind[8] = {};
        unsigned off = 0, size = 0, crc = 0;
        const int got = sscanf(line.c_str(), "%7s %x %x %x", kind, &off, &size, &crc);
        if (got < 3) continue;
        if (!strcmp(kind, "free"))
        {
            for (unsigned i = 0; i < size && off + i < rom.size(); i++)
                if (rom[off + i] != 0xFF) return "ITS FREE SPACE IS IN USE";
        }
        else if (!strcmp(kind, "call") && got >= 4 &&
                 ((size_t)off + size > rom.size() || doCrc32(rom.data() + off, size) != crc))
            return "ITS CODE IS ARRANGED DIFFERENTLY";
    }
    return "";
}

// The games this program knows by their exact ROM.
struct Known
{
    const char* sha1;
    const char* key;  // the save's name, and what --game takes
    const char* name; // as the list shows it
    const char* patch;
    uint32_t colour;
    const char* art; // its picture, art/<art>.jpg, "" for none yet
};
inline const std::vector<Known>& KnownGames()
{
    static const std::vector<Known> k = {
        { "f3ae088181bf583e55daf962a92bb46f4f1d07b7", "emerald", "POKEMON EMERALD", "emerald", 0xFF1E8C55, "emerald" },
        { "3b6c51d8fb6b513bdd01b7bda3ed8965bc985d58", "ultraviolet", "POKEMON ULTRA VIOLET", "firered", 0xFF6A3FC0, "ultraviolet" },
        // (FireRed 1.0 and 1.1 share a save, as do LeafGreen's: the same game either way.)
        { "41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc", "firered", "POKEMON FIRERED", "firered", 0xFFC8382A, "firered" },
        { "dd5945db9b930750cb39d00c84da8571feebf417", "firered", "POKEMON FIRERED", "firered11", 0xFFC8382A, "firered" },
        { "574fa542ffebb14be69902d1d36f1ec0a4afd71e", "leafgreen", "POKEMON LEAFGREEN", "leafgreen", 0xFF4FA83A, "leafgreen" },
        { "7862c67bdecbe21d1d69ce082ce34327e1c6ed5e", "leafgreen", "POKEMON LEAFGREEN", "leafgreen11", 0xFF4FA83A, "leafgreen" },
        // (English Ruby and Sapphire, US and Europe: versions 1.2, 1.1 and 1.0 share a save.)
        { "5b64eacf892920518db4ec664e62a086dd5f5bc8", "ruby", "POKEMON RUBY", "ruby", 0xFFB8202E, "ruby" },
        { "89b45fb172e6b55d51fc0e61989775187f6fe63c", "sapphire", "POKEMON SAPPHIRE", "sapphire", 0xFF2452B0, "sapphire" },
        { "610b96a9c9a7d03d2bafb655e7560ccff1a6d894", "ruby", "POKEMON RUBY", "ruby11", 0xFFB8202E, "ruby" },
        { "4722efb8cd45772ca32555b98fd3b9719f8e60a9", "sapphire", "POKEMON SAPPHIRE", "sapphire11", 0xFF2452B0, "sapphire" },
        { "f28b6ffc97847e94a6c21a63cacf633ee5c8df1e", "ruby", "POKEMON RUBY", "ruby10", 0xFFB8202E, "ruby" },
        { "3ccbbd45f8553c36463f13b938e833f652b793e4", "sapphire", "POKEMON SAPPHIRE", "sapphire10", 0xFF2452B0, "sapphire" },
        // (German Ruby and Sapphire, version 1.1: their own saves, as a German save isn't an English one.)
        { "424740be1fc67a5ddb954794443646e6aeee2c1b", "rubyde", "POKEMON RUBIN", "rubyde", 0xFFB8202E, "ruby" },
        { "7e6e034f9cdca6d2c4a270fdb50a94def5883d17", "sapphirede", "POKEMON SAPHIR", "sapphirede", 0xFF2452B0, "sapphire" },
        // (French, Spanish, Italian and Japanese Ruby and Sapphire 1.1: their own saves too)
        { "ba888dfba231a231cbd60fe228e894b54fb1ed79", "rubyfr", "POKEMON RUBIS", "rubyfr", 0xFFB8202E, "ruby" },
        { "9ac73481d7f5d150a018309bba91d185ce99fb7c", "rubyes", "POKEMON RUBI", "rubyes", 0xFFB8202E, "ruby" },
        { "015a5d380afe316a2a6fcc561798ebff9dfb3009", "rubyit", "POKEMON RUBINO", "rubyit", 0xFFB8202E, "ruby" },
        { "971e0d670a95e5b32240b2deed20405b8daddf47", "rubyjp", "POCKET MONSTERS RUBY", "rubyjp", 0xFFB8202E, "ruby" },
        { "860e93f5ea44f4278132f6c1ee5650d07b852fd8", "sapphirefr", "POKEMON SAPHIR", "sapphirefr", 0xFF2452B0, "sapphire" },
        { "0fe9ad1e602e2fafa090aee25e43d6980625173c", "sapphirees", "POKEMON ZAFIRO", "sapphirees", 0xFF2452B0, "sapphire" },
        { "73edf67b9b82ff12795622dca412733755d2c0fe", "sapphireit", "POKEMON ZAFFIRO", "sapphireit", 0xFF2452B0, "sapphire" },
        { "01f509671445965236ac4c6b5a354fe2f1e69f13", "sapphirejp", "POCKET MONSTERS SAPPHIRE", "sapphirejp", 0xFF2452B0, "sapphire" },
        // (Emerald's, FireRed's and LeafGreen's other languages, #161: their own saves too)
        { "ca666651374d89ca439007bed54d839eb7bd14d0", "emeraldfr", "POKEMON EMERAUDE", "emeraldfr", 0xFF1E8C55, "emerald" },
        { "61c2eb2b380b1a75f0c94b767a2d4c26cd7ce4e3", "emeraldde", "POKEMON SMARAGD", "emeraldde", 0xFF1E8C55, "emerald" },
        { "fe1558a3dcb0360ab558969e09b690888b846dd9", "emeraldes", "POKEMON ESMERALDA", "emeraldes", 0xFF1E8C55, "emerald" },
        { "1692db322400c3141c5de2db38469913ceb1f4d4", "emeraldit", "POKEMON SMERALDO", "emeraldit", 0xFF1E8C55, "emerald" },
        { "d7cf8f156ba9c455d164e1ea780a6bf1945465c2", "emeraldjp", "POCKET MONSTERS EMERALD", "emeraldjp", 0xFF1E8C55, "emerald" },
        { "fc663907256f06a3a09e2d6b967bc9af4919f111", "fireredfr", "POKEMON ROUGE FEU", "fireredfr", 0xFFC8382A, "firered" },
        { "18a3758ceeef2c77b315144be2c3910d6f1f69fe", "fireredde", "POKEMON FEUERROT", "fireredde", 0xFFC8382A, "firered" },
        { "ab8f6bfe0ccdaf41188cd015c8c74c314d02296a", "fireredes", "POKEMON ROJO FUEGO", "fireredes", 0xFFC8382A, "firered" },
        { "66a9d415205321376b4318534c0dce5f69d28362", "fireredit", "POKEMON ROSSO FUOCO", "fireredit", 0xFFC8382A, "firered" },
        { "4b5758c14d0a07b70ef3ef0bd7fa5e7ce6978672", "leafgreenfr", "POKEMON VERT FEUILLE", "leafgreenfr", 0xFF4FA83A, "leafgreen" },
        { "0802d1fb185ee3ed48d9a22afb25e66424076dac", "leafgreende", "POKEMON BLATTGRUN", "leafgreende", 0xFF4FA83A, "leafgreen" },
        { "f9ebee5d228cb695f18ef2ced41630a09fa9eb05", "leafgreenes", "POKEMON VERDE HOJA", "leafgreenes", 0xFF4FA83A, "leafgreen" },
        { "a1dfea1493d26d1f024be8ba1de3d193fcfc651e", "leafgreenit", "POKEMON VERDE FOGLIA", "leafgreenit", 0xFF4FA83A, "leafgreen" },
    };
    return k;
}
// The art for a game the list knows (by its SHA-1), "" for none.
inline std::string ArtCode(const std::string& sha1)
{
    for (const Known& k : KnownGames())
        if (sha1 == k.sha1) return k.art;
    return "";
}
inline const WidePatch* FindPatch(const std::string& name)
{
    for (const WidePatch& p : kWidePatches)
        if (name == p.name) return &p;
    return nullptr;
}
// Which patch a ROM of this game code would take (a hack keeps its base game's code, or not:
// Ultra Violet is PUVV).
inline const char* PatchForCode(const std::string& code)
{
    if (code == "BPEE") return "emerald";
    if (code == "BPRE" || code == "PUVV") return "firered";
    if (code == "AXVE") return "ruby";
    if (code == "AXPE") return "sapphire";
    if (code == "AXVD") return "rubyde";
    if (code == "AXPD") return "sapphirede";
    if (code == "AXVF") return "rubyfr";
    if (code == "AXVS") return "rubyes";
    if (code == "AXVI") return "rubyit";
    if (code == "AXVJ") return "rubyjp";
    if (code == "AXPF") return "sapphirefr";
    if (code == "AXPS") return "sapphirees";
    if (code == "AXPI") return "sapphireit";
    if (code == "AXPJ") return "sapphirejp";
    if (code == "BPGE") return "leafgreen";
    // (Other languages, #161. Not Japanese FireRed and LeafGreen: their code is too different.)
    if (code == "BPEF") return "emeraldfr";
    if (code == "BPED") return "emeraldde";
    if (code == "BPES") return "emeraldes";
    if (code == "BPEI") return "emeraldit";
    if (code == "BPEJ") return "emeraldjp";
    if (code == "BPRF") return "fireredfr";
    if (code == "BPRD") return "fireredde";
    if (code == "BPRS") return "fireredes";
    if (code == "BPRI") return "fireredit";
    if (code == "BPGF") return "leafgreenfr";
    if (code == "BPGD") return "leafgreende";
    if (code == "BPGS") return "leafgreenes";
    if (code == "BPGI") return "leafgreenit";
    return nullptr;
}
// The same for a ROM of that code and version (header 0xBC): the patch for its own version where
// there is one (FireRed and LeafGreen 1.1, English Ruby and Sapphire 1.0 and 1.1).
inline const WidePatch* PatchForVersion(const std::string& code, int version)
{
    const char* base = PatchForCode(code);
    if (!base) return nullptr;
    std::string name = base;
    if ((code == "BPRE" || code == "BPGE") && version == 1) name += "11";
    else if ((code == "AXVE" || code == "AXPE") && version == 1) name += "11";
    else if ((code == "AXVE" || code == "AXPE") && version == 0) name += "10";
    return FindPatch(name);
}

struct Game
{
    std::string rom;       // the ROM's path
    std::string ips;       // a hack's patch put on it first (IPS, UPS, BPS or xdelta: Ultra Violet's .ips on FireRed), or ""
    std::string fileName;  // as shown under the name
    std::string key;       // its save is saves/<key>.sav
    std::string name;
    std::string sha1;      // of the ROM as played (after `ips`)
    std::string patch;     // the widescreen patch it takes, "" for none
    std::string why;       // why it has none
    uint32_t colour = 0xFF505A70;
    uint32_t crc = 0;      // CRC-32 of the ROM before any widescreen patch (for PokeDaisy WIDE)
    bool known = false;
    bool hasSave = false;
    // A hack whose widescreen patch doesn't fit (why says what is in the way): tried all the same,
    // unless the player chose the GBA's own shape (ASPECT RATIO 3:2 or 4:3).
    bool forced = false;
    bool isGb = false;     // Game Boy / Game Boy Color ROM (.gb / .gbc)
};

// A save the player put beside the ROM (or beside a hack's patch), with the ROM's name.
inline std::string SaveBeside(const Game& g)
{
    const std::string& f = g.ips.empty() ? g.rom : g.ips;
    return f.substr(0, f.rfind('.')) + ".sav";
}

inline std::string Header(const std::vector<uint8_t>& rom, size_t off, size_t len)
{
    std::string s;
    for (size_t i = off; i < off + len && i < rom.size() && rom[i]; i++) s += (char)rom[i];
    return s;
}
inline bool LooksLikeGba(const std::vector<uint8_t>& rom)
{
    return rom.size() >= 0x200 && rom.size() <= (32u << 20) && rom[0xB2] == 0x96;
}
inline bool LooksLikeGb(const std::vector<uint8_t>& rom)
{
    if (rom.size() < 0x8000 || rom.size() > (8u << 20)) return false;
    static const uint8_t kLogo[8] = { 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B };
    return memcmp(rom.data() + 0x104, kLogo, sizeof kLogo) == 0;
}
struct KnownGb
{
    uint32_t crc;
    const char* key;
    const char* name;
    uint32_t colour;
    bool hack;
};
inline const std::vector<KnownGb>& KnownGbGames()
{
    static const std::vector<KnownGb> k = {
        { 0x9F7FDD53u, "red", "POKEMON RED", 0xFFC8382Au, false },
        { 0xD6DA8A1Au, "blue", "POKEMON BLUE", 0xFF2B60C8u, false },
        { 0x7D527D62u, "yellow", "POKEMON YELLOW", 0xFFD9AE28u, false },
        { 0x964B7A10u, "yellowes", "POKEMON EDICION AMARILLA", 0xFFD9AE28u, false },
        { 0x7A01E45Au, "yellowde", "POKEMON GELBE EDITION", 0xFFD9AE28u, false },
        { 0xD03426E9u, "yellowfr", "POKEMON VERSION JAUNE", 0xFFD9AE28u, false },
        { 0x8B56FE33u, "yellowit", "POKEMON VERSIONE GIALLA", 0xFFD9AE28u, false },
        { 0x6BDE3C3Eu, "gold", "POKEMON GOLD", 0xFFD4A02Au, false },
        { 0x8AD48636u, "silver", "POKEMON SILVER", 0xFF8EA4BEu, false },
        { 0x3358E30Au, "crystal", "POKEMON CRYSTAL", 0xFF4CA4D8u, false },
        { 0xEE6F5188u, "crystal", "POKEMON CRYSTAL", 0xFF4CA4D8u, false },
        { 0xBB6DD80Cu, "crystal", "POKEMON CRYSTAL", 0xFF4CA4D8u, false },
        { 0x616D85DEu, "crystalde", "POKEMON KRISTALL", 0xFF4CA4D8u, false },
        { 0xE7D07A3Fu, "yellowlegacy", "POKEMON YELLOW LEGACY", 0xFFD9AE28u, true },
        { 0x12E97FB4u, "crystallegacy", "POKEMON CRYSTAL LEGACY", 0xFF4CA4D8u, true },
        { 0x17617CDEu, "crystalclear", "POKEMON CRYSTAL CLEAR", 0xFF3E94C8u, true },
        { 0x58F40DE1u, "crystalclear", "POKEMON CRYSTAL CLEAR", 0xFF3E94C8u, true },
        { 0x5AF9A949u, "crystalclear", "POKEMON CRYSTAL CLEAR", 0xFF3E94C8u, true },
        { 0xFC33D126u, "crystalclear", "POKEMON CRYSTAL CLEAR", 0xFF3E94C8u, true },
    };
    return k;
}
inline std::string KeyFromFile(const std::string& file)
{
    std::string k;
    for (char c : Lower(file.substr(0, file.rfind('.'))))
        k += isalnum((unsigned char)c) ? c : '-';
    return k.empty() ? "game" : k;
}

inline void IdentifyGb(Game& g, const std::vector<uint8_t>& rom)
{
    g.isGb = true;
    g.sha1 = Sha1(rom);
    g.crc = doCrc32(rom.data(), rom.size());
    g.patch.clear();
    g.forced = false;
    g.why.clear();
    for (const KnownGb& k : KnownGbGames())
    {
        if (g.crc == k.crc)
        {
            g.known = !k.hack;
            g.key = k.key;
            g.name = k.name;
            g.colour = k.colour;
            return;
        }
    }
    g.known = false;
    g.key = KeyFromFile(g.fileName);
    g.name = g.fileName.substr(0, g.fileName.rfind('.'));
    for (char& c : g.name) c = c == '_' ? ' ' : (char)toupper((unsigned char)c);
    g.colour = EndsWith(g.fileName, ".gbc") ? 0xFF4C8EC4u : 0xFF4C9A6Eu;
}

inline uint32_t PatchColour(const char* name)
{
    if (!name) return 0xFF505A70;
    if (!strncmp(name, "emerald", 7) || !strcmp(name, "inclement") || !strcmp(name, "imperium") || !strncmp(name, "monomyth", 8)) return 0xFF2A6E50;
    if (!strncmp(name, "leafgreen", 9)) return 0xFF3E7A34;
    if (!strncmp(name, "sapphire", 8)) return 0xFF2A4A8A;
    return 0xFF8A3A30;
}

inline void PatchStats(const std::vector<uint8_t>& rom, const WidePatch& patch, int& callCount, int& callMatch, int& hookMatch, int& sameMatch)
{
    callCount = 0;
    callMatch = 0;
    hookMatch = 0;
    sameMatch = 0;
    auto looksLikeThumbEntry = [&](size_t off) {
        if (off + 2 > rom.size()) return false;
        const uint8_t b0 = rom[off], b1 = rom[off + 1];
        const uint16_t insn = (uint16_t)b0 | ((uint16_t)b1 << 8);
        if ((insn & 0xFE00u) == 0xB400u) return true; // PUSH {..}
        if ((insn & 0xF800u) == 0x4800u) return true; // LDR rN, [PC, #..] (hook trampoline)
        if ((insn & 0xFF00u) == 0x4700u) return true; // BX / BLX
        if ((insn & 0xF000u) == 0xF000u) return true; // BL prefix
        if ((insn & 0xF800u) == 0xE000u) return true; // B unconditional
        if ((insn & 0xFF00u) == 0x4600u) return true; // MOV high/reg
        return false;
    };
    const char* s = patch.safety;
    while (*s)
    {
        const char* end = strchr(s, '\n');
        const std::string line(s, end ? end - s : strlen(s));
        s = end ? end + 1 : s + line.size();
        char kind[8] = {};
        unsigned off = 0, size = 0, crc = 0;
        const int got = sscanf(line.c_str(), "%7s %x %x %x", kind, &off, &size, &crc);
        if (got < 3) continue;
        if (!strcmp(kind, "call"))
        {
            callCount++;
            if (got >= 4 && (size_t)off + size <= rom.size())
            {
                if (doCrc32(rom.data() + off, size) == crc) callMatch++;
                else if (size == 2 && looksLikeThumbEntry(off)) hookMatch++;
            }
        }
        else if (!strcmp(kind, "same") && got >= 4 && (size_t)off + size <= rom.size() &&
                 doCrc32(rom.data() + off, size) == crc)
            sameMatch++;
    }
}

inline bool PatchFreeOk(const std::vector<uint8_t>& rom, const WidePatch& patch)
{
    const char* s = patch.safety;
    while (*s)
    {
        const char* end = strchr(s, '\n');
        const std::string line(s, end ? end - s : strlen(s));
        s = end ? end + 1 : s + line.size();
        char kind[8] = {};
        unsigned off = 0, size = 0;
        if (sscanf(line.c_str(), "%7s %x %x", kind, &off, &size) >= 3 && !strcmp(kind, "free"))
        {
            for (unsigned i = 0; i < size && off + i < rom.size(); i++)
                if (rom[off + i] != 0xFF) return false;
        }
    }
    return true;
}

inline bool SameFamily(const char* patchName, const char* codeBase)
{
    if (!patchName || !codeBase) return false;
    if (!strncmp(codeBase, "firered", 7))
        return !strncmp(patchName, "firered", 7) || !strcmp(patchName, "radicalred") || !strcmp(patchName, "unbound") || !strcmp(patchName, "gen3evolved");
    if (!strncmp(codeBase, "emerald", 7))
        return !strncmp(patchName, "emerald", 7) || !strcmp(patchName, "inclement");
    if (!strncmp(codeBase, "leafgreen", 9)) return !strncmp(patchName, "leafgreen", 9);
    if (!strncmp(codeBase, "ruby", 4)) return !strncmp(patchName, "ruby", 4);
    if (!strncmp(codeBase, "sapphire", 8)) return !strncmp(patchName, "sapphire", 8);
    return !strcmp(patchName, codeBase);
}

// What a ROM (as it will be played) is: one of ours, a hack of one (widescreen if the patch
// fits), or another game (played as it is).
inline void Identify(Game& g, const std::vector<uint8_t>& rom)
{
    g.sha1 = Sha1(rom);
    g.crc = doCrc32(rom.data(), rom.size());
    for (const Known& k : KnownGames())
        if (g.sha1 == k.sha1)
        {
            g.known = true;
            g.key = k.key;
            g.name = k.name;
            g.patch = k.patch;
            g.colour = k.colour;
            return;
        }
    g.key = KeyFromFile(g.fileName);
    // (Never one of the games' own saves: a hack named firered.ips is not FireRed.)
    for (const Known& k : KnownGames())
        if (g.key == k.key) g.key = "hack-" + g.key;
    g.name = g.fileName.substr(0, g.fileName.rfind('.'));
    for (char& c : g.name) c = c == '_' ? ' ' : (char)toupper((unsigned char)c);
    // A hack gets widescreen if it left alone everything a patch needs (whatever its code).
    std::string why;
    for (const WidePatch& p : kWidePatches)
        if (PatchFits(rom, p, why))
        {
            g.patch = p.name;
            g.colour = PatchColour(p.name);
            return;
        }
    const std::string code = Header(rom, 0xAC, 4);
    const char* codeBase = PatchForCode(code);
    const WidePatch* p = PatchForVersion(code, rom.size() > 0xBC ? rom[0xBC] : 0);
    if (p)
    {
        PatchFits(rom, *p, why);
        g.why = why;
        // A hack of one of the games (its code kept): widescreen tried anyway, its patch put on
        // over what the hack changed. (Not when the ROM is too small for it, nor when the hack
        // has moved what the patch needs: PatchCantTry. It plays as it is.)
        const std::string cant = why == "TOO SMALL" ? std::string() : PatchCantTry(rom, *p);
        if (!cant.empty()) g.why = cant;
        else if (why != "TOO SMALL")
        {
            g.patch = p->name;
            g.forced = true;
            g.colour = PatchColour(p->name);
            return;
        }
    }
    // Custom-code hacks or hacks that relocated/filled the vanilla free block can still safely run
    // any sibling widescreen patch whose call hooks and free space are intact (or at least 85%
    // of entry hooks matching with valid Thumb hook prologues and untouched free space).
    const WidePatch* best = nullptr;
    int bestScore = -1;
    for (int stage = 0; stage < 2 && !best; stage++)
    {
        for (const WidePatch& wp : kWidePatches)
        {
            int calls = 0, callMatch = 0, hookMatch = 0, same = 0;
            PatchStats(rom, wp, calls, callMatch, hookMatch, same);
            if (stage == 0)
            {
                if (calls < 10 || !PatchCantTry(rom, wp).empty()) continue;
            }
            else
            {
                if (calls < 20 || !PatchFreeOk(rom, wp)) continue;
                if (callMatch * 100 < calls * 85 || callMatch + hookMatch != calls) continue;
            }
            const int score = (codeBase && SameFamily(wp.name, codeBase) ? 10000 : 0) + (p == &wp ? 500 : 0) +
                              callMatch * 25 + same * 10 + calls;
            if (score > bestScore)
            {
                bestScore = score;
                best = &wp;
            }
        }
    }
    if (best)
    {
        PatchFits(rom, *best, why);
        g.why = why;
        g.patch = best->name;
        g.forced = true;
        g.colour = PatchColour(best->name);
        return;
    }
    if (!p)
        g.why = codeBase ? "NOT A VERSION WITH A WIDESCREEN PATCH" : "NOT ONE OF THE WIDESCREEN GAMES";
}

// What the ROMs in the folder were found to be, kept in a file (saves/rom-list.txt): telling a
// game needs the whole ROM read, which takes seconds from an SD card. A line per ROM (or .ips
// with its base): what it was keyed on (path, size and time of the files), then the Game's
// fields, separated by tabs. Only the lines used in the last look are kept.
struct ScanCache
{
    std::string path;
    std::vector<std::pair<std::string, Game>> entries;
    std::vector<bool> used;
    bool changed = false;

    static std::string Stamp(const std::string& file)
    {
        struct stat st{};
        if (stat(file.c_str(), &st) != 0) return "";
        return file + "|" + std::to_string((long long)st.st_size) + "|" + std::to_string((long long)st.st_mtime);
    }
    void Load(const std::string& p)
    {
        path = p;
        std::vector<uint8_t> data;
        if (path.empty() || !ReadFile(path, data, 4 << 20)) return;
        std::string text(data.begin(), data.end()), line;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            line = text.substr(pos, end - pos);
            pos = end + 1;
            std::vector<std::string> f;
            size_t a = 0;
            while (true)
            {
                const size_t t = line.find('\t', a);
                f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
                if (t == std::string::npos) break;
                a = t + 1;
            }
            // (An older list's lines: looked at again. Version 7 also stores isGb for GB/GBC ROMs.)
            if (f.size() != 15 || f[14] != "7") continue;
            Game g;
            g.rom = f[1];
            g.ips = f[2];
            g.fileName = f[3];
            g.key = f[4];
            g.name = f[5];
            g.sha1 = f[6];
            g.patch = f[7];
            g.why = f[8];
            g.colour = (uint32_t)strtoul(f[9].c_str(), nullptr, 16);
            g.known = f[10] == "1";
            g.forced = f[11] == "1";
            g.crc = (uint32_t)strtoul(f[12].c_str(), nullptr, 16);
            g.isGb = f[13] == "1";
            entries.push_back({ f[0], g });
            used.push_back(false);
        }
    }
    const Game* Find(const std::string& stamp)
    {
        if (stamp.empty()) return nullptr;
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].first == stamp)
            {
                used[i] = true;
                return &entries[i].second;
            }
        return nullptr;
    }
    void Put(const std::string& stamp, const Game& g)
    {
        if (stamp.empty()) return;
        entries.push_back({ stamp, g });
        used.push_back(true);
        changed = true;
    }
    void Save()
    {
        for (bool u : used) changed |= !u;
        if (!changed || path.empty()) return;
        std::string out;
        auto clean = [](std::string v) {
            for (char& c : v)
                if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            return v;
        };
        for (size_t i = 0; i < entries.size(); i++)
        {
            if (!used[i]) continue;
            const auto& e = entries[i];
            const Game& g = e.second;
            char colour[16], crcHex[16];
            snprintf(colour, sizeof colour, "%08X", (unsigned)g.colour);
            snprintf(crcHex, sizeof crcHex, "%08X", (unsigned)g.crc);
            out += clean(e.first) + "\t" + clean(g.rom) + "\t" + clean(g.ips) + "\t" + clean(g.fileName) + "\t" + clean(g.key) +
                   "\t" + clean(g.name) + "\t" + g.sha1 + "\t" + g.patch + "\t" + clean(g.why) + "\t" + colour + "\t" +
                   (g.known ? "1" : "0") + "\t" + (g.forced ? "1" : "0") + "\t" + crcHex + "\t" + (g.isGb ? "1" : "0") + "\t7\n";
        }
        WriteFile(path, std::vector<uint8_t>(out.begin(), out.end()));
        changed = false;
    }
};

// Every game in the roms folder: .gba, .gb and .gbc files, and hacks as patches (.ips; with romhacks.h also
// .ups, .bps, .xdelta) put on one of the games found here (Ultra Violet's .ips on FireRed 1.0).
inline std::vector<Game> Scan(const std::string& romsDir, const std::string& savesDir, const std::string& cacheFile = "")
{
    std::vector<std::string> gba, gbFiles, ips;
    if (DIR* d = opendir(romsDir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            const std::string n = e->d_name;
            if (n.empty() || n[0] == '.') continue;
            if (EndsWith(n, ".gba") || EndsWith(n, ".agb") || EndsWith(n, ".bin")) gba.push_back(n);
            else if (EndsWith(n, ".gb") || EndsWith(n, ".gbc")) gbFiles.push_back(n);
            else if (IsPatchFile(n)) ips.push_back(n);
        }
        closedir(d);
    }
    std::sort(gba.begin(), gba.end());
    std::sort(gbFiles.begin(), gbFiles.end());
    std::sort(ips.begin(), ips.end());
    std::vector<Game> games;
    std::vector<Game> bases;    // (known base ROMs tried for any patch including IPS)
    std::vector<Game> allBases; // (all valid GBA ROMs, also tried for CRC-checked UPS/BPS/xdelta patches)
    ScanCache cache;
    cache.Load(cacheFile);
    for (const std::string& n : gba)
    {
        Game g;
        g.rom = romsDir + "/" + n;
        g.fileName = n;
        const std::string stamp = ScanCache::Stamp(g.rom);
        if (const Game* c = cache.Find(stamp))
            g = *c;
        else
        {
            std::vector<uint8_t> rom;
            if (!ReadFile(g.rom, rom) || !LooksLikeGba(rom)) continue;
            Identify(g, rom);
            cache.Put(stamp, g);
        }
        if (g.ips.empty())
        {
            if (g.known) bases.push_back(g);
            allBases.push_back(g);
        }
        games.push_back(g);
    }
    for (const std::string& n : gbFiles)
    {
        Game g;
        g.rom = romsDir + "/" + n;
        g.fileName = n;
        const std::string stamp = ScanCache::Stamp(g.rom);
        if (const Game* c = cache.Find(stamp))
            g = *c;
        else
        {
            std::vector<uint8_t> rom;
            if (!ReadFile(g.rom, rom, 8 << 20) || !LooksLikeGb(rom)) continue;
            IdentifyGb(g, rom);
            cache.Put(stamp, g);
        }
        games.push_back(g);
    }
    // The likeliest first: the game its file name says (Pokemon Ultra Violet (FireRed).ips), then
    // FireRed 1.0 (most hacks are of it), Emerald, the rest, then other ROMs (for UPS/BPS/xdelta).
    auto order = [&](const std::string& patchName, bool includeAll) {
        std::vector<Game> b = bases;
        auto score = [&](const Game& g) {
            const std::string n = Lower(patchName);
            int s = 0;
            if (n.find(g.key.substr(0, std::min<size_t>(g.key.size(), 6))) != std::string::npos) s += 100;
            if (g.key == "firered" && (n.find("fr") != std::string::npos)) s += 50;
            if (g.key == "emerald" && (n.find("em") != std::string::npos)) s += 50;
            if (g.key == "leafgreen" && (n.find("lg") != std::string::npos)) s += 50;
            if (g.patch == "firered") s += 20;
            else if (g.key == "emerald") s += 10;
            return s;
        };
        std::stable_sort(b.begin(), b.end(), [&](const Game& x, const Game& y) { return score(x) > score(y); });
        if (includeAll)
        {
            for (const Game& extra : allBases)
                if (!extra.known) b.push_back(extra);
        }
        return b;
    };
    for (const std::string& n : ips)
    {
        // (A patch with each base: known from the list if neither file changed; "none" when it
        // doesn't go on that one.)
        const std::string patchPath = romsDir + "/" + n;
        const std::vector<Game> tryCache = order(n, true);
        bool done = false;
        for (const Game& base : tryCache)
        {
            const std::string stamp = ScanCache::Stamp(patchPath) + "+" + ScanCache::Stamp(base.rom);
            if (const Game* c = cache.Find(stamp))
            {
                if (c->key != "none")
                {
                    games.push_back(*c);
                    done = true;
                    break;
                }
            }
        }
        if (done) continue;
        std::vector<uint8_t> patch;
        if (!ReadFile(patchPath, patch, 32 << 20)) continue;
        Game firstGuess;
        std::string firstStamp;
        const bool isIps = FormatIsIps(patch);
#ifdef GBAW_ROMHACKS
        RomHacks::Info info;
        const bool infoOk = RomHacks::ReadInfo(patch, info);
        // (A DS ROM's patch: the DS list's.)
        if (!infoOk || (info.knowsSource && info.sourceSize > (32u << 20))) continue;
        const std::vector<Game> tryOn = order(n, !isIps);
#else
        const std::vector<Game> tryOn = order(n, false);
#endif
        for (const Game& base : tryOn)
        {
            const std::string stamp = ScanCache::Stamp(patchPath) + "+" + ScanCache::Stamp(base.rom);
            if (cache.Find(stamp)) continue; // ("none": tried)
            Game none;
            none.key = "none";
#ifdef GBAW_ROMHACKS
            if (info.knowsSource)
            {
                struct stat st{};
                if (stat(base.rom.c_str(), &st) == 0 && (size_t)st.st_size != info.sourceSize)
                {
                    cache.Put(stamp, none);
                    continue;
                }
            }
#endif
            std::vector<uint8_t> rom;
            if (!ReadFile(base.rom, rom)) continue;
#ifdef GBAW_ROMHACKS
            // (UPS and BPS say the CRC-32 of the ROM they go on.)
            if (info.knowsSource && (rom.size() != info.sourceSize || RomHacks::Crc32(0, rom.data(), rom.size()) != info.sourceCrc))
            {
                cache.Put(stamp, none);
                continue;
            }
#endif
            if (!ApplyPatch(rom, patch) || !LooksLikeGba(rom))
            {
                cache.Put(stamp, none);
                continue;
            }
            Game g;
            g.rom = base.rom;
            g.ips = patchPath;
            g.fileName = n;
            Identify(g, rom);
            // (An IPS patch says nothing of the ROM it is for, and goes on any: one that makes a
            // game known here (Ultra Violet) is the one; else the likeliest.)
            if (!g.known && isIps)
            {
                if (firstGuess.key.empty()) firstGuess = g, firstStamp = stamp;
                else cache.Put(stamp, none);
                continue;
            }
            cache.Put(stamp, g);
            games.push_back(g);
            firstGuess = Game();
            done = true;
            break;
        }
        if (!done && !firstGuess.key.empty())
        {
            cache.Put(firstStamp, firstGuess);
            games.push_back(firstGuess);
        }
    }
    cache.Save();
    // One of each: the same game twice (its .gba and its .ips) is listed once.
    std::vector<Game> out;
    for (const Game& g : games)
    {
        bool dup = false;
        for (const Game& o : out) dup |= o.sha1 == g.sha1;
        if (!dup) out.push_back(g);
    }
    for (Game& g : out)
        g.hasSave = Exists(savesDir + "/" + g.key + ".sav") || Exists(SaveBeside(g));
    // Two versions of one game (FireRed 1.0 and 1.1): each named with its version. (They share
    // the save, as the game's own versions do.)
    for (Game& g : out)
        for (const Game& o : out)
            if (&o != &g && g.known && o.known && o.key == g.key && o.patch != g.patch && g.name.find(" 1.") == std::string::npos)
                g.name += g.patch.size() > 2 && !g.patch.compare(g.patch.size() - 2, 2, "11") ? " 1.1" : " 1.0";
    // Ours first (Emerald, Ultra Violet, FireRed, LeafGreen), then hacks with widescreen, then the rest.
    auto rank = [](const Game& g) {
        if (g.known)
            for (size_t i = 0; i < KnownGames().size(); i++)
                if (g.key == KnownGames()[i].key) return (int)i;
        return g.patch.empty() ? 20 : 10;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Game& a, const Game& b) { return rank(a) < rank(b); });
    return out;
}

// The ROM to play: read, its hack's patch put on, then the widescreen patch if it takes one.
// `beforeWide`, when set, receives the ROM after its own patch and before the widescreen one
// (what RetroAchievements identifies the game by). tryForced false: a hack whose widescreen patch
// doesn't fit (g.forced) plays as it is.
inline bool Prepare(const Game& g, std::vector<uint8_t>& rom, std::string& error, std::vector<uint8_t>* beforeWide = nullptr,
                    bool tryForced = true)
{
    if (!ReadFile(g.rom, rom))
    {
        error = "CAN'T READ THE ROM";
        return false;
    }
    if (!g.ips.empty())
    {
        std::vector<uint8_t> p;
        std::string why;
        if (!ReadFile(g.ips, p, 32 << 20) || !ApplyPatch(rom, p, &why))
        {
            error = why.empty() ? "CAN'T READ ITS PATCH" : why;
            return false;
        }
    }
    if (beforeWide) *beforeWide = rom;
    if (g.patch.empty() || (g.forced && !tryForced)) return true;
    const WidePatch* p = FindPatch(g.patch);
    std::string why;
    if (p && g.forced) return ApplyIps(rom, p->ips, p->ipsSize) || (error = "THE WIDESCREEN PATCH DOESN'T FIT", false);
    if (!p || !PatchFits(rom, *p, why) || !ApplyIps(rom, p->ips, p->ipsSize))
    {
        error = "THE WIDESCREEN PATCH DOESN'T FIT: " + why;
        return false;
    }
    return true;
}

}

} // namespace gbaw
