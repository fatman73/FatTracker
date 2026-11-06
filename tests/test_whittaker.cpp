// David Whittaker (ported from NostalgicPlayer): playback only.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <source_location>
#include <string>
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

// A player-and-data file. The loader finds its tables by looking for the replay routine's instructions,
// so the tiny file carries just those instructions (no real code). The base (startOffset) is 0.
constexpr size_t kSubSongs = 0x200, kPosList0 = 0x210, kPosList1 = 0x214, kTrack0 = 0x240, kTrack1 = 0x250,
                 kSampleInfo = 0x280, kSampleData = 0x800, kPeriodTable = 0xE0, kJumpTable = 0x100;

std::vector<uint8_t> tinyDw() {
    std::vector<uint8_t> f(kSampleData, 0);
    // init: lea base(pc),a3 / bsr / lea subsongs(pc),a0 + move.b (a0,d0),d1 / lea x(pc),a0 + move.w (a0,d0),d0
    // (16 bit offsets) / tst.b + bne / lea sampledata(pc),a0 / lea sampleinfo(pc),a5 + moveq #0,d1 (1 sample)
    putBytes(f, 0x10, {0x47, 0xFA, 0xFF, 0xEE});
    putBytes(f, 0x14, {0x61, 0x00, 0x00, 0x00});
    putBytes(f, 0x18, {0x41, 0xFA});
    put16(f, 0x1A, (int)(kSubSongs - 0x1A));
    putBytes(f, 0x1C, {0x12, 0x30});
    putBytes(f, 0x1E, {0x41, 0xFA, 0x00, 0x00, 0x30, 0x70});
    putBytes(f, 0x24, {0x4A, 0x2B, 0x00, 0x00, 0x66, 0x02, 0x41, 0xFA});
    put16(f, 0x2C, (int)(kSampleData - 0x2C));
    putBytes(f, 0x2E, {0x4B, 0xFA});
    put16(f, 0x30, (int)(kSampleInfo - 0x30));
    putBytes(f, 0x32, {0x72, 0x00});
    // play: lea base(pc),a3 / tst.b + beq / moveq #3,d7 (4 channels) / subq.w #1,x(a0) + beq ReadTrack + bne Frame
    putBytes(f, 0x40, {0x47, 0xFA, 0x00, 0x00, 0x4A, 0x2B, 0x00, 0x00, 0x67, 0x02, 0x4E, 0x71, 0x7E, 0x03});
    putBytes(f, 0x4E, {0x53, 0x68, 0x00, 0x00, 0x67, 0x0C, 0, 0, 0, 0, 0, 0, 0x66, 0x24});
    // ReadTrack (0x60): bmi.w Commands / lea periods(pc),a2 + move.w x(a5),d1
    putBytes(f, 0x60, {0x6B, 0x00, 0x00, 0x3E, 0x45, 0xFA});
    put16(f, 0x66, (int)(kPeriodTable - 0x66));
    putBytes(f, 0x68, {0x32, 0x2D});
    // Commands (0xA0): cmp.b #$C0,d0 + bcs / lea samples(pc),a5 + mulu (new sample command $C0) /
    // lea jumptable(pc),a2 / jmp (a2)
    putBytes(f, 0xA0, {0xB0, 0x3C, 0x00, 0xC0, 0x65, 0x08, 0, 0, 0, 0, 0x4B, 0xFA, 0x00, 0x00, 0xC0, 0xFC, 0x45, 0xFA});
    put16(f, 0xB2, (int)(kJumpTable - 0xB2));
    putBytes(f, 0xB8, {0x4E, 0xD2});
    putBytes(f, kPeriodTable, {0x20, 0x00});  // version with 8192 at the start
    // sub-song 1: speed 6, four position lists (channel 0 plays track 0, the others the silent track 1)
    put16(f, kSubSongs, 6);
    put16(f, kSubSongs + 2, (int)kPosList0);
    for (int i = 1; i < 4; ++i) put16(f, kSubSongs + 2 + 2 * i, (int)kPosList1);
    put16(f, kPosList0, (int)kTrack0);
    put16(f, kPosList1, (int)kTrack1);
    putBytes(f, kTrack0, {0xC0, 0xFF, 0x24, 0x80});  // sample 0, wait 32 rows, note 36 (period 1024), end
    putBytes(f, kTrack1, {0xFF, 0x83, 0x80});        // wait 32 rows, wait, end
    // sample info: loop from 0, 16 words, volume 64
    put32(f, kSampleInfo + 4, 0);
    put16(f, kSampleInfo + 8, 16);
    put16(f, kSampleInfo + 12, 64);
    // sample data: 32 bytes at 8363 Hz (period 428 for note 36)
    put32(f, kSampleData, 32);
    put16(f, kSampleData + 4, 8363);
    for (int i = 0; i < 32; ++i) f.push_back((uint8_t)(i < 16 ? 100 : -100));
    return f;
}

std::filesystem::path modulesDir() {
    std::filesystem::path p = "C:/development/FatTracker/modules";
    if (!std::filesystem::exists(p)) p = std::filesystem::path(FT_SOURCE_DIR) / "modules";
    return p;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// A broken file must be refused or play without crashing (or hanging).
void survives(const std::vector<uint8_t>& f) {
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    if (p) {
        p->setRate(48000);
        peakOf(*p, 0.5);
        p->restart();
        peakOf(*p, 0.2);
    }
}
// Bad values in the song: the file loads, and plays without crashing.
void playsAnyway(const std::vector<uint8_t>& f, std::source_location at = std::source_location::current()) {
    const bool loads = AmigaSynthPlayer::open(f.data(), f.size()) != nullptr;
    CHECK(loads);
    if (!loads) std::printf("  refused: the variant at line %u\n", (unsigned)at.line());
    survives(f);
}
}  // namespace

TEST(whittaker_tiny_song_plays) {
    const auto f = tinyDw();
    CHECK(detectWhittaker(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "DAVID WHITTAKER", 15) == 0);
    CHECK(p->seconds() > 3.0 && p->seconds() < 5.0);  // 32 rows of 6 ticks
    float vu0 = 0, other = 0;
    CHECK(peakOf(*p, 0.8, &vu0, &other) > 0.05f);
    CHECK(vu0 > 0.1f);
    CHECK(other < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

TEST(whittaker_other_tags_never_match) {
    for (const char* tag : {"PSID", "RSID", "SC68"}) {
        auto f = tinyDw();
        std::memcpy(f.data(), tag, 4);
        CHECK(!detectWhittaker(f.data(), f.size()));
        CHECK(!AmigaSynthPlayer::detect(f.data(), f.size()));
        CHECK(AmigaSynthPlayer::open(f.data(), f.size()) == nullptr);
    }
    std::vector<uint8_t> zero(4096, 0);
    CHECK(!detectWhittaker(zero.data(), zero.size()));
    auto g = tinyDw();
    g[0x4C] = 0x4E;  // no channel count in the play routine
    CHECK(!detectWhittaker(g.data(), g.size()));
    g = tinyDw();
    g[kPeriodTable] = 0x30;  // unknown period table
    CHECK(!detectWhittaker(g.data(), g.size()));
}

TEST(whittaker_broken_files) {
    const auto f = tinyDw();
    for (size_t cut = 8; cut < f.size(); cut += 29) CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
    auto g = tinyDw();
    put32(g, kSampleData, 0xFFFFFFF0);  // sample of 4 GB
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 1.0);
    g = tinyDw();
    put16(g, kSampleData + 4, 0);  // sample frequency 0 (a division by zero in the C# code)
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyDw();
    g[kTrack0 + 3] = 0x8F;  // unknown effect instead of the end of the track
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyDw();
    for (size_t i = kTrack0; i < kSampleInfo; ++i) g[i] = 0x24;  // a track that runs into the sample info ...
    for (size_t i = kSampleInfo; i < g.size(); ++i) g[i] = 0x10;  // ... and never ends
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyDw();
    put16(g, kSubSongs + 2, 0);  // no position list
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyDw();
    g = tinyDw();
    put16(g, 0x1A, 0x7000);  // sub-song list outside the file
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = tinyDw();
    put16(g, 0x1A, 0x8000);  // sub-song list before the file
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    // bad values in the song: must still play
    g = tinyDw();
    g[kTrack0] = 0xC7;  // sample 7 of 1
    playsAnyway(g);
    g = tinyDw();
    g[kTrack0 + 2] = 0x7F;  // note past the period table
    playsAnyway(g);
    g = tinyDw();
    putBytes(g, kTrack0, {0xC0, 0x80});  // no note and no wait in any track (the C# code hangs)
    putBytes(g, kTrack1, {0x80});
    playsAnyway(g);
    g = tinyDw();
    put16(g, kPosList1, 0x7FFF);  // empty position lists (the C# code hangs on its empty track)
    playsAnyway(g);
    g = tinyDw();
    put16(g, kSubSongs + 2, 0x7000);  // position list outside the file: then the sub-song list runs on
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);  // into a sub-song without position lists
    g = tinyDw();
    putBytes(g, kTrack0, {0xC0, 0xFF, 0x24, 0x89, 0x7F, 0xFF, 0x80});  // restart position past the list
    playsAnyway(g);
    g = tinyDw();
    putBytes(g, kTrack0, {0xC0, 0xE0, 0x88, 0x24, 0x24, 0x81, 0x80, 0x10, 0x85, 0x40, 0x24, 0x80});  // effects
    playsAnyway(g);
    g = tinyDw();
    put32(g, kSampleInfo + 4, 0x7FF0);  // loop past the sample
    playsAnyway(g);
    g = tinyDw();
    put16(g, kSubSongs, 0);  // speed 0
    playsAnyway(g);
}

// The command thresholds come from the file's cmp.b operands. One at or below $80 would make "end of track"
// (and the empty track of a channel without tracks) a sample command: no track could end, and a channel
// without tracks read forever in one tick. Such a file is refused, quickly.
TEST(whittaker_low_command_threshold_does_not_hang) {
    for (int threshold : {0x80, 0x00, 0x7F}) {
        auto g = tinyDw();
        g[0xA3] = (uint8_t)threshold;  // cmp.b #threshold,d0: the first sample command
        put16(g, kPosList0, 0x8000);    // no tracks in any position list
        put16(g, kPosList1, 0x8000);
        const auto t0 = std::chrono::steady_clock::now();
        auto p = AmigaSynthPlayer::open(g.data(), g.size());
        if (p) {
            p->setRate(48000);
            peakOf(*p, 0.3);
        }
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(p == nullptr);
        CHECK(took < 1.0);
    }
    auto g = tinyDw();
    g[0xA3] = 0x81;  // just above: still a David Whittaker module
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) != nullptr);
}

// A long position list of one long track full of waits and without a note: one replay call must not read
// all of it (about 120 million track bytes), so loading (which measures the song) stays quick.
TEST(whittaker_noteless_tracks_bounded_per_tick) {
    auto g = tinyDw();
    constexpr size_t kBigTrack = 0x900, kWaits = 30000, kBigList = 0x8100, kPositions = 4000;
    g.resize(kBigTrack, 0);
    g.push_back(0xC0);  // sample 0
    for (size_t i = 0; i < kWaits; ++i) g.push_back(0xE0);  // wait one row, again and again
    g.push_back(0x80);  // end of track
    g.resize(kBigList, 0);
    for (size_t i = 0; i < kPositions; ++i) put16(g, kBigList + 2 * i, (int)kBigTrack);
    put16(g, kBigList + 2 * kPositions, 0);
    put16(g, kSubSongs + 2, (int)kBigList);  // channel 0 plays the big list
    put16(g, kSubSongs + 4, (int)kPosList0);  // channel 1 plays the note (the lowest list stays at its place)
    const auto t0 = std::chrono::steady_clock::now();
    auto p = AmigaSynthPlayer::open(g.data(), g.size());
    CHECK(p != nullptr);
    if (p) {
        p->setRate(48000);
        peakOf(*p, 1.0);
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  noteless tracks: %.2f s (song %.1f s)\n", took, p ? p->seconds() : 0.0);
    CHECK(took < 5.0);
}

TEST(whittaker_real_modules_play) {
    const std::filesystem::path dir = modulesDir() / "David Whittaker" / "David Whittaker";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP whittaker_real_modules_play (no David Whittaker modules)\n");
        return;
    }
    // Sound effect banks and a fake that NostalgicPlayer does not take either (its replay routine search
    // fails on them the same way, without reading outside the search window).
    const char* const kUnsupported[] = {"anarchy sfx.dw",   "blood money sfx.dw", "feud fake.dw",
                                        "leviathan-sfx.dw", "spellbound sfx.dw",  "xenon-sfx.dw"};
    // The first sub-song (the one played) is a short jingle that ends with "stop song".
    const char* const kShortFirstSong[] = {"carrier command.dw", "fireandbrimstone-ingame.dw", "fright night.dw",
                                           "sentinel.dw", "superleaguemanager 1.dw", "thekillingcloud-sfx.dw"};
    auto listed = [](const std::string& name, const auto& list) {
        for (const char* l : list)
            if (name == l) return true;
        return false;
    };
    int n = 0, ok = 0, unsupported = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ".dw") continue;
        ++n;
        const auto d = readFile(e.path());
        const std::string name = e.path().filename().string();
        if (listed(name, kUnsupported)) {
            CHECK(!detectWhittaker(d.data(), d.size()));
            ++unsupported;
            continue;
        }
        // the app's routing asks these players first
        CHECK(!TfmxPlayer::detect(d.data(), d.size()));
        CHECK(!SidPlayer::detect(d.data(), d.size()));
        CHECK(!Pt3Player::detect(d.data(), d.size()));
        CHECK(!GmePlayer::detect(d.data(), d.size()));
        CHECK(!AyPlayer::detect(d.data(), d.size()));
        const bool det = AmigaSynthPlayer::detect(d.data(), d.size()) && detectWhittaker(d.data(), d.size());
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(det);
        CHECK(p != nullptr);
        if (!det || !p) {
            std::printf("  FAIL %s (detect %d, open %d)\n", name.c_str(), det, p != nullptr);
            continue;
        }
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), "DAVID WHITTAKER", 15) == 0);
        const float pk = peakOf(*p, 3.0);
        const double minSeconds = listed(name, kShortFirstSong) ? 1.0 : 5.0;
        CHECK(pk > 0.02f);
        CHECK(p->seconds() > minSeconds);
        if (pk > 0.02f && p->seconds() > minSeconds) ++ok;
        else std::printf("  FAIL %s: %.1f s, peak %.3f\n", name.c_str(), p->seconds(), pk);
        // truncated copies are refused or play
        for (size_t cut = 1024; cut < d.size(); cut += d.size() / 7) survives(std::vector<uint8_t>(d.begin(), d.begin() + cut));
    }
    std::printf("  david whittaker: %d of %d modules play, %d not supported (as in NostalgicPlayer)\n", ok, n,
                unsupported);
    CHECK_EQ(ok + unsupported, n);
}

// Nothing else in the modules folder is taken for a David Whittaker module.
TEST(whittaker_no_false_detects) {
    const std::filesystem::path dir = modulesDir();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP whittaker_no_false_detects (no modules folder)\n");
        return;
    }
    int n = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.file_size() > (8u << 20) || e.path().extension() == ".dw") continue;
        const auto d = readFile(e.path());
        const bool det = detectWhittaker(d.data(), d.size());
        CHECK(!det);
        if (det) std::printf("  FALSE DETECT %s\n", e.path().string().c_str());
        ++n;
    }
    std::printf("  david whittaker: %d other files not detected\n", n);
}
