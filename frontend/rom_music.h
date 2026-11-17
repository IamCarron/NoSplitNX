#ifndef SSR_ROM_MUSIC_H
#define SSR_ROM_MUSIC_H

// Runtime ROM soundtrack player for NoSplitNX's game chooser.
// Synthesises background music directly from the user's own Nintendo DS (.nds Nitro SDAT)
// and GBA (.gba MP2K/Sappy) ROMs into 48 kHz stereo PCM with live 9-band spectrum levels.
// No copyrighted audio assets or sequence data are stored in the program.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace RomMusic
{

inline bool HasTag(const std::vector<uint8_t>& buf, size_t off, const char (&tag)[5])
{
    if (off + 4 > buf.size()) return false;
    return buf[off + 0] == (uint8_t)tag[0] && buf[off + 1] == (uint8_t)tag[1] &&
           buf[off + 2] == (uint8_t)tag[2] && buf[off + 3] == (uint8_t)tag[3];
}

inline uint16_t RdU16(const uint8_t* p, size_t len, size_t off)
{
    if (off + 2 > len) return 0;
    return (uint16_t)p[off] | ((uint16_t)p[off + 1] << 8);
}

inline int16_t RdS16(const uint8_t* p, size_t len, size_t off)
{
    return (int16_t)RdU16(p, len, off);
}

inline uint32_t RdU24(const uint8_t* p, size_t len, size_t off)
{
    if (off + 3 > len) return 0;
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) | ((uint32_t)p[off + 2] << 16);
}

inline uint32_t RdU32(const uint8_t* p, size_t len, size_t off)
{
    if (off + 4 > len) return 0;
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) |
           ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
}

inline bool ReadFileSlice(FILE* f, uint64_t off, size_t size, std::vector<uint8_t>& out)
{
    if (!f || size == 0 || size > (64u * 1024u * 1024u)) return false;
#if defined(_WIN32)
    if (_fseeki64(f, (long long)off, SEEK_SET) != 0) return false;
#else
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return false;
#endif
    out.resize(size);
    const size_t got = fread(out.data(), 1, size, f);
    if (got != size) { out.clear(); return false; }
    return true;
}

inline uint64_t FileSize(FILE* f)
{
    if (!f) return 0;
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0) return 0;
    const long long sz = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return 0;
    const off_t sz = ftello(f);
#endif
    return sz > 0 ? (uint64_t)sz : 0;
}

inline std::string UpperAscii(const char* s, size_t maxLen = 96)
{
    std::string r;
    if (!s) return r;
    for (size_t i = 0; i < maxLen && s[i]; i++)
        r.push_back((char)std::toupper((unsigned char)s[i]));
    return r;
}

struct Sample
{
    std::vector<int16_t> pcm;
    bool loop = false;
    uint32_t loopStart = 0; // sample index
    double baseRate = 32768.0; // sample Hz at middle C (note 60)
};

enum class WaveKind
{
    None,
    Pcm,
    Pulse, // PSG duty 0..7 (NDS) or CGB duty 0..3
    Noise, // LFSR noise
};

struct Instrument
{
    WaveKind kind = WaveKind::None;
    bool fixedFreq = false;
    int sampleIndex = -1; // index into shared Sample pool
    uint8_t duty = 3; // 0..7 for NDS PSG, scaled for CGB
    uint8_t baseNote = 60;
    uint8_t attack = 127;
    uint8_t decay = 127;
    uint8_t sustain = 127;
    uint8_t release = 127;
    uint8_t pan = 64;
};

// ============================================================================
// Nintendo DS Nitro SDAT / SSEQ / SBNK / SWAR decoder
// ============================================================================
namespace Nitro
{

static const int8_t kAdpcmIndexTab[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const uint16_t kAdpcmStepTab[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

inline bool DecodeSwav(const uint8_t* data, size_t len, Sample& out)
{
    if (len < 12) return false;
    const uint8_t waveType = data[0];
    const uint8_t loopFlag = data[1];
    const uint16_t sampleRate = RdU16(data, len, 2);
    const uint16_t loopOffsetWords = RdU16(data, len, 6);
    const uint32_t nonLoopWords = RdU32(data, len, 8);
    if (sampleRate < 500) return false;

    const size_t dataOff = 12;
    const size_t totalBytes = ((size_t)loopOffsetWords + (size_t)nonLoopWords) * 4;
    if (totalBytes == 0 || dataOff + totalBytes > len || totalBytes > (4u * 1024u * 1024u))
        return false;

    out.loop = (loopFlag != 0);
    out.baseRate = (double)sampleRate;
    const uint8_t* src = data + dataOff;

    if (waveType == 0) // PCM8 signed
    {
        out.pcm.resize(totalBytes);
        for (size_t i = 0; i < totalBytes; i++)
            out.pcm[i] = (int16_t)((int8_t)src[i]) * 256;
        out.loopStart = (uint32_t)std::min<size_t>((size_t)loopOffsetWords * 4, out.pcm.size());
        return !out.pcm.empty();
    }
    if (waveType == 1) // PCM16 signed LE
    {
        const size_t n = totalBytes / 2;
        out.pcm.resize(n);
        for (size_t i = 0; i < n; i++)
            out.pcm[i] = RdS16(src, totalBytes, i * 2);
        out.loopStart = (uint32_t)std::min<size_t>((size_t)loopOffsetWords * 2, out.pcm.size());
        return !out.pcm.empty();
    }
    if (waveType == 2) // IMA-ADPCM
    {
        if (totalBytes <= 4) return false;
        int32_t val = RdS16(src, totalBytes, 0);
        int idx = (int)(src[2] & 0x7F);
        if (idx > 88) idx = 88;
        const size_t adpcmBytes = totalBytes - 4;
        out.pcm.clear();
        out.pcm.reserve(adpcmBytes * 2);
        for (size_t i = 0; i < adpcmBytes; i++)
        {
            const uint8_t b = src[4 + i];
            for (int nib = 0; nib < 2; nib++)
            {
                const uint8_t code = (nib == 0) ? (b & 0x0F) : (b >> 4);
                const uint16_t step = kAdpcmStepTab[idx];
                int32_t diff = (int32_t)(step >> 3);
                if (code & 1) diff += (int32_t)(step >> 2);
                if (code & 2) diff += (int32_t)(step >> 1);
                if (code & 4) diff += (int32_t)step;
                val = (code & 8) ? (val - diff) : (val + diff);
                if (val < -0x7FFF) val = -0x7FFF;
                if (val > 0x7FFF) val = 0x7FFF;
                idx += kAdpcmIndexTab[code & 7];
                if (idx < 0) idx = 0;
                else if (idx > 88) idx = 88;
                out.pcm.push_back((int16_t)val);
            }
        }
        const size_t loopByte = (size_t)loopOffsetWords * 4;
        out.loopStart = (loopByte >= 4) ? (uint32_t)std::min<size_t>((loopByte - 4) * 2, out.pcm.size()) : 0u;
        return !out.pcm.empty();
    }
    return false;
}

inline uint32_t ReadVlq(const uint8_t* p, size_t len, size_t& pc)
{
    uint32_t v = 0;
    for (int i = 0; i < 4 && pc < len; i++)
    {
        const uint8_t b = p[pc++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

inline int ScoreSeqName(const std::string& name)
{
    if (name.empty()) return 0;
    int score = 0;
    if (name.find("TITLE") != std::string::npos) score += 200;
    if (name.find("OPENING") != std::string::npos) score += 120;
    if (name.find("MAIN_THEME") != std::string::npos || name.find("THEME") != std::string::npos) score += 90;
    if (name.find("BGM_") != std::string::npos || name.find("SEQ_BGM") != std::string::npos ||
        name.find("SEQ_GS_") != std::string::npos || name.find("SEQ_PL_") != std::string::npos)
        score += 40;
    if (name.find("SELECT") != std::string::npos || name.find("MENU") != std::string::npos) score += 35;
    if (name.find("MASARA") != std::string::npos || name.find("WAKABA") != std::string::npos ||
        name.find("FUTABA") != std::string::npos || name.find("KANOKO") != std::string::npos ||
        name.find("TOWN01") != std::string::npos || name.find("ROAD01") != std::string::npos)
        score += 55;
    if (name.find("FANFARE") != std::string::npos || name.find("JINGLE") != std::string::npos ||
        name.find("DUMMY") != std::string::npos || name.find("TEST") != std::string::npos)
        score -= 180;
    if (name.rfind("SE_", 0) == 0 || name.rfind("SEQ_SE", 0) == 0 ||
        name.rfind("ME_", 0) == 0 || name.rfind("SEQ_ME", 0) == 0 ||
        name.rfind("PV_", 0) == 0 || name.find("_SE_") != std::string::npos ||
        name.find("_ME_") != std::string::npos)
        score -= 220;
    // Prefer title-loop tracks over one-shot intro cuts where both exist.
    if (name.find("LOOP") != std::string::npos) score += 25;
    return score;
}

inline bool FindSdatInNds(FILE* f, uint64_t fsz, uint64_t& sdatOff, uint32_t& sdatSize)
{
    if (fsz < 0x200) return false;
    std::vector<uint8_t> hdr;
    if (!ReadFileSlice(f, 0, 0x200, hdr)) return false;

    const uint32_t fntOff = RdU32(hdr.data(), hdr.size(), 0x40);
    const uint32_t fntSize = RdU32(hdr.data(), hdr.size(), 0x44);
    const uint32_t fatOff = RdU32(hdr.data(), hdr.size(), 0x48);
    const uint32_t fatSize = RdU32(hdr.data(), hdr.size(), 0x4C);

    if (fntOff >= 0x200 && fntSize >= 8 && fntSize <= 8u * 1024u * 1024u &&
        fatOff >= 0x200 && fatSize >= 8 && fatSize <= 8u * 1024u * 1024u &&
        (uint64_t)fntOff + fntSize <= fsz && (uint64_t)fatOff + fatSize <= fsz)
    {
        std::vector<uint8_t> fnt, fat;
        if (ReadFileSlice(f, fntOff, fntSize, fnt) && ReadFileSlice(f, fatOff, fatSize, fat))
        {
            const uint16_t numDirs = RdU16(fnt.data(), fnt.size(), 6);
            const size_t maxDirs = std::min<size_t>(std::max<uint16_t>(numDirs, 1), fnt.size() / 8);
            int bestScore = -1;
            uint64_t bestOff = 0;
            uint32_t bestLen = 0;

            for (size_t d = 0; d < maxDirs; d++)
            {
                const uint32_t subOff = RdU32(fnt.data(), fnt.size(), d * 8);
                uint16_t fileId = RdU16(fnt.data(), fnt.size(), d * 8 + 4);
                size_t pos = subOff;
                while (pos < fnt.size())
                {
                    const uint8_t tag = fnt[pos++];
                    if (tag == 0) break;
                    const uint8_t nameLen = tag & 0x7F;
                    if (pos + nameLen > fnt.size()) break;
                    std::string fname = nameLen ? std::string((const char*)(fnt.data() + pos), nameLen) : std::string();
                    pos += nameLen;
                    if (tag & 0x80)
                    {
                        if (pos + 2 > fnt.size()) break;
                        pos += 2;
                        continue;
                    }
                    std::string up = UpperAscii(fname.c_str(), fname.size());
                    if (up.size() >= 5 && up.compare(up.size() - 5, 5, ".SDAT") == 0)
                    {
                        const size_t fentry = (size_t)fileId * 8;
                        if (fentry + 8 <= fat.size())
                        {
                            const uint32_t so = RdU32(fat.data(), fat.size(), fentry);
                            const uint32_t eo = RdU32(fat.data(), fat.size(), fentry + 4);
                            if (eo > so && (uint64_t)eo <= fsz)
                            {
                                const uint32_t sl = eo - so;
                                int sc = (up.find("SOUND") != std::string::npos ? 100 : 10);
                                if (up == "SOUND_DATA.SDAT" || up == "WB_SOUND_DATA.SDAT") sc += 100;
                                if (sl >= 64 * 1024) sc += 20;
                                if (sc > bestScore)
                                {
                                    bestScore = sc;
                                    bestOff = so;
                                    bestLen = sl;
                                }
                            }
                        }
                    }
                    fileId++;
                }
            }
            if (bestLen >= 0x40)
            {
                sdatOff = bestOff;
                sdatSize = bestLen;
                return true;
            }
        }
    }

    // Fallback scan for malformed/trimmed ROM FNT tables: locate "SDAT\xFE\xFF\x00\x01" directly.
    const uint64_t scanLimit = std::min<uint64_t>(fsz, 192ull * 1024ull * 1024ull);
    const size_t kChunk = 256u * 1024u;
    std::vector<uint8_t> buf;
    for (uint64_t base = 0x200; base + 0x40 <= scanLimit; base += kChunk)
    {
        const size_t sz = (size_t)std::min<uint64_t>(kChunk + 0x40, scanLimit - base);
        if (!ReadFileSlice(f, base, sz, buf)) break;
        for (size_t i = 0; i + 0x40 <= buf.size(); i += 4)
        {
            if (buf[i] == 'S' && buf[i + 1] == 'D' && buf[i + 2] == 'A' && buf[i + 3] == 'T' &&
                RdU32(buf.data(), buf.size(), i + 4) == 0x0100FEFFu)
            {
                const uint32_t sl = RdU32(buf.data(), buf.size(), i + 8);
                const uint32_t io = RdU32(buf.data(), buf.size(), i + 0x18);
                const uint32_t isz = RdU32(buf.data(), buf.size(), i + 0x1C);
                const uint32_t fo = RdU32(buf.data(), buf.size(), i + 0x20);
                const uint32_t fsz2 = RdU32(buf.data(), buf.size(), i + 0x24);
                if (sl >= 0x40 && sl <= 256u * 1024u * 1024u && base + i + sl <= fsz &&
                    isz >= 0x28 && fsz2 >= 12 && (uint64_t)io + isz <= sl && (uint64_t)fo + fsz2 <= sl)
                {
                    sdatOff = base + i;
                    sdatSize = sl;
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace Nitro

// ============================================================================
// Game Boy Advance MP2K / Sappy sequence & wave structures
// ============================================================================
namespace Mp2k
{

static const uint8_t kWaitTable[49] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 28, 30, 32, 36, 40, 42, 44,
    48, 52, 54, 56, 60, 64, 66, 68, 72, 76, 78, 80, 84, 88, 90, 92, 96
};

inline bool IsRomPtr(uint32_t addr, size_t romSize, uint32_t& outOff)
{
    if (addr < 0x08000000u || addr >= 0x0A000000u) return false;
    const uint32_t off = addr - 0x08000000u;
    if (off >= romSize) return false;
    outOff = off;
    return true;
}

inline bool ValidateSongHeader(const uint8_t* rom, size_t romSize, uint32_t hdrOff,
                               uint8_t& outTracks, uint32_t& outVgOff, int& outLoopTracks, int& outNoteScore)
{
    outTracks = 0;
    outVgOff = 0;
    outLoopTracks = 0;
    outNoteScore = 0;
    if (hdrOff + 8 > romSize) return false;
    const uint8_t trkCnt = rom[hdrOff];
    if (trkCnt == 0 || trkCnt > 16) return false;
    if (hdrOff + 8 + (size_t)trkCnt * 4 > romSize) return false;
    const uint32_t vgPtr = RdU32(rom, romSize, hdrOff + 4);
    if ((vgPtr & 3u) != 0 || !IsRomPtr(vgPtr, romSize, outVgOff)) return false;
    if (outVgOff + 12 > romSize) return false;

    for (uint8_t t = 0; t < trkCnt; t++)
    {
        uint32_t toff = 0;
        const uint32_t tptr = RdU32(rom, romSize, hdrOff + 8 + t * 4);
        if (!IsRomPtr(tptr, romSize, toff) || toff + 2 > romSize) return false;
        // Scan up to 1024 bytes cleanly for note events, GOTO (0xB2), or FINE (0xB1).
        bool sawEnd = false;
        bool clean = true;
        bool followedJump = false;
        int trkNotes = 0;
        size_t pc = toff;
        size_t limit = std::min(romSize, pc + 1024);
        uint8_t runCmd = 0;
        for (int steps = 0; steps < 512 && pc < limit; steps++)
        {
            const uint8_t op = rom[pc++];
            if (op < 0x80)
            {
                if (runCmd == 0) { clean = false; break; }
                if (pc < limit && rom[pc] < 0x80) pc++;
                if (runCmd >= 0xD0 && pc < limit && rom[pc] < 0x80) pc++; // MP2K gate-time offset
                if (runCmd >= 0xCF) { outNoteScore++; trkNotes++; }
                continue;
            }
            if (op <= 0xB0) continue;
            if (op == 0xB1) { sawEnd = true; break; }
            if (op == 0xB2 || op == 0xB3)
            {
                if (pc + 4 > romSize) { clean = false; break; }
                uint32_t tgt = 0;
                const uint32_t raw = RdU32(rom, romSize, pc);
                pc += 4;
                if (!IsRomPtr(raw, romSize, tgt)) { clean = false; break; }
                if (op == 0xB2)
                {
                    if (tgt <= pc && tgt >= toff) outLoopTracks++;
                    // Follow one stub jump if the track redirected before its first note.
                    if (!followedJump && trkNotes == 0 && tgt < romSize)
                    {
                        followedJump = true;
                        pc = tgt;
                        limit = std::min(romSize, pc + 1024);
                        runCmd = 0;
                        continue;
                    }
                    sawEnd = true;
                    break;
                }
                if (!followedJump && trkNotes == 0 && tgt < romSize)
                {
                    followedJump = true;
                    pc = tgt;
                    limit = std::min(romSize, pc + 1024);
                    runCmd = 0;
                }
                continue;
            }
            if (op == 0xB4) continue;
            if (op == 0xB5) { pc += 5; continue; }
            if (op == 0xB9)
            {
                const uint8_t sub = (pc < limit) ? rom[pc] : 0;
                pc += (sub >= 6 && sub <= 17) ? 7 : 3;
                continue;
            }
            if (op == 0xCD) { pc += 2; continue; }
            if (op >= 0xBA && op <= 0xCC) { pc += 1; continue; }
            if (op >= 0xCE)
            {
                runCmd = op;
                if (pc < limit && rom[pc] < 0x80) pc++;
                if (pc < limit && rom[pc] < 0x80) pc++;
                if (op >= 0xD0 && pc < limit && rom[pc] < 0x80) pc++; // MP2K gate-time offset (gtp)
                if (op >= 0xCF) { outNoteScore++; trkNotes++; }
                continue;
            }
            clean = false;
            break;
        }
        if (!clean && !sawEnd) return false;
    }
    outTracks = trkCnt;
    return outNoteScore >= 2;
}

} // namespace Mp2k

// ============================================================================
// Unified Polyphonic Mixer + 9-Band Spectrum Meter
// ============================================================================
class Player
{
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kMaxTracks = 16;
    static constexpr int kMaxVoices = 24;
    static constexpr int kBandCount = 9;

    enum class SeqMode { None, NitroSseq, GbaMp2k };

    void Clear()
    {
        mode_ = SeqMode::None;
        loadedPath_.clear();
        active_ = false;
        tickAccum_ = 0.0;
        tempoBpm_ = 120.0;
        masterVol_ = 127;
        samples_.clear();
        insts_.clear();
        progs_.clear();
        keySplitMap_.clear();
        std::vector<uint8_t>().swap(seqData_);
        seqBaseRomOff_ = 0;
        gbaRomLimit_ = 0;
        tracks_.clear();
        dcPrevInL_ = 0.0;
        dcPrevOutL_ = 0.0;
        dcPrevInR_ = 0.0;
        dcPrevOutR_ = 0.0;
        for (Voice& v : voices_) v.active = false;
        bands_.fill(0.0f);
    }

    bool IsLoaded() const { return mode_ != SeqMode::None && active_; }
    const std::string& LoadedPath() const { return loadedPath_; }

    void SetVolume(int pct)
    {
        volumePct_ = std::clamp(pct, 0, 100);
    }

    int Volume() const { return volumePct_; }

    bool LoadFromRom(const std::string& path, int platform, const std::string& gameCode)
    {
        (void)gameCode;
        if (path == loadedPath_ && mode_ != SeqMode::None) return active_;
        Clear();
        loadedPath_ = path;
        if (path.empty()) return false;

        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        const uint64_t fsz = FileSize(f);
        bool ok = false;
        if (platform == 1) // GBA (.gba MP2K)
            ok = LoadGba(f, fsz);
        else // DS (.nds SDAT)
            ok = LoadNds(f, fsz);
        fclose(f);

        if (!ok) Clear();
        else active_ = true;
        return ok;
    }

    // Mixes |frames| stereo int16 samples (2 * frames ints) and updates 9 live equalizer bands.
    void Mix(int16_t* stereoOut, int frames)
    {
        if (!stereoOut || frames <= 0) return;
        const size_t totalSamples = (size_t)frames * 2;
        for (size_t i = 0; i < totalSamples; i++) stereoOut[i] = 0;
        if (!active_ || mode_ == SeqMode::None || volumePct_ <= 0)
        {
            for (int b = 0; b < kBandCount; b++) bands_[b] *= 0.80f;
            return;
        }

        // Goertzel filter bank across 9 perceptually spaced bands (75 Hz .. 7200 Hz).
        static constexpr float kBandHz[kBandCount] = {
            75.0f, 140.0f, 250.0f, 440.0f, 780.0f, 1380.0f, 2400.0f, 4200.0f, 7200.0f
        };
        float q0[kBandCount] = {};
        float q1[kBandCount] = {};
        float q2[kBandCount] = {};
        float coeff[kBandCount] = {};
        for (int b = 0; b < kBandCount; b++)
        {
            const float w = 2.0f * 3.14159265f * kBandHz[b] / (float)kSampleRate;
            coeff[b] = 2.0f * std::cos(w);
        }

        const double volScale = (volumePct_ / 100.0) * (masterVol_ / 127.0) * 0.42;
        const int gFrames = std::min(frames, 512);

        for (int i = 0; i < frames; i++)
        {
            StepSequencerOneSample();

            double mixL = 0.0;
            double mixR = 0.0;
            for (Voice& v : voices_)
            {
                if (!v.active) continue;
                const double env = StepEnvelope(v);
                if (!v.active) continue;
                const double samp = SampleVoice(v) * env * v.gain;
                mixL += samp * v.panL;
                mixR += samp * v.panR;
            }

            // First-order DC blocker to keep PSG/ADPCM bias clean at low volume
            const double hpL = mixL - dcPrevInL_ + 0.996 * dcPrevOutL_;
            dcPrevInL_ = mixL;
            dcPrevOutL_ = hpL;
            const double hpR = mixR - dcPrevInR_ + 0.996 * dcPrevOutR_;
            dcPrevInR_ = mixR;
            dcPrevOutR_ = hpR;

            double outL = std::tanh(hpL * volScale) * 28000.0;
            double outR = std::tanh(hpR * volScale) * 28000.0;
            const int32_t iL = std::clamp((int32_t)outL, -32767, 32767);
            const int32_t iR = std::clamp((int32_t)outR, -32767, 32767);
            stereoOut[i * 2 + 0] = (int16_t)iL;
            stereoOut[i * 2 + 1] = (int16_t)iR;

            if (i < gFrames)
            {
                const float mono = ((float)iL + (float)iR) * (0.5f / 32768.0f);
                for (int b = 0; b < kBandCount; b++)
                {
                    q0[b] = coeff[b] * q1[b] - q2[b] + mono;
                    q2[b] = q1[b];
                    q1[b] = q0[b];
                }
            }
        }

        const float norm = 1.0f / std::max(64, gFrames);
        for (int b = 0; b < kBandCount; b++)
        {
            const float p = q1[b] * q1[b] + q2[b] * q2[b] - coeff[b] * q1[b] * q2[b];
            float mag = std::sqrt(std::max(0.0f, p)) * norm * 5.5f;
            // Weight upper bands slightly higher so treble melodies shine alongside bass.
            mag *= (1.0f + 0.14f * (float)b);
            mag = std::clamp(mag, 0.0f, 1.0f);
            const float smooth = (mag > bands_[b]) ? 0.55f : 0.18f;
            bands_[b] = bands_[b] * (1.0f - smooth) + mag * smooth;
        }
    }

    void GetBands(float out[kBandCount], bool& outActive) const
    {
        outActive = active_ && volumePct_ > 0 && mode_ != SeqMode::None;
        for (int i = 0; i < kBandCount; i++) out[i] = bands_[i];
    }

private:
    struct Track
    {
        bool active = false;
        size_t pc = 0;
        uint32_t waitTicks = 0;
        int prog = 0;
        uint8_t vol = 127;
        uint8_t expr = 127;
        uint8_t pan = 64;
        int8_t transpose = 0;
        int8_t bend = 0;
        uint8_t bendRange = 2;
        bool noteWait = true;
        bool tie = false;
        uint8_t att = 255;
        uint8_t dec = 255;
        uint8_t sus = 255;
        uint8_t rel = 255;
        int16_t vars[16] = {};
        bool cmpFlag = false;
        uint8_t runningCmd = 0; // MP2K running status
        uint8_t lastNote = 60;
        uint8_t lastVel = 100;
        size_t startPc = SIZE_MAX;
        size_t loopStartPc = 0;
        uint8_t loopRemain = 0;
        bool inLoop = false;
        std::array<size_t, 8> callStack{};
        int callSp = 0;
    };

    enum class EnvStage { Attack, Decay, Sustain, Release };

    struct Voice
    {
        bool active = false;
        int trackIdx = 0;
        int note = 60;
        bool tied = false;
        uint32_t holdSamples = 0;
        WaveKind kind = WaveKind::None;
        int sampleIndex = -1;
        uint8_t duty = 3;
        double pos = 0.0;
        double inc = 0.0;
        uint16_t lfsr = 0x7FFF;
        EnvStage stage = EnvStage::Attack;
        double envLevel = 0.0;
        double attRate = 0.02;
        double decMul = 0.999;
        double susLevel = 0.8;
        double relMul = 0.999;
        double velGain = 1.0;
        double gain = 0.0;
        double panL = 0.707;
        double panR = 0.707;
    };

    // Instrument lookup table for DrumSet (16) and KeySplit (17): stored compactly per program/key
    struct ProgramSlot
    {
        uint8_t type = 0; // 0=none, 1=single, 16=drum, 17=split
        int directInst = -1;
        uint8_t lowNote = 0;
        uint8_t highNote = 127;
        std::array<uint8_t, 8> splitUpper{};
        int subBase = 0; // index into keySplitMap_
        int subCount = 0;
    };

    SeqMode mode_ = SeqMode::None;
    bool active_ = false;
    std::string loadedPath_;
    int volumePct_ = 80;
    double tickAccum_ = 0.0;
    double tempoBpm_ = 120.0;
    uint8_t masterVol_ = 127;
    uint64_t rng_ = 0x9E3779B97F4A7C15ULL;
    double dcPrevInL_ = 0.0;
    double dcPrevOutL_ = 0.0;
    double dcPrevInR_ = 0.0;
    double dcPrevOutR_ = 0.0;

    std::vector<Sample> samples_;
    std::vector<Instrument> insts_;
    std::vector<ProgramSlot> progs_;
    std::vector<int> keySplitMap_; // indices into insts_
    std::vector<uint8_t> seqData_;
    uint32_t seqBaseRomOff_ = 0;
    uint32_t gbaRomLimit_ = 0;
    std::vector<Track> tracks_;
    std::array<Voice, kMaxVoices> voices_{};
    std::array<float, kBandCount> bands_{};

    uint32_t NextRand()
    {
        rng_ ^= rng_ >> 12;
        rng_ ^= rng_ << 25;
        rng_ ^= rng_ >> 27;
        return (uint32_t)((rng_ * 0x2545F4914F6CDD1DULL) >> 32);
    }

    const Instrument* ResolveInstrument(int prog, int note) const
    {
        if (prog < 0 || prog >= (int)progs_.size()) return nullptr;
        const ProgramSlot& ps = progs_[prog];
        if (ps.type == 1 && ps.directInst >= 0 && ps.directInst < (int)insts_.size())
            return &insts_[ps.directInst];
        if (ps.type == 16 && note >= ps.lowNote && note <= ps.highNote)
        {
            const int idx = note - ps.lowNote;
            if (idx >= 0 && idx < ps.subCount && ps.subBase >= 0 && ps.subBase + idx < (int)keySplitMap_.size())
            {
                const int ii = keySplitMap_[ps.subBase + idx];
                if (ii >= 0 && ii < (int)insts_.size()) return &insts_[ii];
            }
        }
        if (ps.type == 17)
        {
            for (int r = 0; r < 8 && r < ps.subCount; r++)
            {
                if (note <= ps.splitUpper[r])
                {
                    if (ps.subBase >= 0 && ps.subBase + r < (int)keySplitMap_.size())
                    {
                        const int ii = keySplitMap_[ps.subBase + r];
                        if (ii >= 0 && ii < (int)insts_.size()) return &insts_[ii];
                    }
                    break;
                }
            }
        }
        return nullptr;
    }

    static double EnvParamToAttackInc(uint8_t a)
    {
        if (a >= 127) return 1.0;
        const double ms = 2.0 + (double)(127 - a) * (127 - a) * 0.08;
        return 1.0 / std::max(1.0, ms * (kSampleRate / 1000.0));
    }

    static double EnvParamToDecayMul(uint8_t d)
    {
        // Nitro SSEQ/SBNK decay: 127 = steepest (fast decay to sustain), 0 = longest slow fade.
        const double ms = 2.5 + (double)(127 - std::min<uint8_t>(127, d)) * (127 - std::min<uint8_t>(127, d)) * 0.35;
        return std::exp(-6.9 / std::max(1.0, ms * (kSampleRate / 1000.0)));
    }

    static double EnvParamToRelMul(uint8_t r)
    {
        // Nitro SSEQ/SBNK release: 127 = fast crisp cutoff (~3 ms), 0 = long slow tail.
        const double ms = 3.0 + (double)(127 - std::min<uint8_t>(127, r)) * (127 - std::min<uint8_t>(127, r)) * 0.25;
        return std::exp(-6.9 / std::max(1.0, ms * (kSampleRate / 1000.0)));
    }

    void StartNote(int trackIdx, uint8_t rawNote, uint8_t vel, uint32_t durationTicks)
    {
        if (trackIdx < 0 || trackIdx >= (int)tracks_.size()) return;
        const Track& tr = tracks_[trackIdx];
        const Instrument* inst = ResolveInstrument(tr.prog, rawNote);
        if (!inst || inst->kind == WaveKind::None) return;

        // Allocate voice: free slot or oldest/released slot or lowest gain
        int slot = -1;
        double lowestScore = 1e9;
        for (int i = 0; i < kMaxVoices; i++)
        {
            if (!voices_[i].active) { slot = i; break; }
            double sc = voices_[i].envLevel * (voices_[i].stage == EnvStage::Release ? 0.25 : 1.0);
            if (sc < lowestScore) { lowestScore = sc; slot = i; }
        }
        if (slot < 0) slot = 0;

        Voice& v = voices_[slot];
        v = Voice{};
        v.active = true;
        v.trackIdx = trackIdx;
        v.note = rawNote;
        v.tied = tr.tie;
        v.kind = inst->kind;
        v.sampleIndex = inst->sampleIndex;
        v.duty = inst->duty;

        const double ppqn = (mode_ == SeqMode::NitroSseq) ? 48.0 : 24.0;
        const double secPerTick = 60.0 / (std::max(30.0, tempoBpm_) * ppqn);
        if (durationTicks > 0)
            v.holdSamples = (uint32_t)std::max(1.0, durationTicks * secPerTick * kSampleRate);
        else
            v.holdSamples = tr.tie ? 0x7FFFFFFFu : 1u;

        const uint8_t att = (tr.att <= 127) ? tr.att : inst->attack;
        const uint8_t dec = (tr.dec <= 127) ? tr.dec : inst->decay;
        const uint8_t sus = (tr.sus <= 127) ? tr.sus : inst->sustain;
        const uint8_t rel = (tr.rel <= 127) ? tr.rel : inst->release;

        if (mode_ == SeqMode::NitroSseq)
        {
            v.attRate = EnvParamToAttackInc(att);
            v.decMul = EnvParamToDecayMul(dec);
            v.susLevel = (double)std::min<uint8_t>(127, sus) / 127.0;
            v.relMul = EnvParamToRelMul(rel);
        }
        else // MP2K frame-rate (~59.7 Hz = ~800 samples/tick): env *= p / 256 per frame
        {
            v.attRate = att >= 254 ? 1.0 : std::clamp((double)att / (255.0 * 800.0), 0.00001, 1.0);
            v.decMul = dec == 0 ? 0.93 : (dec >= 255 ? 0.99996 : std::pow((double)dec / 256.0, 1.0 / 800.0));
            v.susLevel = std::clamp((double)sus / 255.0, 0.0, 1.0);
            v.relMul = rel == 0 ? 0.95 : (rel >= 255 ? 0.99996 : std::pow((double)rel / 256.0, 1.0 / 800.0));
        }

        v.stage = (v.attRate >= 0.99) ? EnvStage::Decay : EnvStage::Attack;
        v.envLevel = (v.attRate >= 0.99) ? 1.0 : 0.0;

        const int semiOffset = ((int)rawNote - (int)inst->baseNote) + tr.transpose;
        const double bendSemis = ((double)tr.bend / 64.0) * (double)tr.bendRange;
        if (v.kind == WaveKind::Pcm && v.sampleIndex >= 0 && v.sampleIndex < (int)samples_.size())
        {
            const double ratio = inst->fixedFreq ? 1.0 : std::pow(2.0, (semiOffset + bendSemis) / 12.0);
            v.inc = (samples_[v.sampleIndex].baseRate * ratio) / (double)kSampleRate;
        }
        else
            v.inc = (440.0 * std::pow(2.0, (rawNote + tr.transpose - 69 + bendSemis) / 12.0) * 8.0) / (double)kSampleRate;

        // Combine channel/track/instrument volume with quadratic perceptual curve
        v.velGain = std::clamp((double)vel / 127.0, 0.0, 1.0);
        const double vg = v.velGain * (tr.vol / 127.0) * (tr.expr / 127.0);
        v.gain = vg * vg;
        const int instPan = (inst->pan <= 127) ? (int)inst->pan : 64;
        const int panVal = std::clamp((int)tr.pan + (instPan - 64), 0, 127);
        const double p = panVal / 127.0;
        v.panL = std::cos(p * 1.5707963);
        v.panR = std::sin(p * 1.5707963);
    }

    void ReleaseTrackVoices(int trackIdx, int noteFilter = -1)
    {
        for (Voice& v : voices_)
        {
            if (v.active && v.trackIdx == trackIdx && (noteFilter < 0 || v.note == noteFilter))
                v.stage = EnvStage::Release;
        }
    }

    void UpdateTrackVoicePitchAndVol(int trackIdx)
    {
        if (trackIdx < 0 || trackIdx >= (int)tracks_.size()) return;
        const Track& tr = tracks_[trackIdx];
        for (Voice& v : voices_)
        {
            if (!v.active || v.trackIdx != trackIdx) continue;
            const Instrument* inst = ResolveInstrument(tr.prog, v.note);
            const int base = inst ? inst->baseNote : 60;
            const int semiOffset = (v.note - base) + tr.transpose;
            const double bendSemis = ((double)tr.bend / 64.0) * (double)tr.bendRange;
            if (v.kind == WaveKind::Pcm && v.sampleIndex >= 0 && v.sampleIndex < (int)samples_.size())
            {
                const double ratio = (inst && inst->fixedFreq) ? 1.0 : std::pow(2.0, (semiOffset + bendSemis) / 12.0);
                v.inc = (samples_[v.sampleIndex].baseRate * ratio) / (double)kSampleRate;
            }
            else if (v.kind == WaveKind::Pulse || v.kind == WaveKind::Noise)
                v.inc = (440.0 * std::pow(2.0, (v.note + tr.transpose - 69 + bendSemis) / 12.0) * 8.0) / (double)kSampleRate;

            const double vg = v.velGain * (tr.vol / 127.0) * (tr.expr / 127.0);
            v.gain = vg * vg;
            const int instPan = (inst && inst->pan <= 127) ? (int)inst->pan : 64;
            const int panVal = std::clamp((int)tr.pan + (instPan - 64), 0, 127);
            const double p = panVal / 127.0;
            v.panL = std::cos(p * 1.5707963);
            v.panR = std::sin(p * 1.5707963);
        }
    }

    double StepEnvelope(Voice& v)
    {
        if (v.stage != EnvStage::Release && !v.tied && v.holdSamples > 0)
        {
            if (--v.holdSamples == 0) v.stage = EnvStage::Release;
        }
        switch (v.stage)
        {
        case EnvStage::Attack:
            v.envLevel += v.attRate;
            if (v.envLevel >= 1.0) { v.envLevel = 1.0; v.stage = EnvStage::Decay; }
            break;
        case EnvStage::Decay:
            v.envLevel = v.susLevel + (v.envLevel - v.susLevel) * v.decMul;
            if (v.envLevel <= v.susLevel + 0.002) { v.envLevel = v.susLevel; v.stage = EnvStage::Sustain; }
            break;
        case EnvStage::Sustain:
            if (v.susLevel <= 0.001) { v.active = false; return 0.0; }
            break;
        case EnvStage::Release:
            v.envLevel *= v.relMul;
            if (v.envLevel <= 0.002) { v.active = false; return 0.0; }
            break;
        }
        return v.envLevel;
    }

    double SampleVoice(Voice& v)
    {
        if (v.kind == WaveKind::Pulse)
        {
            v.pos += v.inc;
            if (v.pos >= 8.0) v.pos -= std::floor(v.pos / 8.0) * 8.0;
            const int step = (int)v.pos & 7;
            // NDS PSG duty threshold: duty 0=1/8 .. duty 6=7/8
            const int hi = std::clamp((int)v.duty + 1, 1, 7);
            return (step < hi) ? -0.30 : 0.30;
        }
        if (v.kind == WaveKind::Noise)
        {
            v.pos += v.inc * 2.5;
            while (v.pos >= 1.0)
            {
                v.pos -= 1.0;
                const uint16_t bit = ((v.lfsr & 1u) ^ ((v.lfsr >> 1) & 1u)) & 1u;
                v.lfsr = (uint16_t)((v.lfsr >> 1) | (bit << 14));
                if (v.lfsr == 0) v.lfsr = 0x7FFF;
            }
            return (v.lfsr & 1u) ? 0.25 : -0.25;
        }
        if (v.kind == WaveKind::Pcm && v.sampleIndex >= 0 && v.sampleIndex < (int)samples_.size())
        {
            const Sample& s = samples_[v.sampleIndex];
            if (s.pcm.empty()) { v.active = false; return 0.0; }
            const size_t n = s.pcm.size();
            size_t idx = (size_t)v.pos;
            if (idx >= n)
            {
                if (!s.loop || s.loopStart >= n) { v.active = false; return 0.0; }
                const size_t loopLen = n - s.loopStart;
                v.pos = (double)s.loopStart + std::fmod(v.pos - (double)s.loopStart, (double)loopLen);
                idx = (size_t)v.pos;
                if (idx >= n) idx = s.loopStart;
            }
            const double frac = v.pos - (double)idx;
            const double s0 = (double)s.pcm[idx] / 32768.0;
            size_t idx1 = idx + 1;
            if (idx1 >= n) idx1 = (s.loop && s.loopStart < n) ? s.loopStart : idx;
            const double s1 = (double)s.pcm[idx1] / 32768.0;
            v.pos += v.inc;
            return s0 + (s1 - s0) * frac;
        }
        v.active = false;
        return 0.0;
    }

    void StepSequencerOneSample()
    {
        const double ppqn = (mode_ == SeqMode::NitroSseq) ? 48.0 : 24.0;
        const double ticksPerSample = (std::max(30.0, tempoBpm_) * ppqn) / (60.0 * (double)kSampleRate);
        tickAccum_ += ticksPerSample;
        while (tickAccum_ >= 1.0)
        {
            tickAccum_ -= 1.0;
            for (int ti = 0; ti < (int)tracks_.size(); ti++)
            {
                Track& tr = tracks_[ti];
                if (!tr.active) continue;
                if (tr.waitTicks > 0)
                {
                    if (--tr.waitTicks > 0) continue;
                }
                if (mode_ == SeqMode::NitroSseq) ExecuteSseqTrack(ti);
                else ExecuteMp2kTrack(ti);
            }
            // Restart seamlessly if every track stopped naturally (e.g. non-looping ending).
            bool anyActive = false;
            for (const Track& t : tracks_) if (t.active) { anyActive = true; break; }
            if (!anyActive && !tracks_.empty())
            {
                for (Track& t : tracks_)
                {
                    if (t.startPc < seqData_.size())
                    {
                        t.active = true;
                        t.pc = t.startPc;
                        t.waitTicks = 0;
                        t.callSp = 0;
                        t.inLoop = false;
                    }
                }
                if (mode_ == SeqMode::NitroSseq)
                {
                    tracks_[0].active = true;
                    tracks_[0].pc = 0;
                    tracks_[0].waitTicks = 0;
                    tracks_[0].callSp = 0;
                    tracks_[0].inLoop = false;
                }
            }
        }
    }

    void ExecuteSseqTrack(int ti)
    {
        Track& tr = tracks_[ti];
        const uint8_t* d = seqData_.data();
        const size_t n = seqData_.size();
        int loopJumps = 0;
        for (int iter = 0; iter < 64 && tr.active && tr.pc < n; iter++)
        {
            uint8_t cmd = d[tr.pc++];
            bool rndMode = false;
            bool varMode = false;
            bool exec = true;
            if (cmd == 0xA2) // conditional if
            {
                if (tr.pc >= n) { tr.active = false; break; }
                exec = tr.cmpFlag;
                cmd = d[tr.pc++];
            }
            if (cmd == 0xA0) { rndMode = true; if (tr.pc >= n) break; cmd = d[tr.pc++]; }
            else if (cmd == 0xA1) { varMode = true; if (tr.pc >= n) break; cmd = d[tr.pc++]; }

            auto readLastParam = [&](bool isSigned16) -> int32_t {
                if (rndMode)
                {
                    const int16_t lo = RdS16(d, n, tr.pc); tr.pc += 2;
                    const int16_t hi = RdS16(d, n, tr.pc); tr.pc += 2;
                    const int span = std::abs((int)hi - (int)lo) + 1;
                    return std::min(lo, hi) + (int)(NextRand() % (uint32_t)std::max(1, span));
                }
                if (varMode)
                {
                    const uint8_t vid = (tr.pc < n) ? d[tr.pc++] : 0;
                    return (vid < 16) ? tr.vars[vid] : 0;
                }
                if (isSigned16) { const int16_t v = RdS16(d, n, tr.pc); tr.pc += 2; return v; }
                return 0;
            };

            if (cmd < 0x80)
            {
                const uint8_t vel = (tr.pc < n) ? d[tr.pc++] : 100;
                const uint32_t dur = (rndMode || varMode) ? (uint32_t)std::max(0, readLastParam(false)) : Nitro::ReadVlq(d, n, tr.pc);
                if (exec)
                {
                    StartNote(ti, cmd, vel, dur);
                    if (tr.noteWait && dur > 0) { tr.waitTicks = dur; break; }
                }
                continue;
            }
            if (cmd == 0x80)
            {
                const uint32_t dur = (rndMode || varMode) ? (uint32_t)std::max(0, readLastParam(false)) : Nitro::ReadVlq(d, n, tr.pc);
                if (exec && dur > 0) { tr.waitTicks = dur; break; }
                continue;
            }
            if (cmd == 0x81)
            {
                const int32_t prg = (rndMode || varMode) ? readLastParam(false) : (int32_t)Nitro::ReadVlq(d, n, tr.pc);
                if (exec) tr.prog = std::max(0, prg);
                continue;
            }
            if (cmd == 0x93)
            {
                if (tr.pc + 4 > n) { tr.active = false; break; }
                const uint8_t newIdx = d[tr.pc++];
                const uint32_t off = RdU24(d, n, tr.pc); tr.pc += 3;
                if (exec && newIdx < kMaxTracks && (int)newIdx != ti && off < n)
                {
                    if ((int)newIdx >= (int)tracks_.size()) continue;
                    tracks_[newIdx] = Track{};
                    tracks_[newIdx].active = true;
                    tracks_[newIdx].pc = off;
                    tracks_[newIdx].loopStartPc = off;
                }
                continue;
            }
            if (cmd == 0x94)
            {
                if (tr.pc + 3 > n) { tr.active = false; break; }
                const uint32_t off = RdU24(d, n, tr.pc); tr.pc += 3;
                if (exec && off < n)
                {
                    const bool backward = (off < tr.pc);
                    tr.pc = off;
                    if (backward && ++loopJumps >= 8) { tr.waitTicks = 1; break; }
                }
                continue;
            }
            if (cmd == 0x95)
            {
                if (tr.pc + 3 > n) { tr.active = false; break; }
                const uint32_t off = RdU24(d, n, tr.pc); tr.pc += 3;
                if (exec && off < n && tr.callSp >= 0 && tr.callSp < (int)tr.callStack.size())
                {
                    tr.callStack[tr.callSp++] = tr.pc;
                    tr.pc = off;
                }
                continue;
            }
            if (cmd >= 0xB0 && cmd <= 0xBD)
            {
                if (tr.pc >= n) break;
                const uint8_t vid = d[tr.pc++];
                const int16_t rhs = (rndMode || varMode) ? (int16_t)readLastParam(true) : [&]() { const int16_t x = RdS16(d, n, tr.pc); tr.pc += 2; return x; }();
                if (exec && vid < 16)
                {
                    int32_t v = tr.vars[vid];
                    switch (cmd)
                    {
                    case 0xB0: v = rhs; break;
                    case 0xB1: v += rhs; break;
                    case 0xB2: v -= rhs; break;
                    case 0xB3: v *= rhs; break;
                    case 0xB4: if (rhs) v /= rhs; break;
                    case 0xB5: v = (rhs >= 0) ? (decltype(v))((uint32_t)v << std::min(15, (int)rhs)) : (v >> std::min(15, -(int)rhs)); break;
                    case 0xB6: v = rhs > 0 ? (int32_t)(NextRand() % (uint32_t)(rhs + 1)) : 0; break;
                    case 0xB8: tr.cmpFlag = (v == rhs); break;
                    case 0xB9: tr.cmpFlag = (v >= rhs); break;
                    case 0xBA: tr.cmpFlag = (v > rhs); break;
                    case 0xBB: tr.cmpFlag = (v <= rhs); break;
                    case 0xBC: tr.cmpFlag = (v < rhs); break;
                    case 0xBD: tr.cmpFlag = (v != rhs); break;
                    default: break;
                    }
                    tr.vars[vid] = (int16_t)v;
                }
                continue;
            }
            if (cmd >= 0xC0 && cmd <= 0xD7)
            {
                const int32_t arg = (rndMode || varMode) ? readLastParam(false) : (int32_t)(tr.pc < n ? d[tr.pc++] : 0);
                if (exec)
                {
                    switch (cmd)
                    {
                    case 0xC0: tr.pan = (uint8_t)std::clamp(arg, 0, 127); UpdateTrackVoicePitchAndVol(ti); break;
                    case 0xC1: tr.vol = (uint8_t)std::clamp(arg, 0, 127); UpdateTrackVoicePitchAndVol(ti); break;
                    case 0xC2: masterVol_ = (uint8_t)std::clamp(arg, 0, 127); break;
                    case 0xC3: tr.transpose = (int8_t)arg; UpdateTrackVoicePitchAndVol(ti); break;
                    case 0xC4: tr.bend = (int8_t)arg; UpdateTrackVoicePitchAndVol(ti); break;
                    case 0xC5: tr.bendRange = (uint8_t)std::max(0, arg); UpdateTrackVoicePitchAndVol(ti); break;
                    case 0xC7: tr.noteWait = (arg != 0); break;
                    case 0xC8: tr.tie = (arg != 0); if (!tr.tie) ReleaseTrackVoices(ti); break;
                    case 0xD0: tr.att = (uint8_t)std::clamp(arg, 0, 127); break;
                    case 0xD1: tr.dec = (uint8_t)std::clamp(arg, 0, 127); break;
                    case 0xD2: tr.sus = (uint8_t)std::clamp(arg, 0, 127); break;
                    case 0xD3: tr.rel = (uint8_t)std::clamp(arg, 0, 127); break;
                    case 0xD4: tr.loopRemain = (uint8_t)std::max(0, arg); tr.loopStartPc = tr.pc; tr.inLoop = true; break;
                    case 0xD5: tr.expr = (uint8_t)std::clamp(arg, 0, 127); UpdateTrackVoicePitchAndVol(ti); break;
                    default: break;
                    }
                }
                continue;
            }
            if (cmd >= 0xE0 && cmd <= 0xE4)
            {
                const int32_t arg = (rndMode || varMode) ? readLastParam(true) : [&]() { const uint16_t u = RdU16(d, n, tr.pc); tr.pc += 2; return (int32_t)u; }();
                if (exec && cmd == 0xE1 && arg >= 30 && arg <= 320) tempoBpm_ = (double)arg;
                continue;
            }
            if (cmd == 0xFC) // loop end
            {
                if (exec && tr.inLoop)
                {
                    if (tr.loopRemain == 0)
                    {
                        tr.pc = tr.loopStartPc;
                        if (++loopJumps >= 8) { tr.waitTicks = 1; break; }
                    }
                    else if (--tr.loopRemain > 0)
                    {
                        tr.pc = tr.loopStartPc;
                        if (++loopJumps >= 8) { tr.waitTicks = 1; break; }
                    }
                    else tr.inLoop = false;
                }
                continue;
            }
            if (cmd == 0xFD) // return
            {
                if (exec && tr.callSp > 0 && tr.callSp <= (int)tr.callStack.size()) tr.pc = tr.callStack[--tr.callSp];
                continue;
            }
            if (cmd == 0xFE) { tr.pc += 2; continue; } // alloctrack bitmask
            if (cmd == 0xFF) { if (exec) { tr.active = false; ReleaseTrackVoices(ti); break; } continue; }
            tr.active = false;
            break;
        }
    }

    void ExecuteMp2kTrack(int ti)
    {
        Track& tr = tracks_[ti];
        const uint8_t* d = seqData_.data();
        const size_t n = seqData_.size();
        int loopJumps = 0;
        auto resolveSeqPtr = [&](uint32_t rawPtr, size_t& outOff) -> bool {
            uint32_t romOff = 0;
            if (!Mp2k::IsRomPtr(rawPtr, gbaRomLimit_, romOff)) return false;
            if (romOff < seqBaseRomOff_) return false;
            const size_t rel = (size_t)(romOff - seqBaseRomOff_);
            if (rel >= n) return false;
            outOff = rel;
            return true;
        };
        for (int iter = 0; iter < 64 && tr.active && tr.pc < n; iter++)
        {
            uint8_t op = d[tr.pc++];
            if (op >= 0x80 && op <= 0xB0) // W00..W96 wait
            {
                const uint32_t w = Mp2k::kWaitTable[op - 0x80];
                if (w > 0) { tr.waitTicks = w; break; }
                continue;
            }
            if (op == 0xB1) { tr.active = false; ReleaseTrackVoices(ti); break; }
            if (op == 0xB2 || op == 0xB3)
            {
                const uint32_t rawPtr = RdU32(d, n, tr.pc); tr.pc += 4;
                size_t off = 0;
                if (resolveSeqPtr(rawPtr, off))
                {
                    if (op == 0xB3 && tr.callSp >= 0 && tr.callSp < 4) tr.callStack[tr.callSp++] = tr.pc;
                    const bool backward = (off < tr.pc);
                    tr.pc = off;
                    if (op == 0xB2 && backward && ++loopJumps >= 8) { tr.waitTicks = 1; break; }
                }
                else tr.active = false;
                continue;
            }
            if (op == 0xB4)
            {
                if (tr.callSp > 0 && tr.callSp <= 4) tr.pc = tr.callStack[--tr.callSp];
                continue;
            }
            if (op == 0xB5)
            {
                const uint8_t cnt = (tr.pc < n) ? d[tr.pc++] : 0;
                const uint32_t rawPtr = RdU32(d, n, tr.pc); tr.pc += 4;
                size_t off = 0;
                if (!tr.inLoop) { tr.loopRemain = cnt; tr.inLoop = true; }
                if (tr.loopRemain == 0 || --tr.loopRemain > 0)
                {
                    if (resolveSeqPtr(rawPtr, off))
                    {
                        tr.pc = off;
                        if (++loopJumps >= 8) { tr.waitTicks = 1; break; }
                    }
                    else tr.active = false;
                }
                else tr.inLoop = false;
                continue;
            }
            if (op == 0xB9)
            {
                const uint8_t sub = (tr.pc < n) ? d[tr.pc] : 0;
                tr.pc += (sub >= 6 && sub <= 17) ? 7u : 3u;
                continue;
            }
            if (op == 0xCD) { tr.pc += 2; continue; }
            if (op >= 0xBA && op <= 0xCC)
            {
                const uint8_t arg = (tr.pc < n) ? d[tr.pc++] : 0;
                switch (op)
                {
                case 0xBB: tempoBpm_ = std::clamp((int)arg * 2, 40, 300); break;
                case 0xBC: tr.transpose = (int8_t)arg; UpdateTrackVoicePitchAndVol(ti); break;
                case 0xBD: tr.prog = arg; break;
                case 0xBE: tr.vol = std::min<uint8_t>(127, arg); UpdateTrackVoicePitchAndVol(ti); break;
                case 0xBF: tr.pan = (uint8_t)std::clamp((int)arg, 0, 127); UpdateTrackVoicePitchAndVol(ti); break;
                case 0xC0: tr.bend = (int8_t)std::clamp((int)arg - 64, -64, 63); UpdateTrackVoicePitchAndVol(ti); break;
                case 0xC1: tr.bendRange = std::max<uint8_t>(1, arg); UpdateTrackVoicePitchAndVol(ti); break;
                default: break;
                }
                continue;
            }

            uint8_t cmd = op;
            uint8_t note = tr.lastNote;
            uint8_t vel = tr.lastVel;
            uint32_t gateExtra = 0;
            bool hasNoteArg = false;
            if (op < 0x80)
            {
                cmd = tr.runningCmd;
                note = op;
                tr.lastNote = note;
                hasNoteArg = true;
            }
            else if (op >= 0xCE)
            {
                tr.runningCmd = op;
                if (tr.pc < n && d[tr.pc] < 0x80)
                {
                    note = d[tr.pc++];
                    tr.lastNote = note;
                    hasNoteArg = true;
                }
            }
            else
            {
                tr.active = false;
                break;
            }
            if (hasNoteArg && tr.pc < n && d[tr.pc] < 0x80)
            {
                vel = d[tr.pc++];
                tr.lastVel = vel;
                if (cmd >= 0xD0 && tr.pc < n && d[tr.pc] < 0x80)
                    gateExtra = d[tr.pc++]; // MP2K gtp fine duration offset (0..3 ticks)
            }

            if (cmd == 0xCE) // EOT
            {
                ReleaseTrackVoices(ti, hasNoteArg ? (int)note : -1);
            }
            else if (cmd == 0xCF) // TIE
            {
                tr.tie = true;
                StartNote(ti, note, vel, 0);
                tr.tie = false;
            }
            else if (cmd >= 0xD0)
            {
                const uint32_t dur = (uint32_t)Mp2k::kWaitTable[cmd - 0xCF] + gateExtra;
                StartNote(ti, note, vel, dur);
            }
        }
    }

    bool ProbeAudible(int maxSamples)
    {
        for (Voice& v : voices_) v.active = false;
        tickAccum_ = 0.0;
        tempoBpm_ = 120.0;
        masterVol_ = 127;
        dcPrevInL_ = dcPrevOutL_ = dcPrevInR_ = dcPrevOutR_ = 0.0;
        if (mode_ == SeqMode::NitroSseq)
        {
            tracks_.assign(kMaxTracks, Track{});
            tracks_[0].active = true;
            tracks_[0].pc = 0;
            tracks_[0].startPc = 0;
        }
        const std::vector<Track> initTracks = tracks_;

        bool audible = false;
        int nonZeroSamples = 0;
        for (int i = 0; i < maxSamples; i++)
        {
            StepSequencerOneSample();
            double mix = 0.0;
            for (Voice& v : voices_)
            {
                if (!v.active) continue;
                const double env = StepEnvelope(v);
                if (!v.active) continue;
                const double samp = SampleVoice(v) * env * v.gain;
                mix += std::abs(samp);
            }
            if (mix > 1e-4 && ++nonZeroSamples >= 64)
            {
                audible = true;
                break;
            }
        }

        for (Voice& v : voices_) v.active = false;
        tickAccum_ = 0.0;
        tempoBpm_ = 120.0;
        masterVol_ = 127;
        dcPrevInL_ = dcPrevOutL_ = dcPrevInR_ = dcPrevOutR_ = 0.0;
        bands_.fill(0.0f);
        tracks_ = initTracks;
        return audible;
    }

    // ---- NDS SDAT Loader ----
    bool LoadNds(FILE* f, uint64_t fsz)
    {
        uint64_t sdatOff = 0;
        uint32_t sdatSize = 0;
        if (!Nitro::FindSdatInNds(f, fsz, sdatOff, sdatSize)) return false;
        if (sdatSize < 0x40 || sdatSize > 256u * 1024u * 1024u) return false;

        std::vector<uint8_t> hdr;
        if (!ReadFileSlice(f, sdatOff, std::min<uint32_t>(sdatSize, 0x40), hdr)) return false;
        if (hdr.size() < 0x40 || !HasTag(hdr, 0, "SDAT")) return false;

        const uint32_t symbOff = RdU32(hdr.data(), hdr.size(), 0x10);
        const uint32_t symbSize = RdU32(hdr.data(), hdr.size(), 0x14);
        const uint32_t infoOff = RdU32(hdr.data(), hdr.size(), 0x18);
        const uint32_t infoSize = RdU32(hdr.data(), hdr.size(), 0x1C);
        const uint32_t fatOff = RdU32(hdr.data(), hdr.size(), 0x20);
        const uint32_t fatSize = RdU32(hdr.data(), hdr.size(), 0x24);

        if (infoSize < 0x28 || fatSize < 12 || (uint64_t)infoOff + infoSize > sdatSize || (uint64_t)fatOff + fatSize > sdatSize)
            return false;

        std::vector<uint8_t> symb, info, fat;
        if (symbOff != 0 && symbSize >= 0x28 && (uint64_t)symbOff + symbSize <= sdatSize && symbSize <= 16u * 1024u * 1024u)
        {
            if (!ReadFileSlice(f, sdatOff + symbOff, symbSize, symb)) symb.clear();
        }
        if (!ReadFileSlice(f, sdatOff + infoOff, infoSize, info)) return false;
        if (!ReadFileSlice(f, sdatOff + fatOff, fatSize, fat)) return false;

        auto readFatRange = [&](uint16_t fid, uint32_t& outOff, uint32_t& outLen) -> bool {
            const uint32_t fatCount = RdU32(fat.data(), fat.size(), 8);
            if (fid >= fatCount) return false;
            const size_t pos = 12 + (size_t)fid * 16;
            if (pos + 8 > fat.size()) return false;
            outOff = RdU32(fat.data(), fat.size(), pos);
            outLen = RdU32(fat.data(), fat.size(), pos + 4);
            return outLen > 0 && (uint64_t)outOff + outLen <= sdatSize;
        };

        // INFO record 0 = SEQ, record 2 = BANK, record 3 = WAVEARC
        const uint32_t seqRecOff = RdU32(info.data(), info.size(), 8 + 0 * 4);
        const uint32_t bankRecOff = RdU32(info.data(), info.size(), 8 + 2 * 4);
        const uint32_t swarRecOff = RdU32(info.data(), info.size(), 8 + 3 * 4);
        if (seqRecOff + 4 > info.size() || bankRecOff + 4 > info.size() || swarRecOff + 4 > info.size())
            return false;

        const uint32_t seqCount = std::min<uint32_t>(4096, RdU32(info.data(), info.size(), seqRecOff));
        const uint32_t bankCount = std::min<uint32_t>(4096, RdU32(info.data(), info.size(), bankRecOff));
        const uint32_t swarCount = std::min<uint32_t>(4096, RdU32(info.data(), info.size(), swarRecOff));
        const uint32_t symbSeqOff = (symb.size() >= 12) ? RdU32(symb.data(), symb.size(), 8) : 0;
        const uint32_t symbSeqCount = (symbSeqOff != 0 && symbSeqOff + 4 <= symb.size()) ? RdU32(symb.data(), symb.size(), symbSeqOff) : 0;

        struct Candidate { uint32_t idx; uint16_t fid; uint16_t bankId; int score; };
        std::vector<Candidate> cands;
        for (uint32_t i = 0; i < seqCount; i++)
        {
            const uint32_t entryOff = RdU32(info.data(), info.size(), seqRecOff + 4 + i * 4);
            if (entryOff == 0 || entryOff + 12 > info.size()) continue;
            const uint16_t fid = RdU16(info.data(), info.size(), entryOff);
            const uint16_t bankId = RdU16(info.data(), info.size(), entryOff + 4);
            uint32_t fo = 0, fl = 0;
            if (!readFatRange(fid, fo, fl) || fl < 0x30) continue;

            int sc = (int)std::min<uint32_t>(fl / 64, 80);
            if (i < symbSeqCount)
            {
                const uint32_t strOff = RdU32(symb.data(), symb.size(), symbSeqOff + 4 + i * 4);
                if (strOff != 0 && strOff < symb.size())
                {
                    const char* cs = (const char*)(symb.data() + strOff);
                    const size_t maxn = std::min<size_t>(96, symb.size() - strOff);
                    std::string sname = UpperAscii(cs, maxn);
                    sc += Nitro::ScoreSeqName(sname);
                }
            }
            cands.push_back({ i, fid, bankId, sc });
        }
        if (cands.empty()) return false;
        std::stable_sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        for (size_t ci = 0; ci < std::min<size_t>(16, cands.size()); ci++)
        {
            const Candidate& c = cands[ci];
            if (c.bankId >= bankCount) continue;
            const uint32_t bankEntOff = RdU32(info.data(), info.size(), bankRecOff + 4 + c.bankId * 4);
            if (bankEntOff == 0 || bankEntOff + 12 > info.size()) continue;
            const uint16_t sbnkFid = RdU16(info.data(), info.size(), bankEntOff);
            uint16_t swarIds[4] = {};
            for (int w = 0; w < 4; w++) swarIds[w] = RdU16(info.data(), info.size(), bankEntOff + 4 + w * 2);

            uint32_t sseqOff = 0, sseqLen = 0, sbnkOff = 0, sbnkLen = 0;
            if (!readFatRange(c.fid, sseqOff, sseqLen) || !readFatRange(sbnkFid, sbnkOff, sbnkLen)) continue;
            if (sseqLen > 2u * 1024u * 1024u || sbnkLen > 2u * 1024u * 1024u) continue;

            std::vector<uint8_t> sseqBuf, sbnkBuf;
            if (!ReadFileSlice(f, sdatOff + sseqOff, sseqLen, sseqBuf)) continue;
            if (!ReadFileSlice(f, sdatOff + sbnkOff, sbnkLen, sbnkBuf)) continue;
            if (sseqBuf.size() < 0x20 || !HasTag(sseqBuf, 0, "SSEQ")) continue;
            if (sbnkBuf.size() < 0x40 || !HasTag(sbnkBuf, 0, "SBNK")) continue;

            const uint32_t dataStart = RdU32(sseqBuf.data(), sseqBuf.size(), 0x18);
            if (dataStart >= sseqBuf.size()) continue;

            // Load SWARs for the 4 slots (map [slot][swavIdx] -> samples_ index)
            samples_.clear();
            std::array<std::vector<int>, 4> swarMap;

            for (int w = 0; w < 4; w++)
            {
                if (swarIds[w] == 0xFFFF || swarIds[w] >= swarCount) continue;
                const uint32_t swarEntOff = RdU32(info.data(), info.size(), swarRecOff + 4 + swarIds[w] * 4);
                if (swarEntOff == 0 || swarEntOff + 4 > info.size()) continue;
                const uint16_t swarFid = RdU16(info.data(), info.size(), swarEntOff);
                uint32_t woff = 0, wlen = 0;
                if (!readFatRange(swarFid, woff, wlen) || wlen > 64u * 1024u * 1024u) continue;
                std::vector<uint8_t> swarBuf;
                if (!ReadFileSlice(f, sdatOff + woff, wlen, swarBuf) || swarBuf.size() < 0x40) continue;
                if (!HasTag(swarBuf, 0, "SWAR")) continue;

                const uint32_t nWaves = std::min<uint32_t>(4096, RdU32(swarBuf.data(), swarBuf.size(), 0x38));
                if (0x3C + nWaves * 4 > swarBuf.size()) continue;
                swarMap[w].assign(nWaves, -1);
                for (uint32_t si = 0; si < nWaves; si++)
                {
                    const uint32_t soff = RdU32(swarBuf.data(), swarBuf.size(), 0x3C + si * 4);
                    if (soff >= swarBuf.size()) continue;
                    const uint32_t nextOff = (si + 1 < nWaves) ? RdU32(swarBuf.data(), swarBuf.size(), 0x3C + (si + 1) * 4) : (uint32_t)swarBuf.size();
                    const size_t avail = (nextOff > soff && nextOff <= swarBuf.size()) ? (nextOff - soff) : (swarBuf.size() - soff);
                    Sample smp;
                    if (Nitro::DecodeSwav(swarBuf.data() + soff, avail, smp))
                    {
                        swarMap[w][si] = (int)samples_.size();
                        samples_.push_back(std::move(smp));
                    }
                }
            }

            // Parse SBNK
            insts_.clear();
            progs_.clear();
            keySplitMap_.clear();
            const uint32_t pCount = std::min<uint32_t>(256, RdU32(sbnkBuf.data(), sbnkBuf.size(), 0x38));
            progs_.resize(pCount);

            auto parseNoteParam = [&](size_t off, uint8_t recType, Instrument& outInst) -> bool {
                if (off + 10 > sbnkBuf.size()) return false;
                const uint16_t swav = RdU16(sbnkBuf.data(), sbnkBuf.size(), off);
                const uint16_t slot = RdU16(sbnkBuf.data(), sbnkBuf.size(), off + 2);
                outInst.baseNote = sbnkBuf[off + 4];
                outInst.attack = sbnkBuf[off + 5];
                outInst.decay = sbnkBuf[off + 6];
                outInst.sustain = sbnkBuf[off + 7];
                outInst.release = sbnkBuf[off + 8];
                outInst.pan = sbnkBuf[off + 9];
                if (recType == 2) { outInst.kind = WaveKind::Pulse; outInst.duty = (uint8_t)(swav & 7); return true; }
                if (recType == 3) { outInst.kind = WaveKind::Noise; return true; }
                if ((recType == 1 || recType == 4) && slot < 4 && swav < swarMap[slot].size() && swarMap[slot][swav] >= 0)
                {
                    outInst.kind = WaveKind::Pcm;
                    outInst.sampleIndex = swarMap[slot][swav];
                    return true;
                }
                return false;
            };

            for (uint32_t pi = 0; pi < pCount; pi++)
            {
                if (0x3C + (pi + 1) * 4 > sbnkBuf.size()) break;
                const uint32_t entry = RdU32(sbnkBuf.data(), sbnkBuf.size(), 0x3C + pi * 4);
                const uint8_t fRec = (uint8_t)(entry & 0xFFu);
                const size_t off = (size_t)((entry >> 8) & 0xFFFFFFu);
                if (fRec == 0 || off >= sbnkBuf.size()) continue;
                ProgramSlot& ps = progs_[pi];
                if (fRec >= 1 && fRec <= 4)
                {
                    Instrument ins;
                    if (parseNoteParam(off, fRec, ins))
                    {
                        ps.type = 1;
                        ps.directInst = (int)insts_.size();
                        insts_.push_back(ins);
                    }
                }
                else if (fRec == 16 && off + 2 <= sbnkBuf.size())
                {
                    ps.type = 16;
                    ps.lowNote = sbnkBuf[off];
                    ps.highNote = sbnkBuf[off + 1];
                    if (ps.highNote >= ps.lowNote)
                    {
                        const int cnt = (int)ps.highNote - (int)ps.lowNote + 1;
                        ps.subBase = (int)keySplitMap_.size();
                        ps.subCount = cnt;
                        for (int k = 0; k < cnt; k++)
                        {
                            const size_t roff = off + 2 + (size_t)k * 12;
                            if (roff + 12 > sbnkBuf.size()) { keySplitMap_.push_back(-1); continue; }
                            Instrument ins;
                            if (parseNoteParam(roff + 2, sbnkBuf[roff], ins))
                            {
                                keySplitMap_.push_back((int)insts_.size());
                                insts_.push_back(ins);
                            }
                            else keySplitMap_.push_back(-1);
                        }
                    }
                }
                else if (fRec == 17 && off + 8 <= sbnkBuf.size())
                {
                    ps.type = 17;
                    ps.subBase = (int)keySplitMap_.size();
                    int cnt = 0;
                    for (int r = 0; r < 8; r++)
                    {
                        ps.splitUpper[r] = sbnkBuf[off + r];
                        if (r > 0 && ps.splitUpper[r] == 0) break;
                        cnt++;
                        if (ps.splitUpper[r] >= 0x7F) break;
                    }
                    ps.subCount = cnt;
                    for (int r = 0; r < cnt; r++)
                    {
                        const size_t roff = off + 8 + (size_t)r * 12;
                        if (roff + 12 > sbnkBuf.size()) { keySplitMap_.push_back(-1); continue; }
                        Instrument ins;
                        if (parseNoteParam(roff + 2, sbnkBuf[roff], ins))
                        {
                            keySplitMap_.push_back((int)insts_.size());
                            insts_.push_back(ins);
                        }
                        else keySplitMap_.push_back(-1);
                    }
                }
            }

            if (insts_.empty()) continue;
            const size_t seqLen = sseqBuf.size() - (size_t)dataStart;
            if (seqLen == 0) continue;
            seqData_.resize(seqLen);
            for (size_t i = 0; i < seqLen; i++) seqData_[i] = sseqBuf[dataStart + i];

            mode_ = SeqMode::NitroSseq;
            if (!ProbeAudible(96000))
            {
                mode_ = SeqMode::None;
                continue;
            }
            return true;
        }
        return false;
    }

    // ---- GBA MP2K / Sappy Loader ----
    bool LoadGba(FILE* f, uint64_t fsz)
    {
        if (fsz < 0x1000 || fsz > 32u * 1024u * 1024u) return false;
        std::vector<uint8_t> rom;
        if (!ReadFileSlice(f, 0, (size_t)fsz, rom)) return false;

        // Keep full ROM slice during playback/probing so far GOTO/CALL targets in 32 MB hacks never drop tracks.
        seqData_ = std::move(rom);
        seqBaseRomOff_ = 0;
        const size_t rsz = seqData_.size();
        gbaRomLimit_ = (uint32_t)rsz;

        // Scan 4-byte aligned offsets for candidate MP2K SongTable runs (>= 16 entries).
        struct GbaTable { size_t off; size_t entries; int score; };
        std::vector<GbaTable> tables;

        for (size_t off = 0x200; off + 16 * 8 <= rsz; )
        {
            uint32_t dummy = 0;
            const uint32_t ptr0 = RdU32(seqData_.data(), rsz, off);
            if ((ptr0 & 3u) != 0 || !Mp2k::IsRomPtr(ptr0, rsz, dummy))
            {
                off += 4;
                continue;
            }
            size_t cnt = 0;
            int bgmCnt = 0;
            for (size_t pos = off; pos + 8 <= rsz && cnt < 2048; pos += 8)
            {
                const uint32_t p = RdU32(seqData_.data(), rsz, pos);
                const uint16_t ms = RdU16(seqData_.data(), rsz, pos + 4);
                const uint16_t me = RdU16(seqData_.data(), rsz, pos + 6);
                uint32_t hoff = 0;
                if ((p & 3u) != 0 || !Mp2k::IsRomPtr(p, rsz, hoff) || ms != me || ms >= 16) break;
                cnt++;
                if (ms == 0 && hoff + 8 <= rsz && seqData_[hoff] >= 4 && seqData_[hoff] <= 16) bgmCnt++;
            }
            if (cnt >= 16 && bgmCnt >= 1)
            {
                int sc = (int)cnt + bgmCnt * 12;
                if (cnt >= 64) sc += 200;
                if (bgmCnt >= 8) sc += 200;
                tables.push_back({ off, cnt, sc });
                off += cnt * 8;
                continue;
            }
            off += 4;
        }
        if (tables.empty()) return false;
        std::stable_sort(tables.begin(), tables.end(), [](const GbaTable& a, const GbaTable& b) { return a.score > b.score; });

        for (size_t tblIdx = 0; tblIdx < std::min<size_t>(4, tables.size()); tblIdx++)
        {
            const GbaTable& tbl = tables[tblIdx];
            // Pick best rich looping Player-0 BGM header from SongTable.
            struct GbaCand { uint32_t hdrOff; uint32_t vgOff; uint8_t tracks; int score; };
            std::vector<GbaCand> cands;
            for (size_t i = 0; i < tbl.entries; i++)
            {
                const size_t pos = tbl.off + i * 8;
                const uint16_t ms = RdU16(seqData_.data(), rsz, pos + 4);
                if (ms != 0) continue; // Player 0 = main BGM
                uint32_t hoff = 0;
                if (!Mp2k::IsRomPtr(RdU32(seqData_.data(), rsz, pos), rsz, hoff)) continue;
                uint8_t nTrk = 0;
                uint32_t vgOff = 0;
                int loopTrk = 0;
                int noteScore = 0;
                if (Mp2k::ValidateSongHeader(seqData_.data(), rsz, hoff, nTrk, vgOff, loopTrk, noteScore))
                {
                    const uint8_t reverb = seqData_[hoff + 3];
                    int sc = (int)nTrk * 12 + loopTrk * 40 + std::min(noteScore, 80);
                    if (nTrk >= 6) sc += 30;
                    if (reverb & 0x80) sc += 25;
                    cands.push_back({ hoff, vgOff, nTrk, sc });
                }
            }
            if (cands.empty()) continue;
            std::stable_sort(cands.begin(), cands.end(), [](const GbaCand& a, const GbaCand& b) { return a.score > b.score; });

            for (size_t ci = 0; ci < std::min<size_t>(12, cands.size()); ci++)
            {
                const GbaCand& pick = cands[ci];
                samples_.clear();
                insts_.clear();
                progs_.assign(128, ProgramSlot{});
                keySplitMap_.clear();

                auto parseVoiceEntry = [&](const uint8_t* ent, Instrument& outInst) -> bool {
                    const uint8_t vtype = ent[0];
                    if ((vtype & 0xF0u) != 0 || (vtype & 0x07u) > 4) return false;
                    const uint8_t baseKey = ent[1];
                    const uint8_t pan = ent[3];
                    const uint32_t argPtr = RdU32(ent, 12, 4);
                    outInst.baseNote = baseKey ? baseKey : 60;
                    outInst.attack = ent[8];
                    outInst.decay = ent[9];
                    outInst.sustain = ent[10];
                    outInst.release = ent[11];
                    outInst.pan = (pan & 0x80) ? (uint8_t)(pan & 0x7F) : 64;

                    const uint8_t kind = vtype & 0x07u;
                    if (kind == 0) // DirectSound PCM8 signed (bit 3 = 0x08 fixed frequency)
                    {
                        uint32_t woff = 0;
                        if (!Mp2k::IsRomPtr(argPtr, rsz, woff) || woff + 16 > rsz) return false;
                        const uint16_t st = RdU16(seqData_.data(), rsz, woff + 2);
                        const uint32_t freqQ10 = RdU32(seqData_.data(), rsz, woff + 4);
                        const uint32_t loopStart = RdU32(seqData_.data(), rsz, woff + 8);
                        const uint32_t len = RdU32(seqData_.data(), rsz, woff + 12);
                        if (len == 0 || len > 512u * 1024u || woff + 16 + len > rsz) return false;
                        const double rate = (double)freqQ10 / 1024.0;
                        if (rate < 1000.0 || rate > 80000.0) return false;

                        Sample smp;
                        smp.loop = (st & 0x4000) != 0;
                        smp.loopStart = std::min(loopStart, len);
                        smp.baseRate = rate;
                        smp.pcm.resize(len);
                        for (uint32_t k = 0; k < len; k++)
                            smp.pcm[k] = (int16_t)((int8_t)seqData_[woff + 16 + k]) * 256;
                        outInst.kind = WaveKind::Pcm;
                        outInst.fixedFreq = (vtype & 0x08) != 0;
                        outInst.sampleIndex = (int)samples_.size();
                        samples_.push_back(std::move(smp));
                        return true;
                    }
                    auto scaleCgbEnv = [&]() {
                        // CGB hardware envelopes use 0..7 steps (sus 0..15); convert when in native range.
                        if (outInst.attack <= 7 && outInst.decay <= 7 && outInst.sustain <= 15 && outInst.release <= 7)
                        {
                            outInst.attack = outInst.attack == 0 ? 255 : (uint8_t)std::max(8, 255 - outInst.attack * 34);
                            outInst.decay = outInst.decay == 0 ? 255 : (uint8_t)(255 - outInst.decay * 10);
                            outInst.sustain = (uint8_t)(outInst.sustain * 17);
                            outInst.release = outInst.release == 0 ? 40 : (uint8_t)(250 - outInst.release * 16);
                        }
                    };
                    if (kind == 3) // CGB Channel 3 programmable 4-bit Wave RAM (32 samples in 16 bytes)
                    {
                        scaleCgbEnv();
                        uint32_t woff = 0;
                        if (Mp2k::IsRomPtr(argPtr, rsz, woff) && woff + 16 <= rsz)
                        {
                            Sample smp;
                            smp.loop = true;
                            smp.loopStart = 0;
                            // Middle C (note 60 = 261.6256 Hz) * 32 samples/cycle
                            smp.baseRate = 261.625565 * 32.0;
                            smp.pcm.resize(32);
                            for (int k = 0; k < 16; k++)
                            {
                                const uint8_t b = seqData_[woff + k];
                                smp.pcm[k * 2 + 0] = (int16_t)(((int)(b >> 4) - 8) * 3200);
                                smp.pcm[k * 2 + 1] = (int16_t)(((int)(b & 0x0F) - 8) * 3200);
                            }
                            outInst.kind = WaveKind::Pcm;
                            outInst.baseNote = 60;
                            outInst.sampleIndex = (int)samples_.size();
                            samples_.push_back(std::move(smp));
                            return true;
                        }
                        outInst.kind = WaveKind::Pulse;
                        outInst.duty = 3;
                        outInst.baseNote = 60;
                        return true;
                    }
                    if (kind == 1 || kind == 2)
                    {
                        scaleCgbEnv();
                        // GBA duty 0..3 (12.5%, 25%, 50%, 75%) -> 1/8ths threshold (0, 1, 3, 5)
                        static constexpr uint8_t kCgbDuty[4] = { 0, 1, 3, 5 };
                        outInst.kind = WaveKind::Pulse;
                        outInst.duty = kCgbDuty[argPtr & 3u];
                        outInst.baseNote = 60;
                        return true;
                    }
                    if (kind == 4)
                    {
                        scaleCgbEnv();
                        outInst.kind = WaveKind::Noise;
                        outInst.baseNote = 60;
                        return true;
                    }
                    return false;
                };

                for (uint32_t pi = 0; pi < 128; pi++)
                {
                    const uint32_t eoff = pick.vgOff + pi * 12;
                    if (eoff + 12 > rsz) break;
                    const uint8_t* ent = seqData_.data() + eoff;
                    const uint8_t vtype = ent[0];
                    ProgramSlot& ps = progs_[pi];
                    if (vtype == 0x80) // MultiSample / DrumSet
                    {
                        uint32_t subVgOff = 0;
                        if (!Mp2k::IsRomPtr(RdU32(ent, 12, 4), rsz, subVgOff)) continue;
                        ps.type = 16;
                        ps.lowNote = 0;
                        ps.highNote = 127;
                        ps.subBase = (int)keySplitMap_.size();
                        ps.subCount = 128;
                        for (uint32_t n = 0; n < 128; n++)
                        {
                            if (subVgOff + (n + 1) * 12 > rsz) { keySplitMap_.push_back(-1); continue; }
                            Instrument ins;
                            if (parseVoiceEntry(seqData_.data() + subVgOff + n * 12, ins))
                            {
                                keySplitMap_.push_back((int)insts_.size());
                                insts_.push_back(ins);
                            }
                            else keySplitMap_.push_back(-1);
                        }
                    }
                    else if (vtype == 0x40) // KeySplit
                    {
                        uint32_t subVgOff = 0, mapOff = 0;
                        if (!Mp2k::IsRomPtr(RdU32(ent, 12, 4), rsz, subVgOff)) continue;
                        if (!Mp2k::IsRomPtr(RdU32(ent, 12, 8), rsz, mapOff) || mapOff + 128 > rsz) continue;
                        ps.type = 16;
                        ps.lowNote = 0;
                        ps.highNote = 127;
                        ps.subBase = (int)keySplitMap_.size();
                        ps.subCount = 128;
                        for (uint32_t n = 0; n < 128; n++)
                        {
                            const uint8_t subIdx = seqData_[mapOff + n];
                            if (subVgOff + ((uint32_t)subIdx + 1) * 12 > rsz) { keySplitMap_.push_back(-1); continue; }
                            Instrument ins;
                            if (parseVoiceEntry(seqData_.data() + subVgOff + subIdx * 12, ins))
                            {
                                keySplitMap_.push_back((int)insts_.size());
                                insts_.push_back(ins);
                            }
                            else keySplitMap_.push_back(-1);
                        }
                    }
                    else
                    {
                        Instrument ins;
                        if (parseVoiceEntry(ent, ins))
                        {
                            ps.type = 1;
                            ps.directInst = (int)insts_.size();
                            insts_.push_back(ins);
                        }
                    }
                }
                if (insts_.empty()) continue;

                std::vector<uint32_t> trkRomOffs(pick.tracks);
                bool ptrOk = true;
                for (uint8_t t = 0; t < pick.tracks; t++)
                {
                    uint32_t toff = 0;
                    if (!Mp2k::IsRomPtr(RdU32(seqData_.data(), rsz, pick.hdrOff + 8 + t * 4), rsz, toff)) { ptrOk = false; break; }
                    trkRomOffs[t] = toff;
                }
                if (!ptrOk) continue;

                mode_ = SeqMode::GbaMp2k;
                tempoBpm_ = 120.0;
                masterVol_ = 127;
                tracks_.assign(kMaxTracks, Track{});
                for (uint8_t t = 0; t < pick.tracks && t < kMaxTracks; t++)
                {
                    tracks_[t].active = true;
                    tracks_[t].pc = trkRomOffs[t];
                    tracks_[t].startPc = trkRomOffs[t];
                }
                if (!ProbeAudible(96000))
                {
                    mode_ = SeqMode::None;
                    continue;
                }
                return true;
            }
        }
        return false;
    }
};

} // namespace RomMusic

#endif
