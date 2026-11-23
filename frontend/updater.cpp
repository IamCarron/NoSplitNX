// Updates for the all-games program. See updater.h.
#include "updater.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(__SWITCH__)
#include <switch.h>
#include <zlib.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef SSR_BUILT_AT
#define SSR_BUILT_AT 0
#endif

namespace Achievements
{

// (achievements_http.cpp: each platform's way to the internet, also used here.)
bool HttpPost(const std::string& url, const std::string& post, const std::string& contentType,
              const std::string& userAgent, int& status, std::string& body, std::string& error);
bool HttpDownload(const std::string& url, const std::string& userAgent, int& status, std::string& body,
                  std::string& error);
#ifdef __SWITCH__
void HttpStop(bool on);
#endif
}

namespace fs = std::filesystem;

namespace Updater
{
namespace
{
const char* const kReleases = "https://api.github.com/repos/IamCarron/NoSplitNX/releases?per_page=30";
const char* const kDownloads = "https://github.com/IamCarron/NoSplitNX/releases/download/";
const char* const kAgent = "NoSplitNX-Updater";
// ---------------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4).

struct Sha256
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t block[64];
    size_t used = 0;
    uint64_t bits = 0;

    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Compress(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++)
        {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++)
        {
            const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void Add(const uint8_t* p, size_t n)
    {
        bits += (uint64_t)n * 8;
        while (n)
        {
            const size_t take = std::min(n, 64 - used);
            memcpy(block + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { Compress(block); used = 0; }
        }
    }
    std::string Hex()
    {
        const uint64_t total = bits;
        const uint8_t one = 0x80, zero = 0;
        Add(&one, 1);
        while (used != 56) Add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(total >> (56 - 8 * i));
        Add(len, 8);
        static const char digits[] = "0123456789abcdef";
        std::string out;
        for (uint32_t v : h)
            for (int i = 28; i >= 0; i -= 4) out += digits[(v >> i) & 15];
        return out;
    }
};

// ---------------------------------------------------------------------------------------------
// Enough of JSON for GitHub's list of releases.

struct Json
{
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json* Get(const char* key) const
    {
        for (const auto& f : fields)
            if (f.first == key) return &f.second;
        return nullptr;
    }
    std::string Str(const char* key) const
    {
        const Json* j = Get(key);
        return j && j->type == String ? j->s : std::string();
    }
    bool True(const char* key) const
    {
        const Json* j = Get(key);
        return j && j->type == Bool && j->b;
    }
    double Num(const char* key) const
    {
        const Json* j = Get(key);
        return j && j->type == Number ? j->n : 0;
    }
};

struct JsonReader
{
    const char* p;
    const char* end;
    bool ok = true;

    void Space() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool Take(char c)
    {
        Space();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }
    void Utf8(uint32_t c, std::string& out)
    {
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | c >> 6); out += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += (char)(0xE0 | c >> 12); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
        else { out += (char)(0xF0 | c >> 18); out += (char)(0x80 | ((c >> 12) & 63)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
    }
    bool Hex4(uint32_t& v)
    {
        if (end - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; i++)
        {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool Text(std::string& out)
    {
        if (!Take('"')) return false;
        while (p < end && *p != '"')
        {
            if (*p != '\\') { out += *p++; continue; }
            if (++p >= end) return false;
            const char c = *p++;
            switch (c)
            {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
            {
                uint32_t v;
                if (!Hex4(v)) return false;
                if (v >= 0xD800 && v < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                {
                    p += 2;
                    uint32_t lo;
                    if (!Hex4(lo)) return false;
                    v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                }
                Utf8(v, out);
                break;
            }
            default: out += c; break;
            }
        }
        if (p >= end) return false;
        p++;
        return true;
    }
    bool Value(Json& v, int depth)
    {
        if (depth > 32) return false;
        Space();
        if (p >= end) return false;
        if (*p == '{')
        {
            p++;
            v.type = Json::Object;
            if (Take('}')) return true;
            do
            {
                std::string key;
                Space();
                if (!Text(key) || !Take(':')) return false;
                v.fields.emplace_back(std::move(key), Json());
                if (!Value(v.fields.back().second, depth + 1)) return false;
            } while (Take(','));
            return Take('}');
        }
        if (*p == '[')
        {
            p++;
            v.type = Json::Array;
            if (Take(']')) return true;
            do
            {
                v.items.emplace_back();
                if (!Value(v.items.back(), depth + 1)) return false;
            } while (Take(','));
            return Take(']');
        }
        if (*p == '"')
        {
            v.type = Json::String;
            return Text(v.s);
        }
        auto word = [&](const char* w) {
            const size_t n = strlen(w);
            if ((size_t)(end - p) >= n && !memcmp(p, w, n)) { p += n; return true; }
            return false;
        };
        if (word("true")) { v.type = Json::Bool; v.b = true; return true; }
        if (word("false")) { v.type = Json::Bool; return true; }
        if (word("null")) return true;
        const char* start = p;
        while (p < end && (isdigit((unsigned char)*p) || strchr("+-.eE", *p))) p++;
        if (p == start) return false;
        v.type = Json::Number;
        v.n = strtod(std::string(start, p).c_str(), nullptr);
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// This program's download.

enum class Kind { None, LinuxTar, SwitchNro };

// g_program: the program's own file, where the folder has more than one (the Switch's .nro).
std::string g_programDir, g_dataDir, g_program;

Kind KindOf()
{
#if defined(__SWITCH__)
    return Kind::SwitchNro;
#else
    return Kind::LinuxTar;
#endif
}

const char* AssetName()
{
#if defined(__SWITCH__)
    return "nosplitnx-switch.zip";
#else
    return "pokemon-ds-wide-pc-linux-x86_64.tar.gz";
#endif
}

// The program's file in the unpacked download.
const char* ProgramFile()
{
    return "pokemon-ds-wide";
}

std::string ReadFile(const std::string& path);

// The list of releases, and where downloads may come from: GitHub's, or for a test a list made
// by tools/updater/fake_release.py, whose downloads must then be on the same server: on a PC
// FULLDEX_UPDATE_URL; on the Switch, which has no environment, update-test-url.txt in its folder,
// naming a list on the SD card (file:///switch/...).
std::string TestUrl()
{
#ifdef __SWITCH__
    std::string url = ReadFile(g_dataDir + "/update-test-url.txt");
    while (!url.empty() && isspace((unsigned char)url.back())) url.pop_back();
    return url.compare(0, 15, "file:///switch/") == 0 ? url : std::string();
#else
    const char* test = getenv("FULLDEX_UPDATE_URL");
    return test ? test : "";
#endif
}
std::string ReleasesUrl()
{
    const std::string test = TestUrl();
    return test.empty() ? kReleases : test;
}
std::string DownloadPrefix()
{
    const std::string url = TestUrl();
    if (url.empty()) return kDownloads;
    const size_t scheme = url.find("://");
    const size_t path = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
    return path == std::string::npos ? url : url.substr(0, path + 1);
}

std::string UpdateDir() { return g_dataDir + "/update"; }

std::string ReadFile(const std::string& path)
{
    std::string out;
    if (FILE* f = fopen(path.c_str(), "rb"))
    {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        fclose(f);
    }
    return out;
}
bool WriteFile(const std::string& path, const std::string& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

std::string SettingsPath() { return g_dataDir.empty() ? "" : (g_dataDir + "/settings.cfg"); }

std::string ReadCfgValue(const std::string& body, const char* key)
{
    const size_t klen = strlen(key);
    size_t pos = 0;
    while (pos < body.size())
    {
        size_t end = body.find('\n', pos);
        if (end == std::string::npos) end = body.size();
        size_t len = end - pos;
        if (len > 0 && body[pos + len - 1] == '\r') --len;
        if (len > klen && body.compare(pos, klen, key) == 0 && body[pos + klen] == '=')
            return body.substr(pos + klen + 1, len - klen - 1);
        pos = end + 1;
    }
    return "";
}

void WriteCfgValues(const std::string& path, const std::vector<std::pair<std::string, std::string>>& kvs)
{
    if (path.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    const std::string existing = ReadFile(path);
    std::string out;
    out.reserve(existing.size() + 128);
    size_t pos = 0;
    while (pos < existing.size())
    {
        size_t end = existing.find('\n', pos);
        const bool hasNl = end != std::string::npos;
        if (!hasNl) end = existing.size();
        std::string line = existing.substr(pos, end - pos);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        bool replaced = false;
        for (const auto& kv : kvs)
        {
            if (line.size() > kv.first.size() && line.compare(0, kv.first.size(), kv.first) == 0 && line[kv.first.size()] == '=')
            {
                replaced = true;
                break;
            }
        }
        if (!replaced && !line.empty()) { out += line; out += '\n'; }
        pos = end + (hasNl ? 1 : 0);
    }
    for (const auto& kv : kvs)
    {
        out += kv.first;
        out += '=';
        out += kv.second;
        out += '\n';
    }
    WriteFile(path, out);
}

void CleanUpdateArtifacts()
{
    if (g_dataDir.empty()) return;
    const std::string up = UpdateDir();
    std::error_code ec;
    fs::remove_all(fs::u8path(up + "/new"), ec);
    fs::remove_all(fs::u8path(up + "/previous"), ec);
    fs::remove(fs::u8path(up + "/installed.txt"), ec);
    if (fs::is_directory(fs::u8path(up), ec) && fs::is_empty(fs::u8path(up), ec))
        fs::remove(fs::u8path(up), ec);
}

// What was installed: `update_sha` and `update_time` stored inside `settings.cfg`
// (migrating any legacy `update/installed.txt` cleanly without keeping extra files/folders).
bool Installed(std::string& sha, long long& time)
{
    const std::string cfg = ReadFile(SettingsPath());
    const std::string cfgSha = ReadCfgValue(cfg, "update_sha");
    if (cfgSha.size() == 64)
    {
        sha = cfgSha;
        const std::string t = ReadCfgValue(cfg, "update_time");
        time = t.empty() ? 0 : strtoll(t.c_str(), nullptr, 10);
        return true;
    }
    const std::string legacyPath = UpdateDir() + "/installed.txt";
    const std::string line = ReadFile(legacyPath);
    char hash[65] = {};
    long long t = 0;
    if (sscanf(line.c_str(), "%64s %lld", hash, &t) < 1 || strlen(hash) != 64) return false;
    sha = hash;
    time = t;
    WriteCfgValues(SettingsPath(), { { "update_sha", sha }, { "update_time", std::to_string(time) } });
    std::error_code ec;
    fs::remove(fs::u8path(legacyPath), ec);
    return true;
}
void SetInstalled(const std::string& sha, long long time)
{
    WriteCfgValues(SettingsPath(), { { "update_sha", sha }, { "update_time", std::to_string(time) } });
}

// The state, shared with the threads. (Never freed: a check may still be running as the
// program ends.)
struct Shared
{
    std::mutex lock;
    Status status;
    Release release;
    std::string digest;
    bool busy = false;
    std::string restart;
    StatusCallback cb = nullptr;
    void* cbUser = nullptr;
};
Shared& S()
{
    static Shared* s = new Shared;
    return *s;
}
void Set(State state, const std::string& text)
{
    Status snap;
    StatusCallback cb = nullptr;
    void* cbUser = nullptr;
    {
        std::lock_guard<std::mutex> l(S().lock);
        S().status.state = state;
        S().status.text = text;
        S().status.serial++;
        // (A check or install ends with a state that is not one of these: the next may start.)
        if (state != State::Checking && state != State::Downloading) S().busy = false;
        snap = S().status;
        cb = S().cb;
        cbUser = S().cbUser;
    }
    fprintf(stderr, "[update] %s\n", text.c_str());
    if (cb) cb(snap, cbUser);
}

// (curl reports no status for a file:// address: the Switch's tests.)
bool StatusOk(const std::string& url, int status)
{
    return status == 200 || (status == 0 && url.compare(0, 7, "file://") == 0);
}

bool Get(const std::string& url, std::string& body, std::string& error)
{
    int status = 0;
    if (!Achievements::HttpPost(url, "", "", kAgent, status, body, error)) return false;
    if (!StatusOk(url, status)) { error = "HTTP " + std::to_string(status); return false; }
    return true;
}

void RunCheck(bool quiet)
{
    const char* asset = AssetName();
    if (!asset) { Set(State::Failed, "NOT ON THIS SYSTEM"); return; }
    std::string body, error;
    Set(State::Checking, "CHECKING...");
    const std::string list = ReleasesUrl();
    if (!Get(list, body, error))
    {
        fprintf(stderr, "[update] %s: %s\n", ReleasesUrl().c_str(), error.c_str());
        Set(quiet ? State::Idle : State::Failed, quiet ? "CHECK" : "NO CONNECTION TO GITHUB");
        return;
    }
    const bool includeBetas = IncludeBetas();
    Release r;
    if (!FindRelease(body, asset, DownloadPrefix(), r, error, includeBetas))
    {
        fprintf(stderr, "[update] %s\n", error.c_str());
        Set(State::UpToDate, "UP TO DATE");
        return;
    }
    std::string digest;
    long long remoteBuild=0;
    if (!r.sumsUrl.empty())
    {
        std::string sums;
        const std::string& sumAsset = r.asset.empty() ? std::string(asset) : r.asset;
        if (Get(r.sumsUrl, sums, error))
        { digest = ParseSums(sums)[sumAsset]; remoteBuild = ParseBuilds(sums)[sumAsset]; }
        else fprintf(stderr, "[update] SHA256SUMS.txt: %s\n", error.c_str());
    }
    fprintf(stderr, "[update] newest %s: %s (%s), sha256 %s\n", asset, r.tag.c_str(), r.url.c_str(),
            digest.empty() ? "none in the release" : digest.c_str());
    std::string sha;
    long long time = 0;
    Installed(sha,time);
    const bool newer=IsNewerBuild(remoteBuild,(long long)SSR_BUILT_AT,digest,sha,r.updated,time);
    bool canInstall = false;
    const Kind kind = KindOf();
    if (newer && !digest.empty())
    {
        if (kind == Kind::SwitchNro)
        {
            // (Only a .nro on the SD card: not one sent over the network to test.)
            std::error_code ec;
            canInstall = g_program.size() > 4 && g_program.compare(g_program.size() - 4, 4, ".nro") == 0 &&
                         fs::exists(fs::u8path(g_program), ec);
        }
        else if (kind == Kind::LinuxTar)
        {
            // (Only an unpacked download: never a build folder or somewhere else.)
            std::error_code ec;
            canInstall = fs::exists(fs::u8path(g_programDir + "/" + ProgramFile()), ec) &&
                         fs::is_directory(fs::u8path(g_programDir + "/data"), ec);
        }
    }
    {
        std::lock_guard<std::mutex> l(S().lock);
        S().release = r;
        S().digest = digest;
        S().status.page = r.page;
        S().status.tag = r.tag;
        S().status.prerelease = r.prerelease;
        S().status.canInstall = canInstall;
        S().status.currentBuild = (long long)SSR_BUILT_AT;
        S().status.availableBuild = remoteBuild > 0 ? remoteBuild : r.updated;
    }
    if (!newer) Set(State::UpToDate, "UP TO DATE");
    else Set(State::Available, canInstall ? "NEW VERSION: INSTALL" : "NEW VERSION: OPEN PAGE");
}

#ifndef __SWITCH__
bool Run(const std::vector<std::string>& args)
{
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0)
    {
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back((char*)a.c_str());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Unpack(const std::string& archive, const std::string& dest)
{
    return Run({ "tar", "-xzf", archive, "-C", dest });
}
#endif

#ifdef __SWITCH__
uint32_t Le16(const std::string& d, size_t at) { return (uint8_t)d[at] | (uint8_t)d[at + 1] << 8; }
uint32_t Le32(const std::string& d, size_t at) { return Le16(d, at) | Le16(d, at + 2) << 16; }

// The files directly in folder ("switch/pokemon-ds-wide/") of the zip, written into dest, the
// .nro among them named nroName (the name this one has on the SD card). Each is checked against
// the zip's own checksum (CRC-32) as well.
bool UnzipFolder(const std::string& zip, const std::string& folder, const std::string& nroName,
                 const std::string& dest, std::string& error)
{
    error = "not a zip";
    if (zip.size() < 22) return false;
    size_t end = zip.size() - 22; // (the end record, before a comment of up to 64 KB)
    while (Le32(zip, end) != 0x06054b50)
    {
        if (end == 0 || zip.size() - end > 22 + 65535) return false;
        end--;
    }
    const unsigned count = Le16(zip, end + 10);
    size_t at = Le32(zip, end + 16);
    bool nro = false;
    for (unsigned n = 0; n < count; n++)
    {
        if (at + 46 > zip.size() || Le32(zip, at) != 0x02014b50) { error = "the zip's list is damaged"; return false; }
        const unsigned method = Le16(zip, at + 10), nameLen = Le16(zip, at + 28);
        const uint32_t crc = Le32(zip, at + 16), packed = Le32(zip, at + 20), size = Le32(zip, at + 24);
        const size_t local = Le32(zip, at + 42);
        if (at + 46 + nameLen > zip.size()) { error = "the zip's list is damaged"; return false; }
        const std::string name = zip.substr(at + 46, nameLen);
        at += 46 + nameLen + Le16(zip, at + 30) + Le16(zip, at + 32);
        if (name.compare(0, folder.size(), folder) != 0) continue;
        std::string file = name.substr(folder.size());
        if (file.empty() || file.find('/') != std::string::npos) continue; // (its roms folder)
        if (local + 30 > zip.size() || Le32(zip, local) != 0x04034b50) { error = name + " is damaged"; return false; }
        const size_t start = local + 30 + Le16(zip, local + 26) + Le16(zip, local + 28);
        if (start + packed > zip.size()) { error = name + " is cut short"; return false; }
        std::string out;
        if (method == 0 && packed == size) out = zip.substr(start, size);
        else if (method == 8)
        {
            out.resize(size);
            z_stream z = {};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) { error = "zlib"; return false; }
            z.next_in = (Bytef*)&zip[start];
            z.avail_in = packed;
            z.next_out = (Bytef*)&out[0];
            z.avail_out = size;
            const int rc = inflate(&z, Z_FINISH);
            const uLong got = z.total_out;
            inflateEnd(&z);
            if (rc != Z_STREAM_END || got != size) { error = "cannot unpack " + name; return false; }
        }
        else { error = name + ": packed in a way this cannot read"; return false; }
        if (crc32(0L, (const Bytef*)out.data(), (uInt)out.size()) != crc) { error = name + ": wrong CRC"; return false; }
        if (file.size() > 4 && file.compare(file.size() - 4, 4, ".nro") == 0)
        {
            file = nroName;
            nro = true;
        }
        if (!WriteFile(dest + "/" + file, out)) { error = "cannot write " + dest + "/" + file; return false; }
    }
    if (!nro) { error = "no .nro in the download"; return false; }
    return true;
}
#endif

void RunInstall()
{
    Release r;
    std::string digest;
    {
        std::lock_guard<std::mutex> l(S().lock);
        r = S().release;
        digest = S().digest;
    }
    Set(State::Downloading, "DOWNLOADING...");
    std::string data, error;
    int status = 0;
    if (!Achievements::HttpDownload(r.url, kAgent, status, data, error) || !StatusOk(r.url, status))
    {
        fprintf(stderr, "[update] %s: %s (HTTP %d)\n", r.url.c_str(), error.c_str(), status);
        Set(State::Failed, "DOWNLOAD FAILED: NOTHING CHANGED");
        return;
    }
    const std::string got = Sha256Hex(data);
    if (got != digest)
    {
        fprintf(stderr, "[update] checksum %s, the release says %s\n", got.c_str(), digest.c_str());
        Set(State::Failed, "DOWNLOAD DAMAGED: NOTHING CHANGED");
        return;
    }
    Set(State::Downloading, "INSTALLING...");
    std::error_code ec;
    const std::string up = UpdateDir();
    fs::create_directories(fs::u8path(up), ec);
    std::string restart;
#ifdef __SWITCH__
    {
        // The .nro and the files beside it put in place as on a PC: the old ones kept in
        // update/previous, and all put back if one cannot be replaced. Unmount romfs first so
        // Horizon OS does not refuse to rename the open .nro file on the SD card.
        romfsExit();
        const std::string unpacked = up + "/new";
        fs::remove_all(fs::u8path(unpacked), ec);
        fs::create_directories(fs::u8path(unpacked), ec);
        const std::string nroName = g_program.substr(g_program.find_last_of('/') + 1);
        if (!UnzipFolder(data, "switch/nosplitnx/", nroName, unpacked, error) &&
            !UnzipFolder(data, "switch/pokemon-ds-wide/", nroName, unpacked, error))
        {
            fprintf(stderr, "[update] %s\n", error.c_str());
            CleanUpdateArtifacts();
            romfsInit();
            Set(State::Failed, "CANNOT UNPACK: NOTHING CHANGED");
            return;
        }
        if (!SwapIn(g_programDir, unpacked, up + "/previous", error))
        {
            fprintf(stderr, "[update] %s\n", error.c_str());
            CleanUpdateArtifacts();
            romfsInit();
            Set(State::Failed, "CANNOT REPLACE: NOTHING CHANGED");
            return;
        }
        CleanUpdateArtifacts();
        restart = g_program;
    }
#else
    {
        const std::string archive = up + "/" + r.asset, unpacked = up + "/new";
        fs::remove_all(fs::u8path(unpacked), ec);
        fs::create_directories(fs::u8path(unpacked), ec);
        if (!WriteFile(archive, data) || !Unpack(archive, unpacked))
        {
            remove(archive.c_str());
            CleanUpdateArtifacts();
            Set(State::Failed, "CANNOT UNPACK: NOTHING CHANGED");
            return;
        }
        remove(archive.c_str());
        const std::string top = unpacked + "/pokemon-ds-wide";
        if (!fs::exists(fs::u8path(top + "/" + ProgramFile()), ec))
        {
            CleanUpdateArtifacts();
            Set(State::Failed, "NOT THE PROGRAM: NOTHING CHANGED");
            return;
        }
        if (!SwapIn(g_programDir, top, up + "/previous", error))
        {
            fprintf(stderr, "[update] %s\n", error.c_str());
            CleanUpdateArtifacts();
            Set(State::Failed, "CANNOT REPLACE: NOTHING CHANGED");
            return;
        }
        CleanUpdateArtifacts();
        restart = g_programDir + "/" + ProgramFile();
    }
#endif
    SetInstalled(digest, r.updated);
    CleanUpdateArtifacts();
    {
        std::lock_guard<std::mutex> l(S().lock);
        S().restart = restart;
    }
    Set(State::Installed, "RESTARTING...");
}

void Spawn(void (*work)(bool), bool arg)
{
    {
        std::lock_guard<std::mutex> l(S().lock);
        if (S().busy) return;
        S().busy = true;
    }
#ifdef __SWITCH__
    // On this console a second thread that talks to the SD card or the network stops the
    // whole program (a black screen). The check and the install run here, after the settings
    // page has shown CHECKING... or DOWNLOADING...
    work(arg);
#else
    std::thread([work, arg]() { work(arg); }).detach();
#endif
}
}

std::string Sha256Hex(const std::string& data)
{
    Sha256 h;
    h.Add((const uint8_t*)data.data(), data.size());
    return h.Hex();
}

std::map<std::string, long long> ParseBuilds(const std::string& text)
{
    std::map<std::string,long long> builds;
    std::istringstream lines(text);
    std::string line;
    while(std::getline(lines,line))
    {
        std::istringstream fields(line);
        std::string comment,kind,asset,extra;long long epoch=0;
        if(fields>>comment>>kind>>asset>>epoch && comment=="#" && kind=="fulldex-build" && epoch>0 && !(fields>>extra))
            builds[asset]=epoch;
    }
    return builds;
}
bool IsNewerBuild(long long remoteBuild,long long currentBuild,const std::string& digest,
                  const std::string& installedDigest,long long uploaded,long long installedUpload)
{
    if(remoteBuild>0 && currentBuild>0)return remoteBuild>currentBuild;
    if(!installedDigest.empty() && !digest.empty())return digest!=installedDigest && uploaded>installedUpload;
    return uploaded>(installedUpload>0 ? installedUpload : currentBuild);
}
std::string BuildLabel(long long build)
{
    if(build<=0)return "UNKNOWN";
    const time_t t=(time_t)build;struct tm utc{};
    if(!gmtime_r(&t,&utc))return "UNKNOWN";
    char label[32];strftime(label,sizeof label,"%Y-%m-%d %H:%M UTC",&utc);return label;
}

std::map<std::string, std::string> ParseSums(const std::string& text)
{
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (at < text.size())
    {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(at, eol - at);
        at = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() < 66) continue;
        std::string hash = line.substr(0, 64);
        bool hex = true;
        for (char& c : hash)
        {
            c = (char)tolower((unsigned char)c);
            if (!isxdigit((unsigned char)c)) hex = false;
        }
        if (!hex || (line[64] != ' ' && line[64] != '\t')) continue;
        size_t name = 65;
        while (name < line.size() && (line[name] == ' ' || line[name] == '\t')) name++;
        if (name < line.size() && line[name] == '*') name++; // (sha256sum's binary mark)
        if (name < line.size()) out[line.substr(name)] = hash;
    }
    return out;
}

long long ParseTime(const std::string& iso)
{
    int y, mo, d, h, mi, s;
    if (sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    // (Days since 1970 from a date: Howard Hinnant's days_from_civil.)
    y -= mo <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + (long long)doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

bool FindRelease(const std::string& json, const std::string& asset, const std::string& urlPrefix, Release& out,
                 std::string& error, bool includeBetas)
{
    Json root;
    JsonReader rd{ json.data(), json.data() + json.size() };
    if (!rd.Value(root, 0) || root.type != Json::Array) { error = "not a list of releases"; return false; }
    // Beta pre-releases may ship under a "-beta" asset name (e.g. nosplitnx-switch-beta.zip) so
    // older stable installs that did not filter prerelease flags never pull a beta by mistake.
    std::string betaAsset;
    if (includeBetas)
    {
        const size_t dot = asset.rfind('.');
        if (dot != std::string::npos) betaAsset = asset.substr(0, dot) + "-beta" + asset.substr(dot);
    }
    bool found = false;
    for (const Json& rel : root.items)
    {
        if (rel.type != Json::Object || rel.True("draft")) continue;
        const bool isPre = rel.True("prerelease");
        if (isPre && !includeBetas) continue;
        const Json* assets = rel.Get("assets");
        if (!assets || assets->type != Json::Array) continue;
        Release r;
        for (const Json& a : assets->items)
        {
            const std::string name = a.Str("name"), url = a.Str("browser_download_url");
            if (url.compare(0, urlPrefix.size(), urlPrefix) != 0) continue;
            const bool matchAsset = (name == asset) || (includeBetas && !betaAsset.empty() && name == betaAsset);
            if (matchAsset && a.Num("size") > 0)
            {
                r.asset = name;
                r.url = url;
                r.updated = ParseTime(a.Str("updated_at"));
            }
            else if (name == "SHA256SUMS.txt") r.sumsUrl = url;
        }
        if (r.url.empty() || (found && r.updated <= out.updated)) continue;
        r.page = rel.Str("html_url");
        r.tag = rel.Str("tag_name");
        r.prerelease = isPre;
        out = r;
        found = true;
    }
    if (!found) error = "no release has " + asset;
    return found;
}

// A rename, or where the two are on different drives, a copy and then the old one removed.
static void MoveAcross(const fs::path& from, const fs::path& to, std::error_code& ec)
{
    fs::rename(from, to, ec);
    if (!ec) return;
    std::error_code e2;
    if (!fs::exists(fs::symlink_status(from, e2))) return;
    ec.clear();
    if (fs::is_directory(fs::symlink_status(from, e2)))
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    else
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { fs::remove_all(to, e2); return; }
    fs::remove_all(from, e2);
}

bool SwapIn(const std::string& dir, const std::string& fresh, const std::string& backup, std::string& error)
{
    static const char* const keep[] = { "roms", "saves", "update", "settings.cfg", "game.log", "game-previous.log",
                                        "last-game.txt", "icons" };
    std::error_code ec;
    const fs::path d = fs::u8path(dir), f = fs::u8path(fresh), b = fs::u8path(backup);
    fs::remove_all(b, ec);
    fs::create_directories(b, ec);
    if (ec) { error = "cannot make " + backup + ": " + ec.message(); return false; }
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(f, ec))
    {
        const std::string name = e.path().filename().u8string();
        if (std::find_if(std::begin(keep), std::end(keep), [&](const char* k) { return name == k; }) == std::end(keep))
            names.push_back(name);
    }
    if (ec || names.empty()) { error = "nothing in " + fresh; return false; }
    // (What was done, to undo: moved out to the backup, and put in.)
    std::vector<std::string> movedOut, putIn;
    auto undo = [&]() {
        std::error_code e2;
        for (auto it = putIn.rbegin(); it != putIn.rend(); ++it) MoveAcross(d / fs::u8path(*it), f / fs::u8path(*it), e2);
        for (auto it = movedOut.rbegin(); it != movedOut.rend(); ++it) MoveAcross(b / fs::u8path(*it), d / fs::u8path(*it), e2);
    };
    for (const std::string& name : names)
    {
        const fs::path here = d / fs::u8path(name);
        if (fs::exists(fs::symlink_status(here, ec)))
        {
            MoveAcross(here, b / fs::u8path(name), ec);
            if (ec) { error = "cannot move " + name + " aside: " + ec.message(); undo(); return false; }
            movedOut.push_back(name);
        }
        MoveAcross(f / fs::u8path(name), here, ec);
        if (ec) { error = "cannot put " + name + " in place: " + ec.message(); undo(); return false; }
        putIn.push_back(name);
    }
    return true;
}

void Init(const std::string& programDir, const std::string& dataDir, const std::string& program)
{
    g_programDir = programDir;
    g_dataDir = dataDir;
    g_program = program;
    std::string sha;
    long long time = 0;
    Installed(sha, time);
    CleanUpdateArtifacts();
    std::lock_guard<std::mutex> l(S().lock);
    S().status.text = "CHECK";
}

void Check(bool quiet)
{
    Spawn([](bool q) { RunCheck(q); }, quiet);
}

bool IncludeBetas()
{
    const std::string cfg = ReadFile(SettingsPath());
    std::string v = ReadCfgValue(cfg, "update_channel");
    for (char& c : v) c = (char)tolower((unsigned char)c);
    return v == "beta";
}

void ResetCheck()
{
    std::lock_guard<std::mutex> l(S().lock);
    if (S().busy) return;
    S().status.state = State::Idle;
    S().status.text = "CHECK";
    S().status.page.clear();
    S().status.tag.clear();
    S().status.prerelease = false;
    S().status.canInstall = false;
    S().status.availableBuild = 0;
    S().status.serial++;
}

std::string Choose()
{
    Status s = Get();
    if (s.state == State::Checking || s.state == State::Downloading || s.state == State::Installed) return "";
    if (s.state == State::Available)
    {
        if (!s.canInstall) return s.page.empty() ? "https://github.com/IamCarron/NoSplitNX/releases" : s.page;
        Spawn([](bool) { RunInstall(); }, false);
        return "";
    }
    Check(false);
    return "";
}

Status Get()
{
    std::lock_guard<std::mutex> l(S().lock);
    return S().status;
}

void SetStatusCallback(StatusCallback cb, void* user)
{
    std::lock_guard<std::mutex> l(S().lock);
    S().cb = cb;
    S().cbUser = user;
}

void Stop()
{
    // Switch checks and installs finish on the caller's thread before returning.
    // There is no updater worker to join when leaving its game list.
}

bool RestartPending()
{
    std::lock_guard<std::mutex> l(S().lock);
    return !S().restart.empty();
}

void Restart()
{
    std::string path;
    {
        std::lock_guard<std::mutex> l(S().lock);
        path = S().restart;
    }
    if (path.empty()) return;
    fprintf(stderr, "[update] starting %s\n", path.c_str());
    fflush(stderr);
#if defined(__SWITCH__)
    // The loader starts the next .nro in this same process. Sockets left open mean that start
    // never comes (a black screen).
    Achievements::HttpStop(true);
    const std::string cmd = path.find(' ') == std::string::npos ? path : "\"" + path + "\"";
    const Result rc = envSetNextLoad(path.c_str(), cmd.c_str());
    if (R_FAILED(rc)) fprintf(stderr, "[update] envSetNextLoad: 0x%x\n", (unsigned)rc);
#else
    execl(path.c_str(), path.c_str(), (char*)nullptr);
#endif
}
}
