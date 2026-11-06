// AHX ("THX") and HivelyTracker ("HVL") modules through the HivelyTracker replayer: tiny hand-built tunes,
// hostile and truncated files, the real tunes in modules/AHX and the AHX Ocean loader.
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "audio/engine.h"
#include "core/hvl/hvl_replay.h"
#include "core/hvl_player.h"
#include "core/sid_player.h"
#include "core/sndh_player.h"
#include "core/song.h"
#include "test.h"

namespace {
// ---------------------------------------------------------------- tiny hand-built modules
// One instrument (a sawtooth, wave length 3 = 32 samples), one 4-row track (track 1; track 0 is the blank one
// the header flags), a note on row 0. Sub-song n starts at position n.
struct Tiny {
    int version = 0;
    int subsongs = 0;           // positions = subsongs + 1
    uint8_t fx = 0, fxParam = 0;  // on row 0 of track 1
    uint8_t waveLength = 3;
    uint8_t plWave = 2;         // play list wave form (1 triangle, 2 sawtooth, 3 square, 4 noise)
    uint8_t note = 25;
};

void put16(std::vector<uint8_t>& b, size_t at, unsigned v) {
    b[at] = (uint8_t)(v >> 8), b[at + 1] = (uint8_t)v;
}

void instrument(std::vector<uint8_t>& b, const Tiny& t) {
    const uint8_t ins[22] = {64, t.waveLength, 1, 64, 1, 64, 200, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x3f, 0, 0, 1, 1};
    b.insert(b.end(), ins, ins + 22);
}

std::vector<uint8_t> tinyAhx(const Tiny& t = {}) {
    const int posn = t.subsongs + 1;
    std::vector<uint8_t> b(14, 0);
    b[0] = 'T', b[1] = 'H', b[2] = 'X', b[3] = (uint8_t)t.version;
    b[6] = (uint8_t)(0x80 | (posn >> 8));  // track 0 is blank (not stored), speed 1
    b[7] = (uint8_t)posn;
    b[10] = 4, b[11] = 1, b[12] = 1, b[13] = (uint8_t)t.subsongs;
    for (int s = 0; s < t.subsongs; ++s) b.push_back(0), b.push_back((uint8_t)(s + 1));
    for (int p = 0; p < posn; ++p) {
        const uint8_t pos[8] = {1, (uint8_t)(p * 12), 0, 0, 0, 0, 0, 0};  // channel 1 plays track 1
        b.insert(b.end(), pos, pos + 8);
    }
    const uint8_t row0[3] = {(uint8_t)(t.note << 2), (uint8_t)(0x10 | (t.fx & 15)), t.fxParam};  // instrument 1
    b.insert(b.end(), row0, row0 + 3);
    for (int r = 1; r < 4; ++r) b.push_back(0), b.push_back(0), b.push_back(0);
    instrument(b, t);
    // play list entry: FX 0/0, wave form in bits 0-1 of byte 0 and bit 7 of byte 1
    b.push_back((uint8_t)((t.plWave >> 1) & 3)), b.push_back((uint8_t)((t.plWave & 1) << 7)), b.push_back(0), b.push_back(0);
    put16(b, 4, (unsigned)b.size());
    const char names[] = "Tiny AHX\0Saw";
    b.insert(b.end(), names, names + sizeof names);
    return b;
}

// HVL: `channels` channels, the note plays on the channels in `on` (track 1, the others play blank track 0)
std::vector<uint8_t> tinyHvl(int channels = 12, std::vector<int> on = {0, 10}, int version = 1) {
    std::vector<uint8_t> b(16, 0);
    b[0] = 'H', b[1] = 'V', b[2] = 'L', b[3] = (uint8_t)version;
    b[6] = 0x80, b[7] = 1;  // track 0 blank, speed 1, one position
    b[8] = (uint8_t)((channels - 4) << 2);
    b[10] = 4, b[11] = 1, b[12] = 1, b[13] = 0, b[14] = 100, b[15] = 2;
    for (int c = 0; c < channels; ++c) {
        bool play = false;
        for (int x : on) play |= x == c;
        b.push_back(play ? 1 : 0), b.push_back(0);
    }
    const uint8_t row0[5] = {25, 1, 0, 0, 0};
    b.insert(b.end(), row0, row0 + 5);
    for (int r = 1; r < 4; ++r) b.push_back(0x3f);  // empty rows take one byte
    instrument(b, Tiny{});
    const uint8_t pl[5] = {0, 2, 0, 0, 0};  // sawtooth
    b.insert(b.end(), pl, pl + 5);
    put16(b, 4, (unsigned)b.size());
    const char names[] = "Tiny HVL\0Saw";
    b.insert(b.end(), names, names + sizeof names);
    return b;
}

float peakOf(ChipPlayer& p, double seconds, std::vector<float>* keep = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (keep) keep->insert(keep->end(), buf.begin(), buf.end());
    }
    return peak;
}

std::vector<float> renderOf(ChipPlayer& p, uint32_t frames) {
    std::vector<float> buf((size_t)frames * 2);
    p.render(buf.data(), frames, 0.45f);
    return buf;
}

int zeroCrossings(const std::vector<float>& st) {
    int n = 0;
    for (size_t i = 2; i < st.size(); i += 2) n += (st[i - 2] < 0) != (st[i] < 0);
    return n;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p, size_t limit = (size_t)-1) {
    std::ifstream in(p, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() > limit) d.resize(limit);
    return d;
}

std::filesystem::path modulesDir() {
    const std::filesystem::path main = "C:/development/FatTracker/modules";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules";
}

const char* kOcean = "Ocean/Amiga AHX - Jazz (NL) - ocean loader.ahx";

struct Known {
    const char* file;  // in modules/
    int voices, subsongs;
};
const Known kKnown[] = {
    {"Ocean/Amiga AHX - Jazz (NL) - ocean loader.ahx", 4, 1},
    {"AHX/Jazz (NL) - adrenalyn.ahx", 4, 7},
    {"AHX/Pink - back in 1986.ahx", 4, 1},
    {"AHX/Virgill - forsaken.hvl", 6, 1},
    {"AHX/Xeron - unexpected horse.hvl", 10, 1},
};
}  // namespace

// ---------------------------------------------------------------- detection
TEST(hvl_detect) {
    const auto a = tinyAhx(), h = tinyHvl();
    CHECK(HvlPlayer::detect(a.data(), a.size()));
    CHECK(HvlPlayer::detect(h.data(), h.size()));
    Tiny v1;
    v1.version = 1;
    const auto a1 = tinyAhx(v1);
    CHECK(HvlPlayer::detect(a1.data(), a1.size()));
    CHECK(!HvlPlayer::detect(nullptr, 0));
    CHECK(!HvlPlayer::detect(a.data(), 13));
    for (size_t cut = 0; cut < a.size() - 12; ++cut) CHECK(!HvlPlayer::detect(a.data(), cut));  // the data is cut
    for (size_t cut = 0; cut < h.size() - 12; ++cut) CHECK(!HvlPlayer::detect(h.data(), cut));
    auto bad = a;
    bad[3] = 2;  // AHX versions are 0 and 1
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = a, bad[0] = 'X';
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = h, bad[3] = 2;
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = a, bad[7] = 0;  // no positions
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = a, bad[10] = 65;  // track length over 64
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = a, bad[12] = 200;  // more instruments than the file holds
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = a, put16(bad, 4, (unsigned)bad.size() + 10);  // the name is not where the data ends
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = h, bad[8] = (uint8_t)((17 - 4) << 2);  // 17 channels: more than HVL has
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    bad = h, bad[15] = 9;  // stereo separation 0..4
    CHECK(!HvlPlayer::detect(bad.data(), bad.size()));
    // the other chip players do not claim them
    CHECK(!SidPlayer::detect(a.data(), a.size()));
    CHECK(!SndhPlayer::detect(h.data(), h.size()));
}

// ---------------------------------------------------------------- playback
TEST(hvl_tiny_ahx_plays_on_channel_1) {
    const auto f = tinyAhx();
    HvlPlayer p;
    p.setRate(48000);
    CHECK(!p.voices());
    CHECK(p.load(f.data(), f.size()));
    CHECK(std::strcmp(p.title(), "Tiny AHX") == 0);
    CHECK(std::strncmp(p.format(), "AHX", 3) == 0);
    CHECK_EQ(p.voices(), 4);
    CHECK_EQ(p.subsongs(), 1);
    CHECK_EQ(p.subsong(), 0);
    CHECK_NEAR(p.seconds(), 24 / 50.0, 0.05);  // 4 rows at speed 6, then it loops
    std::vector<float> keep;
    CHECK(peakOf(p, 1.0, &keep) > 0.05f);
    // sawtooth of 32 samples at period 856: 3546897 / 856 / 32 = 129.5 Hz, two zero crossings a cycle
    const int zc = zeroCrossings(keep);
    CHECK(zc > 2 * 110 && zc < 2 * 150);
    CHECK(p.vu(0) > 0.2f);
    CHECK(p.vu(1) == 0.f);
    CHECK(p.vu(3) == 0.f);
    float sc[256];
    p.scope(0, sc, 256);
    float hi = 0;
    for (float v : sc) hi = std::fabs(v) > hi ? std::fabs(v) : hi;
    CHECK(hi > 0.2f);
    p.scope(1, sc, 256);
    for (float v : sc) CHECK(v == 0.f);
    p.scope(9, sc, 256);  // no such voice
    for (float v : sc) CHECK(v == 0.f);
    p.clearMeters();
    CHECK(p.vu(0) == 0.f);
    Song s;
    CHECK(p.buildDisplay(s));
    CHECK_EQ(s.numChannels, 4);
    // restart: the same samples as a fresh load
    HvlPlayer q;
    q.setRate(48000);
    CHECK(q.load(f.data(), f.size()));
    p.restart();
    CHECK(renderOf(p, 9000) == renderOf(q, 9000));
    p.setRate(44100);  // another device rate: still the same pitch
    keep.clear();
    peakOf(p, 0.5);
    std::vector<float> buf(44100 * 2);
    p.render(buf.data(), 44100, 0.45f);
    const int zc2 = zeroCrossings(buf);
    CHECK(zc2 > 2 * 110 && zc2 < 2 * 150);
}

TEST(hvl_tiny_hvl_mixes_all_channels) {
    const auto f = tinyHvl(12, {0, 10});
    HvlPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK(std::strcmp(p.title(), "Tiny HVL") == 0);
    CHECK(std::strncmp(p.format(), "HVL", 3) == 0);
    CHECK_EQ(p.voices(), 12);
    CHECK(peakOf(p, 0.5) > 0.05f);
    CHECK(p.vu(0) > 0.2f);
    CHECK(p.vu(1) == 0.f);
    CHECK(p.vu(10) == 0.f);  // beyond the metered voices
    Song s;
    CHECK(p.buildDisplay(s));
    CHECK_EQ(s.numChannels, 12);
    // a channel past the first eight still sounds in the mix
    const auto g = tinyHvl(16, {13});
    HvlPlayer q;
    q.setRate(48000);
    CHECK(q.load(g.data(), g.size()));
    CHECK_EQ(q.voices(), 16);
    CHECK(peakOf(q, 0.5) > 0.05f);
    for (int v = 0; v < ChipPlayer::kMaxVoices; ++v) CHECK(q.vu(v) == 0.f);
    const auto four = tinyHvl(4, {3}, 0);
    HvlPlayer r;
    r.setRate(48000);
    CHECK(r.load(four.data(), four.size()));
    CHECK_EQ(r.voices(), 4);
    CHECK(peakOf(r, 0.3) > 0.05f);
    CHECK(r.vu(3) > 0.2f);
}

TEST(hvl_subsongs) {
    Tiny t;
    t.subsongs = 2;
    const auto f = tinyAhx(t);
    HvlPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.subsongs(), 3);
    CHECK_EQ(p.subsong(), 0);
    CHECK(p.seconds() > 0.4);  // 3 positions from position 0, then the loop
    CHECK(peakOf(p, 0.2) > 0.05f);
    CHECK(p.selectSubsong(2));
    CHECK_EQ(p.subsong(), 2);
    CHECK(p.seconds() > 0.3 && p.seconds() < 0.6);  // position 2 only (the song ends there and loops)
    CHECK(peakOf(p, 0.2) > 0.05f);
    CHECK(!p.selectSubsong(3));
    CHECK(!p.selectSubsong(-1));
    CHECK_EQ(p.subsong(), 2);
    CHECK(p.selectSubsong(0));
    CHECK_EQ(p.subsong(), 0);
    HvlPlayer q;  // a sub-song chosen at load
    q.setRate(48000);
    CHECK(q.load(f.data(), f.size(), 1));
    CHECK_EQ(q.subsong(), 1);
    HvlPlayer r;
    r.setRate(48000);
    CHECK(r.load(f.data(), f.size(), 7));  // out of range: the main song
    CHECK_EQ(r.subsong(), 0);
    HvlPlayer none;
    CHECK(!none.selectSubsong(0));
    CHECK_EQ(none.subsongs(), 1);
}

// ---------------------------------------------------------------- the replayer's own data
TEST(hvl_keeps_its_own_copy) {
    for (int k = 0; k < 2; ++k) {
        std::vector<uint8_t> f = k ? tinyHvl() : tinyAhx();
        const std::vector<uint8_t> keep = f;
        HvlPlayer p, q;
        p.setRate(48000), q.setRate(48000);
        CHECK(p.load(f.data(), f.size()));
        CHECK(q.load(keep.data(), keep.size()));
        std::fill(f.begin(), f.end(), (uint8_t)0xAA);  // the caller's buffer goes away
        f.clear();
        f.shrink_to_fit();
        CHECK(renderOf(p, 48000) == renderOf(q, 48000));
        CHECK(p.selectSubsong(0) && q.selectSubsong(0));
        CHECK(renderOf(p, 4800) == renderOf(q, 4800));
    }
    const auto path = modulesDir() / kOcean;
    if (!std::filesystem::exists(path)) return;
    std::vector<uint8_t> d = readFile(path);
    const std::vector<uint8_t> keep = d;
    HvlPlayer p, q;
    p.setRate(48000), q.setRate(48000);
    CHECK(p.load(d.data(), d.size()));
    CHECK(q.load(keep.data(), keep.size()));
    std::fill(d.begin(), d.end(), (uint8_t)0x55);
    CHECK(renderOf(p, 96000) == renderOf(q, 96000));
}

TEST(hvl_replayer_tables_initialised_once_from_any_thread) {
    const auto f = tinyAhx();
    HvlPlayer ref;
    ref.setRate(48000);
    CHECK(ref.load(f.data(), f.size()));
    const auto want = renderOf(ref, 4800);
    bool same[4] = {};
    std::thread t[4];
    for (int i = 0; i < 4; ++i)
        t[i] = std::thread([&f, &want, &same, i] {
            HvlPlayer p;
            p.setRate(48000);
            same[i] = p.load(f.data(), f.size()) && renderOf(p, 4800) == want;
        });
    for (auto& x : t) x.join();
    for (bool s : same) CHECK(s);
}

// ---------------------------------------------------------------- hostile files
namespace {
// refused, or played for a while without leaving the replayer's buffers; never slow
void playSafely(const std::vector<uint8_t>& d, int& loaded) {
    HvlPlayer p;
    p.setRate(48000);
    if (!p.load(d.data(), d.size())) return;
    ++loaded;
    std::vector<float> buf(2400 * 2);
    for (int i = 0; i < 4; ++i) p.render(buf.data(), 2400, 0.45f);
    for (float v : buf) CHECK(std::isfinite(v));
    CHECK(p.seconds() >= 0 && p.seconds() < 3600);
    if (p.subsongs() > 1) p.selectSubsong(p.subsongs() - 1), p.render(buf.data(), 2400, 0.45f);
}
}  // namespace

TEST(hvl_truncated_and_corrupted_files) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::vector<uint8_t>> files = {tinyAhx(), tinyHvl()};
    Tiny t;
    t.subsongs = 3;
    files.push_back(tinyAhx(t));
    for (const Known& k : kKnown) {
        const auto path = modulesDir() / k.file;
        if (std::filesystem::exists(path)) files.push_back(readFile(path));
    }
    int loaded = 0, tries = 0;
    for (const auto& f : files) {
        const size_t step = f.size() / 200 + 1;  // every few bytes
        for (size_t cut = 0; cut < f.size(); cut += step) {
            std::vector<uint8_t> c(f.begin(), f.begin() + (long long)cut);
            c.shrink_to_fit();  // the end of the data is the end of the allocation
            playSafely(c, loaded), ++tries;
        }
        uint32_t seed = 12345;
        for (int n = 0; n < 150; ++n) {  // random bytes changed (the header too)
            std::vector<uint8_t> c = f;
            const int changes = 1 + n % 8;
            for (int i = 0; i < changes; ++i) {
                seed = seed * 1664525u + 1013904223u;
                const size_t at = n % 3 == 0 ? (seed >> 8) % (c.size() < 16 ? c.size() : 16) : (seed >> 8) % c.size();
                c[at] = (uint8_t)(seed >> 24);
            }
            playSafely(c, loaded), ++tries;
        }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  %d damaged files, %d played, %.1f s\n", tries, loaded, s);
    CHECK(loaded > 0);
    CHECK(s < 60.0);
}

// the vendored loader itself (no detect in front of it): cut files and impossible headers are refused or
// parsed inside the buffer
TEST(hvl_replayer_loader_bounds) {
    HvlPlayer warm;  // generates the replayer's tables
    const auto a = tinyAhx(), h = tinyHvl(16, {0, 15});
    std::vector<std::vector<uint8_t>> files = {a, h};
    const auto path = modulesDir() / kOcean;
    if (std::filesystem::exists(path)) files.push_back(readFile(path));
    int parsed = 0;
    for (const auto& f : files)
        for (size_t cut = 0; cut <= f.size(); cut += (cut < 64 ? 1 : f.size() / 97 + 1)) {
            std::vector<uint8_t> c(f.begin(), f.begin() + (long long)cut);
            c.shrink_to_fit();
            hvl_tune* t = hvl_ParseTune(c.empty() ? nullptr : c.data(), (uint32)c.size(), 48000, 2);
            if (!t) continue;
            ++parsed;
            for (int i = 0; i < 300; ++i) hvl_play_irq(t);
            hvl_FreeTune(t);
        }
    CHECK(parsed > 0);
    auto many = h;
    many[8] = 0xFC;  // 67 channels
    CHECK(hvl_ParseTune(many.data(), (uint32)many.size(), 48000, 2) == nullptr);
    auto none = a;
    none[6] = 0x80, none[7] = 0;  // no positions
    CHECK(hvl_ParseTune(none.data(), (uint32)none.size(), 48000, 2) == nullptr);
    auto noTrack = a;
    noTrack[10] = 0;
    CHECK(hvl_ParseTune(noTrack.data(), (uint32)noTrack.size(), 48000, 2) == nullptr);
    auto farName = a;
    put16(farName, 4, 0xFFFF);  // the name past the end: empty names
    hvl_tune* t = hvl_ParseTune(farName.data(), (uint32)farName.size(), 48000, 2);
    CHECK(t != nullptr);
    if (t) CHECK(t->ht_Name[0] == 0);
    hvl_FreeTune(t);
    auto stereo = h;
    stereo[15] = 200;
    t = hvl_ParseTune(stereo.data(), (uint32)stereo.size(), 48000, 2);
    CHECK(t != nullptr);
    if (t) CHECK(t->ht_defstereo <= 4);
    hvl_FreeTune(t);
    hvl_InitReplayer();  // again: nothing changes
    const auto f = tinyAhx();
    HvlPlayer p, q;
    p.setRate(48000), q.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    hvl_InitReplayer();
    CHECK(q.load(f.data(), f.size()));
    CHECK(renderOf(p, 4800) == renderOf(q, 4800));
}

TEST(hvl_hostile_commands_stay_inside) {
    // jumps past the last position, notes past the period table, wave lengths and wave forms past the tables
    struct Case {
        uint8_t fx, param, waveLength, plWave, note;
    };
    const Case cases[] = {
        {0xB, 0x99, 3, 2, 25},  // position jump to 99 in a 1 position song
        {0x0, 0x09, 3, 2, 25},  // position jump high digit, then the row's B-less advance
        {0xD, 0x64, 3, 2, 25},  // pattern break to row 64 (the track has 4)
        {0xD, 0x04, 3, 2, 25},  // pattern break to row 4 = the track length
        {0x3, 0x10, 3, 2, 63},  // tone portamento towards note 63
        {0x5, 0x10, 3, 2, 62},
        {0x0, 0x00, 7, 2, 25},  // wave length 7 (AHX has 0..5)
        {0x9, 0xFF, 6, 3, 25},  // square with wave length 6, square offset FF
        {0x0, 0x00, 3, 7, 25},  // play list wave form 7 (1..4)
        {0x0, 0x00, 3, 5, 25},
        {0xF, 0x00, 3, 2, 25},  // speed 0
        {0xC, 0xFF, 3, 4, 25},  // noise, volume FF
        {0x4, 0x7F, 3, 1, 25},  // filter override
        {0x7, 0x80, 3, 2, 25},  // panning
    };
    int loaded = 0;
    for (const Case& c : cases) {
        for (int version = 0; version < 2; ++version) {
            Tiny t;
            t.version = version, t.fx = c.fx, t.fxParam = c.param, t.waveLength = c.waveLength, t.plWave = c.plWave;
            t.note = c.note;
            const auto f = tinyAhx(t);
            const int before = loaded;
            playSafely(f, loaded);
            CHECK_EQ(loaded, before + 1);
        }
    }
    // HVL: notes are a whole byte and instruments are checked against the count
    auto h = tinyHvl(8, {0, 7});
    const size_t row0 = 16 + 8 * 2;
    h[row0] = 0xF0, h[row0 + 1] = 0xFF;
    int n = 0;
    playSafely(h, n);
    h[row0] = 200, h[row0 + 1] = 1, h[row0 + 2] = 0x30, h[row0 + 3] = 0x10;  // portamento to note 200
    playSafely(h, n);
    h[row0 + 2] = 0xB0, h[row0 + 3] = 0xFF;  // jump far away
    playSafely(h, n);
    CHECK_EQ(n, 3);
}

// ---------------------------------------------------------------- engine
TEST(hvl_engine_load) {
    Tiny t;
    t.subsongs = 2;
    const auto f = tinyAhx(t);
    Engine e;
    CHECK(e.loadHvl(f.data(), f.size(), "", false));
    CHECK(e.tfmx());
    CHECK_EQ(e.chipSubsongs(), 3);
    CHECK(std::strncmp(e.tfmxFormat(), "AHX", 3) == 0);
    CHECK(e.song() != nullptr);
    if (e.song()) CHECK(_strnicmp(e.song()->name, "TINY AHX", 8) == 0);
    CHECK(e.selectChipSubsong(2));
    CHECK_EQ(e.chipSubsong(), 2);
    const auto h = tinyHvl(10, {0});
    CHECK(e.loadHvl(h.data(), h.size(), "", false));
    CHECK(e.song() && e.song()->numChannels == 10);
    std::vector<uint8_t> junk(64, 0x4E);
    CHECK(!e.loadHvl(junk.data(), junk.size(), "", false));
}

// ---------------------------------------------------------------- the real tunes
TEST(hvl_real_files_play) {
    int n = 0;
    for (const Known& k : kKnown) {
        const auto path = modulesDir() / k.file;
        if (!std::filesystem::exists(path)) {
            std::printf("SKIP %s (not there)\n", k.file);
            continue;
        }
        const auto d = readFile(path);
        CHECK(HvlPlayer::detect(d.data(), d.size()));
        HvlPlayer p;
        p.setRate(48000);
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = p.load(d.data(), d.size());
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s\n", k.file);
            continue;
        }
        CHECK_EQ(p.voices(), k.voices);
        CHECK_EQ(p.subsongs(), k.subsongs);
        CHECK(p.seconds() > 1.0);
        double longest = p.seconds();  // (a sub-song may be a short jingle: Adrenalyn's first one is)
        const float pk = peakOf(p, 3.0);
        CHECK(pk > 0.02f);
        float vu = 0;
        for (int v = 0; v < p.voices() && v < ChipPlayer::kMaxVoices; ++v) vu += p.vu(v);
        CHECK(vu > 0.f);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %s: \"%s\" %s, %d voices, %d song(s), %.1f s, peak %.2f (%.2f s to load and play 3 s)\n", k.file,
                    p.title(), p.format(), p.voices(), p.subsongs(), p.seconds(), pk, s);
        if (p.subsongs() > 1) {
            for (int i = 1; i < p.subsongs(); ++i) {
                CHECK(p.selectSubsong(i));
                CHECK(p.seconds() > 0.0);
                const float spk = peakOf(p, 0.5);
                CHECK(spk > 0.01f);
                std::printf("    sub-song %d: %.1f s, peak %.2f\n", i, p.seconds(), spk);
                longest = p.seconds() > longest ? p.seconds() : longest;
            }
        }
        CHECK(longest > 5.0);
        ++n;
    }
    // every other AHX / HVL file in modules/AHX loads and sounds too
    const auto dir = modulesDir() / "AHX";
    if (std::filesystem::exists(dir))
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            bool listed = false;
            for (const Known& k : kKnown) listed |= std::filesystem::path(k.file).filename() == e.path().filename();
            if (listed) continue;
            const auto d = readFile(e.path());
            if (!HvlPlayer::detect(d.data(), d.size())) continue;
            HvlPlayer p;
            p.setRate(48000);
            CHECK(p.load(d.data(), d.size()));
            CHECK(peakOf(p, 3.0) > 0.02f);
            ++n;
        }
    if (!n) std::printf("SKIP hvl_real_files_play (no AHX files)\n");
}

TEST(hvl_detect_skips_other_formats) {
    const auto dir = modulesDir();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP hvl_detect_skips_other_formats (no modules folder)\n");
        return;
    }
    int files = 0, ours = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        const std::string name = e.path().filename().string();
        const bool named = ext == ".ahx" || ext == ".thx" || ext == ".hvl" || !_strnicmp(name.c_str(), "ahx.", 4) ||
                           !_strnicmp(name.c_str(), "thx.", 4) || !_strnicmp(name.c_str(), "hvl.", 4);
        const auto d = readFile(e.path(), 1u << 20);  // every AHX / HVL structure fits in the first MB
        const bool det = HvlPlayer::detect(d.data(), d.size());
        if (named) {
            CHECK(det);
            ++ours;
        } else {
            if (det) std::printf("  claims %s\n", e.path().string().c_str());
            CHECK(!det);
            ++files;
        }
    }
    std::printf("  %d other files not claimed, %d AHX/HVL files detected\n", files, ours);
}
