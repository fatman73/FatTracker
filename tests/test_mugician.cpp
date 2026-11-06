// Digital Mugician 1 / 2 (ported from NostalgicPlayer): hand-built songs, broken files, the real modules.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

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
            for (int c = 0; c < 8; ++c) vu[c] = p.vu(c);
    }
    return peak;
}
void be16(std::vector<uint8_t>& f, int v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void be32(std::vector<uint8_t>& f, uint32_t v) { be16(f, (int)(v >> 16)), be16(f, (int)(v & 0xFFFF)); }
void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) f[at + i] = (uint8_t)(v >> (24 - 8 * i));
}

// A Digital Mugician song as the file lays it out (built into bytes by build()).
struct Dmu {
    bool v2 = false, arps = true;
    struct Sub {
        uint8_t loop = 1, loopPos = 0, speed = 6, numSeq = 0;
        std::vector<std::array<uint8_t, 2>> seq;  // 4 per position: track, transpose
        const char* name = "TINY";                // 12 characters at most
    } sub[8];
    std::vector<std::array<uint8_t, 16>> inst;
    std::vector<std::vector<uint8_t>> waves;  // 128 bytes each
    struct Smp {
        uint32_t start, end, loop;
    };
    std::vector<Smp> samples;
    std::vector<uint8_t> sampleData;
    std::vector<std::array<uint8_t, 256>> tracks;  // 64 rows: note, instrument, effect, parameter
    std::vector<uint8_t> arp = std::vector<uint8_t>(256, 0);

    std::vector<uint8_t> build() const {
        const char* mark = v2 ? " MUGICIAN2/SOFTEYES 1990" : " MUGICIAN/SOFTEYES 1990 ";
        std::vector<uint8_t> f(mark, mark + 24);
        be16(f, arps), be16(f, (int)tracks.size());
        for (const Sub& s : sub) be32(f, (uint32_t)(s.seq.size() / 4));
        be32(f, (uint32_t)inst.size()), be32(f, (uint32_t)waves.size()), be32(f, (uint32_t)samples.size());
        be32(f, (uint32_t)sampleData.size());
        for (const Sub& s : sub) {
            f.push_back(s.loop), f.push_back(s.loopPos), f.push_back(s.speed), f.push_back(s.numSeq);
            const size_t len = std::strlen(s.name);
            for (size_t i = 0; i < 12; ++i) f.push_back(i < len ? (uint8_t)s.name[i] : ' ');
        }
        for (const Sub& s : sub)
            for (const auto& q : s.seq) f.insert(f.end(), q.begin(), q.end());
        for (const auto& i : inst) f.insert(f.end(), i.begin(), i.end());
        for (const auto& w : waves) f.insert(f.end(), w.begin(), w.end());
        for (const Smp& s : samples) {
            be32(f, s.start), be32(f, s.end), be32(f, s.loop);
            for (int i = 0; i < 20; ++i) f.push_back(0);
        }
        for (const auto& t : tracks) f.insert(f.end(), t.begin(), t.end());
        f.insert(f.end(), sampleData.begin(), sampleData.end());
        if (arps) f.insert(f.end(), arp.begin(), arp.end());
        return f;
    }
};

// One position of 64 rows: `channel` plays track 1 (row 0: note 24, instrument `instNo`), every other
// channel the empty track 0. Instrument 1 is a synth sound on the first 32 bytes of waveform 0 (a square),
// instrument 2 the looped 64 byte square sample 0; both take their volume from waveform 1 (full volume).
Dmu tiny(bool v2, int instNo = 1, int channel = 0) {
    Dmu d;
    d.v2 = v2;
    for (int c = 0; c < 4; ++c)
        d.sub[0].seq.push_back({(uint8_t)(c == channel ? 1 : 0), (uint8_t)(c == 2 && channel != 2)});  // never empty
    d.sub[0].numSeq = 1;
    if (v2) {
        for (int c = 3; c < 7; ++c) d.sub[1].seq.push_back({(uint8_t)(c == channel ? 1 : 0), 0});
        d.sub[1].numSeq = 1;
    }
    d.inst.push_back({0, 16, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
    d.inst.push_back({32, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
    std::vector<uint8_t> w(128);
    for (int i = 0; i < 128; ++i) w[i] = (i & 31) < 16 ? 100 : (uint8_t)-100;
    d.waves.push_back(w);
    d.waves.push_back(std::vector<uint8_t>(128, 0x80));
    d.samples.push_back({2, 66, 2});
    d.sampleData = {0, 0};
    for (int i = 0; i < 64; ++i) d.sampleData.push_back(i < 32 ? 100 : (uint8_t)-100);
    d.tracks.resize(2);
    d.tracks[0].fill(0), d.tracks[1].fill(0);
    d.tracks[1][0] = 24, d.tracks[1][1] = (uint8_t)instNo;
    return d;
}

void checkTinyPlays(const Dmu& d, int channel = 0) {
    const auto f = d.build();
    CHECK(isMugician(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "DIGITAL MUGICIAN", 16) == 0);
    CHECK_EQ(p->voices(), d.v2 ? 7 : 4);
    CHECK(p->seconds() > 7 && p->seconds() < 8.5);  // 64 rows of 6 ticks
    float vu[8] = {};
    CHECK(peakOf(*p, 0.5, vu) > 0.05f);
    for (int c = 0; c < 8; ++c) {
        if (c == channel) CHECK(vu[c] > 0.1f);
        else CHECK(vu[c] < 0.01f);
    }
    Song s;
    CHECK(p->buildDisplay(s));
}

void playOdd(const std::vector<uint8_t>& f) {
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    peakOf(*p, 0.3);
    CHECK(p->seconds() >= 0 && p->seconds() <= 30 * 60 + 1);
}

bool refused(const Dmu& d) {
    const auto f = d.build();
    return AmigaSynthPlayer::open(f.data(), f.size()) == nullptr;
}

std::filesystem::path dmuFolder() {
    const std::filesystem::path main = "C:/development/FatTracker/modules/Digital Mugician";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules" / "Digital Mugician";
}
// The players main.cpp tries before the Amiga synth formats must leave the file alone.
bool claimedFirst(const std::vector<uint8_t>& d) {
    return TfmxPlayer::detect(d.data(), d.size()) || SidPlayer::detect(d.data(), d.size()) ||
           Pt3Player::detect(d.data(), d.size()) || GmePlayer::detect(d.data(), d.size()) ||
           AyPlayer::detect(d.data(), d.size());
}

}  // namespace

TEST(mugician1_tiny_synth_song_plays) { checkTinyPlays(tiny(false, 1)); }
TEST(mugician1_tiny_sample_song_plays) { checkTinyPlays(tiny(false, 2)); }
TEST(mugician2_tiny_synth_song_plays) { checkTinyPlays(tiny(true, 1)); }
TEST(mugician2_tiny_sample_song_plays) { checkTinyPlays(tiny(true, 2)); }
TEST(mugician2_mixed_voice_plays) { checkTinyPlays(tiny(true, 1, 4), 4); }  // a voice mixed into Paula 3

TEST(mugician_broken_files) {
    for (int v2 = 0; v2 < 2; ++v2) {
        const auto f = tiny(v2 != 0, 2).build();
        for (size_t cut = 0; cut < f.size(); cut += 7) {
            CHECK(!isMugician(f.data(), cut));
            CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
        }
        // counts that claim gigabytes
        for (size_t at : {size_t(28), size_t(60), size_t(64), size_t(68)}) {
            auto g = f;
            put32(g, at, 0xFFFFFFF0u);
            CHECK(!isMugician(g.data(), g.size()));
            CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
        }
        auto g = f;
        g[26] = 0xFF, g[27] = 0xFF;  // 65535 tracks
        CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
        Dmu d = tiny(v2 != 0, 2);
        d.samples[0] = {66, 2, 0};  // ends before it starts
        CHECK(refused(d));
        d.samples[0] = {2, 0x7FFFFFF0u, 0};  // ends far past the file
        CHECK(refused(d));
        d = tiny(v2 != 0, 2);
        d.sub[0].numSeq = 2;  // no playable sub-song
        CHECK(refused(d));
        d = tiny(v2 != 0, 2);
        d.tracks.clear();  // no tracks
        CHECK(refused(d));
    }

    std::vector<std::vector<uint8_t>> odd;
    for (int v2 = 0; v2 < 2; ++v2) {
        const bool b2 = v2 != 0;
        auto add = [&](const Dmu& d) { odd.push_back(d.build()); };
        Dmu d = tiny(b2, 64);  // instrument 64 of 2
        add(d);
        d = tiny(b2, 1);
        d.inst[0][0] = 31;  // waveform 31 of 2
        add(d);
        d = tiny(b2, 2);
        d.inst[1][0] = 40;  // sample 8 of 1
        add(d);
        d = tiny(b2, 1);
        d.inst[0] = {0, 255, 200, 1, 200, 200, 0, 0, 255, 255, 0, 0, 200, 200, 0, 1};  // every table out of range
        add(d);
        for (int fx = 1; fx <= 17; ++fx)
            for (int src = 0; src < 2; ++src) {
                d = tiny(b2, 1);
                d.inst[0] = {0, (uint8_t)(src ? 255 : 16), 1, 1, 0, 1, 0, 2, 0, 3, 1, (uint8_t)(fx == 17 ? 255 : fx),
                             (uint8_t)(src ? 200 : 1), (uint8_t)(src ? 200 : 0), 0, 1};
                add(d);
            }
        d = tiny(b2, 2);
        d.samples[0].loop = 1;  // loop before the sample's start
        add(d);
        d.samples[0].loop = 500;  // and past its end
        add(d);
        d = tiny(b2, 1);
        d.tracks[1][0] = 255, d.sub[0].seq[0][1] = 127;  // note past the period table, transposed
        add(d);
        d.sub[0].seq[0][1] = 0x80;  // and below it
        add(d);
        const uint8_t effects[][2] = {{67, 0}, {67, 65}, {67, 1}, {68, 0}, {68, 0xF7}, {75, 0x0F}, {75, 0x31},
                                      {74, 0x80}, {72, 0}, {73, 0xFF}, {64, 0}, {66, 0}, {71, 0}, {200, 0x80},
                                      {63, 0x7F}, {0, 0x80}};
        for (const auto& e : effects) {
            d = tiny(b2, 1);
            for (int row = 0; row < 64; row += 3) {
                d.tracks[1][row * 4] = (uint8_t)(24 + row % 12);
                d.tracks[1][row * 4 + 1] = 1;
                d.tracks[1][row * 4 + 2] = e[0];
                d.tracks[1][row * 4 + 3] = e[1];
            }
            add(d);
        }
        d = tiny(b2, 1);
        d.sub[0].speed = 0;  // speed 0: rows come every 65536 ticks
        add(d);
        d = tiny(b2, 1);
        d.sub[0].loopPos = 200;  // loop position past the song
        add(d);
        d = tiny(b2, 1);
        d.sub[0].loop = 0;  // no loop: the song starts over
        add(d);
        d = tiny(b2, 1);
        d.sub[0].seq[0][0] = 200;  // track 200 of 2
        add(d);
        d = tiny(b2, 1);
        d.arps = false;  // no arpeggio tables
        d.inst[0][4] = 3;
        add(d);
    }
    // Mugician 2: the second sequence list shorter than the first
    Dmu d = tiny(true, 1, 5);
    d.sub[0].seq.resize(8, {0, 1}), d.sub[0].numSeq = 2;
    odd.push_back(d.build());
    // Mugician 2: seven voices, seven instruments with wave effects (more than NostalgicPlayer's chTab of 4)
    d = tiny(true, 1);
    d.sub[0].seq = {{1, 0}, {2, 0}, {3, 0}, {0, 0}};
    d.sub[1].seq = {{4, 0}, {5, 0}, {6, 0}, {7, 0}};
    d.tracks.resize(8);
    for (int t = 1; t < 8; ++t) {
        d.tracks[t].fill(0);
        d.tracks[t][0] = (uint8_t)(20 + t), d.tracks[t][1] = (uint8_t)(t + 2);
        d.inst.push_back({0, 16, 1, 1, 0, 0, 0, 0, 0, 0, 0, (uint8_t)t, 1, 0, 0, 1});
    }
    odd.push_back(d.build());
    for (const auto& v : odd) playOdd(v);
}

// Sub-songs: the playable ones in file order (Mugician 2 takes them in pairs), each with its own length and name.
TEST(mugician_subsongs) {
    for (int v2 = 0; v2 < 2; ++v2) {
        Dmu d = tiny(v2 != 0);
        d.sub[0].name = "FIRST";
        // slot 2 (and 3 for the second sequence list of Mugician 2): two positions
        for (int pos = 0; pos < 2; ++pos)
            for (int c = 0; c < 4; ++c) d.sub[2].seq.push_back({(uint8_t)(c == 1 ? 1 : 0), 0});
        d.sub[2].numSeq = 2;
        d.sub[2].name = "SECOND SONG";
        if (v2) {
            for (int pos = 0; pos < 2; ++pos)
                for (int c = 0; c < 4; ++c) d.sub[3].seq.push_back({0, 0});
            d.sub[3].numSeq = 2;
        }
        const auto f = d.build();
        auto p = AmigaSynthPlayer::open(f.data(), f.size());
        CHECK(p != nullptr);
        if (!p) continue;
        p->setRate(48000);
        CHECK_EQ(p->subsongs(), 2);
        CHECK_EQ(p->subsong(), 0);
        CHECK(std::strcmp(p->subsongTitle(), "FIRST") == 0);
        CHECK(p->seconds() > 7 && p->seconds() < 8.5);
        CHECK(p->selectSubsong(1));
        CHECK_EQ(p->subsong(), 1);
        CHECK(std::strcmp(p->subsongTitle(), "SECOND SONG") == 0);
        CHECK(p->seconds() > 14 && p->seconds() < 17);  // two positions
        float vu[8] = {};
        CHECK(peakOf(*p, 0.5, vu) > 0.05f);
        CHECK(vu[1] > 0.1f);  // the second sub-song plays track 1 on voice 2
        CHECK(vu[0] < 0.01f);
        CHECK(!p->selectSubsong(2));
        CHECK(!p->selectSubsong(-1));
        CHECK_EQ(p->subsong(), 1);
        CHECK(p->selectSubsong(0));
        CHECK(p->seconds() > 7 && p->seconds() < 8.5);
    }
}

TEST(mugician_detect_is_strict) {
    std::vector<uint8_t> big(4096, 0);
    std::memcpy(big.data(), " MUGICIAN/SOFTEYES 1990 ", 24);  // the mark alone, nothing to play
    CHECK(!isMugician(big.data(), big.size()));
    CHECK(AmigaSynthPlayer::open(big.data(), big.size()) == nullptr);
    for (size_t i = 24; i < big.size(); ++i) big[i] = 0x7F;  // counts that point nowhere
    CHECK(!isMugician(big.data(), big.size()));
    std::memcpy(big.data(), " MUGICIAN3/SOFTEYES 1990", 24);
    CHECK(!isMugician(big.data(), big.size()));
}

TEST(mugician_real_modules_play) {
    const std::filesystem::path dir = dmuFolder();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP mugician_real_modules_play (no Digital Mugician modules)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        CHECK(isMugician(d.data(), d.size()));
        CHECK(AmigaSynthPlayer::detect(d.data(), d.size()));
        CHECK(!claimedFirst(d));
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), "DIGITAL MUGICIAN", 16) == 0);
        CHECK(p->seconds() > 5);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.05f);
        float vuMax[8] = {};  // which voices sound in the first 15 seconds
        std::vector<float> buf(4800 * 2);
        for (int i = 0; i < 150; ++i) {
            p->render(buf.data(), 4800, 0.45f);
            for (int c = 0; c < 8; ++c) vuMax[c] = p->vu(c) > vuMax[c] ? p->vu(c) : vuMax[c];
        }
        int used = 0, mixed = 0;
        for (int c = 0; c < 8; ++c) used += vuMax[c] > 0.01f, mixed += c >= 3 && vuMax[c] > 0.01f;
        CHECK(used >= 2);
        if (p->voices() == 7) CHECK(mixed >= 1);  // the voices mixed into Paula 3 play too
        std::printf("  %s: %s, %d voices (%d sound), %.1f s, peak %.2f\n", e.path().filename().string().c_str(),
                    p->format(), p->voices(), used, p->seconds(), pk);
        ++n;
    }
    CHECK(n > 0);
}
