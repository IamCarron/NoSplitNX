#pragma once
// ROM hacks: any hack of a game Fulldex plays, as a ROM already patched or as a patch file.
//  - Clean or modified: a DS ROM is checked against the clean releases (their sizes and CRC-32s,
//    from No-Intro's DS list), so that a hack gets a save of its own instead of the clean game's.
//  - Patch files (IPS, UPS, BPS, and xdelta/VCDIFF with xdelta's LZMA compression) are put on the
//    player's own clean ROM, found among their ROMs by the patch's own checks (UPS and BPS: the
//    source's CRC-32; xdelta: the checksum of the first window it makes).
// Nothing of the games is in here: only the clean ROMs' sizes and checksums.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <system_error>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// The LZMA2 data of xdelta's compressed sections (romhacks_lzma.c, on the LZMA SDK). xdelta keeps
// one stream for each kind of section (data, instructions, addresses) from window to window: open
// one with the dictionary's size (the xz block header's property), then each section's part in
// turn, which must give exactly outSize bytes.
extern "C" void* romhacks_lzma2_open(unsigned char prop);
extern "C" int romhacks_lzma2_next(void* stream, const unsigned char* in, size_t inSize, unsigned char* out, size_t outSize);
extern "C" void romhacks_lzma2_close(void* stream);

namespace RomHacks
{

// ---------------------------------------------------------------- checksums

struct CrcTables
{
    uint32_t t[8][256];
    CrcTables()
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[0][i] = c;
        }
        for (int k = 1; k < 8; k++)
            for (uint32_t i = 0; i < 256; i++) t[k][i] = (t[k - 1][i] >> 8) ^ t[0][t[k - 1][i] & 0xFF];
    }
};
inline const CrcTables& Tables()
{
    static const CrcTables t;
    return t;
}
// A CRC-32 continued over n more bytes (start with 0).
inline uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n)
{
    const auto& T = Tables().t;
    crc = ~crc;
    while (n >= 8)
    {
        const uint32_t a = crc ^ (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
        const uint32_t b = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
        crc = T[7][a & 0xFF] ^ T[6][a >> 8 & 0xFF] ^ T[5][a >> 16 & 0xFF] ^ T[4][a >> 24] ^ T[3][b & 0xFF] ^
              T[2][b >> 8 & 0xFF] ^ T[1][b >> 16 & 0xFF] ^ T[0][b >> 24];
        p += 8;
        n -= 8;
    }
    while (n--) crc = T[0][(crc ^ *p++) & 0xFF] ^ crc >> 8;
    return ~crc;
}
inline uint32_t Adler32(const uint8_t* p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n)
    {
        size_t k = n < 5552 ? n : 5552;
        n -= k;
        while (k--)
        {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}

// ---------------------------------------------------------------- files

inline std::string Lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
inline bool EndsWithCi(const std::string& s, const char* end)
{
    const size_t n = strlen(end);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == end;
}
inline std::string FileName(const std::string& path)
{
    const size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}
inline std::string Stem(const std::string& path)
{
    const std::string n = FileName(path);
    const size_t dot = n.rfind('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}
inline bool Stat(const std::string& path, uint64_t& size, long long& mtime)
{
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    size = (uint64_t)st.st_size;
    mtime = (long long)st.st_mtime;
    return true;
}
inline bool Exists(const std::string& path)
{
    uint64_t s;
    long long m;
    return Stat(path, s, m);
}
inline void MakeDir(const std::string& path)
{
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}
inline bool ReadAll(const std::string& path, std::vector<uint8_t>& out, uint64_t limit)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    bool ok = size >= 0 && (uint64_t)size <= limit && fseek(f, 0, SEEK_SET) == 0;
    if (ok)
    {
        out.resize((size_t)size);
        ok = size == 0 || fread(out.data(), 1, out.size(), f) == out.size();
    }
    fclose(f);
    return ok;
}
inline bool CopyFile(const std::string& from, const std::string& to)
{
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    const std::string tmp = to + ".tmp";
    FILE* out = fopen(tmp.c_str(), "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }
    std::vector<uint8_t> buf(1 << 16);
    bool ok = true;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), in)) > 0) ok &= fwrite(buf.data(), 1, n, out) == n;
    ok &= !ferror(in);
    fclose(in);
    ok &= fclose(out) == 0;
    if (ok)
    {
        remove(to.c_str());
        ok = rename(tmp.c_str(), to.c_str()) == 0;
    }
    if (!ok) remove(tmp.c_str());
    return ok;
}

// What patches read from: the clean ROM, in memory (GBA) or in a file read in blocks (DS: up to
// 512 MB, as much as the Switch can't hold twice).
class Reader
{
public:
    virtual ~Reader() {}
    virtual uint64_t Size() const = 0;
    virtual bool Read(uint64_t off, uint8_t* dst, size_t n) = 0; // (false past the end)
};
class MemReader : public Reader
{
public:
    explicit MemReader(const std::vector<uint8_t>& d) : data(d) {}
    uint64_t Size() const override { return data.size(); }
    bool Read(uint64_t off, uint8_t* dst, size_t n) override
    {
        if (off > data.size() || n > data.size() - off) return false;
        if (n) memcpy(dst, data.data() + off, n);
        return true;
    }
private:
    const std::vector<uint8_t>& data;
};
class FileReader : public Reader
{
public:
    ~FileReader() { if (f) fclose(f); }
    bool Open(const std::string& path)
    {
        f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        const long s = ftell(f);
        if (s < 0) return false;
        size = (uint64_t)s;
        return true;
    }
    uint64_t Size() const override { return size; }
    bool Read(uint64_t off, uint8_t* dst, size_t n) override
    {
        if (off > size || n > size - off) return false;
        while (n)
        {
            const uint64_t block = off / kBlock;
            const uint8_t* b = Block(block);
            if (!b) return false;
            const size_t at = (size_t)(off - block * kBlock);
            const size_t k = std::min(n, (size_t)kBlock - at);
            memcpy(dst, b + at, k);
            dst += k;
            off += k;
            n -= k;
        }
        return true;
    }
private:
    static constexpr uint64_t kBlock = 1 << 20;
    struct Slot
    {
        uint64_t block = ~0ull;
        uint64_t used = 0;
        std::vector<uint8_t> data;
    };
    const uint8_t* Block(uint64_t block)
    {
        Slot* oldest = &slots[0];
        for (Slot& s : slots)
        {
            if (s.block == block)
            {
                s.used = ++clock;
                return s.data.data();
            }
            if (s.used < oldest->used) oldest = &s;
        }
        oldest->data.resize(kBlock);
        const uint64_t at = block * kBlock;
        const size_t want = (size_t)std::min<uint64_t>(kBlock, size - at);
        if (fseek(f, (long)at, SEEK_SET) != 0 || fread(oldest->data.data(), 1, want, f) != want) return nullptr;
        oldest->block = block;
        oldest->used = ++clock;
        return oldest->data.data();
    }
    FILE* f = nullptr;
    uint64_t size = 0, clock = 0;
    Slot slots[8];
};

// What patches write: the hack, made from the start to the end (and read back where a patch
// copies from what it has made already), with its CRC-32 kept as it goes.
class Writer
{
public:
    virtual ~Writer() {}
    uint64_t Size() const { return size; }
    uint32_t Crc() const { return crc; }
    bool Write(const uint8_t* p, size_t n)
    {
        if (!n) return true;
        if (!Put(p, n)) return false;
        crc = Crc32(crc, p, n);
        size += n;
        return true;
    }
    bool Fill(uint8_t v, uint64_t n)
    {
        uint8_t buf[4096];
        memset(buf, v, sizeof buf);
        while (n)
        {
            const size_t k = (size_t)std::min<uint64_t>(n, sizeof buf);
            if (!Write(buf, k)) return false;
            n -= k;
        }
        return true;
    }
    virtual bool ReadBack(uint64_t off, uint8_t* dst, size_t n) = 0;
protected:
    virtual bool Put(const uint8_t* p, size_t n) = 0;
    uint64_t size = 0;
    uint32_t crc = 0;
};
class MemWriter : public Writer
{
public:
    std::vector<uint8_t> data;
    bool ReadBack(uint64_t off, uint8_t* dst, size_t n) override
    {
        if (off > data.size() || n > data.size() - off) return false;
        if (n) memcpy(dst, data.data() + off, n);
        return true;
    }
protected:
    bool Put(const uint8_t* p, size_t n) override
    {
        data.insert(data.end(), p, p + n);
        return true;
    }
};
// (Kept in memory up to 4 MB at a time, then written: reading back the part not yet written
// comes from memory.)
class FileWriter : public Writer
{
public:
    ~FileWriter() { if (f) fclose(f); }
    bool Open(const std::string& path)
    {
        f = fopen(path.c_str(), "w+b");
        return f != nullptr;
    }
    bool Close()
    {
        if (!f) return false;
        const bool ok = Flush();
        const bool closed = fclose(f) == 0;
        f = nullptr;
        return ok && closed;
    }
    bool ReadBack(uint64_t off, uint8_t* dst, size_t n) override
    {
        if (off > size || n > size - off) return false;
        if (off < flushed)
        {
            const size_t k = (size_t)std::min<uint64_t>(n, flushed - off);
            if (fseek(f, (long)off, SEEK_SET) != 0 || fread(dst, 1, k, f) != k) return false;
            dst += k;
            off += k;
            n -= k;
        }
        if (n) memcpy(dst, pending.data() + (off - flushed), n);
        return true;
    }
protected:
    bool Put(const uint8_t* p, size_t n) override
    {
        pending.insert(pending.end(), p, p + n);
        return pending.size() < (4u << 20) || Flush();
    }
private:
    bool Flush()
    {
        if (pending.empty()) return true;
        if (fseek(f, (long)flushed, SEEK_SET) != 0 || fwrite(pending.data(), 1, pending.size(), f) != pending.size()) return false;
        flushed += pending.size();
        pending.clear();
        return true;
    }
    FILE* f = nullptr;
    uint64_t flushed = 0;
    std::vector<uint8_t> pending;
};

// ---------------------------------------------------------------- patch formats

enum class Format { None, Ips, Ups, Bps, Vcdiff };
inline Format FormatOf(const uint8_t* p, size_t n)
{
    if (n >= 8 && !memcmp(p, "PATCH", 5)) return Format::Ips;
    if (n >= 18 && !memcmp(p, "UPS1", 4)) return Format::Ups;
    if (n >= 19 && !memcmp(p, "BPS1", 4)) return Format::Bps;
    if (n >= 5 && p[0] == 0xD6 && p[1] == 0xC3 && p[2] == 0xC4 && p[3] == 0) return Format::Vcdiff;
    return Format::None;
}
// The file names a patch can have (the format is told by its contents).
inline bool IsPatchName(const std::string& name)
{
    for (const char* e : { ".ips", ".ups", ".bps", ".xdelta", ".xdelta3", ".vcdiff", ".delta" })
        if (EndsWithCi(name, e)) return true;
    return false;
}

// What a patch says about the ROM it goes on and the one it makes (UPS and BPS say both).
struct Info
{
    Format format = Format::None;
    bool knowsSource = false;
    uint64_t sourceSize = 0, targetSize = 0;
    uint32_t sourceCrc = 0, targetCrc = 0;
};

// UPS's and BPS's numbers.
inline bool Number(const uint8_t* p, size_t n, size_t& i, uint64_t& out)
{
    out = 0;
    uint64_t shift = 1;
    for (int k = 0; k < 10; k++)
    {
        if (i >= n) return false;
        const uint8_t x = p[i++];
        out += (x & 0x7F) * shift;
        if (x & 0x80) return true;
        shift <<= 7;
        out += shift;
    }
    return false;
}
inline uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

inline bool ReadInfo(const std::vector<uint8_t>& patch, Info& info)
{
    const uint8_t* p = patch.data();
    const size_t n = patch.size();
    info = Info();
    info.format = FormatOf(p, n);
    if (info.format == Format::Ups || info.format == Format::Bps)
    {
        size_t i = 4;
        if (!Number(p, n, i, info.sourceSize) || !Number(p, n, i, info.targetSize)) return false;
        info.knowsSource = true;
        info.sourceCrc = Le32(p + n - 12);
        info.targetCrc = Le32(p + n - 8);
        // (The patch's own CRC-32 is its last four bytes, over everything before them.)
        if (Crc32(0, p, n - 4) != Le32(p + n - 4)) return false;
    }
    return info.format != Format::None;
}

// IPS: records of bytes at offsets, put on a copy of the ROM. (Up to 16 MB: a GBA ROM.)
inline bool ApplyIps(const std::vector<uint8_t>& patch, Reader& src, Writer& out, std::string& error)
{
    const uint8_t* p = patch.data();
    const size_t n = patch.size();
    if (src.Size() > (64u << 20))
    {
        error = "AN IPS PATCH IS FOR A SMALLER ROM";
        return false;
    }
    std::vector<uint8_t> rom((size_t)src.Size());
    if (!src.Read(0, rom.data(), rom.size()))
    {
        error = "CAN'T READ THE ROM";
        return false;
    }
    size_t i = 5;
    bool done = false;
    while (i + 3 <= n)
    {
        if (!memcmp(p + i, "EOF", 3) && (i + 3 == n || i + 6 == n))
        {
            if (i + 6 == n) rom.resize((size_t)(p[i + 3] << 16 | p[i + 4] << 8 | p[i + 5])); // (Lunar IPS: cut to this size)
            done = true;
            break;
        }
        const size_t off = (size_t)(p[i] << 16 | p[i + 1] << 8 | p[i + 2]);
        i += 3;
        if (i + 2 > n) break;
        size_t len = (size_t)(p[i] << 8 | p[i + 1]);
        i += 2;
        if (len)
        {
            if (i + len > n) break;
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memcpy(rom.data() + off, p + i, len);
            i += len;
        }
        else
        {
            if (i + 3 > n) break;
            len = (size_t)(p[i] << 8 | p[i + 1]);
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memset(rom.data() + off, p[i + 2], len);
            i += 3;
        }
    }
    if (!done)
    {
        error = "THE IPS PATCH IS CUT SHORT";
        return false;
    }
    if (!out.Write(rom.data(), rom.size()))
    {
        error = "CAN'T WRITE THE PATCHED ROM";
        return false;
    }
    return true;
}

// UPS: the ROM with bytes XORed, in order from the start.
inline bool ApplyUps(const std::vector<uint8_t>& patch, Reader& src, Writer& out, std::string& error)
{
    Info info;
    if (!ReadInfo(patch, info))
    {
        error = "THE UPS PATCH IS DAMAGED";
        return false;
    }
    const uint8_t* p = patch.data();
    const size_t end = patch.size() - 12;
    size_t i = 4;
    uint64_t skip;
    Number(p, end, i, skip);
    Number(p, end, i, skip);
    auto source = [&](uint64_t at, uint8_t* dst, size_t k) {
        // (Past the end of the source: zeros.)
        const uint64_t have = at < src.Size() ? std::min<uint64_t>(k, src.Size() - at) : 0;
        if (have && !src.Read(at, dst, (size_t)have)) return false;
        memset(dst + have, 0, k - (size_t)have);
        return true;
    };
    std::vector<uint8_t> buf(1 << 16);
    auto copyTo = [&](uint64_t to) {
        while (out.Size() < to)
        {
            const size_t k = (size_t)std::min<uint64_t>(buf.size(), to - out.Size());
            if (!source(out.Size(), buf.data(), k) || !out.Write(buf.data(), k)) return false;
        }
        return true;
    };
    while (i < end)
    {
        uint64_t gap;
        if (!Number(p, end, i, gap) || !copyTo(out.Size() + gap))
        {
            error = "THE UPS PATCH IS DAMAGED";
            return false;
        }
        while (true)
        {
            if (i >= end)
            {
                error = "THE UPS PATCH IS DAMAGED";
                return false;
            }
            const uint8_t x = p[i++];
            if (out.Size() >= info.targetSize)
            {
                // (Past the end only the run's closing zero: it changes nothing.)
                if (x)
                {
                    error = "THE UPS PATCH IS DAMAGED";
                    return false;
                }
                break;
            }
            uint8_t s;
            if (!source(out.Size(), &s, 1)) return false;
            s ^= x;
            if (!out.Write(&s, 1)) return false;
            if (!x) break;
        }
    }
    if (!copyTo(info.targetSize) || out.Size() != info.targetSize)
    {
        error = "CAN'T WRITE THE PATCHED ROM";
        return false;
    }
    if (out.Crc() != info.targetCrc)
    {
        error = "THE PATCHED ROM ISN'T RIGHT (UPS CHECKSUM)";
        return false;
    }
    return true;
}

// BPS: the hack made from the start, from the ROM, the patch and what it has made already.
inline bool ApplyBps(const std::vector<uint8_t>& patch, Reader& src, Writer& out, std::string& error)
{
    Info info;
    if (!ReadInfo(patch, info))
    {
        error = "THE BPS PATCH IS DAMAGED";
        return false;
    }
    const uint8_t* p = patch.data();
    const size_t end = patch.size() - 12;
    size_t i = 4;
    uint64_t v, meta;
    Number(p, end, i, v);
    Number(p, end, i, v);
    if (!Number(p, end, i, meta) || meta > end - i)
    {
        error = "THE BPS PATCH IS DAMAGED";
        return false;
    }
    i += (size_t)meta;
    uint64_t sourceRel = 0, targetRel = 0;
    std::vector<uint8_t> buf(1 << 16);
    auto bad = [&]() {
        error = "THE BPS PATCH IS DAMAGED";
        return false;
    };
    while (i < end)
    {
        uint64_t data;
        if (!Number(p, end, i, data)) return bad();
        uint64_t len = (data >> 2) + 1;
        if (out.Size() + len > info.targetSize) return bad();
        switch (data & 3)
        {
        case 0: // SourceRead: the ROM's bytes at the same place
            while (len)
            {
                const size_t k = (size_t)std::min<uint64_t>(len, buf.size());
                if (!src.Read(out.Size(), buf.data(), k) || !out.Write(buf.data(), k)) return bad();
                len -= k;
            }
            break;
        case 1: // TargetRead: the patch's bytes
            if (len > end - i || !out.Write(p + i, (size_t)len)) return bad();
            i += (size_t)len;
            break;
        case 2: // SourceCopy: the ROM's bytes from elsewhere
        case 3: // TargetCopy: bytes made already (which can be the ones being made: byte by byte)
        {
            uint64_t d;
            if (!Number(p, end, i, d)) return bad();
            uint64_t& rel = (data & 3) == 2 ? sourceRel : targetRel;
            rel = d & 1 ? rel - (d >> 1) : rel + (d >> 1);
            while (len)
            {
                size_t k = (size_t)std::min<uint64_t>(len, buf.size());
                if ((data & 3) == 2)
                {
                    if (!src.Read(rel, buf.data(), k)) return bad();
                }
                else
                {
                    if (rel >= out.Size()) return bad();
                    k = (size_t)std::min<uint64_t>(k, out.Size() - rel);
                    if (!out.ReadBack(rel, buf.data(), k)) return bad();
                }
                if (!out.Write(buf.data(), k)) return bad();
                rel += k;
                len -= k;
            }
            break;
        }
        }
    }
    if (out.Size() != info.targetSize) return bad();
    if (out.Crc() != info.targetCrc)
    {
        error = "THE PATCHED ROM ISN'T RIGHT (BPS CHECKSUM)";
        return false;
    }
    return true;
}

// VCDIFF (RFC 3284) as xdelta makes it: windows of the hack, each made from part of the ROM (or
// of the hack made already), the patch's bytes and the window itself, with xdelta's checksum
// of each window and its sections compressed with LZMA (xdelta 3.0.11 and later, by default).
// maxWindows > 0: only that many windows (enough to tell whether a ROM is the right one).
inline bool ApplyVcdiff(const std::vector<uint8_t>& patch, Reader& src, Writer& out, std::string& error, int maxWindows = 0)
{
    const uint8_t* p = patch.data();
    const size_t n = patch.size();
    size_t i = 4;
    auto bad = [&](const char* why = "THE XDELTA PATCH IS DAMAGED") {
        error = why;
        return false;
    };
    auto num = [&](const uint8_t* q, size_t qn, size_t& at, uint64_t& v) {
        v = 0;
        for (int k = 0; k < 10; k++)
        {
            if (at >= qn) return false;
            const uint8_t x = q[at++];
            v = v << 7 | (x & 0x7F);
            if (!(x & 0x80)) return true;
        }
        return false;
    };
    if (n < 5) return bad();
    const uint8_t hdr = p[i++];
    int secondary = 0;
    if (hdr & 1)
    {
        if (i >= n) return bad();
        secondary = p[i++];
    }
    if (hdr & 2) return bad("THE XDELTA PATCH HAS A CODE TABLE OF ITS OWN: NOT SUPPORTED");
    if (hdr & 4)
    {
        uint64_t len;
        if (!num(p, n, i, len) || len > n - i) return bad();
        i += (size_t)len; // (xdelta's file names)
    }
    // The default code table: for each instruction byte, up to two instructions (type, size, mode).
    struct Op { uint8_t type, size, mode; };
    struct Entry { Op a, b; };
    static const std::vector<Entry> table = [] {
        enum { NOOP, ADD, RUN, COPY };
        std::vector<Entry> t;
        t.push_back({ { RUN, 0, 0 }, { NOOP, 0, 0 } });
        for (int s = 0; s <= 17; s++) t.push_back({ { ADD, (uint8_t)s, 0 }, { NOOP, 0, 0 } });
        for (int m = 0; m <= 8; m++)
        {
            t.push_back({ { COPY, 0, (uint8_t)m }, { NOOP, 0, 0 } });
            for (int s = 4; s <= 18; s++) t.push_back({ { COPY, (uint8_t)s, (uint8_t)m }, { NOOP, 0, 0 } });
        }
        for (int m = 0; m <= 5; m++)
            for (int a = 1; a <= 4; a++)
                for (int s = 4; s <= 6; s++) t.push_back({ { ADD, (uint8_t)a, 0 }, { COPY, (uint8_t)s, (uint8_t)m } });
        for (int m = 6; m <= 8; m++)
            for (int a = 1; a <= 4; a++) t.push_back({ { ADD, (uint8_t)a, 0 }, { COPY, 4, (uint8_t)m } });
        for (int m = 0; m <= 8; m++) t.push_back({ { COPY, 4, (uint8_t)m }, { ADD, 1, 0 } });
        return t;
    }();
    int windows = 0;
    std::vector<uint8_t> win, segment, sections[3];
    struct CloseLzma
    {
        void operator()(void* x) const { romhacks_lzma2_close(x); }
    };
    std::unique_ptr<void, CloseLzma> lzma[3];
    while (i < n)
    {
        if (maxWindows > 0 && windows >= maxWindows) return true;
        const uint8_t wi = p[i++];
        uint64_t segLen = 0, segPos = 0;
        if (wi & 3)
        {
            if (!num(p, n, i, segLen) || !num(p, n, i, segPos)) return bad();
        }
        uint64_t encLen, tgtLen, lens[3];
        if (!num(p, n, i, encLen) || encLen > n - i) return bad();
        const size_t encEnd = i + (size_t)encLen;
        if (!num(p, encEnd, i, tgtLen) || i >= encEnd) return bad();
        const uint8_t delta = p[i++];
        for (uint64_t& l : lens)
            if (!num(p, encEnd, i, l)) return bad();
        uint32_t adler = 0;
        const bool hasAdler = wi & 4;
        if (hasAdler)
        {
            if (i + 4 > encEnd) return bad();
            adler = (uint32_t)p[i] << 24 | p[i + 1] << 16 | p[i + 2] << 8 | p[i + 3];
            i += 4;
        }
        if (tgtLen > (256u << 20) || segLen > (1ull << 32)) return bad("THE XDELTA PATCH'S WINDOWS ARE TOO LARGE");
        // The sections: data, instructions, addresses (each maybe compressed).
        for (int s = 0; s < 3; s++)
        {
            if (lens[s] > encEnd - i) return bad();
            const uint8_t* q = p + i;
            const size_t qn = (size_t)lens[s];
            i += qn;
            if (!(delta & (1 << s)))
            {
                sections[s].assign(q, q + qn);
                continue;
            }
            if (secondary != 2) return bad("THE XDELTA PATCH USES DJW OR FGK COMPRESSION: MAKE IT AGAIN WITHOUT -S");
            size_t at = 0;
            uint64_t size;
            if (!num(q, qn, at, size) || size > (256u << 20)) return bad();
            // The first is the start of an xz stream with one LZMA2 block (xdelta never ends it),
            // the next windows' carry on from it.
            size_t b = at;
            if (qn - at >= 12 + 8 && !memcmp(q + at, "\xFD" "7zXZ\0", 6))
            {
                b = at + 12;
                const size_t bhSize = ((size_t)q[b] + 1) * 4;
                if (b + bhSize > qn) return bad();
                const uint8_t flags = q[b + 1];
                size_t f = b + 2;
                uint64_t skip;
                if (flags & 3) return bad("THE XDELTA PATCH'S COMPRESSION ISN'T SUPPORTED");
                auto xzNum = [&](uint64_t& v) {
                    v = 0;
                    for (int k = 0; k < 9; k++)
                    {
                        if (f >= b + bhSize) return false;
                        const uint8_t x = q[f++];
                        v |= (uint64_t)(x & 0x7F) << (7 * k);
                        if (!(x & 0x80)) return true;
                    }
                    return false;
                };
                if ((flags & 0x40) && !xzNum(skip)) return bad();
                if ((flags & 0x80) && !xzNum(skip)) return bad();
                uint64_t filter, propSize;
                if (!xzNum(filter) || filter != 0x21 || !xzNum(propSize) || propSize != 1 || f >= b + bhSize)
                    return bad("THE XDELTA PATCH'S COMPRESSION ISN'T SUPPORTED");
                lzma[s].reset(romhacks_lzma2_open(q[f]));
                if (!lzma[s]) return bad("NOT ENOUGH MEMORY FOR THE PATCH");
                b += bhSize;
            }
            else if (!lzma[s])
                return bad();
            sections[s].resize((size_t)size);
            if (!romhacks_lzma2_next(lzma[s].get(), q + b, qn - b, sections[s].data(), sections[s].size())) return bad();
        }
        i = encEnd;
        // The part of the ROM (or of the hack so far) this window copies from.
        segment.resize((size_t)segLen);
        if (wi & 1)
        {
            if (!src.Read(segPos, segment.data(), segment.size())) return bad("THE XDELTA PATCH IS FOR ANOTHER ROM");
        }
        else if (wi & 2)
        {
            if (!out.ReadBack(segPos, segment.data(), segment.size())) return bad();
        }
        win.clear();
        win.reserve((size_t)tgtLen);
        const std::vector<uint8_t>& data = sections[0];
        const std::vector<uint8_t>& inst = sections[1];
        const std::vector<uint8_t>& addr = sections[2];
        size_t di = 0, ii = 0, ai = 0;
        uint64_t nearC[4] = {}, sameC[3 * 256] = {};
        int nextNear = 0;
        auto doOp = [&](const Op& op) {
            if (op.type == 0) return true;
            uint64_t size = op.size;
            if (!size && !num(inst.data(), inst.size(), ii, size)) return false;
            if (win.size() + size > tgtLen) return false;
            if (op.type == 1) // ADD
            {
                if (size > data.size() - di) return false;
                win.insert(win.end(), data.begin() + di, data.begin() + di + (size_t)size);
                di += (size_t)size;
            }
            else if (op.type == 2) // RUN
            {
                if (di >= data.size()) return false;
                win.insert(win.end(), (size_t)size, data[di++]);
            }
            else // COPY
            {
                const uint64_t here = segLen + win.size();
                uint64_t a = 0, v = 0;
                if (op.mode == 0)
                {
                    if (!num(addr.data(), addr.size(), ai, a)) return false;
                }
                else if (op.mode == 1)
                {
                    if (!num(addr.data(), addr.size(), ai, v) || v > here) return false;
                    a = here - v;
                }
                else if (op.mode < 6)
                {
                    if (!num(addr.data(), addr.size(), ai, v)) return false;
                    a = nearC[op.mode - 2] + v;
                }
                else
                {
                    if (ai >= addr.size()) return false;
                    a = sameC[(op.mode - 6) * 256 + addr[ai++]];
                }
                nearC[nextNear] = a;
                nextNear = (nextNear + 1) % 4;
                sameC[a % (3 * 256)] = a;
                if (a >= here) return false;
                for (uint64_t k = 0; k < size; k++, a++)
                {
                    if (a < segLen) win.push_back(segment[(size_t)a]);
                    else if (a - segLen < win.size()) win.push_back(win[(size_t)(a - segLen)]);
                    else return false;
                }
            }
            return true;
        };
        while (ii < inst.size())
        {
            const Entry& e = table[inst[ii++]];
            if (!doOp(e.a) || !doOp(e.b)) return bad();
        }
        if (win.size() != tgtLen) return bad();
        if (hasAdler && Adler32(win.data(), win.size()) != adler) return bad("THE XDELTA PATCH IS FOR ANOTHER ROM");
        if (!out.Write(win.data(), win.size())) return bad("CAN'T WRITE THE PATCHED ROM");
        windows++;
    }
    return windows > 0 || bad();
}

// Whether an xdelta patch is for a GBA ROM: its file names (xdelta keeps them) say .gba.
inline bool IsGbaVcdiff(const std::vector<uint8_t>& patch)
{
    const uint8_t* p = patch.data();
    const size_t n = patch.size();
    if (FormatOf(p, n) != Format::Vcdiff) return false;
    size_t i = 4;
    auto num = [&](uint64_t& v) {
        v = 0;
        for (int k = 0; k < 10; k++)
        {
            if (i >= n) return false;
            const uint8_t x = p[i++];
            v = v << 7 | (x & 0x7F);
            if (!(x & 0x80)) return true;
        }
        return false;
    };
    const uint8_t hdr = p[i++];
    if (hdr & 1) i++;
    if (hdr & 4)
    {
        uint64_t len;
        if (!num(len) || len > n - i) return false;
        const std::string names = Lower(std::string((const char*)p + i, (size_t)len));
        i += (size_t)len;
        if (names.find(".gba") != std::string::npos || names.find(".agb") != std::string::npos) return true;
    }
    return false; // (no names: the DS list tries it first)
}

inline bool Apply(const std::vector<uint8_t>& patch, Reader& src, Writer& out, std::string& error)
{
    switch (FormatOf(patch.data(), patch.size()))
    {
    case Format::Ips: return ApplyIps(patch, src, out, error);
    case Format::Ups: return ApplyUps(patch, src, out, error);
    case Format::Bps: return ApplyBps(patch, src, out, error);
    case Format::Vcdiff: return ApplyVcdiff(patch, src, out, error);
    default: error = "NOT A PATCH FILE"; return false;
    }
}

// ---------------------------------------------------------------- clean DS ROMs

struct Clean
{
    const char* code;
    uint32_t size, crc;
};
// The clean releases of the DS games (No-Intro): the size of the ROM and its CRC-32.
inline const std::vector<Clean>& CleanDs()
{
    static const std::vector<Clean> list = {
        { "ADAD", 67108864u, 0x87CF7B1Fu }, // Pokemon - Diamant-Edition (Germany) (Rev 5)
        { "ADAE", 67108864u, 0x84427823u }, // Pokemon - Diamond Version (USA) (Rev 5)
        { "ADAE", 67108864u, 0xDCF7050Bu }, // Pokemon - Diamond Version (Europe) (Rev 5)
        { "ADAF", 67108864u, 0x2E6901A8u }, // Pokemon - Version Diamant (France) (Rev 5)
        { "ADAI", 67108864u, 0x1ED511ABu }, // Pokemon - Versione Diamante (Italy) (Rev 5)
        { "ADAJ", 134217728u, 0xAE2DB9A9u }, // Pocket Monsters - Diamond (Japan)
        { "ADAJ", 134217728u, 0xE2A6A6CFu }, // Pocket Monsters - Diamond (Japan) (Rev 1)
        { "ADAJ", 67108864u, 0x66B25E29u }, // Pocket Monsters - Diamond (Japan) (Rev 5)
        { "ADAJ", 67108864u, 0xAA6224A2u }, // Pocket Monsters - Diamond (Japan) (Rev 6)
        { "ADAK", 67108864u, 0xB185A382u }, // Pocket Monsters DP - Dialga (Korea)
        { "ADAS", 67108864u, 0xAA3E6B39u }, // Pokemon - Edicion Diamante (Spain) (Rev 5)
        { "APAD", 67108864u, 0x95A4FCABu }, // Pokemon - Perl-Edition (Germany) (Rev 5)
        { "APAE", 67108864u, 0xBA6D0397u }, // Pokemon - Pearl Version (Europe) (Rev 5)
        { "APAE", 67108864u, 0xE2D87EBFu }, // Pokemon - Pearl Version (USA) (Rev 5)
        { "APAF", 67108864u, 0x3B0F21C6u }, // Pokemon - Version Perle (France) (Rev 5)
        { "APAI", 67108864u, 0xC6EACC7Du }, // Pokemon - Versione Perla (Italy) (Rev 5)
        { "APAJ", 134217728u, 0x50DD7518u }, // Pocket Monsters - Pearl (Japan)
        { "APAJ", 134217728u, 0x9F9C293Du }, // Pocket Monsters - Pearl (Japan) (Rev 1)
        { "APAJ", 67108864u, 0x2FE1A094u }, // Pocket Monsters - Pearl (Japan) (Rev 5)
        { "APAJ", 67108864u, 0x8FE6A84Cu }, // Pocket Monsters - Pearl (Japan) (Rev 6)
        { "APAK", 67108864u, 0xD04A0344u }, // Pocket Monsters DP - Palkia (Korea)
        { "APAS", 67108864u, 0x4B3E6450u }, // Pokemon - Edicion Perla (Spain) (Rev 5)
        { "CPUD", 134217728u, 0x37E9D9E5u }, // Pokemon - Platin-Edition (Germany)
        { "CPUE", 134217728u, 0x69D628E8u }, // Pokemon - Platinum Version (USA) (Rev 1)
        { "CPUE", 134217728u, 0x71ACA63Au }, // Pokemon - Platinum Version (Europe)
        { "CPUE", 134217728u, 0x9253921Du }, // Pokemon - Platinum Version (USA)
        { "CPUF", 134217728u, 0xA54A9984u }, // Pokemon - Version Platine (France)
        { "CPUI", 134217728u, 0x0DDBE664u }, // Pokemon - Versione Platino (Italy)
        { "CPUJ", 134217728u, 0xBE130BC9u }, // Pocket Monsters - Platinum (Japan)
        { "CPUK", 134217728u, 0x70AE5C10u }, // Pocket Monsters Pt - Giratina (Korea)
        { "CPUS", 134217728u, 0x9B4C38AEu }, // Pokemon - Edicion Platino (Spain)
        { "IPGD", 134217728u, 0x94E4A5B5u }, // Pokemon - Silberne Edition SoulSilver (Germany)
        { "IPGE", 134217728u, 0x968CDE08u }, // Pokemon - SoulSilver Version (Europe)
        { "IPGE", 134217728u, 0xD8EA6090u }, // Pokemon - SoulSilver Version (USA, Australia)
        { "IPGF", 134217728u, 0x36D0A539u }, // Pokemon - Version Argent SoulSilver (France)
        { "IPGI", 134217728u, 0x0041AE66u }, // Pokemon - Versione Argento SoulSilver (Italy)
        { "IPGJ", 134217728u, 0xCC4C2BEFu }, // Pocket Monsters - SoulSilver (Japan)
        { "IPGK", 134217728u, 0x691A264Du }, // Pocket Monsters - SoulSilver (Korea)
        { "IPGS", 134217728u, 0x50238ECBu }, // Pokemon - Edicion Plata SoulSilver (Spain)
        { "IPKD", 134217728u, 0x0A994C0Cu }, // Pokemon - Goldene Edition HeartGold (Germany)
        { "IPKE", 134217728u, 0xB64A5EFBu }, // Pokemon - HeartGold Version (Europe)
        { "IPKE", 134217728u, 0xC180A0E9u }, // Pokemon - HeartGold Version (USA)
        { "IPKF", 134217728u, 0x533A6E55u }, // Pokemon - Version Or HeartGold (France)
        { "IPKI", 134217728u, 0x2646AE79u }, // Pokemon - Versione Oro HeartGold (Italy)
        { "IPKJ", 134217728u, 0xFC7D8F28u }, // Pocket Monsters - HeartGold (Japan)
        { "IPKK", 134217728u, 0x23850525u }, // Pocket Monsters - HeartGold (Korea)
        { "IPKS", 134217728u, 0x0E3670CDu }, // Pokemon - Edicion Oro HeartGold (Spain)
        { "IRAD", 268435456u, 0x95CF004Du }, // Pokemon - Weisse Edition (Germany) (NDSi Enhanced)
        { "IRAF", 268435456u, 0x8B4CBA44u }, // Pokemon - Version Blanche (France) (NDSi Enhanced)
        { "IRAI", 268435456u, 0x2F025937u }, // Pokemon - Versione Bianca (Italy) (NDSi Enhanced)
        { "IRAJ", 268435456u, 0xCAA820CBu }, // Pocket Monsters - White (Japan) (NDSi Enhanced)
        { "IRAK", 268435456u, 0x672539F1u }, // Pocket Monsters - White (Korea) (NDSi Enhanced)
        { "IRAO", 268435456u, 0xB552501Cu }, // Pokemon - White Version (USA, Europe) (NDSi Enhanced)
        { "IRAS", 268435456u, 0x32657121u }, // Pokemon - Edicion Blanca (Spain) (NDSi Enhanced)
        { "IRBD", 268435456u, 0xEE469543u }, // Pokemon - Schwarze Edition (Germany) (NDSi Enhanced)
        { "IRBF", 268435456u, 0x308F9D56u }, // Pokemon - Version Noire (France) (NDSi Enhanced)
        { "IRBI", 268435456u, 0x732BC6DCu }, // Pokemon - Versione Nera (Italy) (NDSi Enhanced)
        { "IRBJ", 268435456u, 0x33AF46D5u }, // Pocket Monsters - Black (Japan) (NDSi Enhanced)
        { "IRBK", 268435456u, 0x523E8A13u }, // Pocket Monsters - Black (Korea) (NDSi Enhanced)
        { "IRBO", 268435456u, 0x4F6E5580u }, // Pokemon - Black Version (USA, Europe) (NDSi Enhanced)
        { "IRBS", 268435456u, 0x2CA88FAAu }, // Pokemon - Edicion Negra (Spain) (NDSi Enhanced)
        { "IRDD", 536870912u, 0x7BB14936u }, // Pokemon - Weisse Edition 2 (Germany) (NDSi Enhanced)
        { "IRDF", 536870912u, 0x27F31411u }, // Pokemon - Version Blanche 2 (France) (NDSi Enhanced)
        { "IRDI", 536870912u, 0x4D4433D9u }, // Pokemon - Versione Bianca 2 (Italy) (NDSi Enhanced)
        { "IRDJ", 536870912u, 0xA9833314u }, // Pocket Monsters - White 2 (Japan) (Rev 1) (NDSi Enhanced)
        { "IRDK", 536870912u, 0xD3E316EFu }, // Pocket Monsters - White 2 (Korea) (NDSi Enhanced)
        { "IRDO", 536870912u, 0x777EB04Fu }, // Pokemon - White Version 2 (USA, Europe) (NDSi Enhanced)
        { "IRDS", 536870912u, 0xEA70C572u }, // Pokemon - Edicion Blanca 2 (Spain) (NDSi Enhanced)
        { "IRED", 536870912u, 0xFA496FD0u }, // Pokemon - Schwarze Edition 2 (Germany) (NDSi Enhanced)
        { "IREF", 536870912u, 0x99AFB52Fu }, // Pokemon - Version Noire 2 (France) (NDSi Enhanced)
        { "IREI", 536870912u, 0x91E863E7u }, // Pokemon - Versione Nera 2 (Italy) (NDSi Enhanced)
        { "IREJ", 536870912u, 0x29C68B2Bu }, // Pocket Monsters - Black 2 (Japan) (Rev 1) (NDSi Enhanced)
        { "IREK", 536870912u, 0x9928FD8Du }, // Pocket Monsters - Black 2 (Korea) (NDSi Enhanced)
        { "IREO", 536870912u, 0xD4427FD1u }, // Pokemon - Black Version 2 (USA, Europe) (NDSi Enhanced)
        { "IRES", 536870912u, 0x06CE7DD3u }, // Pokemon - Edicion Negra 2 (Spain) (NDSi Enhanced)
    };
    return list;
}

// What was found out about files, kept between looks (reading a whole DS ROM takes seconds, much
// longer from a Switch's SD card): a line per file, its path, size and time, "=", then the result.
class Cache
{
public:
    void Load(const std::string& p)
    {
        path = p;
        std::vector<uint8_t> d;
        if (!ReadAll(path, d, 4 << 20)) return;
        std::string text(d.begin(), d.end());
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            const size_t eq = line.find("\t=\t");
            if (eq != std::string::npos) lines.push_back({ line.substr(0, eq), line.substr(eq + 3), false });
        }
    }
    static std::string Key(const std::string& file, const std::string& what)
    {
        uint64_t size = 0;
        long long mtime = 0;
        if (!Stat(file, size, mtime)) return "";
        return what + "\t" + file + "\t" + std::to_string((unsigned long long)size) + "\t" + std::to_string(mtime);
    }
    bool Get(const std::string& key, std::string& value)
    {
        if (key.empty()) return false;
        for (Line& l : lines)
            if (l.key == key)
            {
                l.used = true;
                value = l.value;
                return true;
            }
        return false;
    }
    void Put(const std::string& key, const std::string& value)
    {
        if (key.empty()) return;
        for (Line& l : lines)
            if (l.key == key)
            {
                l.value = value;
                l.used = true;
                changed = true;
                return;
            }
        lines.push_back({ key, value, true });
        changed = true;
    }
    // Keeps what was looked up this time (and drops the rest: files gone or changed), or with
    // prune false everything.
    void Save(bool prune = true)
    {
        if (prune)
            for (const Line& l : lines) changed |= !l.used;
        if (!changed || path.empty()) return;
        std::string out;
        for (const Line& l : lines)
            if (l.used || !prune) out += l.key + "\t=\t" + l.value + "\n";
        if (FILE* f = fopen(path.c_str(), "wb"))
        {
            fwrite(out.data(), 1, out.size(), f);
            fclose(f);
        }
        changed = false;
    }
private:
    struct Line
    {
        std::string key, value;
        bool used;
    };
    std::string path;
    std::vector<Line> lines;
    bool changed = false;
};

enum class Kind { Unknown, Clean, Modified };
inline const char* KindName(Kind k) { return k == Kind::Clean ? "clean" : k == Kind::Modified ? "modified" : "unknown"; }

// Reading whole ROMs to their CRC-32s, in the background (a 512 MB ROM takes seconds, far
// longer from a Switch's SD card): the list asks and carries on, and has the answer on a later
// look; starting a game waits for its own.
class Hasher
{
public:
    static Hasher& Get()
    {
        static Hasher h;
        return h;
    }
    ~Hasher()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
        }
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }
    // The CRC-32 and size of the file (as it is now: `key` is its path, size and time), if read;
    // else asked for. wait: until it is.
    bool Result(const std::string& path, const std::string& key, uint32_t& crc, uint64_t& size, bool wait)
    {
        std::unique_lock<std::mutex> lock(m);
        while (true)
        {
            auto it = done.find(key);
            if (it != done.end())
            {
                crc = it->second.crc;
                size = it->second.size;
                return it->second.ok;
            }
            if (std::find(queue.begin(), queue.end(), Job{ path, key }) == queue.end() && current != key)
            {
                if (wait) queue.push_front({ path, key });
                else queue.push_back({ path, key });
                if (!worker.joinable())
                {
                    try { worker = std::thread([this] { Run(); }); }
                    catch (const std::system_error& e)
                    {
                        // A missing worker must not terminate the launcher or leave a waiter
                        // asleep forever. A later attempt may succeed; do not cache a failure.
                        queue.pop_front();
                        fprintf(stderr, "[romhacks] cannot start ROM check: %s\n", e.what());
                        return false;
                    }
                }
                cv.notify_all();
            }
            else if (wait)
            {
                // (Asked for already: first.)
                auto q = std::find(queue.begin(), queue.end(), Job{ path, key });
                if (q != queue.end() && q != queue.begin())
                {
                    Job j = *q;
                    queue.erase(q);
                    queue.push_front(j);
                }
            }
            if (!wait) return false;
            cv.wait(lock);
        }
    }
    bool Busy()
    {
        std::lock_guard<std::mutex> lock(m);
        return !queue.empty() || !current.empty();
    }
private:
    struct Job
    {
        std::string path, key;
        bool operator==(const Job& o) const { return key == o.key; }
    };
    struct Done
    {
        bool ok;
        uint32_t crc;
        uint64_t size;
    };
    void Run()
    {
        std::vector<uint8_t> buf(1 << 20);
        std::unique_lock<std::mutex> lock(m);
        while (!stop)
        {
            if (queue.empty())
            {
                cv.wait(lock);
                continue;
            }
            const Job job = queue.front();
            queue.pop_front();
            current = job.key;
            lock.unlock();
            const auto began = std::chrono::steady_clock::now();
            fprintf(stderr, "[romhacks] CRC begin: %s\n", job.path.c_str());
            Done d{ false, 0, 0 };
            if (FILE* f = fopen(job.path.c_str(), "rb"))
            {
                size_t k;
                while (!stop && (k = fread(buf.data(), 1, buf.size(), f)) > 0)
                {
                    d.crc = Crc32(d.crc, buf.data(), k);
                    d.size += k;
                }
                d.ok = !stop && !ferror(f);
                fclose(f);
            }
            fprintf(stderr, "[romhacks] CRC end: %s, %llu bytes, %lld ms, %s\n",
                job.path.c_str(), (unsigned long long)d.size,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - began).count(), d.ok ? "ok" : "failed");
            lock.lock();
            current.clear();
            done[job.key] = d;
            cv.notify_all();
        }
    }
    std::mutex m;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::map<std::string, Done> done;
    std::string current;
    std::thread worker;
    std::atomic<bool> stop{false};
};

// A CRC-32 carried on over n more bytes of the value v (a trimmed ROM's cut-off end).
inline uint32_t CrcPadded(uint32_t crc, uint8_t v, uint64_t n)
{
    std::vector<uint8_t> buf((size_t)std::min<uint64_t>(n, 1 << 20), v);
    while (n)
    {
        const size_t k = (size_t)std::min<uint64_t>(n, buf.size());
        crc = Crc32(crc, buf.data(), k);
        n -= k;
    }
    return crc;
}

// What tells a DS hack from its game, quickly (a few MB read, not the whole ROM): the CRC-32 of
// the ARM9 program (from 0x8000: not the secure area, which copies of one game have differently),
// the ARM7 program, the file name and file allocation tables, the overlay tables, the banner (icon
// and title) and the ARM9 overlays' files. A hack changes these (its code, its title, or its
// files' sizes); copies of the clean game, trimmed or not, don't. (tools/romhacks/ds_fingerprint.py
// works it out the same way.) False if the ROM's tables don't hold together.
inline bool DsFingerprint(const std::string& rom, uint32_t& fp, uint8_t& rev, uint64_t& size)
{
    FileReader r;
    if (!r.Open(rom)) return false;
    size = r.Size();
    uint8_t h[0x200];
    if (size < sizeof h || !r.Read(0, h, sizeof h)) return false;
    rev = h[0x1E];
    const uint32_t a9 = Le32(h + 0x20), a9s = Le32(h + 0x2C), a7 = Le32(h + 0x30), a7s = Le32(h + 0x3C);
    const uint32_t fnt = Le32(h + 0x40), fnts = Le32(h + 0x44), fat = Le32(h + 0x48), fats = Le32(h + 0x4C);
    const uint32_t ov9 = Le32(h + 0x50), ov9s = Le32(h + 0x54), ov7 = Le32(h + 0x58), ov7s = Le32(h + 0x5C);
    const uint32_t ban = Le32(h + 0x68);
    std::vector<std::pair<uint64_t, uint64_t>> regions = {
        { std::max<uint64_t>(a9, 0x8000), (uint64_t)a9 + a9s }, { a7, (uint64_t)a7 + a7s }, { fnt, (uint64_t)fnt + fnts },
        { fat, (uint64_t)fat + fats }, { ov9, (uint64_t)ov9 + ov9s }, { ov7, (uint64_t)ov7 + ov7s },
        { ban, ban ? (uint64_t)ban + 0x840 : 0 },
    };
    if (ov9s % 32 == 0 && ov9s <= 32 * 1024 && fats % 8 == 0 && (uint64_t)fat + fats <= size && (uint64_t)ov9 + ov9s <= size)
    {
        std::vector<uint8_t> table(ov9s);
        if (!r.Read(ov9, table.data(), table.size())) return false;
        for (size_t i = 0; i + 32 <= table.size(); i += 32)
        {
            const uint32_t id = Le32(table.data() + i + 0x18);
            if ((uint64_t)id * 8 + 8 <= fats)
            {
                uint8_t entry[8];
                if (!r.Read((uint64_t)fat + (uint64_t)id * 8, entry, sizeof entry)) return false;
                regions.push_back({ Le32(entry), Le32(entry + 4) });
            }
        }
    }
    fp = 0;
    uint64_t total = 0;
    std::vector<uint8_t> buf;
    for (const auto& reg : regions)
    {
        const uint64_t s = reg.first, e = std::min(reg.second, size);
        if (e <= s) continue;
        total += e - s;
        if (total > (64u << 20)) return false;
        // Stream the same bytes, in the same order. A ROM-supplied FAT/region size
        // must not become a whole-region allocation before the game can start.
        buf.resize((size_t)std::min<uint64_t>(e - s, 1u << 20));
        for (uint64_t at = s; at < e;)
        {
            const size_t n = (size_t)std::min<uint64_t>(e - at, buf.size());
            if (!r.Read(at, buf.data(), n)) return false;
            fp = Crc32(fp, buf.data(), n);
            at += n;
        }
    }
    return true;
}

struct Fingerprint
{
    const char* code;
    uint8_t rev;
    uint32_t fp;
};
// The clean DS releases' fingerprints (DsFingerprint), as far as they are known: the game code
// and revision (header 0x1E) of each, worked out from copies matching No-Intro's.
inline const std::vector<Fingerprint>& CleanDsFingerprints()
{
    static const std::vector<Fingerprint> list = {
        { "ADAD", 5, 0xD29AEBECu }, // Pokemon - Diamant-Edition (Germany) (Rev 5)
        { "ADAE", 5, 0x40D3818Cu }, // Pokemon - Diamond Version (USA) (Rev 5)
        { "ADAE", 13, 0x40D3818Cu }, // Pokemon - Diamond Version (Europe) (Rev 5)
        { "ADAI", 5, 0xFB54528Du }, // Pokemon - Versione Diamante (Italy) (Rev 5)
        { "ADAJ", 0, 0x5E24A75Eu }, // Pocket Monsters - Diamond (Japan)
        { "ADAK", 0, 0xD8EF216Du }, // Pocket Monsters DP - Dialga (Korea)
        { "ADAS", 5, 0x9EAD892Eu }, // Pokemon - Edicion Diamante (Spain) (Rev 5)
        { "APAD", 5, 0xBC04BBFBu }, // Pokemon - Perl-Edition (Germany) (Rev 5)
        { "APAE", 5, 0xC0EC1B53u }, // Pokemon - Pearl Version (USA) (Rev 5)
        { "APAE", 13, 0xC0EC1B53u }, // Pokemon - Pearl Version (Europe) (Rev 5)
        { "APAF", 5, 0x9AEF5813u }, // Pokemon - Version Perle (France) (Rev 5)
        { "APAI", 5, 0x83DFD63Cu }, // Pokemon - Versione Perla (Italy) (Rev 5)
        { "APAJ", 5, 0x95E4D0D7u }, // Pocket Monsters - Pearl (Japan) (Rev 5)
        { "APAK", 0, 0xBCE5B5A6u }, // Pocket Monsters DP - Palkia (Korea)
        { "APAS", 5, 0x5951482Fu }, // Pokemon - Edicion Perla (Spain) (Rev 5)
        { "CPUD", 0, 0x20013794u }, // Pokemon - Platin-Edition (Germany)
        { "CPUE", 0, 0x9E24E992u }, // Pokemon - Platinum Version (USA)
        { "CPUE", 10, 0xFEDE1D5Bu }, // Pokemon - Platinum Version (Europe)
        { "CPUF", 0, 0x7F5EE3FEu }, // Pokemon - Version Platine (France)
        { "CPUI", 0, 0xE1A6C8B2u }, // Pokemon - Versione Platino (Italy)
        { "CPUJ", 0, 0xB2AE835Au }, // Pocket Monsters - Platinum (Japan)
        { "CPUS", 0, 0xD2005271u }, // Pokemon - Edicion Platino (Spain)
        { "IPGD", 0, 0x05346910u }, // Pokemon - Silberne Edition SoulSilver (Germany)
        { "IPGE", 0, 0x437E16E4u }, // Pokemon - SoulSilver Version (USA, Australia)
        { "IPGE", 10, 0x437E16E4u }, // Pokemon - SoulSilver Version (Europe)
        { "IPGF", 0, 0x73B391C3u }, // Pokemon - Version Argent SoulSilver (France)
        { "IPGI", 0, 0xFA7A333Au }, // Pokemon - Versione Argento SoulSilver (Italy)
        { "IPGJ", 0, 0xB55680D6u }, // Pocket Monsters - SoulSilver (Japan)
        { "IPGK", 0, 0xD44A3064u }, // Pocket Monsters - SoulSilver (Korea)
        { "IPGS", 0, 0xBF9664C3u }, // Pokemon - Edicion Plata SoulSilver (Spain)
        { "IPKD", 0, 0x2C14C2FAu }, // Pokemon - Goldene Edition HeartGold (Germany)
        { "IPKE", 0, 0xE3534BF0u }, // Pokemon - HeartGold Version (USA)
        { "IPKE", 10, 0xE3534BF0u }, // Pokemon - HeartGold Version (Europe)
        { "IPKF", 0, 0x542393F9u }, // Pokemon - Version Or HeartGold (France)
        { "IPKI", 0, 0x9C60FEEAu }, // Pokemon - Versione Oro HeartGold (Italy)
        { "IPKJ", 0, 0x98648A76u }, // Pocket Monsters - HeartGold (Japan)
        { "IPKK", 0, 0x4697735Au }, // Pocket Monsters - HeartGold (Korea)
        { "IRAD", 0, 0xC94F55C9u }, // Pokemon - Weisse Edition (Germany) (NDSi Enhanced)
        { "IRAF", 0, 0xA0F9715Du }, // Pokemon - Version Blanche (France) (NDSi Enhanced)
        { "IRAI", 0, 0x7552E6F7u }, // Pokemon - Versione Bianca (Italy) (NDSi Enhanced)
        { "IRAJ", 0, 0xB104FB83u }, // Pocket Monsters - White (Japan) (NDSi Enhanced)
        { "IRAK", 0, 0xD0D98102u }, // Pocket Monsters - White (Korea) (NDSi Enhanced)
        { "IRAO", 0, 0xB7838203u }, // Pokemon - White Version (USA, Europe) (NDSi Enhanced)
        { "IRAS", 0, 0xDB141047u }, // Pokemon - Edicion Blanca (Spain) (NDSi Enhanced)
        { "IRBD", 0, 0xB3C29B6Eu }, // Pokemon - Schwarze Edition (Germany) (NDSi Enhanced)
        { "IRBF", 0, 0xC8BAAA9Eu }, // Pokemon - Version Noire (France) (NDSi Enhanced)
        { "IRBI", 0, 0x81E615B6u }, // Pokemon - Versione Nera (Italy) (NDSi Enhanced)
        { "IRBJ", 0, 0xB4B23197u }, // Pocket Monsters - Black (Japan) (NDSi Enhanced)
        { "IRBK", 0, 0xA7FBE7B0u }, // Pocket Monsters - Black (Korea) (NDSi Enhanced)
        { "IRBO", 0, 0x749916A6u }, // Pokemon - Black Version (USA, Europe) (NDSi Enhanced)
        { "IRDD", 0, 0x322A603Cu }, // Pokemon - Weisse Edition 2 (Germany) (NDSi Enhanced)
        { "IRDF", 0, 0xD2AB62FEu }, // Pokemon - Version Blanche 2 (France) (NDSi Enhanced)
        { "IRDI", 0, 0x1F877132u }, // Pokemon - Versione Bianca 2 (Italy) (NDSi Enhanced)
        { "IRDJ", 1, 0x28E5F51Au }, // Pocket Monsters - White 2 (Japan) (Rev 1) (NDSi Enhanced)
        { "IRDK", 0, 0x55A2F72Bu }, // Pocket Monsters - White 2 (Korea) (NDSi Enhanced)
        { "IRDO", 0, 0x09BD1933u }, // Pokemon - White Version 2 (USA, Europe) (NDSi Enhanced)
        { "IRDS", 0, 0xC5999366u }, // Pokemon - Edicion Blanca 2 (Spain) (NDSi Enhanced)
        { "IRED", 0, 0x7D6D3ACCu }, // Pokemon - Schwarze Edition 2 (Germany) (NDSi Enhanced)
        { "IREF", 0, 0x27E32F38u }, // Pokemon - Version Noire 2 (France) (NDSi Enhanced)
        { "IREI", 0, 0x152B3973u }, // Pokemon - Versione Nera 2 (Italy) (NDSi Enhanced)
        { "IREJ", 1, 0x93D3DEAEu }, // Pocket Monsters - Black 2 (Japan) (Rev 1) (NDSi Enhanced)
        { "IREO", 0, 0x9E241CD4u }, // Pokemon - Black Version 2 (USA, Europe) (NDSi Enhanced): from a copy whose secure area differs from No-Intro's
    };
    return list;
}

// Whether a DS ROM (of game code `code`) is a clean release, or modified (a hack). Clean when its
// fingerprint (its program, overlays and file tables: DsFingerprint) is a clean release's, or the
// whole ROM's CRC-32 is (read in the background, Hasher: wait false leaves it Unknown until it
// is). Modified when it is larger than any release of its game, whatever an older check kept, or
// its release (code and revision) is known and its fingerprint isn't that. Unknown otherwise
// (another copy of a release not known here): played as the game.
// The whole ROM's CRC-32 is not asked of a copy whose fingerprint matches: real dumps of one
// release differ outside the game itself (the secure area left encrypted, the padding and the
// DSi part at the end), and a player's own clean game must keep its save and its features.
inline Kind CheckDs(const std::string& rom, const std::string& code, Cache& cache, bool wait = false)
{
    std::vector<const Clean*> mine;
    for (const Clean& c : CleanDs())
        if (code == c.code) mine.push_back(&c);
    if (mine.empty()) return Kind::Unknown;
    // (A new key: verdicts kept by earlier checks are not trusted.)
    const std::string key = Cache::Key(rom, "ds-v3");
    uint32_t biggest = 0;
    for (const Clean* c : mine) biggest = std::max(biggest, c->size);
    uint64_t size = 0;
    long long modifiedAt = 0;
    if (!Stat(rom, size, modifiedAt) || !size) return Kind::Unknown;
    if (size > biggest) { cache.Put(key, "modified"); return Kind::Modified; }
    std::string value;
    if (cache.Get(key, value)) return value == "clean" ? Kind::Clean : value == "modified" ? Kind::Modified : Kind::Unknown;
    uint32_t fp = 0;
    uint8_t rev = 0;
    uint64_t fingerprintSize = 0;
    const bool haveFp = DsFingerprint(rom, fp, rev, fingerprintSize);
    Kind kind = Kind::Unknown;
    bool releaseKnown = false;
    for (const Fingerprint& f : CleanDsFingerprints())
        if (code == f.code)
        {
            if (haveFp && fp == f.fp) kind = Kind::Clean;
            releaseKnown |= f.rev == rev;
        }
    if (kind != Kind::Clean)
    {
        if (releaseKnown && haveFp)
            kind = Kind::Modified;
        else
        {
            uint32_t crc = 0;
            uint64_t read = 0;
            if (!Hasher::Get().Result(rom, key, crc, read, wait)) return Kind::Unknown; // (being read: asked again later)
            // (A trimmed ROM: its end, all 0xFF or all 0x00, cut off.)
            if (read == size)
                for (const Clean* c : mine)
                    if (size <= c->size &&
                        (crc == c->crc || CrcPadded(crc, 0xFF, c->size - size) == c->crc || CrcPadded(crc, 0x00, c->size - size) == c->crc))
                        kind = Kind::Clean;
        }
    }
    fprintf(stderr, "[romhacks] %s: %s (fingerprint %08X, revision %u)\n", rom.c_str(), KindName(kind), (unsigned)fp, (unsigned)rev);
    cache.Put(key, KindName(kind));
    return kind;
}

// A DS ROM's game code (header 0x0C), "" if it isn't one.
inline std::string DsCode(const std::string& rom)
{
    FILE* f = fopen(rom.c_str(), "rb");
    if (!f) return "";
    uint8_t h[0x10];
    const bool ok = fread(h, 1, sizeof h, f) == sizeof h;
    fclose(f);
    if (!ok) return "";
    std::string code((const char*)h + 0x0C, 4);
    for (char c : code)
        if (c < 0x20 || c > 0x7E) return "";
    return code;
}

// ---------------------------------------------------------------- making DS hacks from patches

// A patch file that could not be used, and why (for the game list).
struct Problem
{
    std::string patch; // its path
    std::string why;
};

// The words in a patch's or a ROM's file name, to try the likeliest ROM first.
inline int NameMatch(const std::string& a, const std::string& b)
{
    auto words = [](const std::string& s) {
        std::vector<std::string> w;
        std::string cur;
        for (char c : Lower(s))
        {
            if (isalnum((unsigned char)c)) cur += c;
            else if (!cur.empty())
            {
                w.push_back(cur);
                cur.clear();
            }
        }
        if (!cur.empty()) w.push_back(cur);
        return w;
    };
    int n = 0;
    for (const std::string& x : words(a))
        for (const std::string& y : words(b))
            if (x == y && x != "pokemon" && x != "version" && x.size() > 1) n++;
    return n;
}

// The DS hacks made from the patch files in `folders` (.xdelta, .bps, .ups ...): each put on the
// clean DS ROM it is for (among `cleanRoms`), written once to outDir/<patch's name>.nds, and made
// again only when the patch changes. Patches for GBA ROMs are left alone (the GBA list's).
// Returns the hacks' ROMs; `problems` gets the patches that couldn't be used.
// wait false: a ROM still being read (Hasher) is left for a later look, and so is its patch.
inline std::vector<std::string> MakeDsHacks(const std::vector<std::string>& folders, const std::vector<std::string>& cleanRoms,
                                            const std::string& outDir, Cache& cache, std::vector<Problem>& problems, bool wait,
                                            const std::function<void(const std::string&)>& status = {})
{
    std::vector<std::string> made;
    std::vector<std::string> patches;
    for (const std::string& dir : folders)
    {
        if (dir.empty()) continue;
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> names;
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.' && IsPatchName(e->d_name)) names.push_back(e->d_name);
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names)
        {
            const std::string path = dir + "/" + n;
            bool dup = false;
            for (const std::string& p : patches) dup |= FileName(p) == n;
            if (!dup) patches.push_back(path);
        }
    }
    for (const std::string& patchPath : patches)
    {
        const std::string out = outDir + "/" + Stem(patchPath) + ".nds";
        const std::string key = Cache::Key(patchPath, "patch");
        std::string v;
        if (cache.Get(key, v))
        {
            if (v == "gba") continue;
            if (v.compare(0, 5, "made\t") == 0 && Cache::Key(out, "out") == v.substr(5))
            {
                made.push_back(out);
                continue;
            }
            if (v.compare(0, 8, "problem\t") == 0)
            {
                problems.push_back({ patchPath, v.substr(8) });
                continue;
            }
        }
        std::vector<uint8_t> patch;
        if (!ReadAll(patchPath, patch, 64u << 20))
        {
            problems.push_back({ patchPath, "CAN'T READ THE PATCH" });
            continue;
        }
        Info info;
        if (!ReadInfo(patch, info))
        {
            const std::string why = FormatOf(patch.data(), patch.size()) == Format::None ? "NOT A PATCH FILE" : "THE PATCH IS DAMAGED";
            problems.push_back({ patchPath, why });
            cache.Put(key, "problem\t" + why);
            continue;
        }
        // (IPS can't reach past 16 MB, and a UPS or BPS patch says its ROM's size: a GBA ROM's
        // is the GBA list's to use.)
        if (info.format == Format::Ips || (info.knowsSource && info.sourceSize <= (32u << 20)) || IsGbaVcdiff(patch))
        {
            cache.Put(key, "gba");
            continue;
        }
        // (No ROM it goes on, the last time, among the same ROMs: not tried again.)
        std::string sig;
        for (const std::string& rom : cleanRoms) sig += Cache::Key(rom, "") + "\n";
        char sigHex[16];
        snprintf(sigHex, sizeof sigHex, "%08X", (unsigned)Crc32(0, (const uint8_t*)sig.data(), sig.size()));
        if (v == std::string("nobase\t") + sigHex)
        {
            problems.push_back({ patchPath, "NO CLEAN ROM IT GOES ON: ADD THE ROM THIS HACK IS FOR" });
            continue;
        }
        // The clean ROMs it could be for, likeliest first.
        std::vector<std::string> bases = cleanRoms;
        std::stable_sort(bases.begin(), bases.end(), [&](const std::string& a, const std::string& b) {
            return NameMatch(patchPath, a) > NameMatch(patchPath, b);
        });
        std::string base, why = "NO CLEAN ROM IT GOES ON: ADD THE ROM THIS HACK IS FOR";
        bool pending = false;
        for (const std::string& rom : bases)
        {
            uint64_t size = 0;
            long long mtime = 0;
            if (!Stat(rom, size, mtime)) continue;
            if (info.knowsSource)
            {
                if (size != info.sourceSize) continue;
                const std::string crcKey = Cache::Key(rom, "crc");
                std::string c;
                uint32_t crc = 0;
                if (cache.Get(crcKey, c)) crc = (uint32_t)strtoul(c.c_str(), nullptr, 16);
                else
                {
                    uint64_t read = 0;
                    if (wait && status) status("CHECKING " + FileName(rom));
                    if (!Hasher::Get().Result(rom, Cache::Key(rom, "ds"), crc, read, wait))
                    {
                        pending = true;
                        continue;
                    }
                    if (read != size) continue;
                    char hex[16];
                    snprintf(hex, sizeof hex, "%08X", (unsigned)crc);
                    cache.Put(crcKey, hex);
                }
                if (crc == info.sourceCrc)
                {
                    base = rom;
                    break;
                }
            }
            else
            {
                // (xdelta: the first window of the hack made from this ROM, checked by its
                // checksum.)
                FileReader r;
                MemWriter w;
                std::string e;
                if (r.Open(rom) && ApplyVcdiff(patch, r, w, e, 1))
                {
                    base = rom;
                    break;
                }
            }
        }
        if (base.empty())
        {
            if (pending) continue; // (the next look)
            problems.push_back({ patchPath, why });
            cache.Put(key, std::string("nobase\t") + sigHex);
            continue;
        }
        if (status) status("MAKING " + Stem(patchPath) + " (ONCE)");
        fprintf(stderr, "[romhacks] %s on %s -> %s\n", patchPath.c_str(), base.c_str(), out.c_str());
        MakeDir(outDir);
        const std::string tmp = out + ".part";
        FileReader r;
        FileWriter w;
        std::string error;
        bool ok = r.Open(base) && w.Open(tmp);
        if (!ok) error = "CAN'T WRITE THE PATCHED ROM";
        ok = ok && Apply(patch, r, w, error);
        ok = w.Close() && ok;
        if (ok)
        {
            remove(out.c_str());
            ok = rename(tmp.c_str(), out.c_str()) == 0;
            if (!ok) error = "CAN'T WRITE THE PATCHED ROM";
        }
        if (!ok)
        {
            remove(tmp.c_str());
            fprintf(stderr, "[romhacks] %s: %s\n", patchPath.c_str(), error.c_str());
            problems.push_back({ patchPath, error });
            cache.Put(key, "problem\t" + error);
            continue;
        }
        cache.Put(key, "made\t" + Cache::Key(out, "out"));
        cache.Put(Cache::Key(out, "ds"), "modified"); // (no need to read it to know)
        made.push_back(out);
    }
    return made;
}

}
