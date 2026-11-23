#pragma once
// Downloads missing launcher thumbnails / box art when the Switch is online:
// 1. SteamGridDB API v2 (https://www.steamgriddb.com/api/v2) when a user API key is stored
//    in settings.cfg (sgdb_api_key=...) or <dir>/sgdb_api_key.txt (finds custom ROM hack art too).
// 2. Libretro Thumbnails CDN (thumbnails.libretro.com) as a zero-key fallback for official GBA
//    and Nintendo DS Pokémon titles.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "launcher.h"

namespace CoverFetch
{

using HttpGetFn = std::function<bool(const std::string& url, const std::string& authHeader,
                                     int& status, std::string& body, std::string& error)>;
using ProgressFn = std::function<void(int done, int total, const std::string& gameName)>;

inline std::string Trim(std::string s)
{
    while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(s.begin());
    while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
    return s;
}

inline std::string UrlEncode(const std::string& s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

inline std::string FileStem(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    const size_t end = (dot != std::string::npos && dot > start) ? dot : path.size();
    return path.substr(start, end - start);
}

// Normalizes a ROM filename or display title into a clean search query (e.g.
// "POKEMON_SOULGOLD_V1_2_1" -> "Pokemon SoulGold", "POKEMON BLACK 2 (ES)" -> "Pokemon Black 2").
inline std::string CleanTitleQuery(const Launcher::Game& g)
{
    std::string raw = !g.name.empty() ? g.name : FileStem(g.rom);
    for (char& c : raw)
        if (c == '_' || c == '.') c = ' ';
    // Remove bracketed region/build/version suffixes like (ES), (USA), [v1.2], etc.
    std::string stripped;
    int depthParen = 0, depthBrack = 0;
    for (char c : raw)
    {
        if (c == '(') { depthParen++; continue; }
        if (c == ')') { if (depthParen > 0) depthParen--; continue; }
        if (c == '[') { depthBrack++; continue; }
        if (c == ']') { if (depthBrack > 0) depthBrack--; continue; }
        if (depthParen == 0 && depthBrack == 0) stripped += c;
    }
    std::vector<std::string> words;
    std::string cur;
    for (char c : stripped)
    {
        if (isalnum((unsigned char)c)) cur += c;
        else
        {
            if (!cur.empty()) { words.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty()) words.push_back(cur);

    // Drop trailing pure version tokens ("v1", "1", "2", "1" in "v1 2 1") without dropping sequel "2" in "Black 2"
    auto isVersionStart = [](const std::string& w) {
        if (w.size() >= 2 && (w[0] == 'v' || w[0] == 'V') && isdigit((unsigned char)w[1])) return true;
        std::string u;
        for (char ch : w) u += (char)toupper((unsigned char)ch);
        return u == "REV" || u == "BETA" || u == "ALPHA" || u == "FINAL" || u == "FIX" || u == "PATCHED";
    };
    std::vector<std::string> out;
    for (size_t i = 0; i < words.size(); i++)
    {
        if (isVersionStart(words[i])) break;
        out.push_back(words[i]);
    }
    std::string joined;
    for (size_t i = 0; i < out.size(); i++)
    {
        std::string up;
        for (char ch : out[i]) up += (char)toupper((unsigned char)ch);
        if (up == "GBA" || up == "NDS") continue;
        if (!joined.empty()) joined += ' ';
        joined += up;
    }
    return Trim(Launcher::TitleCase(joined));
}

inline std::string ReadSgdbKey(const std::string& dir, const std::string& cfgPath)
{
    std::string key = Trim(Launcher::ConfigValue(cfgPath, "sgdb_api_key"));
    if (!key.empty()) return key;
    for (const char* name : { "sgdb_api_key.txt", "sgdb.key" })
    {
        if (FILE* f = fopen((dir + "/" + name).c_str(), "rb"))
        {
            char buf[512] = {};
            const size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            key = Trim(std::string(buf, n));
            if (!key.empty()) return key;
        }
    }
    return "";
}

inline int ParseSgdbGameId(const std::string& json)
{
    const size_t dataPos = json.find("\"data\"");
    if (dataPos == std::string::npos) return 0;
    const size_t idPos = json.find("\"id\":", dataPos);
    if (idPos == std::string::npos) return 0;
    size_t p = idPos + 5;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
    int id = 0;
    while (p < json.size() && isdigit((unsigned char)json[p]))
        id = id * 10 + (json[p++] - '0');
    return id;
}

inline std::vector<std::string> ParseSgdbGridUrls(const std::string& json)
{
    std::vector<std::pair<int, std::string>> ranked;
    size_t pos = 0;
    while (true)
    {
        const size_t k = json.find("\"url\"", pos);
        if (k == std::string::npos) break;
        const size_t colon = json.find(':', k + 5);
        if (colon == std::string::npos) break;
        const size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) break;
        size_t q2 = q1 + 1;
        std::string url;
        while (q2 < json.size())
        {
            if (json[q2] == '\\' && q2 + 1 < json.size())
            {
                q2++;
                if (json[q2] == '/') url += '/';
                else url += json[q2];
            }
            else if (json[q2] == '"') break;
            else url += json[q2];
            q2++;
        }
        pos = q2 + 1;
        if (url.rfind("http", 0) != 0) continue;
        // Look briefly backwards or ahead within the entry object for width/height hints
        int score = 0;
        const size_t objStart = json.rfind('{', k);
        const size_t objEnd = json.find('}', pos);
        if (objStart != std::string::npos && objEnd != std::string::npos && objEnd > objStart)
        {
            const std::string obj = json.substr(objStart, objEnd - objStart);
            auto readInt = [&](const char* fld) -> int {
                const size_t fp = obj.find(fld);
                if (fp == std::string::npos) return 0;
                const char* s = obj.c_str() + fp + strlen(fld);
                while (*s == ':' || *s == ' ') s++;
                return atoi(s);
            };
            const int w = readInt("\"width\""), h = readInt("\"height\"");
            if (w > 0 && h > 0 && w == h) score += 20;
        }
        if (url.find(".png") != std::string::npos || url.find(".jpg") != std::string::npos) score += 5;
        ranked.push_back({ score, url });
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (const auto& r : ranked) out.push_back(r.second);
    return out;
}

inline std::vector<std::string> LibretroUrls(const Launcher::Game& g)
{
    const bool isGbc = g.platform == Launcher::GB &&
        ((g.rom.size() >= 4 && (g.rom.substr(g.rom.size() - 4) == ".gbc" || g.rom.substr(g.rom.size() - 4) == ".GBC")) ||
         g.code.find("crystal") != std::string::npos || g.code.find("gold") != std::string::npos || g.code.find("silver") != std::string::npos);
    const std::string sys = g.platform == Launcher::GBA
                          ? "Nintendo%20-%20Game%20Boy%20Advance"
                          : g.platform == Launcher::GB
                          ? (isGbc ? "Nintendo%20-%20Game%20Boy%20Color" : "Nintendo%20-%20Game%20Boy")
                          : "Nintendo%20-%20Nintendo%20DS";
    const std::string base = "https://thumbnails.libretro.com/" + sys + "/Named_Boxarts/";
    std::vector<std::string> names;
    if (!g.hack)
    {
        const std::string c = BaseGameCode(g.code);
        if (c == "IRBO") names.push_back("Pokemon - Black Version (USA, Europe)");
        else if (c == "IRAO") names.push_back("Pokemon - White Version (USA, Europe)");
        else if (c == "IREO") names.push_back("Pokemon - Black Version 2 (USA, Europe)");
        else if (c == "IRDO") names.push_back("Pokemon - White Version 2 (USA, Europe)");
        else if (c == "IPKE") names.push_back("Pokemon - HeartGold Version (USA)");
        else if (c == "IPGE") names.push_back("Pokemon - SoulSilver Version (USA)");
        else if (c == "CPUE") names.push_back("Pokemon - Platinum Version (USA)");
        else if (c == "ADAE") names.push_back("Pokemon - Diamond Version (USA)");
        else if (c == "APAE") names.push_back("Pokemon - Pearl Version (USA)");
        else if (c == "BPEE") names.push_back("Pokemon - Emerald Version (USA, Europe)");
        else if (c == "BPRE") names.push_back("Pokemon - FireRed Version (USA, Europe)");
        else if (c == "BPGE") names.push_back("Pokemon - LeafGreen Version (USA, Europe)");
        else if (c == "AXVE") names.push_back("Pokemon - Ruby Version (USA, Europe)");
        else if (c == "AXPE") names.push_back("Pokemon - Sapphire Version (USA, Europe)");
        else if (g.code == "GB-red") names.push_back("Pokemon - Red Version (USA, Europe) (SGB Enhanced)");
        else if (g.code == "GB-blue") names.push_back("Pokemon - Blue Version (USA, Europe) (SGB Enhanced)");
        else if (g.code.rfind("GB-yellow", 0) == 0) names.push_back("Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (CGB+SGB Enhanced)");
        else if (g.code == "GB-gold") names.push_back("Pokemon - Gold Version (USA, Europe) (SGB Enhanced) (GB Compatible)");
        else if (g.code == "GB-silver") names.push_back("Pokemon - Silver Version (USA, Europe) (SGB Enhanced) (GB Compatible)");
        else if (g.code.rfind("GB-crystal", 0) == 0) names.push_back("Pokemon - Crystal Version (USA, Europe) (Rev 1)");
    }
    const std::string clean = CleanTitleQuery(g);
    if (!clean.empty())
    {
        for (const char* region : { " (USA, Europe)", " (USA)", " (Europe)" })
        {
            names.push_back(clean + region);
            if (clean.rfind("Pokemon ", 0) == 0)
            {
                const std::string rest = clean.substr(8);
                names.push_back("Pokemon - " + rest + region);
                if (rest.find("Version") == std::string::npos)
                    names.push_back("Pokemon - " + rest + " Version" + region);
            }
        }
    }
    std::vector<std::string> urls;
    for (const std::string& n : names)
    {
        const std::string u = base + UrlEncode(n) + ".png";
        if (std::find(urls.begin(), urls.end(), u) == urls.end()) urls.push_back(u);
    }
    return urls;
}

inline const char* DetectImageExt(const std::string& bytes)
{
    if (bytes.size() < 32) return nullptr;
    const unsigned char* d = reinterpret_cast<const unsigned char*>(bytes.data());
    if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return ".png";
    if (d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ".jpg";
    return nullptr;
}

inline bool HasCustomArtFile(const Launcher::Game& g, const std::string& dir)
{
    const std::string stem = FileStem(g.rom);
    std::string upCode = g.code;
    for (char& c : upCode) c = (char)toupper((unsigned char)c);
    struct stat st;
    for (const std::string& name : { g.code, upCode, stem })
    {
        if (name.empty()) continue;
        for (const char* ext : { ".jpg", ".png" })
            if (stat((dir + "/icons/" + name + ext).c_str(), &st) == 0) return true;
    }
    return false;
}

struct Result
{
    int checked = 0;
    int downloaded = 0;
    int failed = 0;
    int skippedHacksNoKey = 0;
    bool hadNetworkError = false;
    bool usedSgdb = false;
};

inline Result DownloadMissing(const std::string& dir, const std::string& cfgPath,
                              std::vector<Launcher::Game>& games, const HttpGetFn& httpGet,
                              const ProgressFn& onProgress = nullptr)
{
    Result res;
    if (dir.empty() || !httpGet) return res;
    mkdir((dir + "/icons").c_str(), 0777);
    const std::string sgdbKey = ReadSgdbKey(dir, cfgPath);
    res.usedSgdb = !sgdbKey.empty();

    std::vector<size_t> todo;
    for (size_t i = 0; i < games.size(); i++)
    {
        const Launcher::Game& g = games[i];
        if (g.comingSoon || g.rom.empty()) continue;
        if (!g.art.empty() || HasCustomArtFile(g, dir)) continue;
        todo.push_back(i);
    }
    res.checked = (int)todo.size();

    for (size_t ti = 0; ti < todo.size(); ti++)
    {
        Launcher::Game& g = games[todo[ti]];
        if (onProgress) onProgress((int)ti, (int)todo.size(), g.name);

        std::string savedBytes;
        const char* savedExt = nullptr;
        const std::string query = CleanTitleQuery(g);

        // 1. SteamGridDB API v2 (handles ROM hacks & custom titles cleanly when key is configured)
        if (!sgdbKey.empty() && !query.empty())
        {
            const std::string auth = "Authorization: Bearer " + sgdbKey;
            int st = 0;
            std::string body, err;
            const std::string searchUrl = "https://www.steamgriddb.com/api/v2/search/autocomplete/" + UrlEncode(query);
            if (!httpGet(searchUrl, auth, st, body, err))
                res.hadNetworkError = true;
            else if (st == 200)
            {
                const int gid = ParseSgdbGameId(body);
                if (gid > 0)
                {
                    int gst = 0;
                    std::string gbody, gerr;
                    const std::string gridsUrl = "https://www.steamgriddb.com/api/v2/grids/game/" + std::to_string(gid) +
                                                 "?dimensions=512x512,1024x1024,600x900&types=static";
                    if (httpGet(gridsUrl, auth, gst, gbody, gerr) && gst == 200)
                    {
                        for (const std::string& imgUrl : ParseSgdbGridUrls(gbody))
                        {
                            int ist = 0;
                            std::string ibody, ierr;
                            if (httpGet(imgUrl, "", ist, ibody, ierr) && ist == 200)
                            {
                                if (const char* ext = DetectImageExt(ibody))
                                {
                                    savedBytes = std::move(ibody);
                                    savedExt = ext;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2. Libretro Thumbnails public CDN (no API key needed)
        if (!savedExt)
        {
            for (const std::string& url : LibretroUrls(g))
            {
                int st = 0;
                std::string body, err;
                if (!httpGet(url, "", st, body, err))
                {
                    res.hadNetworkError = true;
                    break;
                }
                if (st == 200)
                {
                    if (const char* ext = DetectImageExt(body))
                    {
                        savedBytes = std::move(body);
                        savedExt = ext;
                        break;
                    }
                }
            }
        }

        if (savedExt && !savedBytes.empty())
        {
            const std::string stem = FileStem(g.rom);
            const std::string saveBase = stem.empty() ? g.code : stem;
            const std::string outPath = dir + "/icons/" + saveBase + savedExt;
            if (FILE* f = fopen(outPath.c_str(), "wb"))
            {
                fwrite(savedBytes.data(), 1, savedBytes.size(), f);
                fclose(f);
                Launcher::FindArt(g, dir);
                res.downloaded++;
            }
            else res.failed++;
        }
        else
        {
            if (g.hack && sgdbKey.empty()) res.skippedHacksNoKey++;
            else res.failed++;
        }
    }
    if (onProgress) onProgress((int)todo.size(), (int)todo.size(), "");
    return res;
}

} // namespace CoverFetch
