// ZX Spectrum AY tracker modules played on ayumi: Sound Tracker / ST Song Compiler (.stc, .st13),
// Sound Tracker 1.1 uncompiled (.st11), Pro Tracker 2 (.pt2), ASC Sound Master (.asc), SQ-Tracker (.sqt).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/song.h"
#include "core/zx_trackers.h"
#include "test.h"

namespace {
using Kind = ZxTrackerPlayer::Kind;

float peakOf(ZxTrackerPlayer& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}

void put16(std::vector<uint8_t>& f, size_t at, size_t v) {
    if (f.size() < at + 2) f.resize(at + 2, 0);
    f[at] = (uint8_t)(v & 0xFF);
    f[at + 1] = (uint8_t)(v >> 8);
}
void append(std::vector<uint8_t>& f, std::initializer_list<uint8_t> b) { f.insert(f.end(), b.begin(), b.end()); }

double secondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// Every tiny module below: one position, one pattern of 64 rows at speed 3 (3.84 s); channel A plays one held
// note on a looping full-volume tone sample, B and C stay silent.

// ST Song Compiler: header, one sample, positions, ornament 0, pattern table, pattern data.
std::vector<uint8_t> tinyStc() {
    std::vector<uint8_t> f(27, 0);
    f[0] = 3;  // delay
    std::memcpy(f.data() + 7, "TINY TUNE         ", 18);
    f.push_back(1);  // sample 1: 32 lines of (amplitude 15, noise off, tone on)
    for (int i = 0; i < 32; ++i) append(f, {0x0F, 0x80, 0x00});
    append(f, {1, 31});  // loop from line 1, 31 lines
    const size_t pos = f.size();
    append(f, {0, 1, 0});  // one position: pattern 1, no transposition
    const size_t orn = f.size();
    f.resize(f.size() + 33, 0);  // ornament 0: no offsets
    const size_t pat = f.size();
    f.resize(f.size() + 7, 0);
    f[pat] = 1;
    f.push_back(0xFF);
    const size_t a = f.size();
    append(f, {0x61, 0x82, 0xE0, 0x30, 0xFF});  // sample 1, ornament 0, skip 63 rows, note C-4, end
    const size_t b = f.size();
    append(f, {0xE0, 0x80, 0xFF});  // skip 63 rows, rest
    const size_t c = f.size();
    append(f, {0xE0, 0x80, 0xFF});
    put16(f, 1, pos);
    put16(f, 3, orn);
    put16(f, 5, pat);
    put16(f, pat + 1, a);
    put16(f, pat + 3, b);
    put16(f, pat + 5, c);
    put16(f, 25, f.size());
    return f;
}

// Pro Tracker 2: header with sample / ornament / pattern pointers and the name, position list, pattern table.
std::vector<uint8_t> tinyPt2() {
    std::vector<uint8_t> f(131, 0);
    f[0] = 3;  // delay
    f[1] = 1;  // positions
    f[2] = 0;  // loop position
    std::memcpy(f.data() + 101, "TINY TUNE                     ", 30);
    append(f, {0, 0xFF});  // position list: pattern 0
    const size_t pat = f.size();
    f.resize(f.size() + 6, 0);
    append(f, {0xFF, 0xFF});
    const size_t a = f.size();
    append(f, {0xE1, 0x60, 0x1F, 0x5F, 0xB0, 0x00});  // sample 1, ornament 0, volume 15, skip 63, note C-4, end
    const size_t b = f.size();
    append(f, {0x5F, 0xE0, 0x00});  // skip 63, sound off
    const size_t c = f.size();
    append(f, {0x5F, 0xE0, 0x00});
    const size_t smp = f.size();
    append(f, {1, 0, 0x01, 0xF0, 0x00});  // length 1, loop 0: noise off, tone on, amplitude 15
    const size_t orn = f.size();
    append(f, {1, 0, 0});  // length 1, loop 0: +0
    put16(f, 99, pat);
    put16(f, pat, a);
    put16(f, pat + 2, b);
    put16(f, pat + 4, c);
    put16(f, 3 + 2 * 1, smp);
    put16(f, 67, orn);
    return f;
}

// ASC Sound Master (with a loop position): pattern, sample and ornament tables are relative to their own start.
std::vector<uint8_t> tinyAsc() {
    std::vector<uint8_t> f(9, 0);
    f[0] = 3;  // delay
    f[1] = 0;  // loop position
    f[8] = 1;  // positions
    f.push_back(0);  // position list: pattern 0
    const size_t pat = f.size();
    f.resize(f.size() + 6, 0);
    const size_t a = f.size();
    append(f, {0xA0, 0xC0, 0xEF, 0x9F, 0x30, 0xFF});  // sample 0, ornament 0, volume 15, skip 63, note, end
    const size_t b = f.size();
    append(f, {0x9F, 0x5F, 0xFF});  // skip 63, sound off
    const size_t c = f.size();
    append(f, {0x9F, 0x5F, 0xFF});
    const size_t smpTab = f.size();
    f.resize(f.size() + 0x40, 0);
    const size_t smp = f.size();
    append(f, {0xC0, 0x00, 0xF8});  // loop start + loop end on one line: amplitude 15, tone on, noise off
    const size_t ornTab = f.size();
    f.resize(f.size() + 0x40, 0);
    const size_t orn = f.size();
    append(f, {0xC0, 0x00});
    put16(f, 2, pat);
    put16(f, 4, smpTab);
    put16(f, 6, ornTab);
    put16(f, pat, a - pat);
    put16(f, pat + 2, b - pat);
    put16(f, pat + 4, c - pat);
    for (int i = 0; i < 32; ++i) {
        put16(f, smpTab + 2 * i, smp - smpTab);
        put16(f, ornTab + 2 * i, orn - ornTab);
    }
    return f;
}

// SQ-Tracker (compiled at address 0): header pointers, one sample, one ornament, patterns 1 (A) and 2 (B, C).
std::vector<uint8_t> tinySqt() {
    std::vector<uint8_t> f(12, 0);
    put16(f, 2, 10);  // samples: the table's entry 1 is at 12
    put16(f, 4, 12);  // ornaments: entry 1 at 14
    put16(f, 6, 14);  // patterns: entries 1, 2 at 16, 18
    f.resize(20, 0);
    const size_t smp = f.size();
    append(f, {0, 32});  // loop from line 0, 32 lines
    for (int i = 0; i < 32; ++i) append(f, {0x0F, 0x40, 0x00});  // amplitude 15, tone on, noise off
    const size_t orn = f.size();
    append(f, {0, 32});
    f.resize(f.size() + 32, 0);
    const size_t p1 = f.size();
    append(f, {64, 0x30, 0x82, 0xAF, 0xAF, 0xAF, 0xAE});  // 64 lines: note + sample 1, then 63 empty lines
    const size_t p2 = f.size();
    append(f, {64, 0x6F, 0xAF, 0xAF, 0xAF, 0xAE});  // channel off
    const size_t pos = f.size();
    append(f, {2, 0x00, 2, 0x00, 1, 0x00, 3, 0});  // C: pattern 2, B: pattern 2, A: pattern 1, delay 3; end
    put16(f, 0, f.size());
    put16(f, 8, pos);
    put16(f, 10, pos);  // loop
    put16(f, 12, smp);
    put16(f, 14, orn);
    put16(f, 16, p1);
    put16(f, 18, p2);
    return f;
}

void checkTiny(const std::vector<uint8_t>& f, Kind kind, const char* title) {
    CHECK(ZxTrackerPlayer::identify(f.data(), f.size()) == kind);
    CHECK(ZxTrackerPlayer::detect(f.data(), f.size()));
    ZxTrackerPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK(p.kind() == kind);
    CHECK_NEAR(p.seconds(), 3.84, 0.05);
    if (title) CHECK(std::strcmp(p.title(), title) == 0);
    CHECK(*p.format() != 0);
    CHECK(p.voices() == 3);
    CHECK(peakOf(p, 0.5) > 0.05f);
    CHECK(p.vu(0) > 0.1f);   // channel A sounds
    CHECK(p.vu(1) < 0.01f);  // B and C stay silent
    CHECK(p.vu(2) < 0.01f);
    float sc[256];
    p.scope(0, sc, 256);
    Song s;
    CHECK(p.buildDisplay(s));
    p.restart();
    CHECK(peakOf(p, 0.2) > 0.05f);
    p.clearMeters();
    CHECK(p.vu(0) == 0.f);
}

// Truncated and randomly damaged copies: refused, or played without reading outside the data, in bounded time.
void checkBroken(const std::vector<uint8_t>& good, size_t protect) {
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t cut = 0; cut < good.size(); ++cut) {
        ZxTrackerPlayer p;
        p.setRate(22050);
        if (p.load(good.data(), cut)) peakOf(p, 0.1);
    }
    uint32_t seed = 4242;
    for (int k = 0; k < 300; ++k) {
        auto f = good;
        for (int n = 0; n < 6; ++n) {
            seed = seed * 1103515245u + 12345u;
            const size_t at = protect + (seed >> 8) % (f.size() - protect);
            seed = seed * 1103515245u + 12345u;
            f[at] = (uint8_t)(seed >> 16);
        }
        ZxTrackerPlayer p;
        p.setRate(22050);
        if (p.load(f.data(), f.size())) peakOf(p, 0.1);
    }
    CHECK(secondsSince(t0) < 20.0);
}

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// What a file name says it is (Modland extensions), None for anything else.
Kind kindByName(const std::filesystem::path& p) {
    std::string x = p.extension().string();
    for (char& c : x) c = (char)tolower((unsigned char)c);
    if (x == ".stc" || x == ".st13") return Kind::Stc;
    if (x == ".st11") return Kind::St1;
    if (x == ".pt2") return Kind::Pt2;
    if (x == ".asc") return Kind::Asc;
    if (x == ".sqt") return Kind::Sqt;
    return Kind::None;
}

// the Windows checkout's modules when they exist (worktrees have none), else <source>/modules
const std::filesystem::path kModules = std::filesystem::exists("C:\\development\\FatTracker\\modules")
                                           ? std::filesystem::path("C:\\development\\FatTracker\\modules")
                                           : std::filesystem::path(FT_SOURCE_DIR) / "modules";
}  // namespace

TEST(zx_stc_tiny_module_plays_channel_a) { checkTiny(tinyStc(), Kind::Stc, "TINY TUNE"); }
TEST(zx_pt2_tiny_module_plays_channel_a) { checkTiny(tinyPt2(), Kind::Pt2, "TINY TUNE"); }
TEST(zx_asc_tiny_module_plays_channel_a) { checkTiny(tinyAsc(), Kind::Asc, nullptr); }
TEST(zx_sqt_tiny_module_plays_channel_a) { checkTiny(tinySqt(), Kind::Sqt, nullptr); }

TEST(zx_st1_container_converted_and_played) {
    // Sound Tracker 1.1 source module in Ay_Emul's ZXAYST11 wrapper: sample 1 holds a tone, pattern 1 plays it
    // on channel A for 64 rows at speed 3.
    std::vector<uint8_t> f(0x38, 0);
    std::memcpy(f.data(), "ZXAYST11", 8);
    f[0x13] = 2;  // songs structure at 0x14
    f[0x15] = 4;  // song name at 0x18
    f[0x17] = 0x1A;  // song data at 0x30
    std::memcpy(f.data() + 0x18, "TINY TUNE", 10);
    f[0x31] = 1, f[0x32] = 2, f[0x33] = 3, f[0x37] = 0x32;  // song data: channel map, fade; the module follows
    const size_t base = f.size();
    f.resize(base + 3009 + 576, 0);
    uint8_t* s = f.data() + base;  // sample 1
    for (int i = 0; i < 32; ++i) s[i] = 15, s[32 + i] = 0x80;  // amplitude 15, noise off, tone on
    s[128] = 1, s[129] = 31;
    for (int i = 0; i < 256; ++i) f[base + 1950 + 2 * i] = 1;  // every position: pattern 1
    f[base + 2462] = 0;    // one position
    f[base + 3007] = 3;    // tempo
    f[base + 3008] = 64;   // rows
    uint8_t* row0 = f.data() + base + 3009;
    row0[0] = 0x30 | 3;  // C-3 (halftone index 6 = C, octave 3)
    row0[1] = 0x1F;      // sample 1, effect 15: ornament 0
    row0[2] = 0x00;
    CHECK(ZxTrackerPlayer::identify(f.data(), f.size()) == Kind::St1);
    ZxTrackerPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_NEAR(p.seconds(), 3.84, 0.05);
    CHECK(std::strcmp(p.title(), "TINY TUNE") == 0);
    CHECK(peakOf(p, 0.5) > 0.05f);
    CHECK(p.vu(0) > 0.1f);
    CHECK(p.vu(1) < 0.01f);
    CHECK(p.vu(2) < 0.01f);
    checkBroken(f, 0);
}

TEST(zx_trackers_reject_other_data) {
    std::vector<uint8_t> junk(2048, 0);
    std::memcpy(junk.data() + 1080, "M.K.", 4);
    CHECK(!ZxTrackerPlayer::detect(junk.data(), junk.size()));
    std::vector<uint8_t> ff(4096, 0xFF);
    CHECK(!ZxTrackerPlayer::detect(ff.data(), ff.size()));
    CHECK(!ZxTrackerPlayer::detect(nullptr, 0));
    ZxTrackerPlayer p;
    CHECK(!p.load(junk.data(), junk.size()));
    CHECK(!p.load(nullptr, 0));
    CHECK(p.seconds() == 0.0);
    CHECK(*p.format() == 0);
    uint32_t seed = 99;  // random data: never detected
    int hits = 0;
    for (int k = 0; k < 2000; ++k) {
        std::vector<uint8_t> r(512 + k % 3000);
        for (auto& b : r) {
            seed = seed * 1103515245u + 12345u;
            b = (uint8_t)(seed >> 16);
        }
        hits += ZxTrackerPlayer::detect(r.data(), r.size());
    }
    CHECK_EQ(hits, 0);
}

TEST(zx_trackers_broken_modules_refused_or_safe) {
    checkBroken(tinyStc(), 0);
    checkBroken(tinyPt2(), 0);
    checkBroken(tinyAsc(), 0);
    checkBroken(tinySqt(), 0);
    {  // STC position naming a pattern that is not in the table: refused
        auto f = tinyStc();
        const size_t pos = f[1] | f[2] << 8;
        f[pos + 1] = 7;
        ZxTrackerPlayer p;
        CHECK(!p.load(f.data(), f.size()));
    }
    {  // PT2 pattern pointer outside the file: refused
        auto f = tinyPt2();
        const size_t pat = f[99] | f[100] << 8;
        put16(f, pat + 2, 0xF000);
        ZxTrackerPlayer p;
        CHECK(!p.load(f.data(), f.size()));
    }
    {  // patterns that never end a row: capped
        for (auto make : {tinyStc, tinyPt2, tinyAsc, tinySqt}) {
            auto f = make();
            for (size_t i = f.size() * 2 / 3; i < f.size(); ++i) f[i] = 0x60;
            ZxTrackerPlayer p;
            p.setRate(22050);
            if (p.load(f.data(), f.size())) peakOf(p, 0.3);
        }
    }
}

TEST(zx_trackers_keep_their_own_data) {
    for (auto make : {tinyStc, tinyPt2, tinyAsc, tinySqt}) {
        auto f = make();
        ZxTrackerPlayer ref, p;
        ref.setRate(22050), p.setRate(22050);
        CHECK(ref.load(f.data(), f.size()));
        CHECK(p.load(f.data(), f.size()));
        for (auto& b : f) b = 0x5A;
        std::vector<float> a(2205 * 2), b(2205 * 2);
        bool same = true;
        for (int i = 0; i < 10 && same; ++i) {
            ref.render(a.data(), 2205, 0.45f);
            p.render(b.data(), 2205, 0.45f);
            same = a == b;
        }
        CHECK(same);
    }
}

TEST(zx_trackers_real_files_play) {
    std::error_code ec;
    if (!std::filesystem::exists(kModules, ec)) {
        std::printf("SKIP zx_trackers_real_files_play (no modules folder)\n");
        return;
    }
    int n = 0, perKind[6] = {};
    for (const char* sub : {"Draconus", "Zybex", "Warhawk", "Spectrum"}) {
        const std::filesystem::path dir = std::filesystem::path(kModules) / sub;
        if (!std::filesystem::exists(dir, ec)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const Kind want = kindByName(e.path());
            if (!e.is_regular_file() || want == Kind::None) continue;
            const auto d = readAll(e.path());
            const std::string name = e.path().filename().string();
            const Kind got = ZxTrackerPlayer::identify(d.data(), d.size());
            CHECK(got == want);
            ZxTrackerPlayer p;
            p.setRate(48000);
            const bool ok = p.load(d.data(), d.size());
            CHECK(ok);
            if (!ok || got != want) {
                std::printf("  FAIL %s (identified as %d)\n", name.c_str(), (int)got);
                continue;
            }
            CHECK(p.seconds() > 5.0);
            const float pk = peakOf(p, 3.0);
            CHECK(pk > 0.02f);
            std::printf("  %s: %s \"%s\" by \"%s\", %.1f s, peak %.2f\n", name.c_str(), p.format(), p.title(),
                        p.author(), p.seconds(), pk);
            ++n;
            ++perKind[(int)want];
            for (size_t cut = 0; cut < d.size(); cut += d.size() / 9 + 1) {  // truncated: refused or played
                ZxTrackerPlayer q;
                q.setRate(22050);
                if (q.load(d.data(), cut)) peakOf(q, 0.1);
            }
        }
    }
    std::printf("  zx trackers: %d files (stc %d, st1 %d, pt2 %d, asc %d, sqt %d)\n", n, perKind[1], perKind[2],
                perKind[3], perKind[4], perKind[5]);
}

TEST(zx_trackers_detect_nothing_else) {
    // every other file of the collection (MOD, XM, S3M, IT, SID, PT3, YM, AY, SNDH, Amiga formats ...) is left alone
    std::error_code ec;
    if (!std::filesystem::exists(kModules, ec)) {
        std::printf("SKIP zx_trackers_detect_nothing_else (no modules folder)\n");
        return;
    }
    int scanned = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(kModules, ec)) {
        if (!e.is_regular_file() || e.file_size() > (4u << 20)) continue;
        const auto d = readAll(e.path());
        const Kind got = ZxTrackerPlayer::identify(d.data(), d.size());
        const Kind want = kindByName(e.path());
        ++scanned;
        if (got != want) std::printf("  WRONG %s: %d, expected %d\n", e.path().string().c_str(), (int)got, (int)want);
        CHECK(got == want);
    }
    std::printf("  scanned %d files\n", scanned);
}
