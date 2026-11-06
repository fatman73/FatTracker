// Art Of Noise 4 / 8 voices (ported from NostalgicPlayer): hand-built songs, broken files, the real modules,
// and a scan that no other module in the collection passes as one.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "build_mod.h"
#include "core/amiga_formats.h"
#include "core/amiga_synth.h"
#include "core/ay_player.h"
#include "core/gme_player.h"
#include "core/pt3_player.h"
#include "core/sid_player.h"
#include "core/sndh_player.h"
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
        if (vu && i == 3)
            for (int c = 0; c < 8; ++c) vu[c] = p.vu(c);
    }
    return peak;
}
void be16(std::vector<uint8_t>& f, int v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void be32(std::vector<uint8_t>& f, uint32_t v) { be16(f, (int)(v >> 16)), be16(f, (int)(v & 0xFFFF)); }
void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) {
    f[at] = (uint8_t)(v >> 24), f[at + 1] = (uint8_t)(v >> 16), f[at + 2] = (uint8_t)(v >> 8), f[at + 3] = (uint8_t)v;
}
void chunk(std::vector<uint8_t>& f, const char* name, const std::vector<uint8_t>& data) {
    f.insert(f.end(), name, name + 4);
    be32(f, (uint32_t)data.size());
    f.insert(f.end(), data.begin(), data.end());
}

// The chunks of a tiny song. One position playing pattern 0, whose row 0 plays note 25 with instrument 1 on
// voice 0 (row `cell` of the pattern: the 4 bytes of a voice). Instrument 1 is a looped 32 byte square sample
// or a synth sound over the first 16 words of wave 0; 64 rows at speed 6 and 50 Hz.
struct Aon {
    bool eight = false;
    std::vector<uint8_t> info = {0, 1, 0, 0}, arpg = std::vector<uint8_t>(64, 0), plst = {0}, patt, inst, wlen, wave;
};
Aon tinyAon(bool eight, bool synth) {
    Aon a;
    a.eight = eight;
    const int ch = eight ? 8 : 4;
    a.patt.assign((size_t)4 * ch * 64, 0);
    a.patt[0] = 25, a.patt[1] = 1;
    std::vector<uint8_t> in(32, 0);
    in[0] = synth ? 1 : 0, in[1] = 64;  // type, volume
    if (synth) {
        in[4] = 16;                                        // length (words)
        in[14] = 1, in[15] = 0, in[16] = 1, in[17] = 0;    // wave length 1, loop start 0, loop length 1, normal
    } else {
        in[11] = 16, in[19] = 16;  // length 16 words, loop start 0, loop length 16 words
    }
    a.inst = in;
    be32(a.wlen, 32);
    for (int i = 0; i < 32; ++i) a.wave.push_back(i < 16 ? 100 : (uint8_t)-100);
    return a;
}
std::vector<uint8_t> aonFile(const Aon& a) {
    std::vector<uint8_t> f = {'A', 'O', 'N', (uint8_t)(a.eight ? '8' : '4')};
    const char* text = "artofnoise by bastian spiegel (twice/lego)";
    f.insert(f.end(), text, text + 42);
    chunk(f, "NAME", {'t', 'i', 'n', 'y'});
    chunk(f, "AUTH", {'m', 'e'});
    chunk(f, "INFO", a.info);
    chunk(f, "ARPG", a.arpg);
    chunk(f, "PLST", a.plst);
    chunk(f, "PATT", a.patt);
    chunk(f, "INST", a.inst);
    chunk(f, "WLEN", a.wlen);
    chunk(f, "WAVE", a.wave);
    return f;
}

void checkTinyPlays(bool eight, bool synth) {
    const auto f = aonFile(tinyAon(eight, synth));
    CHECK(isArtOfNoise(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "ART OF NOISE", 12) == 0);
    CHECK_EQ(p->voices(), eight ? 8 : 4);
    CHECK(p->seconds() > 7.0 && p->seconds() < 8.5);  // 64 rows of 6 ticks
    float vu[8] = {};
    CHECK(peakOf(*p, 0.6, vu) > 0.05f);
    CHECK(vu[0] > 0.1f);
    for (int c = 1; c < 8; ++c) CHECK(vu[c] < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

void playOdd(const std::vector<uint8_t>& f) {
    const auto t0 = std::chrono::steady_clock::now();
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    if (p) {
        p->setRate(48000);
        peakOf(*p, 0.3);
        CHECK(p->seconds() >= 0 && p->seconds() <= 30 * 60 + 1);
    }
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0);
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
bool claimedFirst(const std::vector<uint8_t>& d) {
    return TfmxPlayer::detect(d.data(), d.size()) || SidPlayer::detect(d.data(), d.size()) ||
           SndhPlayer::detect(d.data(), d.size()) || Pt3Player::detect(d.data(), d.size()) ||
           GmePlayer::detect(d.data(), d.size()) || AyPlayer::detect(d.data(), d.size());
}
}  // namespace

TEST(artofnoise_tiny_sample_song_plays) { checkTinyPlays(false, false); }
TEST(artofnoise_tiny_synth_song_plays) { checkTinyPlays(false, true); }
TEST(artofnoise_8_voices_tiny_song_plays) {
    checkTinyPlays(true, false);
    checkTinyPlays(true, true);
}

TEST(artofnoise_8_voices_last_voice_plays_alone) {
    Aon a = tinyAon(true, false);
    a.patt[0] = a.patt[1] = 0;
    a.patt[4 * 7] = 25, a.patt[4 * 7 + 1] = 1;  // voice 8
    const auto f = aonFile(a);
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    float vu[8] = {};
    CHECK(peakOf(*p, 0.6, vu) > 0.05f);
    CHECK(vu[7] > 0.1f);
    for (int c = 0; c < 7; ++c) CHECK(vu[c] < 0.01f);
}

TEST(artofnoise_broken_files) {
    for (int eight = 0; eight < 2; ++eight)
        for (int synth = 0; synth < 2; ++synth) {
            const auto f = aonFile(tinyAon(eight != 0, synth != 0));
            for (size_t cut = 0; cut < f.size(); cut += 7) {
                const auto t0 = std::chrono::steady_clock::now();
                auto p = AmigaSynthPlayer::open(f.data(), cut);  // refused, or (cut between chunks) safe
                if (p) peakOf(*p, 0.1);
                CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 2.0);
            }
            CHECK(!isArtOfNoise(f.data(), 60));
        }
    const auto base = aonFile(tinyAon(false, false));
    const size_t info = 46 + 12 + 10;  // after NAME and AUTH
    auto g = base;
    put32(g, info + 4, 0x7FFFFFFF);  // a chunk past the end
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = base;
    put32(g, info + 4, 0xFFFFFFF0u);  // a negative chunk size
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = base;
    put32(g, info + 4, 0);  // INFO too short: reads into the next chunk, then goes on after INFO
    playOdd(g);
    // WLEN asking for 4 GB of waves is refused, not allocated
    Aon a = tinyAon(false, false);
    a.wlen.clear(), be32(a.wlen, 0xFFFFFFF0u);
    g = aonFile(a);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 1.0);
    a = tinyAon(false, false);
    a.inst[0] = 2;  // instrument type 2
    g = aonFile(a);
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    a = tinyAon(false, false);
    a.info[1] = 0;  // no positions
    g = aonFile(a);
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    a = tinyAon(false, false);
    a.plst.clear();  // an empty position list
    g = aonFile(a);
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
}

TEST(artofnoise_odd_files_play_safely) {
    std::vector<std::vector<uint8_t>> odd;
    for (int eight = 0; eight < 2; ++eight)
        for (int synth = 0; synth < 2; ++synth) {
            const bool e = eight != 0, s = synth != 0;
            const int rowBytes = 4 * (e ? 8 : 4);
            Aon a = tinyAon(e, s);
            a.plst = {9, 0, 7}, a.info[1] = 3, a.info[2] = 2;  // patterns 9 and 7 of 1
            odd.push_back(aonFile(a));
            a = tinyAon(e, s);
            a.patt[0] = 63, a.patt[1] = 63;  // note 63, instrument 63
            odd.push_back(aonFile(a));
            a = tinyAon(e, s);
            a.inst[2] = 0xFF, a.inst[3] = 200;  // fine tune 255, wave 200
            odd.push_back(aonFile(a));
            a = tinyAon(e, s);
            for (auto& b : a.arpg) b = 0xFF;  // arpeggios of 15 notes (the table holds 7)
            a.patt[1] = 0xC1, a.patt[2] = 0xC0;
            odd.push_back(aonFile(a));
            a = tinyAon(e, s);
            if (s) a.inst[4] = 0xFF, a.inst[14] = 0xFF, a.inst[15] = 0xF0, a.inst[16] = 0xFF, a.inst[17] = 2;
            else put32(a.inst, 4, 0x7FFFFFFF), put32(a.inst, 8, 0xFFFF), put32(a.inst, 12, 0xFFFFFF), put32(a.inst, 16, 0xFFFF);
            odd.push_back(aonFile(a));
            if (s) {  // wave tables walking backwards and ping-pong, fast and frozen
                for (int ctrl = 0; ctrl < 4; ++ctrl) {
                    a = tinyAon(e, s);
                    a.inst[13] = 1, a.inst[14] = 3, a.inst[15] = 1, a.inst[16] = 2, a.inst[17] = (uint8_t)ctrl;
                    a.inst[10] = 0x34, a.inst[11] = 2, a.inst[12] = 1;  // vibrato
                    a.inst[28] = 10, a.inst[29] = 20, a.inst[30] = 30, a.inst[31] = 5;  // envelope
                    a.patt[rowBytes * 2 + 2] = 30, a.patt[rowBytes * 2 + 3] = (uint8_t)(ctrl * 0x11);  // U: wave mode
                    a.patt[rowBytes * 3 + 0] = 30, a.patt[rowBytes * 3 + 2] = 17, a.patt[rowBytes * 3 + 3] = 0x11;  // H
                    odd.push_back(aonFile(a));
                }
            }
            // every effect with odd arguments on rows 0 (with the note), 1 and 63
            for (int fx = 0; fx < 64; ++fx)
                for (int arg : {0, 0x01, 0x0F, 0x10, 0x5A, 0xE6, 0xFF}) {
                    a = tinyAon(e, s);
                    a.patt[2] = (uint8_t)fx, a.patt[3] = (uint8_t)arg;
                    a.patt[rowBytes + 2] = (uint8_t)fx, a.patt[rowBytes + 3] = (uint8_t)arg;
                    a.patt[rowBytes * 63 + 2] = (uint8_t)fx, a.patt[rowBytes * 63 + 3] = (uint8_t)arg;
                    a.patt[rowBytes * 63] = 13;  // a note without instrument
                    odd.push_back(aonFile(a));
                }
            // extra effects E0x - EFx
            for (int ex = 0; ex < 16; ++ex) {
                a = tinyAon(e, s);
                a.patt[2] = 14, a.patt[3] = (uint8_t)(ex << 4 | 2);
                a.patt[rowBytes * 4 + 2] = 14, a.patt[rowBytes * 4 + 3] = (uint8_t)(ex << 4);  // E50 / E60 at row 4
                a.patt[rowBytes * 5 + 2] = 14, a.patt[rowBytes * 5 + 3] = (uint8_t)(ex << 4 | 3);
                odd.push_back(aonFile(a));
            }
        }
    for (const auto& f : odd) playOdd(f);
}

TEST(artofnoise_detect_is_strict) {
    std::vector<std::vector<ModCell>> pat(1, std::vector<ModCell>(64 * 4, ModCell{-1, 0, 0, 0}));
    for (const char* name : {"AON4artofnoise", "AON8", "AON4\0\0\0\0INFO"}) {
        auto mod = buildMod("M.K.", 4, {0}, pat, {ModSample{16, 0, 64, 0, 0, std::vector<int8_t>(32, 50)}});
        std::memcpy(mod.data(), name, 4);
        CHECK(!isArtOfNoise(mod.data(), mod.size()));
        auto p = AmigaSynthPlayer::open(mod.data(), mod.size());
        CHECK(p == nullptr || std::strncmp(p->format(), "ART OF NOISE", 12) != 0);
    }
    std::vector<uint8_t> big(4096, 0);
    std::memcpy(big.data(), "AON4", 4);
    CHECK(!isArtOfNoise(big.data(), big.size()));
}

TEST(artofnoise_real_modules_play) {
    const std::filesystem::path root = modulesRoot();
    std::vector<std::filesystem::path> files = {root / "Draconus" / "Amiga ArtOfNoise - Twice - draconus.aon"};
    if (std::filesystem::exists(root / "ArtOfNoise"))
        for (const auto& e : std::filesystem::directory_iterator(root / "ArtOfNoise"))
            if (e.is_regular_file()) files.push_back(e.path());
    int n = 0;
    for (const auto& path : files) {
        if (!std::filesystem::exists(path)) continue;
        const auto d = readAll(path);
        CHECK(isArtOfNoise(d.data(), d.size()));
        CHECK(AmigaSynthPlayer::detect(d.data(), d.size()));
        CHECK(!claimedFirst(d));
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s\n", path.filename().string().c_str());
            continue;
        }
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), "ART OF NOISE", 12) == 0);
        CHECK(p->seconds() > 5);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.02f);
        std::printf("  %s: %s, %d voices, %.1f s, peak %.2f\n", path.filename().string().c_str(), p->format(),
                    p->voices(), p->seconds(), pk);
        ++n;
    }
    if (n == 0) std::printf("SKIP artofnoise_real_modules_play (no Art Of Noise modules)\n");
}

TEST(artofnoise_no_false_positives) {
    const std::filesystem::path root = modulesRoot();
    if (!std::filesystem::exists(root)) {
        std::printf("SKIP artofnoise_no_false_positives (no modules folder)\n");
        return;
    }
    int scanned = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || e.file_size() > (16u << 20)) continue;
        const std::string name = e.path().filename().string();
        if (e.path().parent_path().filename() == "ArtOfNoise" || name.rfind("Amiga ArtOfNoise", 0) == 0) continue;
        const auto d = readAll(e.path());
        const bool aon = isArtOfNoise(d.data(), d.size());
        if (aon) std::printf("  FALSE POSITIVE %s\n", e.path().string().c_str());
        CHECK(!aon);
        ++scanned;
    }
    CHECK(scanned > 0);
}
