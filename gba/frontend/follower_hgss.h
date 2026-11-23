// FOLLOWING POKEMON for the GBA games: the party's lead walking behind the player, drawn by pk_wide.c's FOLLOWER
// (PokeDaisy's). Its sprites come from the player's own HeartGold or SoulSilver, whose following Pokemon cover all 493:
// a/0/8/1's textures named tsure_poke (eight frames of 32 x 32 or 64 x 64, 16 colours, a normal and a shiny palette),
// picked by overlay 1's table of species (six bytes each, the texture file first). Read once per species asked for,
// from the cart file itself (only the parts needed); nothing is bundled or written out.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace HgssFollow
{
using Bytes = std::vector<uint8_t>;
inline uint32_t U16(const uint8_t* p) { return p[0] | p[1] << 8; }
inline uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

class Source
{
public:
    // A HeartGold or SoulSilver cart at path (any language): true once its sprites are found.
    bool Open(const std::string& path)
    {
        Close();
        f = fopen(path.c_str(), "rb");
        if (!f) return false;
        Bytes h = Read(0, 0x200);
        if (h.size() < 0x200 || !(!memcmp(&h[0x0C], "IPK", 3) || !memcmp(&h[0x0C], "IPG", 3))) return Fail();
        fnt = U32(&h[0x40]); fntSize = U32(&h[0x44]); fat = U32(&h[0x48]); fatSize = U32(&h[0x4C]);
        ovt = U32(&h[0x50]); ovtSize = U32(&h[0x54]);
        if (fntSize > 0x400000) return Fail();
        fntData = Read(fnt, fntSize);
        if (fntData.size() != fntSize) return Fail();
        const long sprites = FileId("a/0/8/1");
        if (sprites < 0) return Fail();
        narc = Narc(ReadFile((uint32_t)sprites));
        table = Overlay1();
        if (narc.empty() || table.empty()) return Fail();
        return true;
    }
    void Close()
    {
        if (f) fclose(f);
        f = nullptr;
        narc.clear();
        table.clear();
    }
    ~Source() { Close(); }
    bool Ready() const { return !narc.empty() && !table.empty(); }

    // The lead's sprite for the follower: six frames (south, south stepping, north, north stepping, west, west stepping)
    // of 32 x 32 as GBA OBJ tiles (4 x 4 tiles each, 512 bytes), and its 16 colours. national: its National Pokedex
    // number (1-493). False when there is none, or a 64 x 64 one whose drawing is wider than 32 x 32 (the big ones).
    bool Mon(int national, bool shiny, uint8_t frames[6 * 512], uint8_t pal[32]) const
    {
        if (national < 1 || (size_t)national > table.size()) return false;
        const uint32_t file = table[national - 1] + Extra(national);
        if (file >= narc.size()) return false;
        const Bytes& b = narc[file];
        struct Tex { int num; size_t off; int w, h, fmt; };
        std::vector<Tex> texs;
        std::vector<std::pair<std::string, size_t>> pals;
        if (!Btx(b, texs, pals) || texs.size() < 8 || pals.empty() || pals[0].first.compare(0, 10, "tsure_poke")) return false;
        // (Their names end .1, .10, .11 ... .16: back, back stepping, front, front stepping, left, left stepping, right, right.)
        auto find = [&](int n) -> const Tex* {
            for (const Tex& t : texs) if (t.num == n) return &t;
            return nullptr;
        };
        static const int order[6] = { 11, 12, 1, 10, 13, 14 }; // (S, S', N, N', W, W')
        const size_t palOff = pals[shiny && pals.size() > 1 ? 1 : 0].second;
        if (palOff + 32 > b.size()) return false;
        memcpy(pal, &b[palOff], 32);
        pal[1] = (uint8_t)(pal[1] & 0x7F);
        for (int k = 0; k < 6; k++)
        {
            const Tex* t = find(order[k]);
            if (!t || t->fmt != 3 || (t->w != 32 && t->w != 64) || t->w != t->h) return false;
            const int w = t->w;
            if (t->off + (size_t)w * w / 2 > b.size()) return false;
            auto px = [&](int x, int y) { const uint8_t v = b[t->off + (size_t)(y * w + x) / 2]; return (x & 1) ? v >> 4 : v & 15; };
            // A 64 x 64 one: its bottom middle 32 x 32, only if nothing of it is outside that.
            int ox = 0, oy = 0;
            if (w == 64)
            {
                ox = 16; oy = 32;
                for (int y = 0; y < 64; y++)
                    for (int x = 0; x < 64; x++)
                        if ((x < ox || x >= ox + 32 || y < oy) && px(x, y)) return false;
            }
            uint8_t* out = frames + k * 512;
            for (int ty = 0; ty < 4; ty++)
                for (int tx = 0; tx < 4; tx++)
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x += 2)
                        {
                            const int sx = ox + tx * 8 + x, sy = oy + ty * 8 + y;
                            out[(ty * 4 + tx) * 32 + y * 4 + x / 2] = (uint8_t)(px(sx, sy) | px(sx + 1, sy) << 4);
                        }
        }
        return true;
    }

private:
    // The table's file is the first of a species' only up to Unown: the files of some species' other forms and females
    // come in between later on, and the table counts only some of them (worked out against HeartGold's own files).
    static int Extra(int s)
    {
        static const struct { int to, extra; } runs[] = {
            { 201, 0 }, { 202, 27 }, { 208, 28 }, { 214, 29 }, { 229, 30 }, { 235, 29 }, { 241, 28 }, { 386, 27 }, { 412, 30 },
            { 413, 32 }, { 415, 34 }, { 422, 35 }, { 423, 36 }, { 443, 37 }, { 444, 38 }, { 445, 39 }, { 450, 40 }, { 479, 41 },
            { 480, 45 }, { 481, 44 }, { 487, 43 }, { 492, 44 }, { 493, 45 } };
        for (const auto& r : runs) if (s <= r.to) return r.extra;
        return 0;
    }
    FILE* f = nullptr;
    uint32_t fnt = 0, fntSize = 0, fat = 0, fatSize = 0, ovt = 0, ovtSize = 0;
    Bytes fntData;
    std::vector<Bytes> narc;
    std::vector<uint32_t> table; // species - 1 -> a/0/8/1 member

    bool Fail() { Close(); return false; }
    Bytes Read(uint32_t at, uint32_t n) const
    {
        Bytes out(n);
        if (!f || fseek(f, (long)at, SEEK_SET) || fread(out.data(), 1, n, f) != n) return {};
        return out;
    }
    Bytes ReadFile(uint32_t id) const
    {
        if ((size_t)id * 8 + 8 > fatSize) return {};
        const Bytes e = Read(fat + id * 8, 8);
        if (e.size() != 8) return {};
        const uint32_t a = U32(&e[0]), z = U32(&e[4]);
        return z > a && z - a < (32u << 20) ? Read(a, z - a) : Bytes{};
    }
    long FileId(std::string path) const
    {
        uint32_t dir = 0xF000;
        for (int depth = 0; depth < 32; depth++)
        {
            const size_t entry = (dir & 0xFFF) * 8, slash = path.find('/');
            if (entry + 8 > fntData.size()) return -1;
            size_t pos = U32(&fntData[entry]);
            uint32_t id = U16(&fntData[entry + 4]);
            bool down = false;
            while (pos < fntData.size())
            {
                const int tag = fntData[pos++];
                if (!tag) break;
                const size_t n = tag & 127;
                if (pos + n > fntData.size()) return -1;
                const std::string name((const char*)&fntData[pos], n);
                pos += n;
                if (tag & 128)
                {
                    if (pos + 2 > fntData.size()) return -1;
                    const uint32_t sub = U16(&fntData[pos]);
                    pos += 2;
                    if (slash != std::string::npos && name == path.substr(0, slash)) { dir = sub; path = path.substr(slash + 1); down = true; break; }
                }
                else
                {
                    if (slash == std::string::npos && name == path) return id;
                    id++;
                }
            }
            if (!down) return -1;
        }
        return -1;
    }
    static std::vector<Bytes> Narc(const Bytes& d)
    {
        std::vector<Bytes> out;
        if (d.size() < 16 || memcmp(d.data(), "NARC", 4)) return out;
        size_t pos = U16(&d[12]), fatAt = 0, img = 0, imgSize = 0;
        uint32_t count = 0;
        for (int i = 0; i < 16 && pos + 8 <= d.size(); i++)
        {
            const uint32_t len = U32(&d[pos + 4]);
            if (len < 8 || pos + len > d.size()) return {};
            if (!memcmp(&d[pos], "BTAF", 4)) { count = U16(&d[pos + 8]); fatAt = pos + 12; }
            if (!memcmp(&d[pos], "GMIF", 4)) { img = pos + 8; imgSize = len - 8; }
            pos += len;
        }
        if (!fatAt || !img) return {};
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t a = U32(&d[fatAt + i * 8]), z = U32(&d[fatAt + i * 8 + 4]);
            if (z < a || z > imgSize) return {};
            out.emplace_back(d.begin() + img + a, d.begin() + img + z);
        }
        return out;
    }
    static Bytes UnpackBlz(const Bytes& in)
    {
        if (in.size() < 8) return {};
        const size_t n = in.size(), word = U32(&in[n - 8]), extra = U32(&in[n - 4]);
        if (!extra) return in;
        const size_t header = word >> 24, packed = word & 0xFFFFFF;
        if (header < 8 || packed < header || packed > n || extra > 0x400000) return {};
        Bytes out = in;
        out.resize(n + extra);
        const size_t start = n - packed;
        size_t ip = n - header, op = out.size();
        while (ip > start && op > start)
        {
            unsigned flags = out[--ip];
            for (int bit = 0; bit < 8 && ip > start && op > start; bit++, flags <<= 1)
            {
                if (flags & 128)
                {
                    if (ip - start < 2) return {};
                    ip -= 2;
                    const size_t dist = (((out[ip + 1] & 15) << 8) | out[ip]) + 3, len = (out[ip + 1] >> 4) + 3;
                    if (len > op - start) return {};
                    for (size_t i = 0; i < len; i++) { --op; if (dist >= out.size() - op) return {}; out[op] = out[op + dist]; }
                }
                else { if (op <= start) return {}; out[--op] = out[--ip]; }
            }
        }
        return op == start ? out : Bytes{};
    }
    // Overlay 1's species table, six bytes an entry: found by its first eight files (Venusaur has a second, female one).
    std::vector<uint32_t> Overlay1() const
    {
        std::vector<uint32_t> out;
        if (ovtSize < 64) return out;
        const Bytes e = Read(ovt + 32, 32);
        if (e.size() != 32) return out;
        Bytes ov = ReadFile(U32(&e[24]));
        if (U32(&e[28]) & 0x01000000) ov = UnpackBlz(ov);
        // (Read at the wrong two bytes, the entry before's last word and this one's first match as well: the table
        // whose first file comes first is the one.)
        size_t best = 0;
        uint32_t bestV = ~0u;
        for (size_t o = 0; o + 6 * 494 <= ov.size(); o += 2)
        {
            const uint32_t v = U16(&ov[o]);
            static const uint32_t step[8] = { 0, 1, 2, 4, 5, 6, 7, 8 };
            bool match = v >= 100 && v <= 2000 && v < bestV;
            for (int k = 1; k < 8 && match; k++) match = U16(&ov[o + k * 6]) == v + step[k];
            for (int s = 1; s < 493 && match; s++) match = U16(&ov[o + s * 6]) > U16(&ov[o + (s - 1) * 6]) && U16(&ov[o + s * 6]) < v + 1000;
            if (match) { best = o; bestV = v; }
        }
        if (bestV != ~0u)
            for (int s = 0; s < 493; s++) out.push_back(U16(&ov[best + s * 6]));
        return out;
    }
    template <class T>
    static bool Dict(const Bytes& b, size_t o, std::vector<std::pair<std::string, T>>& out, T (*get)(const uint8_t*))
    {
        if (o + 4 > b.size()) return false;
        const int num = b[o + 1];
        size_t p = o + 4 + 8 + 4 * num;
        if (p + 4 > b.size()) return false;
        const size_t unit = U16(&b[p]);
        p += 4;
        if (p + unit * num + 16 * num > b.size()) return false;
        for (int i = 0; i < num; i++)
        {
            const uint8_t* nm = &b[p + unit * num + 16 * i];
            out.push_back({ std::string((const char*)nm, strnlen((const char*)nm, 16)), get(&b[p + unit * i]) });
        }
        return true;
    }
    template <class Tex>
    static bool Btx(const Bytes& b, std::vector<Tex>& texs, std::vector<std::pair<std::string, size_t>>& pals)
    {
        if (b.size() < 0x14 || memcmp(b.data(), "BTX0", 4)) return false;
        const size_t t = U32(&b[0x10]);
        if (t + 0x3C > b.size() || memcmp(&b[t], "TEX0", 4)) return false;
        const size_t texInfo = t + U16(&b[t + 0x0E]), texData = t + U32(&b[t + 0x14]);
        const size_t palInfo = t + U32(&b[t + 0x34]), palData = t + U32(&b[t + 0x38]);
        std::vector<std::pair<std::string, uint32_t>> ti;
        std::vector<std::pair<std::string, uint32_t>> pi;
        if (!Dict<uint32_t>(b, texInfo, ti, [](const uint8_t* p) { return U32(p); })) return false;
        if (!Dict<uint32_t>(b, palInfo, pi, [](const uint8_t* p) { return U16(p); })) return false;
        for (auto& x : ti)
        {
            const size_t dot = x.first.rfind('.');
            const uint32_t p1 = x.second;
            texs.push_back({ dot == std::string::npos ? -1 : atoi(x.first.c_str() + dot + 1), texData + ((p1 & 0xFFFF) << 3),
                             8 << ((p1 >> 20) & 7), 8 << ((p1 >> 23) & 7), (int)((p1 >> 26) & 7) });
        }
        for (auto& x : pi) pals.push_back({ x.first, palData + ((size_t)x.second << 3) });
        return true;
    }
};
} // namespace HgssFollow
