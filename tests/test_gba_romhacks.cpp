#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define SSR_GBA

#include "games.h"
#include "gba_games.h"
#include "gba_host_ui.h"
#include "launcher.h"
#include "cover_fetch.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{

void Put32LE(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
}

void PutVarIntUps(std::vector<uint8_t>& out, uint64_t x)
{
    while (true)
    {
        uint64_t b = x & 0x7F;
        x >>= 7;
        if (x == 0)
        {
            out.push_back((uint8_t)(0x80 | b));
            return;
        }
        out.push_back((uint8_t)b);
        x -= 1;
    }
}

void WriteBytes(const fs::path& p, const std::vector<uint8_t>& data)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    assert(f.good());
}

std::vector<uint8_t> MakeSyntheticGba(size_t size, const char title[12], const char code[4], uint8_t version = 0, uint8_t seed = 0x11)
{
    std::vector<uint8_t> rom(size, seed);
    rom[0xB2] = 0x96; // mandatory GBA cartridge fixed byte
    rom[0xBC] = version;
    for (size_t i = 0; i < 12; i++) rom[0xA0 + i] = (uint8_t)(title[i] ? title[i] : 0);
    for (size_t i = 0; i < 4; i++) rom[0xAC + i] = (uint8_t)code[i];
    return rom;
}

std::vector<uint8_t> MakeIps(size_t off, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> p = { 'P', 'A', 'T', 'C', 'H' };
    p.push_back((uint8_t)((off >> 16) & 0xFF));
    p.push_back((uint8_t)((off >> 8) & 0xFF));
    p.push_back((uint8_t)(off & 0xFF));
    p.push_back((uint8_t)((payload.size() >> 8) & 0xFF));
    p.push_back((uint8_t)(payload.size() & 0xFF));
    p.insert(p.end(), payload.begin(), payload.end());
    p.insert(p.end(), { 'E', 'O', 'F' });
    return p;
}

std::vector<uint8_t> MakeUps(const std::vector<uint8_t>& src, const std::vector<uint8_t>& dst)
{
    std::vector<uint8_t> p = { 'U', 'P', 'S', '1' };
    PutVarIntUps(p, src.size());
    PutVarIntUps(p, dst.size());
    const size_t n = std::max(src.size(), dst.size());
    size_t i = 0;
    while (i < n)
    {
        size_t same = 0;
        while (i + same < n)
        {
            const uint8_t a = i + same < src.size() ? src[i + same] : 0;
            const uint8_t b = i + same < dst.size() ? dst[i + same] : 0;
            if (a != b) break;
            same++;
        }
        i += same;
        if (i >= n) break;
        PutVarIntUps(p, same);
        while (i < n)
        {
            const uint8_t a = i < src.size() ? src[i] : 0;
            const uint8_t b = i < dst.size() ? dst[i] : 0;
            i++;
            if (a == b)
            {
                p.push_back(0);
                break;
            }
            p.push_back((uint8_t)(a ^ b));
            if (i == n) p.push_back(0);
        }
    }
    Put32LE(p, RomHacks::Crc32(0, src.data(), src.size()));
    Put32LE(p, RomHacks::Crc32(0, dst.data(), dst.size()));
    Put32LE(p, RomHacks::Crc32(0, p.data(), p.size()));
    return p;
}

std::vector<uint8_t> MakeBpsTargetOnly(const std::vector<uint8_t>& src, const std::vector<uint8_t>& dst)
{
    std::vector<uint8_t> p = { 'B', 'P', 'S', '1' };
    PutVarIntUps(p, src.size());
    PutVarIntUps(p, dst.size());
    PutVarIntUps(p, 0); // metadata length
    // Command: TargetRead ((len - 1) << 2) | 1
    PutVarIntUps(p, ((dst.size() - 1) << 2) | 1u);
    p.insert(p.end(), dst.begin(), dst.end());
    Put32LE(p, RomHacks::Crc32(0, src.data(), src.size()));
    Put32LE(p, RomHacks::Crc32(0, dst.data(), dst.size()));
    Put32LE(p, RomHacks::Crc32(0, p.data(), p.size()));
    return p;
}

const gbaw::Games::Game* FindByFile(const std::vector<gbaw::Games::Game>& games, const std::string& name)
{
    for (const auto& g : games)
        if (g.fileName == name) return &g;
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const fs::path root(argv[1]);
    const fs::path roms = root / "roms";
    const fs::path saves = root / "saves";
    fs::create_directories(roms);
    fs::create_directories(saves);

    // 1. Custom standalone GBA ROM hack WITHOUT "POKEMON"/"POCKET" in header and WITHOUT vanilla code.
    auto customHack = MakeSyntheticGba(0x1000, "UNBOUNDHACK", "UNBX", 0, 0x33);
    WriteBytes(roms / "Pokemon_Unbound_Custom.gba", customHack);

    // 2. Non-standard base .gba + UPS and BPS hacks targeting that non-KnownGames base.
    auto customBase = MakeSyntheticGba(0x2000, "ROJO FUEGO ", "BPRS", 0, 0x24);
    WriteBytes(roms / "FireRed_ES_Base.gba", customBase);

    auto upsTarget = customBase;
    upsTarget[0xA0] = 'H';
    upsTarget[0x300] = 0xAA;
    WriteBytes(roms / "Hispano_Hack.ups", MakeUps(customBase, upsTarget));

    auto bpsTarget = customBase;
    bpsTarget[0x310] = 0x77;
    WriteBytes(roms / "Andaluz_Hack.bps", MakeBpsTargetOnly(customBase, bpsTarget));

    // 3. Synthetic hack reusing Emerald code ("BPEE") where `free` space is used -> PatchCantTry rejects unsafe widescreen.
    const WidePatch* emeraldPatch = gbaw::Games::FindPatch("emerald");
    assert(emeraldPatch != nullptr);
    auto unsafeHack = MakeSyntheticGba(0x01000000, "EMERALD LEG", "BPEE", 0, 0x00);
    WriteBytes(roms / "Emerald_Legacy_SafeFallback.gba", unsafeHack);

    // 3b. Direct PatchCantTry test: `call` + `free` intact allows `same`-changed hack (forced), `call` moved or `free` used blocks it.
    std::vector<uint8_t> miniRom(0x1000, 0xFF);
    miniRom[0x100] = 0xAA; miniRom[0x101] = 0xBB; // call block
    miniRom[0x200] = 0x11;                        // same block
    char safetyBuf[256];
    snprintf(safetyBuf, sizeof safetyBuf, "call 0000100 2 %08X TestCall\nsame 0000200 1 00000000 TestSame\nfree 0000300 10\n",
             (unsigned)doCrc32(miniRom.data() + 0x100, 2));
    const unsigned char dummyIps[] = { 'P', 'A', 'T', 'C', 'H', 'E', 'O', 'F' };
    WidePatch customWide{ "testhack", dummyIps, sizeof dummyIps, safetyBuf };
    assert(gbaw::Games::PatchCantTry(miniRom, customWide).empty());
    miniRom[0x300] = 0x00; // contaminate free space
    assert(!gbaw::Games::PatchCantTry(miniRom, customWide).empty());
    miniRom[0x300] = 0xFF;
    miniRom[0x100] = 0xCC; // move call target
    assert(!gbaw::Games::PatchCantTry(miniRom, customWide).empty());

    // 4. IPS & IPS32 decoding directly (both 24-bit and 32-bit offsets).
    std::vector<uint8_t> rom24 = customBase;
    auto ips24 = MakeIps(0x200, { 0xDE, 0xAD, 0xBE, 0xEF });
    assert(gbaw::Games::ApplyIps(rom24, ips24.data(), ips24.size()));
    assert(rom24[0x200] == 0xDE && rom24[0x203] == 0xEF);
    std::vector<uint8_t> rom32(0x1000010, 0x00);
    std::vector<uint8_t> ips32 = { 'I', 'P', 'S', '3', '2',
                                   0x01, 0x00, 0x00, 0x04,
                                   0x00, 0x04,
                                   'W', 'I', 'D', 'E',
                                   'E', 'E', 'O', 'F' };
    assert(gbaw::Games::ApplyIps(rom32, ips32.data(), ips32.size()));
    assert(rom32[0x1000004] == 'W' && rom32[0x1000007] == 'E');

    // Run Scan twice to exercise cache read/write consistency too.
    const std::string cachePath = (saves / "gba-rom-list.txt").string();
    auto scanned1 = gbaw::Games::Scan(roms.string(), saves.string(), cachePath);
    auto scanned2 = gbaw::Games::Scan(roms.string(), saves.string(), cachePath);
    assert(scanned1.size() == scanned2.size());

    const auto* unbound = FindByFile(scanned2, "Pokemon_Unbound_Custom.gba");
    assert(unbound != nullptr);
    assert(unbound->name == "POKEMON UNBOUND CUSTOM");
    assert(unbound->key == "pokemon-unbound-custom");
    assert(!unbound->known);

    const auto* upsGame = FindByFile(scanned2, "Hispano_Hack.ups");
    assert(upsGame != nullptr);
    assert(upsGame->key == "hispano-hack");
    std::vector<uint8_t> outRom;
    std::string err;
    assert(gbaw::Games::Prepare(*upsGame, outRom, err, nullptr, false));
    assert(outRom == upsTarget);

    const auto* bpsGame = FindByFile(scanned2, "Andaluz_Hack.bps");
    assert(bpsGame != nullptr);
    assert(bpsGame->key == "andaluz-hack");
    assert(gbaw::Games::Prepare(*bpsGame, outRom, err, nullptr, false));
    assert(outRom == bpsTarget);

    const auto* unsafe = FindByFile(scanned2, "Emerald_Legacy_SafeFallback.gba");
    assert(unsafe != nullptr);
    assert(unsafe->patch.empty());
    assert(!unsafe->forced);
    assert(gbaw::Games::Prepare(*unsafe, outRom, err, nullptr, true));

    // Verify PatchForCode/PatchForVersion works for Emerald ES ("BPES") and FireRed ES ("BPRS").
    assert(gbaw::Games::FindPatch("emeraldes") != nullptr);
    assert(gbaw::Games::FindPatch("fireredes") != nullptr);
    assert(gbaw::Games::FindPatch("leafgreenes") != nullptr);

    // 5. Launcher GBA hack card rendering with long custom code does not crash or corrupt canvas.
    UiLang::Set("es");
    Canvas c;
    c.Resize(640, 360);
    std::fill(c.px.begin(), c.px.end(), 0xFF000000u);
    Launcher::DrawCartridgeCase(c, 20, 20, 224, 224, Launcher::GBA, nullptr,
                                "GBA-my-super-long-rom-hack-name-v2", "Pokemon Gaia V3.2", 2, true, true);

    // 6. Custom-code / relocated-free-space hack widescreen fallback (Identify finds compatible sibling patch).
    {
        const WidePatch* rrPatch = gbaw::Games::FindPatch("radicalred");
        assert(rrPatch != nullptr);
        auto rrRom = MakeSyntheticGba(0x01000000, "SOULGOLDGBA", "SGLD", 0, 0x00);
        // Populate call hooks and free space required by radicalred, leaving FireRed's vanilla 0x0F00000 occupied (0x00).
        auto find2ForCrc = [](uint32_t targetCrc) -> uint16_t {
            for (uint32_t v = 0; v < 65536; v++)
            {
                const uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
                if (doCrc32(b, 2) == targetCrc) return (uint16_t)v;
            }
            std::abort();
        };
        std::map<uint32_t, uint16_t> crcCache;
        const char* s = rrPatch->safety;
        while (*s)
        {
            const char* end = strchr(s, '\n');
            const std::string line(s, end ? end - s : strlen(s));
            s = end ? end + 1 : s + line.size();
            char kind[8] = {};
            unsigned off = 0, sz = 0, crc = 0;
            const int got = sscanf(line.c_str(), "%7s %x %x %x", kind, &off, &sz, &crc);
            if (got < 3) continue;
            if (!strcmp(kind, "free"))
                for (unsigned i = 0; i < sz && off + i < rrRom.size(); i++) rrRom[off + i] = 0xFF;
            else if (!strcmp(kind, "call") && got >= 4 && sz == 2 && (size_t)off + 2 <= rrRom.size())
            {
                if (crcCache.find(crc) == crcCache.end()) crcCache[crc] = find2ForCrc(crc);
                const uint16_t val = crcCache[crc];
                rrRom[off + 0] = (uint8_t)(val & 0xFF);
                rrRom[off + 1] = (uint8_t)(val >> 8);
            }
        }
        gbaw::Games::Game g;
        g.fileName = "Pokemon_SoulGold_v1.2.1.gba";
        gbaw::Games::Identify(g, rrRom);
        assert(!g.patch.empty());
        assert(g.forced);
        assert(g.patch == "radicalred" || gbaw::Games::PatchCantTry(rrRom, *gbaw::Games::FindPatch(g.patch)).empty());
    }

    // 7. Uppercase GBA forwarder code, FindArt stem resolution on .ups/long extensions, and start_button -> gba_start sync.
    {
        assert(GbaGames::CodeOf("pokemon-soulgold-v1-2-1") == "GBA-POKEMON-SOULGOLD-V1-2-1");

        // Minimal 1x1 red RGBA PNG generated cleanly via chunk CRCs? Wait: embed exact valid 1x1 PNG bytes.
        static const unsigned char kPng1x1[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE,
            0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54,
            0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00,
            0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D, 0xB0,
            0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
            0xAE, 0x42, 0x60, 0x82
        };
        const fs::path iconDir = root / "icons";
        WriteBytes(iconDir / "Long_Name_Hack.png", std::vector<unsigned char>(kPng1x1, kPng1x1 + sizeof kPng1x1));
        Launcher::Game artGame;
        artGame.platform = Launcher::GBA;
        artGame.rom = (roms / "Long_Name_Hack.xdelta").string();
        artGame.folder = saves.string();
        artGame.code = "GBA-LONG-NAME-HACK";
        Launcher::FindArt(artGame, root.string());
        assert(artGame.art.size() == (size_t)Launcher::Art * Launcher::Art);

        const std::string hostCfg = (root / "settings.cfg").string();
        Launcher::SetConfigValue(hostCfg, "start_button", "menu");
        Launcher::SetConfigValue(hostCfg, "list_lang", "es");
        Launcher::SyncGbaSettings(hostCfg, root.string());
        assert(Launcher::ConfigValue((root / "gba-settings.ini").string(), "gba_start") == "menu");
        assert(Launcher::ConfigValue((root / "gba-settings.ini").string(), "language") == "es");
        Launcher::SetConfigValue(hostCfg, "start_button", "ds");
        Launcher::SyncGbaSettings(hostCfg, root.string());
        assert(Launcher::ConfigValue((root / "gba-settings.ini").string(), "gba_start") == "start");
    }

    // 8. Gen 3 Evolved patch present and Stage 2 >=85% hooked-prologue fallback works without accepting shifted code.
    {
        assert(gbaw::Games::FindPatch("gen3evolved") != nullptr);
        const WidePatch* frPatch = gbaw::Games::FindPatch("firered");
        assert(frPatch != nullptr);
        auto hookedRom = MakeSyntheticGba(0x01000000, "SOULGOLD", "HACK", 0, 0x00);
        auto find2ForCrc = [](uint32_t targetCrc) -> uint16_t {
            for (uint32_t v = 0; v < 65536; v++)
            {
                const uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
                if (doCrc32(b, 2) == targetCrc) return (uint16_t)v;
            }
            std::abort();
        };
        std::map<uint32_t, uint16_t> crcCache;
        int callIdx = 0;
        const char* s = frPatch->safety;
        while (*s)
        {
            const char* end = strchr(s, '\n');
            const std::string line(s, end ? end - s : strlen(s));
            s = end ? end + 1 : s + line.size();
            char kind[8] = {};
            unsigned off = 0, sz = 0, crc = 0;
            const int got = sscanf(line.c_str(), "%7s %x %x %x", kind, &off, &sz, &crc);
            if (got < 3) continue;
            if (!strcmp(kind, "free"))
                for (unsigned i = 0; i < sz && off + i < hookedRom.size(); i++) hookedRom[off + i] = 0xFF;
            else if (!strcmp(kind, "call") && got >= 4 && sz == 2 && (size_t)off + 2 <= hookedRom.size())
            {
                if (callIdx < 2)
                {
                    // Place valid Thumb LDR trampoline prologue (00 48) instead of vanilla bytes
                    hookedRom[off + 0] = 0x00;
                    hookedRom[off + 1] = 0x48;
                }
                else
                {
                    if (crcCache.find(crc) == crcCache.end()) crcCache[crc] = find2ForCrc(crc);
                    const uint16_t val = crcCache[crc];
                    hookedRom[off + 0] = (uint8_t)(val & 0xFF);
                    hookedRom[off + 1] = (uint8_t)(val >> 8);
                }
                callIdx++;
            }
        }
        gbaw::Games::Game hg;
        hg.fileName = "Pokemon_SoulGold_GBA.gba";
        gbaw::Games::Identify(hg, hookedRom);
        assert(!hg.patch.empty() && hg.forced);

        // Corrupt 50% of calls -> safely rejected
        for (size_t i = 0; i < 0x1F0000; i += 4) hookedRom[i] = 0x00;
        gbaw::Games::Game bad;
        bad.fileName = "Broken_Decomp_Hack.gba";
        gbaw::Games::Identify(bad, hookedRom);
        assert(bad.patch.empty());
    }

    // 9. CoverFetch URL building, JSON parsing, SteamGridDB key & keyless Libretro mock downloads.
    {
        Launcher::Game hack;
        hack.platform = Launcher::GBA;
        hack.name = "POKEMON_SOULGOLD_V1_2_1";
        hack.rom = (roms / "Pokemon_SoulGold_v1_2_1.gba").string();
        hack.code = "GBA-POKEMON-SOULGOLD-V1-2-1";
        hack.hack = true;
        assert(CoverFetch::CleanTitleQuery(hack) == "Pokemon Soulgold");

        assert(CoverFetch::ParseSgdbGameId("{\"success\":true,\"data\":[{\"id\":98765,\"name\":\"SoulGold\"}]}") == 98765);
        auto grids = CoverFetch::ParseSgdbGridUrls("{\"data\":[{\"width\":600,\"height\":900,\"url\":\"https:\\/\\/cdn.example\\/tall.png\"},{\"width\":512,\"height\":512,\"url\":\"https:\\/\\/cdn.example\\/sq.png\"}]}");
        assert(grids.size() == 2 && grids[0] == "https://cdn.example/sq.png");

        static const unsigned char kPng1x1[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE,
            0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54,
            0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00,
            0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D, 0xB0,
            0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
            0xAE, 0x42, 0x60, 0x82
        };
        const fs::path fetchDir = root / "fetchtest";
        fs::create_directories(fetchDir);
        WriteBytes(fetchDir / "sgdb_api_key.txt", std::vector<unsigned char>{ 't', 'e', 's', 't', '-', 'k', 'e', 'y' });
        Launcher::Game dsVanilla;
        dsVanilla.platform = Launcher::DS;
        dsVanilla.name = "Pokemon Black 2 (ES)";
        dsVanilla.rom = (roms / "Black2.nds").string();
        dsVanilla.code = "IREO";
        std::vector<Launcher::Game> list = { hack, dsVanilla };
        auto mockGet = [&](const std::string& url, const std::string& auth, int& st, std::string& body, std::string&) {
            if (url.find("/search/autocomplete/") != std::string::npos)
            {
                assert(auth == "Authorization: Bearer test-key");
                st = 200;
                body = url.find("Soulgold") != std::string::npos ? "{\"success\":true,\"data\":[{\"id\":42}]}" : "{\"success\":true,\"data\":[]}";
                return true;
            }
            if (url.find("/grids/game/42") != std::string::npos)
            {
                st = 200;
                body = "{\"data\":[{\"width\":512,\"height\":512,\"url\":\"https://cdn.example/sg.png\"}]}";
                return true;
            }
            if (url == "https://cdn.example/sg.png" || url.find("thumbnails.libretro.com") != std::string::npos)
            {
                st = 200;
                body.assign(reinterpret_cast<const char*>(kPng1x1), sizeof kPng1x1);
                return true;
            }
            st = 404;
            return true;
        };
        const CoverFetch::Result r = CoverFetch::DownloadMissing(fetchDir.string(), (fetchDir / "settings.cfg").string(), list, mockGet);
        assert(r.usedSgdb && r.downloaded == 2);
        assert(list[0].art.size() == (size_t)Launcher::Art * Launcher::Art);
        assert(list[1].art.size() == (size_t)Launcher::Art * Launcher::Art);
        assert(fs::exists(fetchDir / "icons/Pokemon_SoulGold_v1_2_1.png"));
    }

    // 10. Original NoSplitNX unified-widescreen logo and GbaHostUi native pause menu & slider rendering.
    {
        Canvas lc;
        lc.Resize(1280, 720);
        lc.Clear();
        Launcher::DrawDsLogo(lc, 30, 10, 3, AccentNamed("red"));
        bool hasAccent = false, hasPearlWhite = false, hasOldCyan = false;
        for (uint32_t px : lc.px)
        {
            if (px == AccentNamed("red")) hasAccent = true;
            if (px == 0xFFF2F2F6u) hasPearlWhite = true;
            if (px == 0xFF00B8E6u) hasOldCyan = true;
        }
        assert(hasAccent && hasPearlWhite && !hasOldCyan);
        assert(lc.px[(size_t)(10 + 13) * lc.w + (30 + 8)] == 0);

        UiLang::Set("es");
        const GbaWide::UiHost* uiHost = GbaHostUi::Api();
        assert(uiHost && uiHost->drawOverlay);
        std::vector<uint32_t> px;
        GbaWide::MenuHostState drawn;
        std::vector<GbaWide::MenuHostItem> items = {
            { "GRAPHICS", "", false, -1 },
            { "VOLUME", "80%", false, 8 },
            { "BACK", "", false, -1 },
        };
        uiHost->drawOverlay(uiHost->ud, px, 1280, 720, true, items, 1,
                            "SOUND", "A: CHANGE   B: BACK", "THE GAME'S SOUND.",
                            "SAVED", 255, 60, true, "purple", drawn);
        assert(px.size() == (size_t)1280 * 720);
        assert(Menu::Accent == AccentNamed("purple"));
        assert(drawn.boxW > 300 && drawn.boxH > 120 && drawn.shown == 3);
        assert(drawn.ItemAt(drawn.boxX - 10, drawn.boxY) == -2);
        assert(drawn.ItemAt(drawn.boxX + 30, drawn.firstY + drawn.lineH + 2) == 1);
        assert(drawn.barStep > 0 && drawn.SliderAt(drawn.barX + 4 * drawn.barStep + 1) == 5);
        UiLang::Set("en");
    }

    std::puts("GBA ROM hacks test OK: unrestricted detection, UPS/BPS/IPS32 patching, PatchCantTry, Spanish patches, and card UI");
    return 0;
}
