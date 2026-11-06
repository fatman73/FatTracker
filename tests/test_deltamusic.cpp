// Delta Music 1.0 / 2.0 (ported from NostalgicPlayer): hand-built songs, broken files, the real modules.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "build_mod.h"
#include "core/amiga_synth.h"
#include "core/song.h"
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
void be32(std::vector<uint8_t>& f, uint32_t v) { be16(f, (int)(v >> 16)), be16(f, (int)(v & 0xFFFF)); }
void put16(std::vector<uint8_t>& f, size_t at, int v) { f[at] = (uint8_t)(v >> 8), f[at + 1] = (uint8_t)v; }
void square(std::vector<uint8_t>& f, int n) {
    for (int i = 0; i < n; ++i) f.push_back(i < n / 2 ? 100 : (uint8_t)-100);
}

// ---------------------------------------------------------------- Delta Music 1.0
// Channel 0 plays block 0 (row 0: instrument `inst`, note 36), channels 1-3 play the empty block 1;
// every track is "block, then jump back to position 0". Instrument 0 is a looped 32 byte square sample,
// instrument 1 a synth sound whose table plays wave 1 of two 32 byte waves.
constexpr size_t kDm1Tracks = 104, kDm1Blocks = kDm1Tracks + 4 * 6, kDm1Inst = kDm1Blocks + 128;
std::vector<uint8_t> tinyDm1(int inst = 0) {
    std::vector<uint8_t> f = {'A', 'L', 'L', ' '};
    for (int c = 0; c < 4; ++c) be32(f, 6);
    be32(f, 128);
    be32(f, 30 + 32);
    be32(f, 78 + 64);
    for (int i = 2; i < 20; ++i) be32(f, 0);
    for (int c = 0; c < 4; ++c) {
        const uint8_t t[6] = {(uint8_t)(c == 0 ? 0 : 1), 0, 0xFF, 0xFF, 0, 0};
        f.insert(f.end(), t, t + 6);
    }
    for (int b = 0; b < 2; ++b)
        for (int row = 0; row < 16; ++row) {
            const bool on = b == 0 && row == 0;
            const uint8_t line[4] = {(uint8_t)(on ? inst : 0), (uint8_t)(on ? 36 : 0), 0, 0};
            f.insert(f.end(), line, line + 4);
        }
    for (int k = 0; k < 2; ++k) {
        const uint8_t head[16] = {64, 0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, (uint8_t)(k == 0), 0};
        f.insert(f.end(), head, head + 16);
        for (int a = 0; a < 8; ++a) f.push_back(0);  // arpeggio
        be16(f, 32);                                  // length (bytes)
        be16(f, 0), be16(f, k == 0 ? 16 : 0);         // repeat start / length (words)
        if (k == 1) {
            const uint8_t table[48] = {1, 0xFF, 0};
            f.insert(f.end(), table, table + 48);
            for (int i = 0; i < 32; ++i) f.push_back(0);  // wave 0: silence
        }
        square(f, 32);
    }
    return f;
}

// ---------------------------------------------------------------- Delta Music 2.0
// Same song: channel 0 plays block 0 (row 0: note 37, instrument `inst`), the others the empty block 1.
// Instrument 0 is a looped 32 byte square sample, instrument 1 a synth sound on waveform 1 (0 is noise).
constexpr size_t kDm2TrackHead = 0xfca, kDm2Tracks = 0xfda, kDm2Blocks = kDm2Tracks + 4 * 2 + 4;
constexpr size_t kDm2Inst = kDm2Blocks + 128 + 254 + 2;
std::vector<uint8_t> tinyDm2(int inst = 0, int speed = 5) {
    std::vector<uint8_t> f(0xbc6, 0);
    f[0xbbb] = (uint8_t)speed;
    f.insert(f.end(), {'.', 'F', 'N', 'L'});
    f.resize(0xfca, 0);  // arpeggios: all zero
    for (int c = 0; c < 4; ++c) be16(f, 0), be16(f, 2);
    for (int c = 0; c < 4; ++c) f.push_back(c == 0 ? 0 : 1), f.push_back(0);
    be32(f, 128);
    for (int b = 0; b < 2; ++b)
        for (int row = 0; row < 16; ++row) {
            const bool on = b == 0 && row == 0;
            const uint8_t line[4] = {(uint8_t)(on ? 37 : 0), (uint8_t)(on ? inst : 0), 0, 0};
            f.insert(f.end(), line, line + 4);
        }
    for (int i = 1; i < 128; ++i) be16(f, i == 1 ? 88 : 176);  // instrument offsets 1..127
    be16(f, 176);                                               // end of the instruments
    for (int k = 0; k < 2; ++k) {
        be16(f, 16), be16(f, 0), be16(f, 16);  // length, repeat start, repeat length (words)
        for (int j = 0; j < 5; ++j) f.push_back(255), f.push_back(252), f.push_back(0);  // volume table
        for (int j = 0; j < 5; ++j) f.push_back(0), f.push_back(1), f.push_back(0);      // vibrato table
        be16(f, 0);                        // pitch bend
        f.push_back(k == 0 ? 0xFF : 0);    // sample / synth
        f.push_back(0);                    // sample number / table delay
        const uint8_t table[48] = {1, 0xFF, 0};
        f.insert(f.end(), table, table + 48);
    }
    be32(f, 512);
    for (int i = 0; i < 256; ++i) f.push_back(0);  // waveform 0 (noise)
    square(f, 32);
    for (int i = 32; i < 256; ++i) f.push_back(0);  // waveform 1
    for (int i = 0; i < 64; ++i) f.push_back(0);
    for (int i = 0; i < 8; ++i) be32(f, 0);  // sample offsets
    square(f, 32);
    return f;
}

void checkTinyPlays(const std::vector<uint8_t>& f, const char* format) {
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), format, std::strlen(format)) == 0);
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

std::filesystem::path dmFolder() {
    const std::filesystem::path main = "C:/development/FatTracker/modules/Dr. Awesome/Delta Music";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules" / "Dr. Awesome" / "Delta Music";
}
}  // namespace

TEST(deltamusic1_tiny_sample_song_plays) { checkTinyPlays(tinyDm1(0), "DELTA MUSIC 1.0"); }
TEST(deltamusic1_tiny_synth_song_plays) { checkTinyPlays(tinyDm1(1), "DELTA MUSIC 1.0"); }
TEST(deltamusic2_tiny_sample_song_plays) { checkTinyPlays(tinyDm2(0), "DELTA MUSIC 2.0"); }
TEST(deltamusic2_tiny_synth_song_plays) { checkTinyPlays(tinyDm2(1), "DELTA MUSIC 2.0"); }

TEST(deltamusic1_broken_files) {
    const auto f = tinyDm1(1);
    for (size_t cut = 4; cut < f.size(); cut += 5) CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
    auto g = f;
    put16(g, 4, 0x7FFF);  // a track claims 2 GB
    CHECK(!AmigaSynthPlayer::detect(g.data(), g.size()));
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    std::vector<std::vector<uint8_t>> odd;
    auto h = f;
    h[kDm1Blocks] = 200;  // instrument 200 of 20
    odd.push_back(h);
    h = f;
    h[kDm1Blocks] = 5;  // an instrument that is not there
    odd.push_back(h);
    h = f;
    h[kDm1Blocks + 1] = 250, h[kDm1Tracks + 1] = 100;  // note far past the period table, transposed
    h[kDm1Blocks + 2] = 0x0B, h[kDm1Blocks + 3] = 200;  // arpeggio far past it too
    odd.push_back(h);
    h = f;
    h[kDm1Tracks] = 99;  // block 99 of 2
    odd.push_back(h);
    h = f;
    h[kDm1Tracks] = 0xFF, h[kDm1Tracks + 1] = 0xFF;  // jump to position 0, which is the jump: endless
    h[kDm1Tracks + 2] = 0, h[kDm1Tracks + 3] = 0;
    odd.push_back(h);
    h = f;
    h[kDm1Tracks + 4] = 0x07, h[kDm1Tracks + 5] = 0xFF;  // jump far past the track's end
    odd.push_back(h);
    const size_t synthTable = kDm1Inst + 62 + 30;
    h = f;
    for (int i = 0; i < 48; ++i) h[synthTable + i] = 0x85;  // only delays: an endless table walk
    odd.push_back(h);
    h = f;
    h[synthTable + 47] = 0xFF;  // jump at the last entry, its target past the table
    h[synthTable] = 0x7F;       // wave 127 of 2
    h[synthTable + 1] = 47;
    odd.push_back(h);
    h = f;
    put16(h, kDm1Inst + 62 + 24, 0xFFFF);  // synth wave length far past the waves
    put16(h, kDm1Inst + 24, 0xFFFF);       // sample length too
    put16(h, kDm1Inst + 26, 0xFFFF);       // and a loop outside it
    odd.push_back(h);
    h[kDm1Blocks] = 0;  // the sample plays
    odd.push_back(h);
    for (const auto& v : odd) playOdd(v);
}

TEST(deltamusic2_broken_files) {
    const auto f = tinyDm2(1);
    for (size_t cut = 0xbca; cut + 300 < f.size(); cut += 97) CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
    std::vector<std::vector<uint8_t>> odd;
    auto h = f;
    h[kDm2Blocks + 1] = 200;  // instrument 200 of 2
    odd.push_back(h);
    h = f;
    h[kDm2Blocks + 1] = 100;  // an instrument that is not there
    odd.push_back(h);
    h = f;
    h[kDm2Blocks] = 250, h[kDm2Tracks + 1] = 0x7F;  // note past the period table, transposed
    odd.push_back(h);
    h = f;
    h[kDm2Tracks] = 99;  // block 99 of 2
    odd.push_back(h);
    h = f;
    h[kDm2TrackHead + 1] = 50;  // loop position past the track
    odd.push_back(h);
    const size_t table = kDm2Inst + 88 + 40;
    h = f;
    h[table] = 0xFF, h[table + 1] = 200;  // jump past the table
    odd.push_back(h);
    h = f;
    h[table + 47] = 0xFF;  // jump at the last entry
    h[table] = 0x80;       // wave 128 of 2
    odd.push_back(h);
    h = f;
    put16(h, kDm2Inst + 88, 0x7FFF);  // synth wave length far past a waveform
    put16(h, kDm2Inst, 100);          // sample length past its data (up to 256 bytes may be missing)
    put16(h, kDm2Inst + 2, 0xFFFF);   // and a loop outside it
    odd.push_back(h);
    h[kDm2Blocks + 1] = 0;  // the sample plays
    odd.push_back(h);
    h = tinyDm2(0, 0x80);  // negative speed: rows never come
    odd.push_back(h);
    h = f;
    h[kDm2Blocks + 2] = 8, h[kDm2Blocks + 3] = 0xFF;  // arpeggio table 63
    odd.push_back(h);
    for (const auto& v : odd) playOdd(v);
}

TEST(deltamusic_detect_is_strict) {
    // A MOD whose title starts with "ALL " must not pass as Delta Music 1.0
    std::vector<std::vector<ModCell>> pat(1, std::vector<ModCell>(64 * 4, ModCell{-1, 0, 0, 0}));
    auto mod = buildMod("M.K.", 4, {0}, pat, {ModSample{16, 0, 64, 0, 0, std::vector<int8_t>(32, 50)}});
    std::memcpy(mod.data(), "ALL YOU NEED", 12);
    CHECK(!AmigaSynthPlayer::detect(mod.data(), mod.size()));
    CHECK(AmigaSynthPlayer::open(mod.data(), mod.size()) == nullptr);
    std::vector<uint8_t> big(0x1000, 0);
    std::memcpy(big.data() + 0xbc6, ".FNL", 4);  // the mark alone, lengths that point nowhere
    for (size_t i = 0xfca; i < big.size(); ++i) big[i] = 0x7F;
    CHECK(!AmigaSynthPlayer::detect(big.data(), big.size()));
    CHECK(AmigaSynthPlayer::open(big.data(), big.size()) == nullptr);
}

TEST(deltamusic_real_modules_play) {
    const std::filesystem::path dir = dmFolder();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP deltamusic_real_modules_play (no Delta Music modules)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        CHECK(AmigaSynthPlayer::detect(d.data(), d.size()));
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), "DELTA MUSIC", 11) == 0);
        CHECK(p->seconds() > 10);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.05f);
        std::printf("  %s: %s, %.1f s, peak %.2f\n", e.path().filename().string().c_str(), p->format(), p->seconds(), pk);
        ++n;
    }
    CHECK(n > 0);
}
