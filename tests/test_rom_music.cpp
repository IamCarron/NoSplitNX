#include "rom_music.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

void PutU16(std::vector<uint8_t>& b, size_t off, uint16_t v)
{
    if (off + 2 > b.size()) b.resize(off + 2, 0);
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

void PutU24(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
}

void PutU32(std::vector<uint8_t>& b, size_t off, uint32_t v)
{
    if (off + 4 > b.size()) b.resize(off + 4, 0);
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

void AppendU16(std::vector<uint8_t>& b, uint16_t v) { const size_t p = b.size(); PutU16(b, p, v); }
void AppendU32(std::vector<uint8_t>& b, uint32_t v) { const size_t p = b.size(); PutU32(b, p, v); }
void AppendTag(std::vector<uint8_t>& b, const char t[4]) { b.insert(b.end(), t, t + 4); }
void Pad4(std::vector<uint8_t>& b) { while (b.size() & 3) b.push_back(0); }

std::vector<uint8_t> BuildSseq(const std::vector<uint8_t>& code)
{
    std::vector<uint8_t> s;
    AppendTag(s, "SSEQ");
    AppendU32(s, 0x0100FEFFu);
    AppendU32(s, 0x1C + (uint32_t)code.size());
    AppendU16(s, 0x10);
    AppendU16(s, 1);
    AppendTag(s, "DATA");
    AppendU32(s, 8 + (uint32_t)code.size());
    AppendU32(s, 0x1C);
    s.insert(s.end(), code.begin(), code.end());
    Pad4(s);
    PutU32(s, 8, (uint32_t)s.size());
    return s;
}

std::vector<uint8_t> BuildSwar()
{
    // SWAV 0: PCM8 signed looping 440 Hz sine @ 32768 Hz (1 period ~74 samples, 4x = 296 samples = 74 words)
    std::vector<uint8_t> w0;
    w0.push_back(0); // PCM8
    w0.push_back(1); // loop
    AppendU16(w0, 32768);
    AppendU16(w0, 32768 / 64);
    AppendU16(w0, 0);   // loopOffset = 0 words
    AppendU32(w0, 74);  // 296 samples = 74 words
    for (int i = 0; i < 296; i++)
    {
        double ph = 2.0 * 3.14159265 * (double)(i % 74) / 74.0;
        w0.push_back((uint8_t)(int8_t)(std::sin(ph) * 100.0));
    }

    // SWAV 1: IMA-ADPCM looping wave @ 22050 Hz
    std::vector<uint8_t> w1;
    w1.push_back(2); // ADPCM
    w1.push_back(1); // loop
    AppendU16(w1, 22050);
    AppendU16(w1, 22050 / 64);
    AppendU16(w1, 1);   // loopOffset = 1 word (4 bytes header)
    AppendU32(w1, 64);  // 64 words = 256 bytes = 512 nibbles
    AppendU16(w1, 0);   // initial predictor = 0
    w1.push_back(24);   // step index
    w1.push_back(0);
    for (int i = 0; i < 256; i++)
        w1.push_back((i % 32 < 16) ? 0x33 : 0xBB);

    std::vector<uint8_t> swar(0x3C + 8, 0);
    std::memcpy(&swar[0], "SWAR", 4);
    PutU32(swar, 4, 0x0100FEFFu);
    PutU16(swar, 12, 0x10);
    PutU16(swar, 14, 1);
    std::memcpy(&swar[0x10], "DATA", 4);
    PutU32(swar, 0x38, 2);
    PutU32(swar, 0x3C, (uint32_t)swar.size());
    swar.insert(swar.end(), w0.begin(), w0.end());
    Pad4(swar);
    PutU32(swar, 0x40, (uint32_t)swar.size());
    swar.insert(swar.end(), w1.begin(), w1.end());
    Pad4(swar);
    PutU32(swar, 8, (uint32_t)swar.size());
    PutU32(swar, 0x14, (uint32_t)swar.size() - 0x10);
    return swar;
}

std::vector<uint8_t> BuildSbnk()
{
    std::vector<uint8_t> b(0x3C + 16, 0);
    std::memcpy(&b[0], "SBNK", 4);
    PutU32(b, 4, 0x0100FEFFu);
    PutU16(b, 12, 0x10);
    PutU16(b, 14, 1);
    std::memcpy(&b[0x10], "DATA", 4);
    PutU32(b, 0x38, 4);

    auto addInst = [&](uint32_t slot, uint8_t recType, uint16_t swav, uint16_t swarSlot, uint8_t baseNote) {
        const uint32_t off = (uint32_t)b.size();
        PutU32(b, 0x3C + slot * 4, (uint32_t)recType | (off << 8));
        AppendU16(b, swav);
        AppendU16(b, swarSlot);
        b.push_back(baseNote);
        b.push_back(125); // attack
        b.push_back(100); // decay
        b.push_back(110); // sustain
        b.push_back(90);  // release
        b.push_back(64);  // pan
    };
    addInst(0, 1, 0, 0, 69); // PCM8 tuned A4
    addInst(1, 1, 1, 0, 57); // ADPCM
    addInst(2, 2, 3, 0, 60); // PSG pulse 50%
    addInst(3, 3, 0, 0, 60); // PSG noise
    PutU32(b, 8, (uint32_t)b.size());
    PutU32(b, 0x14, (uint32_t)b.size() - 0x10);
    return b;
}

std::vector<uint8_t> BuildSyntheticNds()
{
    // Track 0 (lead + bass track 1 launcher): E1 tempo 132, FE mask 3, 93 open trk 1, loop of 4 notes
    std::vector<uint8_t> trk1 = {
        0x81, 0x01,       // prg 1 (ADPCM)
        0xC1, 110,        // vol 110
        0xC7, 0x01,       // note wait on
        0x39, 100, 24,    // A3
        0x3D, 100, 24,    // C#4
        0x40, 100, 24,    // E4
        0x94, 0, 0, 0     // jump placeholder -> patched to loop offset
    };
    // Build track 0 ahead of track 1
    std::vector<uint8_t> seqCode = {
        0xFE, 0x03, 0x00, // alloc tracks 0..1
        0x93, 0x01, 0, 0, 0, // open track 1 (offset patched below)
        0xE1, 132, 0x00,  // tempo 132 BPM
        0xC1, 118,        // vol
        0x81, 0x00,       // prg 0 (PCM8)
        0xC7, 0x01,       // note wait on
    };
    const uint32_t loop0 = (uint32_t)seqCode.size();
    seqCode.insert(seqCode.end(), {
        0x45, 115, 24,    // A4, dur 24
        0x49, 115, 24,    // C#5, dur 24
        0x4C, 115, 24,    // E5, dur 24
        0x81, 0x02,       // prg 2 (PSG square)
        0x48, 95, 12,
        0x81, 0x00,       // prg 0
        0x94
    });
    PutU24(seqCode, loop0);
    const uint32_t trk1Off = (uint32_t)seqCode.size();
    seqCode[5] = (uint8_t)(trk1Off & 0xFF);
    seqCode[6] = (uint8_t)((trk1Off >> 8) & 0xFF);
    seqCode[7] = (uint8_t)((trk1Off >> 16) & 0xFF);
    const uint32_t trk1Loop = trk1Off + 6;
    trk1[trk1.size() - 3] = (uint8_t)(trk1Loop & 0xFF);
    trk1[trk1.size() - 2] = (uint8_t)((trk1Loop >> 8) & 0xFF);
    trk1[trk1.size() - 1] = (uint8_t)((trk1Loop >> 16) & 0xFF);
    seqCode.insert(seqCode.end(), trk1.begin(), trk1.end());

    const auto fSfx = BuildSseq({ 0x45, 100, 4, 0xFF });
    const auto fTitle = BuildSseq(seqCode);
    const auto fBank = BuildSbnk();
    const auto fSwar = BuildSwar();

    // SYMB block
    std::vector<uint8_t> symb(8 + 32, 0);
    std::memcpy(&symb[0], "SYMB", 4);
    PutU32(symb, 8, (uint32_t)symb.size()); // recOff[0] = SEQ list
    const uint32_t sListOff = (uint32_t)symb.size();
    AppendU32(symb, 2); // 2 seq names
    const uint32_t str0Off = sListOff + 12;
    const uint32_t str1Off = str0Off + 10; // "SE_CURSOR\0"
    AppendU32(symb, str0Off);
    AppendU32(symb, str1Off);
    const char n0[] = "SE_CURSOR";
    const char n1[] = "SEQ_BGM_TITLE";
    symb.insert(symb.end(), n0, n0 + sizeof(n0));
    symb.insert(symb.end(), n1, n1 + sizeof(n1));
    Pad4(symb);
    PutU32(symb, 4, (uint32_t)symb.size());

    // INFO block: rec0=SEQ(2), rec2=BANK(1), rec3=SWAR(1)
    std::vector<uint8_t> info(8 + 32, 0);
    std::memcpy(&info[0], "INFO", 4);
    const uint32_t seqRecOff = (uint32_t)info.size();
    PutU32(info, 8 + 0 * 4, seqRecOff);
    AppendU32(info, 2);
    const uint32_t e0 = seqRecOff + 12;
    const uint32_t e1 = e0 + 12;
    AppendU32(info, e0);
    AppendU32(info, e1);
    for (uint16_t fid = 0; fid < 2; fid++)
    {
        AppendU16(info, fid); AppendU16(info, 0);
        AppendU16(info, 0); // bank 0
        info.push_back(120); info.push_back(64); info.push_back(64); info.push_back(0);
        AppendU16(info, 0);
    }
    const uint32_t bankRecOff = (uint32_t)info.size();
    PutU32(info, 8 + 2 * 4, bankRecOff);
    AppendU32(info, 1);
    AppendU32(info, bankRecOff + 8);
    AppendU16(info, 2); AppendU16(info, 0); // bank fid=2
    AppendU16(info, 0); AppendU16(info, 0xFFFF); AppendU16(info, 0xFFFF); AppendU16(info, 0xFFFF);
    const uint32_t swarRecOff = (uint32_t)info.size();
    PutU32(info, 8 + 3 * 4, swarRecOff);
    AppendU32(info, 1);
    AppendU32(info, swarRecOff + 8);
    AppendU16(info, 3); AppendU16(info, 0); // swar fid=3
    Pad4(info);
    PutU32(info, 4, (uint32_t)info.size());

    // FAT block (4 entries) + FILE block
    const uint32_t symbOff = 0x40;
    const uint32_t infoOff = symbOff + (uint32_t)symb.size();
    const uint32_t fatOff = infoOff + (uint32_t)info.size();
    const uint32_t fatSize = 12 + 4 * 16;
    const uint32_t fileOff = fatOff + fatSize;
    const std::vector<const std::vector<uint8_t>*> files = { &fSfx, &fTitle, &fBank, &fSwar };

    std::vector<uint8_t> fat(fatSize, 0);
    std::memcpy(&fat[0], "FAT ", 4);
    PutU32(fat, 4, fatSize);
    PutU32(fat, 8, 4);

    std::vector<uint8_t> fileBlk(8, 0);
    std::memcpy(&fileBlk[0], "FILE", 4);
    for (size_t i = 0; i < files.size(); i++)
    {
        const uint32_t relOff = fileOff + (uint32_t)fileBlk.size();
        PutU32(fat, 12 + i * 16, relOff);
        PutU32(fat, 12 + i * 16 + 4, (uint32_t)files[i]->size());
        fileBlk.insert(fileBlk.end(), files[i]->begin(), files[i]->end());
        Pad4(fileBlk);
    }
    PutU32(fileBlk, 4, (uint32_t)fileBlk.size());

    std::vector<uint8_t> sdat(0x40, 0);
    std::memcpy(&sdat[0], "SDAT", 4);
    PutU32(sdat, 4, 0x0100FEFFu);
    PutU16(sdat, 12, 0x40);
    PutU16(sdat, 14, 4);
    PutU32(sdat, 0x10, symbOff); PutU32(sdat, 0x14, (uint32_t)symb.size());
    PutU32(sdat, 0x18, infoOff); PutU32(sdat, 0x1C, (uint32_t)info.size());
    PutU32(sdat, 0x20, fatOff);  PutU32(sdat, 0x24, fatSize);
    PutU32(sdat, 0x28, fileOff); PutU32(sdat, 0x2C, (uint32_t)fileBlk.size());
    sdat.insert(sdat.end(), symb.begin(), symb.end());
    sdat.insert(sdat.end(), info.begin(), info.end());
    sdat.insert(sdat.end(), fat.begin(), fat.end());
    sdat.insert(sdat.end(), fileBlk.begin(), fileBlk.end());
    PutU32(sdat, 8, (uint32_t)sdat.size());

    // Build minimal Nitro .nds container (0x200 header + FNT + FAT + SDAT payload)
    const char sdatName[] = "sound_data.sdat";
    std::vector<uint8_t> fnt(8, 0);
    PutU32(fnt, 0, 8);
    PutU16(fnt, 4, 0);
    PutU16(fnt, 6, 1);
    fnt.push_back((uint8_t)strlen(sdatName));
    fnt.insert(fnt.end(), sdatName, sdatName + strlen(sdatName));
    fnt.push_back(0);
    Pad4(fnt);

    const uint32_t ndsFntOff = 0x200;
    const uint32_t ndsFatOff = ndsFntOff + (uint32_t)fnt.size();
    const uint32_t ndsSdatOff = ndsFatOff + 8;

    std::vector<uint8_t> nds(ndsSdatOff, 0);
    PutU32(nds, 0x40, ndsFntOff);
    PutU32(nds, 0x44, (uint32_t)fnt.size());
    PutU32(nds, 0x48, ndsFatOff);
    PutU32(nds, 0x4C, 8);
    std::memcpy(&nds[ndsFntOff], fnt.data(), fnt.size());
    PutU32(nds, ndsFatOff + 0, ndsSdatOff);
    PutU32(nds, ndsFatOff + 4, ndsSdatOff + (uint32_t)sdat.size());
    nds.insert(nds.end(), sdat.begin(), sdat.end());
    return nds;
}

std::vector<uint8_t> BuildSyntheticGba()
{
    std::vector<uint8_t> rom(64 * 1024, 0);
    const uint32_t base = 0x08000000u;

    // 1. Place WaveData at 0x1000 (DirectSound PCM8 sine wave, 256 samples @ 16384 Hz)
    const uint32_t waveOff = 0x1000;
    PutU16(rom, waveOff + 0, 0);
    PutU16(rom, waveOff + 2, 0x4000); // looped
    PutU32(rom, waveOff + 4, 16384u << 10);
    PutU32(rom, waveOff + 8, 0);
    PutU32(rom, waveOff + 12, 256);
    for (int i = 0; i < 256; i++)
        rom[waveOff + 16 + i] = (uint8_t)(int8_t)(std::sin(2.0 * 3.14159265 * i / 32.0) * 100.0);

    // 2. VoiceGroup at 0x1400 (128 * 12 bytes)
    const uint32_t vgOff = 0x1400;
    auto writeVoice = [&](uint32_t slot, uint8_t type, uint32_t ptrOrDuty) {
        const uint32_t off = vgOff + slot * 12;
        rom[off + 0] = type;
        rom[off + 1] = 60;
        rom[off + 2] = 0;
        rom[off + 3] = 0x80 | 64;
        PutU32(rom, off + 4, ptrOrDuty);
        rom[off + 8] = 255; // att
        rom[off + 9] = 0;   // dec
        rom[off + 10] = 220; // sus
        rom[off + 11] = 180; // rel
    };
    writeVoice(0, 0x00, base + waveOff); // DirectSound PCM8
    writeVoice(1, 0x01, 2);              // CGB Square
    writeVoice(2, 0x04, 0);              // CGB Noise

    // 3. Track bytecode at 0x2000 .. 0x2300 (6 tracks for rich BGM + 1 SFX track)
    const uint32_t sfxTrkOff = 0x1F00;
    rom[sfxTrkOff + 0] = 0xBD; rom[sfxTrkOff + 1] = 0;
    rom[sfxTrkOff + 2] = 0xD4; rom[sfxTrkOff + 3] = 60; rom[sfxTrkOff + 4] = 100;
    rom[sfxTrkOff + 5] = 0x84;
    rom[sfxTrkOff + 6] = 0xB1;
    const uint32_t sfxHdrOff = 0x1F40;
    rom[sfxHdrOff + 0] = 1; rom[sfxHdrOff + 1] = 0; rom[sfxHdrOff + 2] = 0; rom[sfxHdrOff + 3] = 0;
    PutU32(rom, sfxHdrOff + 4, base + vgOff);
    PutU32(rom, sfxHdrOff + 8, base + sfxTrkOff);

    const uint32_t bgmHdrOff = 0x2800;
    rom[bgmHdrOff + 0] = 6; // 6 tracks
    rom[bgmHdrOff + 1] = 0;
    rom[bgmHdrOff + 2] = 0;
    rom[bgmHdrOff + 3] = 0x85; // reverb
    PutU32(rom, bgmHdrOff + 4, base + vgOff);
    for (uint32_t t = 0; t < 6; t++)
    {
        const uint32_t toff = 0x2000 + t * 0x40;
        PutU32(rom, bgmHdrOff + 8 + t * 4, base + toff);
        size_t p = toff;
        rom[p++] = 0xBB; rom[p++] = 65;          // tempo 130
        rom[p++] = 0xBD; rom[p++] = (uint8_t)(t % 3); // voice
        rom[p++] = 0xBE; rom[p++] = 110;         // vol
        const uint32_t loopOff = (uint32_t)p;
        rom[p++] = 0xE8; rom[p++] = (uint8_t)(52 + t * 3); rom[p++] = 105; // N24
        rom[p++] = 0x98;                         // W24
        rom[p++] = (uint8_t)(56 + t * 3);        // running status note + default vel
        rom[p++] = 0x98;                         // W24
        rom[p++] = 0xCF; rom[p++] = (uint8_t)(59 + t * 3); rom[p++] = 95; // TIE
        rom[p++] = 0x8C;                         // W12
        rom[p++] = 0xCE;                         // EOT
        rom[p++] = 0xB2; PutU32(rom, p, base + loopOff);
    }

    // 4. SongTable at 0x3000 (40 entries: 0..37 SFX Player 1, 38..39 BGM Player 0)
    const uint32_t stOff = 0x3000;
    for (uint32_t i = 0; i < 40; i++)
    {
        const bool isBgm = (i >= 38);
        PutU32(rom, stOff + i * 8 + 0, base + (isBgm ? bgmHdrOff : sfxHdrOff));
        PutU16(rom, stOff + i * 8 + 4, isBgm ? 0 : 1);
        PutU16(rom, stOff + i * 8 + 6, isBgm ? 0 : 1);
    }
    return rom;
}

double ComputeRms(const std::vector<int16_t>& buf)
{
    double sum = 0.0;
    for (int16_t s : buf) sum += (double)s * (double)s;
    return std::sqrt(sum / std::max<size_t>(1, buf.size()));
}

void WriteBytes(const std::filesystem::path& p, const std::vector<uint8_t>& data)
{
    FILE* f = fopen(p.c_str(), "wb");
    assert(f);
    if (!data.empty()) assert(fwrite(data.data(), 1, data.size(), f) == data.size());
    fclose(f);
}

} // namespace

int main()
{
    const auto tmp = std::filesystem::temp_directory_path() / "nosplitnx_rom_music_test";
    std::filesystem::create_directories(tmp);

    // 1. Corrupt / empty / truncated inputs fail safely with zero crashes
    {
        const auto emptyPath = tmp / "empty.nds";
        WriteBytes(emptyPath, {});
        RomMusic::Player p;
        assert(!p.LoadFromRom(emptyPath.string(), 0, "IPKE"));
        assert(!p.IsLoaded());

        const auto garbageGba = tmp / "garbage.gba";
        WriteBytes(garbageGba, std::vector<uint8_t>(8192, 0xA5));
        assert(!p.LoadFromRom(garbageGba.string(), 1, "BPEE"));
        assert(!p.IsLoaded());

        const auto truncNds = tmp / "trunc.nds";
        std::vector<uint8_t> bad(1024, 0);
        PutU32(bad, 0x40, 0xFFFFFF00u);
        PutU32(bad, 0x44, 0x100000);
        WriteBytes(truncNds, bad);
        assert(!p.LoadFromRom(truncNds.string(), 0, "CPUJ"));
        assert(!p.IsLoaded());
    }

    // 2. Synthetic Nintendo DS (.nds SDAT + SSEQ + SBNK + SWAR PCM8/ADPCM/PSG)
    {
        const auto ndsPath = tmp / "synthetic_ds.nds";
        WriteBytes(ndsPath, BuildSyntheticNds());

        RomMusic::Player p;
        p.SetVolume(100);
        assert(p.LoadFromRom(ndsPath.string(), 0, "IPKE"));
        assert(p.IsLoaded());

        // Render 3.5 seconds @ 48 kHz (spans multiple loop boundaries)
        std::vector<int16_t> firstSec(48000 * 2);
        std::vector<int16_t> laterSec(48000 * 2);
        p.Mix(firstSec.data(), 48000);
        std::vector<int16_t> mid(72000 * 2);
        p.Mix(mid.data(), 72000);
        p.Mix(laterSec.data(), 48000);

        const double rms1 = ComputeRms(firstSec);
        const double rms2 = ComputeRms(laterSec);
        assert(rms1 > 400.0);
        assert(rms2 > 400.0); // loop stays active cleanly

        float bands100[RomMusic::Player::kBandCount] = {};
        bool active = false;
        p.GetBands(bands100, active);
        assert(active);
        float maxBand = 0.0f;
        for (float v : bands100) maxBand = std::max(maxBand, v);
        assert(maxBand > 0.05f);

        // Volume scaling check
        p.SetVolume(25);
        std::vector<int16_t> quiet(24000 * 2);
        p.Mix(quiet.data(), 24000);
        const double rmsQuiet = ComputeRms(quiet);
        assert(rmsQuiet > 50.0 && rmsQuiet < rms1 * 0.65);

        p.SetVolume(0);
        std::vector<int16_t> muted(4096 * 2);
        p.Mix(muted.data(), 4096);
        assert(ComputeRms(muted) == 0.0);
        p.GetBands(bands100, active);
        assert(!active);
    }

    // 3. Synthetic GBA (.gba MP2K SongTable + DirectSound + CGB + loops)
    {
        const auto gbaPath = tmp / "synthetic_gba.gba";
        WriteBytes(gbaPath, BuildSyntheticGba());

        RomMusic::Player p;
        p.SetVolume(90);
        assert(p.LoadFromRom(gbaPath.string(), 1, "BPEE"));
        assert(p.IsLoaded());

        std::vector<int16_t> buf1(48000 * 2);
        std::vector<int16_t> buf2(48000 * 2);
        p.Mix(buf1.data(), 48000);
        p.Mix(buf2.data(), 48000);
        p.Mix(buf2.data(), 48000);
        const double rms1 = ComputeRms(buf1);
        const double rms2 = ComputeRms(buf2);
        assert(rms1 > 400.0 && rms2 > 400.0);

        float bands[RomMusic::Player::kBandCount] = {};
        bool active = false;
        p.GetBands(bands, active);
        assert(active);
        float bandSum = 0.0f;
        for (float v : bands) bandSum += v;
        assert(bandSum > 0.15f);
    }

    std::filesystem::remove_all(tmp);
    std::printf("RomMusicPlayer tests OK (DS SDAT/SSEQ/SWAR + GBA MP2K + fuzz safety)\n");
    return 0;
}
