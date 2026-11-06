// SidMon 1.0 and SidMon 2.0 (ported from NostalgicPlayer): playback only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/amiga_synth.h"
#include "core/song.h"
#include "test.h"

namespace {
float peakOf(AmigaSynthPlayer& p, double seconds, float* vu0 = nullptr, float* vuOther = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (vu0 && i == 2) *vu0 = p.vu(0);
        if (vuOther)
            for (int v = 1; v < 4; ++v) *vuOther = p.vu(v) > *vuOther ? p.vu(v) : *vuOther;
    }
    return peak;
}
void put16(std::vector<uint8_t>& f, size_t at, int v) {
    if (f.size() < at + 2) f.resize(at + 2);
    f[at] = (uint8_t)(v >> 8);
    f[at + 1] = (uint8_t)v;
}
void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) {
    put16(f, at, (int)(v >> 16));
    put16(f, at + 2, (int)(v & 0xFFFF));
}
void putBytes(std::vector<uint8_t>& f, size_t at, std::initializer_list<int> b) {
    if (f.size() < at + b.size()) f.resize(at + b.size());
    for (int v : b) f[at++] = (uint8_t)v;
}

// ---- SidMon 1: a player-and-data file. The loader finds its tables by looking for the replay
// routine's instructions, so the tiny file carries just those instructions (no real code).
constexpr uint32_t kB = 0x100;  // the player's base (lea pc)
// absolute data positions
constexpr size_t kSongData = 0x140, kSeq = 0x170, kTrackTab = 0x190, kTrackData = 0x198, kInstr = 0x1A8,
                 kWaveList = 0x1C8, kWaves = 0x1D8, kSampleInfo = 0x1F8;

std::vector<uint8_t> tinySid1() {
    std::vector<uint8_t> f(1024, 0);
    // init routine: lea base(pc),a0 / four sequence pointers / moveq #3 / track data, track table, instruments
    putBytes(f, 0, {0x41, 0xFA, 0x00, 0xFE});
    for (int i = 0; i < 4; ++i) putBytes(f, 4 + 4 * i, {0xD1, 0xE8, 0x00, 4 * i});
    putBytes(f, 20, {0x70, 0x03, 0x41, 0xFA, 0x00, 0x00, 0xD1, 0xE8, 0x00, 0x10});
    putBytes(f, 30, {0xD9, 0xEC, 0x00, 0x14, 0xD9, 0xEC, 0x00, 0x18});
    // play routine: movem / bsr PlayNote / song data pointer / movem back
    putBytes(f, 38, {0x48, 0xE7, 0xFF, 0xFE, 0x61, 0x00, 0x00, 0x0C, 0xD3, 0xE9, 0x00, 0x28, 0, 0, 0, 0, 0x4C, 0xDF});
    // PlayNote: wave list pointer, lsl, bcc PlaySample, waveforms pointer, period table (version 1)
    putBytes(f, 56, {0xD9, 0xEC, 0x00, 0x1C, 0xEB, 0x81, 0x64, 0x00, 0x00, 0x0E, 0xDD, 0xEE, 0x00, 0x20});
    putBytes(f, 70, {0x16, 0x2C, 0x00, 0x00, 0xC6, 0xFC, 0x00, 0x00});
    // PlaySample: sample table pointer, cmp.l #noloop
    putBytes(f, 78, {0xD9, 0xEC, 0x00, 0x24, 0x0C, 0xAB, 0xFF, 0xFF, 0xFF, 0xFF, 0x4E, 0x75});
    // pointer slots (relative to the base)
    for (int i = 0; i < 4; ++i) put32(f, kB + 4 * i, (uint32_t)(kSeq + 8 * i - kB));
    put32(f, kB + 0x10, (uint32_t)(kTrackData - kB));
    put32(f, kB + 0x14, (uint32_t)(kTrackTab - kB));
    put32(f, kB + 0x18, (uint32_t)(kInstr - kB));
    put32(f, kB + 0x1C, (uint32_t)(kWaveList - kB));
    put32(f, kB + 0x20, (uint32_t)(kWaves - kB));
    put32(f, kB + 0x24, (uint32_t)(kSampleInfo - kB));
    put32(f, kB + 0x28, (uint32_t)(kSongData - kB));
    // song data: no waveform mixing, 16 rows per track, 1 position, speed 5
    put32(f, kSongData + 24, 16);
    put32(f, kSongData + 28, 2);
    put32(f, kSongData + 32, 5);
    // sequences: channel 0 plays track 0, the others the silent track 1
    for (int i = 0; i < 4; ++i) put32(f, kSeq + 8 * i, i == 0 ? 0 : 1);
    put32(f, kTrackTab, 0), put32(f, kTrackTab + 4, 5);
    putBytes(f, kTrackData, {40, 1, 0, 0, 63, 0, 0, 0, 0, 63});  // note 40 with instrument 1, held
    // instrument 1: wave list 1, fast attack to full volume, held
    put32(f, kInstr, 1);
    putBytes(f, kInstr + 20, {0x40, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF});
    putBytes(f, kWaveList, {1, 1, 0xFF, 0x10});
    for (int i = 0; i < 32; ++i) f[kWaves + i] = (uint8_t)(i < 16 ? 100 : -100);
    put32(f, kSampleInfo, 0);
    return f;
}

// ---- SidMon 2: 1 position, 1 instrument, 1 looped 32 byte square sample; channel 0 plays note 37.
constexpr size_t kSd2Pos = 90, kSd2Instr = 102, kSd2WaveList = 134, kSd2SampleInfo = 182, kSd2TrackTab = 246,
                 kSd2Tracks = 250, kSd2Sample = 254;
std::vector<uint8_t> tinySid2() {
    std::vector<uint8_t> f(kSd2Sample + 32, 0);
    putBytes(f, 2, {0, 3});  // 1 position, speed 3
    put16(f, 4, 64);         // 1 sample
    const uint32_t lens[] = {28, 4, 4, 4, 4, 32, 16, 16, 16, 64, 4, 4};
    for (int i = 0; i < 12; ++i) put32(f, 6 + 4 * i, lens[i]);
    std::memcpy(&f[58], "SIDMON II - THE MIDI VERSION", 28);
    putBytes(f, kSd2Pos, {0, 1, 1, 1});
    putBytes(f, kSd2Instr + 16, {0xFF, 0x40, 0xFF, 0x00, 0x00, 0xFF, 0x00});  // attack to full, held
    put16(f, kSd2SampleInfo + 4, 16);   // 16 words
    put16(f, kSd2SampleInfo + 8, 16);   // looped from 0, 16 words
    put16(f, kSd2TrackTab + 2, 3);
    putBytes(f, kSd2Tracks, {37, 1, 0xC1, 0x81});
    for (int i = 0; i < 32; ++i) f[kSd2Sample + i] = (uint8_t)(i < 16 ? 100 : -100);
    return f;
}

std::vector<uint8_t> readModule(const char* rel) {
    std::filesystem::path p = std::filesystem::path("C:/development/FatTracker/modules") / rel;
    if (!std::filesystem::exists(p)) p = std::filesystem::path(FT_SOURCE_DIR) / "modules" / rel;
    std::vector<uint8_t> d;
    if (!std::filesystem::exists(p)) return d;
    std::ifstream in(p, std::ios::binary);
    d.assign(std::istreambuf_iterator<char>(in), {});
    return d;
}

void checkTinyPlays(const std::vector<uint8_t>& f, const char* format) {
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), format, std::strlen(format)) == 0);
    CHECK(p->seconds() > 1.0 && p->seconds() < 10.0);
    float vu0 = 0, other = 0;
    CHECK(peakOf(*p, 0.8, &vu0, &other) > 0.05f);
    CHECK(vu0 > 0.1f);
    CHECK(other < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

// A broken file must be refused or play without crashing.
void survives(const std::vector<uint8_t>& f) {
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    if (p) {
        p->setRate(48000);
        peakOf(*p, 0.5);
        p->restart();
        peakOf(*p, 0.2);
    }
}
// Bad indexes in the song: the file loads, and plays without crashing.
void playsAnyway(const std::vector<uint8_t>& f) {
    CHECK(AmigaSynthPlayer::open(f.data(), f.size()) != nullptr);
    survives(f);
}
}  // namespace

TEST(sidmon1_tiny_song_plays) { checkTinyPlays(tinySid1(), "SIDMON 1"); }

TEST(sidmon2_tiny_song_plays) { checkTinyPlays(tinySid2(), "SIDMON 2"); }

TEST(sidmon2_keeps_its_own_copy_of_the_file) {
    // the loader's buffer is gone once the file is loaded: playback must not read it any more
    auto render = [](AmigaSynthPlayer& p) {
        std::vector<float> buf(4800 * 2), all;
        for (int i = 0; i < 10; ++i) {
            p.render(buf.data(), 4800, 0.45f);
            all.insert(all.end(), buf.begin(), buf.end());
        }
        return all;
    };
    const auto f = tinySid2();
    auto ref = AmigaSynthPlayer::open(f.data(), f.size());
    std::vector<uint8_t> g = f;
    auto p = AmigaSynthPlayer::open(g.data(), g.size());
    CHECK(ref && p);
    if (!ref || !p) return;
    ref->setRate(48000), p->setRate(48000);
    for (auto& b : g) b = 0x5A;  // the caller's buffer changes (or is freed)
    CHECK(render(*ref) == render(*p));
}

TEST(sidmon_c64_sid_never_matches) {
    auto f = tinySid1();
    std::memcpy(f.data(), "PSID", 4);
    CHECK(!AmigaSynthPlayer::detect(f.data(), f.size()));
    std::memcpy(f.data(), "RSID", 4);
    CHECK(!AmigaSynthPlayer::detect(f.data(), f.size()));
    std::vector<uint8_t> psid(2048, 0);
    std::memcpy(psid.data(), "PSID", 4);
    CHECK(!AmigaSynthPlayer::detect(psid.data(), psid.size()));
    CHECK(AmigaSynthPlayer::open(psid.data(), psid.size()) == nullptr);
}

TEST(sidmon1_broken_files) {
    const auto f = tinySid1();
    for (size_t cut = 8; cut < f.size(); cut += 37) CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
    auto g = tinySid1();
    put32(g, kB, 0x7FFFFFF0);  // sequence pointer far outside the file
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinySid1();
    put32(g, kSongData + 28, 0);  // 0 positions (would wrap to 4 billion)
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinySid1();
    put32(g, kSongData + 28, 0x10000000);  // more positions than the file holds
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    // bad indexes in the song: must still play
    g = tinySid1();
    put32(g, kSeq, 1000);  // track 1000 of 2
    playsAnyway(g);
    g = tinySid1();
    g[kTrackData] = (uint8_t)-100;  // note far below the period table
    playsAnyway(g);
    g = tinySid1();
    g[kTrackData + 1] = 200;  // instrument 200 of 1
    playsAnyway(g);
    g = tinySid1();
    g[kTrackData + 2] = 0x7F, g[kTrackData + 3] = 0x80;  // bend to a note past the table
    playsAnyway(g);
    g = tinySid1();
    putBytes(g, kWaveList, {0, 0, 0xFF, 0xFE});  // waveform 0, loop to entry 254
    playsAnyway(g);
    g = tinySid1();
    put32(g, kInstr, 15);  // wave list 15 of 1
    playsAnyway(g);
    g = tinySid1();
    put32(g, kInstr, 0x7FFFFFFF);  // a sample number of 2 billion
    playsAnyway(g);
    g = tinySid1();
    g[kInstr + 28] = 200;  // phase shift with waveform 200
    g[kInstr + 30] = 200;  // fine tune past the period table
    playsAnyway(g);
    for (int fx = 0; fx < 2; ++fx) {  // filter / waveform mixing routines present (bsr from the play routine)
        g = tinySid1();
        putBytes(g, 50, {0x61, 0x00, 0x00, 0x8C});
        putBytes(g, 0xC0 + 12, fx ? std::initializer_list<int>{0, 0, 0x44, 0x10} : std::initializer_list<int>{0x20, 0x10});
        put32(g, kSongData + 36, 1), put32(g, kSongData + 40, 2);
        put32(g, kSongData, 1), put32(g, kSongData + 8, 1), put32(g, kSongData + 16, 1);
        auto p = AmigaSynthPlayer::open(g.data(), g.size());
        CHECK(p != nullptr);
        if (p) CHECK(peakOf(*p, 0.5) > 0.05f);
        put32(g, kSongData, 0), put32(g, kSongData + 16, 99);  // mixing with waveforms 0 and 99 of 1
        playsAnyway(g);
    }
    g = tinySid1();
    put32(g, kSongData + 24, 0);  // 0 rows per track
    playsAnyway(g);
}

TEST(sidmon2_broken_files) {
    const auto f = tinySid2();
    for (size_t cut = 8; cut < f.size(); cut += 7) CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
    auto g = tinySid2();
    put32(g, 6 + 4 * 11, 0xFFFFFF00);  // tracks block of 4 GB
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinySid2();
    put16(g, kSd2TrackTab + 2, 0xFFFF);  // second track starts past the tracks block
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinySid2();
    put32(g, 6 + 4 * 5, 33);  // instrument block not a multiple of 32
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    // bad indexes in the song: must still play
    g = tinySid2();
    g[kSd2Pos] = 200;  // track 200 of 2
    playsAnyway(g);
    g = tinySid2();
    g[kSd2Tracks] = 0x6F;  // the highest note
    g[kSd2Tracks + 1] = 0x10;  // instrument 16 of 1
    playsAnyway(g);
    g = tinySid2();
    g[kSd2Tracks + 2] = 0x6F, g[kSd2Tracks + 3] = 0x05;  // slide to note 0x6F (past the table)
    playsAnyway(g);
    g = tinySid2();
    putBytes(g, kSd2Instr, {200, 255, 0, 0, 200, 255, 0, 0, 200, 255, 0, 0, 0x80, 0});  // lists past the end
    playsAnyway(g);
    g = tinySid2();
    putBytes(g, kSd2WaveList, {100, 0x7F});  // sample 100 of 1
    g[kSd2Instr + 1] = 2;
    playsAnyway(g);
    g = tinySid2();
    putBytes(g, kSd2SampleInfo + 10, {0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x7F, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF});
    playsAnyway(g);  // sample negation running wild
    g = tinySid2();
    putBytes(g, kSd2Tracks, {37, 1, 0x7C, 0xFF});  // volume 255 effect
    playsAnyway(g);
}

TEST(sidmon_real_modules_play) {
    struct M {
        const char* path;
        const char* format;
    } mods[] = {{"Dr. Awesome/SidMon 1/disco-donut.sid", "SIDMON 1"},
                {"Dr. Awesome/SidMon 2/software 2000.sid2", "SIDMON 2"}};
    for (const M& m : mods) {
        const auto d = readModule(m.path);
        if (d.empty()) {
            std::printf("SKIP sidmon_real_modules_play (%s not found)\n", m.path);
            continue;
        }
        CHECK(AmigaSynthPlayer::detect(d.data(), d.size()));
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) continue;
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), m.format, std::strlen(m.format)) == 0);
        CHECK(p->seconds() > 10.0);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.02f);
        int voices = 0;  // the song uses more than one channel in its first 15 seconds
        float vuMax[4] = {};
        std::vector<float> buf(4800 * 2);
        for (int i = 0; i < 120; ++i) {
            p->render(buf.data(), 4800, 0.45f);
            for (int v = 0; v < 4; ++v) vuMax[v] = p->vu(v) > vuMax[v] ? p->vu(v) : vuMax[v];
        }
        for (float v : vuMax) voices += v > 0.02f;
        CHECK(voices >= 2);
        std::printf("  %s: %s, %.1f s, peak %.2f, %d voices\n", m.path, p->format(), p->seconds(), pk, voices);
        // truncated copies are refused or play
        for (size_t cut = 64; cut < d.size(); cut += d.size() / 23) {
            const std::vector<uint8_t> t(d.begin(), d.begin() + cut);
            survives(t);
        }
    }
}
