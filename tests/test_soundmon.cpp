// BP SoundMon V.2 / V.3 (ported from NostalgicPlayer): hand-built songs, broken files, the real modules,
// and a scan that no other module in the collection passes as SoundMon or Digital Mugician.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "build_mod.h"
#include "core/amiga_formats.h"
#include "core/amiga_synth.h"
#include "core/ay_player.h"
#include "core/gme_player.h"
#include "core/pt3_player.h"
#include "core/sid_player.h"
#include "core/song.h"
#include "core/tfmx_player.h"
#include "test.h"

namespace {
float peakOf(AmigaSynthPlayer& p, double seconds, float* vu = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (vu && i == 2)
            for (int c = 0; c < 4; ++c) vu[c] = p.vu(c);
    }
    return peak;
}
void be16(std::vector<uint8_t>& f, int v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void put16(std::vector<uint8_t>& f, size_t at, int v) { f[at] = (uint8_t)(v >> 8), f[at + 1] = (uint8_t)v; }
void square(std::vector<uint8_t>& f, int n) {
    for (int i = 0; i < n; ++i) f.push_back(i < n / 2 ? 100 : (uint8_t)-100);
}

// One position: channel 0 plays track 1 (row 0: note 13, instrument 1), channels 1-3 the empty track 2.
// Instrument 1 is a looped 32 byte square sample (synth = false) or a synth sound playing the first 32
// bytes of wave table 0 (a square); instruments 2-15 are empty samples. One 64 byte wave table.
constexpr size_t kInst = 32, kSteps = kInst + 15 * 32, kTracks = kSteps + 16, kWaves = kTracks + 2 * 48;
constexpr size_t kSampleData = kWaves + 64;
std::vector<uint8_t> tinyBp(bool v3, bool synth) {
    std::vector<uint8_t> f(26, 0);
    std::memcpy(f.data(), "TINY", 4);
    f.push_back('V'), f.push_back('.'), f.push_back(v3 ? '3' : '2');
    f.push_back(1);  // wave tables
    be16(f, 1);      // positions
    for (int i = 0; i < 15; ++i) {
        uint8_t inst[32] = {};
        if (i == 0 && synth) {
            inst[0] = 0xFF;
            inst[3] = 16;              // wave length (words)
            inst[v3 ? 29 : 25] = 64;   // volume
        } else if (i == 0) {
            std::memcpy(inst, "SQUARE", 6);
            inst[25] = 16;  // length (words)
            inst[29] = 16;  // loop length (words)
            inst[31] = 64;  // volume
        }
        f.insert(f.end(), inst, inst + 32);
    }
    for (int c = 0; c < 4; ++c) be16(f, c == 0 ? 1 : 2), f.push_back(0), f.push_back(0);
    for (int t = 0; t < 2; ++t)
        for (int row = 0; row < 16; ++row) {
            const bool on = t == 0 && row == 0;
            f.push_back(on ? 13 : 0), f.push_back(on ? 0x10 : 0), f.push_back(0);
        }
    square(f, 32);
    for (int i = 0; i < 32; ++i) f.push_back(0);
    if (!synth) square(f, 32);
    return f;
}

void checkTinyPlays(const std::vector<uint8_t>& f) {
    CHECK(isSoundMon(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "SOUNDMON", 8) == 0);
    CHECK_EQ(p->voices(), 4);
    CHECK(p->seconds() > 1.5 && p->seconds() < 2.5);  // 16 rows of 6 ticks
    float vu[4] = {};
    CHECK(peakOf(*p, 0.5, vu) > 0.05f);
    CHECK(vu[0] > 0.1f);
    CHECK(vu[1] < 0.01f && vu[2] < 0.01f && vu[3] < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

// Odd but complete files load and play (briefly) without crashing, reading out of range or hanging.
void playOdd(const std::vector<uint8_t>& f) {
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    peakOf(*p, 0.3);
    CHECK(p->seconds() >= 0 && p->seconds() <= 30 * 60 + 1);
}

std::filesystem::path modulesRoot() {
    const std::filesystem::path main = "C:/development/FatTracker/modules";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules";
}

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}
// The players main.cpp tries before the Amiga synth formats must leave the file alone.
bool claimedFirst(const std::vector<uint8_t>& d) {
    return TfmxPlayer::detect(d.data(), d.size()) || SidPlayer::detect(d.data(), d.size()) ||
           Pt3Player::detect(d.data(), d.size()) || GmePlayer::detect(d.data(), d.size()) ||
           AyPlayer::detect(d.data(), d.size());
}

}  // namespace

TEST(soundmon_v2_tiny_sample_song_plays) { checkTinyPlays(tinyBp(false, false)); }
TEST(soundmon_v2_tiny_synth_song_plays) { checkTinyPlays(tinyBp(false, true)); }
TEST(soundmon_v3_tiny_sample_song_plays) { checkTinyPlays(tinyBp(true, false)); }
TEST(soundmon_v3_tiny_synth_song_plays) { checkTinyPlays(tinyBp(true, true)); }

TEST(soundmon_broken_files) {
    for (int v3 = 0; v3 < 2; ++v3)
        for (int synth = 0; synth < 2; ++synth) {
            const auto f = tinyBp(v3 != 0, synth != 0);
            for (size_t cut = 0; cut < f.size(); cut += 7) {
                CHECK(!isSoundMon(f.data(), cut));
                CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
            }
        }
    auto g = tinyBp(true, false);
    put16(g, 30, 0xFFFF);  // 65535 positions in a tiny file
    CHECK(!isSoundMon(g.data(), g.size()));
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyBp(true, false);
    put16(g, kSteps, 0x7FFF);  // a track number that would need 1.5 MB of tracks
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyBp(true, false);
    put16(g, kSteps + 4, 0);  // track 0 (NostalgicPlayer would index tracks[-1])
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyBp(true, false);
    put16(g, 30, 0);  // no positions
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);

    std::vector<std::vector<uint8_t>> odd;
    for (int v3 = 0; v3 < 2; ++v3) {
        const bool b3 = v3 != 0;
        auto h = tinyBp(b3, false);
        h[kTracks + 1] = 0xF0;  // instrument 15: an empty sample
        odd.push_back(h);
        h = tinyBp(b3, false);
        h[kTracks] = 0x7F, h[kSteps + 3] = 0x7F, h[kSteps + 2] = 0x7F;  // note far past the table, transposed
        odd.push_back(h);
        h = tinyBp(b3, false);
        h[kTracks] = 0x80, h[kSteps + 3] = 0x80;  // and far below it
        odd.push_back(h);
        h = tinyBp(b3, false);
        h[kTracks + 1] = 0x12, h[kTracks + 2] = 0;  // speed 0
        odd.push_back(h);
        h = tinyBp(b3, false);
        h[kTracks + 1] = 0x17, h[kTracks + 2] = 200;  // jump past the last position (V.3) / dbra (V.2)
        odd.push_back(h);
        h = tinyBp(b3, false);
        h[kTracks + 3 + 1] = 0x16, h[kTracks + 3 + 2] = 0xFF;  // repeat count 255 (V.2) / vibrato -1 (V.3)
        h[kTracks + 6 + 1] = 0x17, h[kTracks + 6 + 2] = 0;     // then back to position 0
        odd.push_back(h);
        h = tinyBp(b3, false);
        h[kTracks + 1] = 0x16, h[kTracks + 2] = 0x80;  // vibrato -128
        h[kTracks + 3 + 1] = 0x19, h[kTracks + 3 + 2] = 0xFF;  // continuous arpeggio past the table
        h[kTracks + 6 + 1] = 0x18, h[kTracks + 6 + 2] = 0x80;  // auto slide
        h[kTracks + 9 + 1] = 0x14, h[kTracks + 9 + 2] = 0xFF;  // portamento up past period 0
        odd.push_back(h);
        h = tinyBp(b3, false);
        put16(h, kInst + 26, 0xFFFF);  // loop start far past the sample
        odd.push_back(h);
        // a synth sound whose every table points past the wave tables
        h = tinyBp(b3, true);
        uint8_t* s = h.data() + kInst;
        s[1] = 0xFF, s[2] = 0xFF, s[3] = 0xFF;            // wave table 255, 128 KB long
        s[4] = 2, s[5] = 200, s[8] = 1;                    // ADSR looping over table 200, length 0
        s[9] = 2, s[10] = 250, s[11] = 3, s[b3 ? 15 : 16] = 1;  // LFO
        odd.push_back(h);
        // a synth sound on wave table 0 whose EG / MOD tables and effects reach past the wave tables
        for (int fx = 0; fx <= 7; ++fx) {
            h = tinyBp(b3, true);
            s = h.data() + kInst;
            s[4] = 2, s[5] = 0, s[8] = 1;  // ADSR over wave 0, length 0: walks the whole 64 KB range
            if (b3) {
                s[16] = 2, s[17] = 200, s[21] = 1;  // EG over table 200
                s[22] = (uint8_t)fx, s[23] = 3, s[24] = 0;  // effect fx, speed 3
                s[25] = 2, s[26] = 255, s[27] = 1;  // MOD over table 255
            } else {
                s[17] = 2, s[18] = 200, s[24] = 1;  // EG over table 200
                h[kTracks + 1] = 0x1B, h[kTracks + 2] = (uint8_t)fx;  // effect fx via the track
            }
            h[kTracks + 3 + 1] = 0x1D, h[kTracks + 3 + 2] = 0x11;  // change inversion
            h[kTracks + 6] = 1, h[kTracks + 6 + 1] = 0x1E;          // reset ADSR with a note
            odd.push_back(h);
        }
    }
    for (const auto& v : odd) playOdd(v);
}

TEST(soundmon_detect_is_strict) {
    // A MOD whose first sample name carries "V.3" at offset 26 must not pass as SoundMon
    std::vector<std::vector<ModCell>> pat(1, std::vector<ModCell>(64 * 4, ModCell{-1, 0, 0, 0}));
    for (const char* name : {"ABCDEFV.3ABCDEFGHIJKLM", "ABCDEFV.2", "\xFF\xFF\xFF\xFF\xFFV.3\x01\x00\x01"}) {
        auto mod = buildMod("M.K.", 4, {0}, pat, {ModSample{16, 0, 64, 0, 0, std::vector<int8_t>(32, 50)}});
        std::memcpy(mod.data() + 20, name, std::strlen(name) < 22 ? std::strlen(name) : 22);
        CHECK(!isSoundMon(mod.data(), mod.size()));
        CHECK(AmigaSynthPlayer::open(mod.data(), mod.size()) == nullptr);
    }
    std::vector<uint8_t> big(4096, 0);
    std::memcpy(big.data() + 26, "V.3", 3);  // the mark alone, everything else zero: no positions
    CHECK(!isSoundMon(big.data(), big.size()));
}

TEST(soundmon_real_modules_play) {
    const std::filesystem::path dir = modulesRoot() / "SoundMon";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP soundmon_real_modules_play (no SoundMon modules)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const auto d = readAll(e.path());
        CHECK(isSoundMon(d.data(), d.size()));
        CHECK(AmigaSynthPlayer::detect(d.data(), d.size()));
        CHECK(!claimedFirst(d));
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), "SOUNDMON", 8) == 0);
        CHECK(p->seconds() > 5);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.05f);
        std::printf("  %s: %s, %.1f s, peak %.2f\n", e.path().filename().string().c_str(), p->format(), p->seconds(), pk);
        ++n;
    }
    CHECK(n > 0);
}

// Every module in the collection outside the SoundMon / Digital Mugician folders: neither format claims it.
TEST(soundmon_mugician_no_false_positives) {
    const std::filesystem::path root = modulesRoot();
    if (!std::filesystem::exists(root)) {
        std::printf("SKIP soundmon_mugician_no_false_positives (no modules folder)\n");
        return;
    }
    int scanned = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || e.file_size() > (16u << 20)) continue;
        const std::string folder = e.path().parent_path().filename().string();
        if (folder == "SoundMon" || folder == "Digital Mugician") continue;
        const auto d = readAll(e.path());
        const bool bp = isSoundMon(d.data(), d.size()), dmu = isMugician(d.data(), d.size());
        if (bp || dmu) std::printf("  FALSE POSITIVE %s\n", e.path().string().c_str());
        CHECK(!bp);
        CHECK(!dmu);
        ++scanned;
    }
    CHECK(scanned > 0);
}
