// Sub-song selection in the playback-only chip players (ChipPlayer::subsongs / subsong / selectSubsong)
// and through the Engine (chipSubsongs / chipSubsong / selectChipSubsong).
#include <atomic>
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
#include "core/amiga_synth.h"
#include "core/gme_player.h"
#include "core/hippel_player.h"
#include "core/sid_player.h"
#include "core/song.h"
#include "core/tfmx_player.h"
#include "test.h"

namespace {
std::filesystem::path modulesDir() {
    std::filesystem::path p = "C:/development/FatTracker/modules";
    if (!std::filesystem::exists(p)) p = std::filesystem::path(FT_SOURCE_DIR) / "modules";
    return p;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// PSID v2 at $1000 with three subtunes (default: the second). init: volume 15, voice 1 sawtooth with the gate
// on; the frequency high byte is (A + 1) * 16, so subtune 1 / 2 / 3 play about 240 / 480 / 720 Hz. play: RTS.
std::vector<uint8_t> threeTunePsid() {
    std::vector<uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[5] = 2, f[7] = 0x7C;
    f[8] = 0x10, f[9] = 0x00;      // load
    f[0xA] = 0x10, f[0xB] = 0x00;  // init
    f[0xC] = 0x10, f[0xD] = 0x40;  // play
    f[0xF] = 3, f[0x11] = 2;       // 3 subtunes, the second is the default
    std::memcpy(f.data() + 0x16, "THREE TUNES", 11);
    const uint8_t code[] = {
        0x18, 0x69, 0x01,              // CLC  ADC #1
        0x0A, 0x0A, 0x0A, 0x0A,        // ASL x4
        0x8D, 0x01, 0xD4,              // STA $D401 (frequency hi)
        0xA9, 0x00, 0x8D, 0x00, 0xD4,  // frequency lo
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,  // volume 15
        0xA9, 0x00, 0x8D, 0x05, 0xD4,  // attack / decay 0
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,  // sustain 15
        0xA9, 0x21, 0x8D, 0x04, 0xD4,  // sawtooth + gate
        0x60};                         // RTS
    f.insert(f.end(), code, code + sizeof code);
    f.resize(0x7C + 0x40, 0x60);  // play at $1040: RTS
    return f;
}

// NSF at $8000 with three songs. init: pulse 1 on (50 % duty, constant volume 15, length counter halted), its
// timer from a table indexed by the song in A: 253 / 126 / 84 give about 440 / 880 / 1320 Hz. play: RTS.
std::vector<uint8_t> threeSongNsf() {
    std::vector<uint8_t> f(0x80, 0);
    std::memcpy(f.data(), "NESM\x1A", 5);
    f[5] = 1;                      // version
    f[6] = 3, f[7] = 1;            // 3 songs, the first is the default
    f[8] = 0x00, f[9] = 0x80;      // load $8000
    f[0xA] = 0x00, f[0xB] = 0x80;  // init $8000
    f[0xC] = 0x20, f[0xD] = 0x80;  // play $8020
    std::memcpy(f.data() + 0x0E, "THREE SONGS", 11);
    f[0x6E] = 0x1A, f[0x6F] = 0x41;  // NTSC: 16666 us a frame
    f[0x78] = 0x20, f[0x79] = 0x4E;  // PAL: 20000 us
    const uint8_t code[] = {
        0xAA,                    // TAX
        0xA9, 0x01, 0x8D, 0x15, 0x40,  // pulse 1 on
        0xA9, 0xBF, 0x8D, 0x00, 0x40,  // duty 50 %, length halted, constant volume 15
        0xA9, 0x00, 0x8D, 0x01, 0x40,  // no sweep
        0xBD, 0x30, 0x80, 0x8D, 0x02, 0x40,  // LDA $8030,X  STA $4002 (timer low)
        0xA9, 0x00, 0x8D, 0x03, 0x40,  // timer high 0 (starts the note)
        0x60};                         // RTS
    f.insert(f.end(), code, code + sizeof code);
    f.resize(0x80 + 0x20, 0xEA);
    f.push_back(0x60);  // play at $8020: RTS
    f.resize(0x80 + 0x30, 0xEA);
    f.push_back(253), f.push_back(126), f.push_back(84);  // timer table at $8030
    f.resize(0x80 + 0x100, 0);
    return f;
}

float peakOf(ChipPlayer& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}

// Upward crossings of the mean per second (the pitch of a plain waveform), after a short settle.
double pitchOf(ChipPlayer& p) {
    std::vector<float> buf(4800 * 2);
    p.render(buf.data(), 4800, 0.45f);  // envelope attack, emulator start-up
    std::vector<float> mono;
    for (int i = 0; i < 5; ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (int f = 0; f < 4800; ++f) mono.push_back(buf[2 * f]);
    }
    double mean = 0;
    for (float v : mono) mean += v;
    mean /= (double)mono.size();
    int ups = 0;
    for (size_t i = 1; i < mono.size(); ++i) ups += mono[i - 1] < mean && mono[i] >= mean;
    return ups / (mono.size() / 48000.0);
}

// Out-of-range indexes are refused and leave the current sub-song alone.
void refusesOutOfRange(ChipPlayer& p) {
    const int cur = p.subsong(), n = p.subsongs();
    CHECK(!p.selectSubsong(-1));
    CHECK(!p.selectSubsong(n));
    CHECK(!p.selectSubsong(n + 1000));
    CHECK(!p.selectSubsong(-100000));
    CHECK_EQ(p.subsong(), cur);
    CHECK_EQ(p.subsongs(), n);
}

// The interface's defaults: one sub-song.
class OneSong : public ChipPlayer {
public:
    void restart() override {}
    void setRate(uint32_t) override {}
    void clearMeters() override {}
    void render(float* stereo, uint32_t frames, float) override { std::memset(stereo, 0, sizeof(float) * 2 * frames); }
    int voices() const override { return 4; }
    float vu(int) const override { return 0; }
    void scope(int, float* dst, int n) const override {
        for (int i = 0; i < n; ++i) dst[i] = 0;
    }
    double seconds() const override { return 0; }
    const char* format() const override { return "ONE"; }
    bool buildDisplay(Song& out) override {
        out = makeEmptySong(4);
        return true;
    }
    int tick() const override { return 0; }
};
}  // namespace

TEST(subsong_default_is_one_song) {
    OneSong p;
    CHECK_EQ(p.subsongs(), 1);
    CHECK_EQ(p.subsong(), 0);
    CHECK(p.selectSubsong(0));
    refusesOutOfRange(p);
}

TEST(subsong_sid_switch_changes_pitch) {
    const auto f = threeTunePsid();
    SidPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.subsongs(), 3);
    CHECK_EQ(p.subsong(), 1);  // the header's default subtune (2, counted from 1)
    const double second = pitchOf(p);
    CHECK(p.selectSubsong(0));
    CHECK_EQ(p.subsong(), 0);
    const double first = pitchOf(p);
    CHECK(p.selectSubsong(2));
    CHECK_EQ(p.subsong(), 2);
    const double third = pitchOf(p);
    std::printf("  sid subtunes: %.0f / %.0f / %.0f Hz\n", first, second, third);
    CHECK_NEAR(second / first, 2.0, 0.15);
    CHECK_NEAR(third / first, 3.0, 0.2);
    refusesOutOfRange(p);
    CHECK_EQ(p.subsong(), 2);
    p.setRate(44100);  // a rate change rebuilds the C64 and keeps the subtune
    CHECK_EQ(p.subsong(), 2);
    CHECK_NEAR(pitchOf(p) / third, 48000.0 / 44100, 0.03);  // (pitchOf counts at 48 kHz)
}

TEST(subsong_sid_single_tune) {
    auto f = threeTunePsid();
    f[0xF] = 1, f[0x11] = 1;
    SidPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.subsongs(), 1);
    CHECK_EQ(p.subsong(), 0);
    CHECK(p.selectSubsong(0));
    refusesOutOfRange(p);
}

TEST(subsong_gme_nsf_tracks) {
    const auto f = threeSongNsf();
    GmePlayer p;
    p.setRate(48000);
    CHECK(GmePlayer::detect(f.data(), f.size()));
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.subsongs(), 3);
    CHECK_EQ(p.subsong(), 0);
    const double first = pitchOf(p);
    CHECK(p.selectSubsong(1));
    CHECK_EQ(p.subsong(), 1);
    const double second = pitchOf(p);
    CHECK(p.selectSubsong(2));
    CHECK_EQ(p.subsong(), 2);
    const double third = pitchOf(p);
    std::printf("  nsf tracks: %.0f / %.0f / %.0f Hz\n", first, second, third);
    CHECK_NEAR(second / first, 2.0, 0.1);
    CHECK_NEAR(third / first, 3.0, 0.15);
    p.restart();  // restarts the selected track
    CHECK_EQ(p.subsong(), 2);
    p.setRate(44100);  // the emulator is rebuilt for the rate: same track
    CHECK_EQ(p.subsong(), 2);
    CHECK_NEAR(pitchOf(p) / third, 48000.0 / 44100, 0.03);  // (pitchOf counts at 48 kHz)
    refusesOutOfRange(p);
    CHECK_EQ(p.subsong(), 2);
}

// The real console files: as many sub-songs as GME has tracks; every track can be selected and plays.
TEST(subsong_gme_real_files) {
    const char* dirs[] = {"NES", "SNES", "Atari8", "Spectrum", "MegaDrive"};
    int files = 0, multi = 0;
    for (const char* sub : dirs) {
        const std::filesystem::path dir = modulesDir() / sub;
        if (!std::filesystem::exists(dir)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            const auto d = readFile(e.path());
            if (!GmePlayer::detect(d.data(), d.size())) continue;
            GmePlayer p;
            p.setRate(48000);
            if (!p.load(d.data(), d.size())) continue;
            ++files;
            CHECK_EQ(p.subsongs(), p.tracks() > 0 ? p.tracks() : 1);
            CHECK_EQ(p.subsong(), 0);
            refusesOutOfRange(p);
            if (p.subsongs() < 2) continue;
            ++multi;
            const int n = p.subsongs() < 6 ? p.subsongs() : 6;
            float loudest = 0;
            for (int i = 0; i < n; ++i) {
                CHECK(p.selectSubsong(i));
                CHECK_EQ(p.subsong(), i);
                const float pk = peakOf(p, 1.0);
                loudest = pk > loudest ? pk : loudest;
            }
            CHECK(loudest > 0.01f);
            std::printf("  %s: %d tracks\n", e.path().filename().string().c_str(), p.subsongs());
        }
    }
    std::printf("  gme: %d files, %d with several tracks\n", files, multi);
}

TEST(subsong_whittaker_jingles_start_on_a_long_song) {
    const std::filesystem::path dir = modulesDir() / "David Whittaker" / "David Whittaker";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP subsong_whittaker_jingles_start_on_a_long_song (no David Whittaker modules)\n");
        return;
    }
    // The first sub-song of these is a short jingle that ends with "stop song". The first three have the real
    // tune in a later sub-song; the other three have no longer sub-song (one, two and one sub-songs, which is
    // also what NostalgicPlayer's sub-song list reads from them).
    struct Case {
        const char* name;
        bool longerLater;
    };
    const Case kJingleFirst[] = {{"fireandbrimstone-ingame.dw", true}, {"fright night.dw", true},
                                 {"sentinel.dw", true},                {"carrier command.dw", false},
                                 {"superleaguemanager 1.dw", false},   {"thekillingcloud-sfx.dw", false}};
    for (const Case& c : kJingleFirst) {
        const auto d = readFile(dir / c.name);
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) continue;
        p->setRate(48000);
        const int start = p->subsong();
        const double len = p->seconds();
        std::printf("  %s: %d sub-songs, starts on %d (%.1f s)\n", c.name, p->subsongs(), start + 1, len);
        if (c.longerLater) {
            CHECK(p->subsongs() > 1);
            CHECK(start > 0);
            CHECK(len > 10.0);
        }
        // the longest one: no other sub-song is longer
        for (int i = 0; i < p->subsongs(); ++i) {
            CHECK(p->selectSubsong(i));
            CHECK_EQ(p->subsong(), i);
            CHECK(p->seconds() <= len + 0.01);
        }
        CHECK(p->selectSubsong(0));
        CHECK(p->seconds() < 10.0);  // the jingle, measured again
        CHECK(p->selectSubsong(start));
        CHECK_NEAR(p->seconds(), len, 0.01);
        CHECK(peakOf(*p, 2.0) > 0.02f);
        refusesOutOfRange(*p);
    }
}

TEST(subsong_tfmx_real_songs) {
    namespace fs = std::filesystem;
    const fs::path dir = modulesDir();
    if (!fs::exists(dir)) {
        std::printf("SKIP subsong_tfmx_real_songs (no modules folder)\n");
        return;
    }
    int tested = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (tested >= 3) break;
        if (!e.is_regular_file()) continue;
        const std::wstring name = e.path().filename().wstring();
        if (name.size() < 5 || _wcsnicmp(name.c_str(), L"mdat.", 5) != 0) continue;
        const auto mdat = readFile(e.path()), smpl = readFile(e.path().parent_path() / (L"smpl." + name.substr(5)));
        TfmxPlayer p;
        p.setRate(48000);
        if (!p.load(mdat.data(), mdat.size(), smpl.data(), smpl.size()) || p.songs() < 2) continue;
        CHECK_EQ(p.subsongs(), p.songs());
        CHECK_EQ(p.subsong(), 0);
        const int count = p.subsongs();
        const double firstLen = p.seconds();
        const int last = p.subsongs() - 1;
        CHECK(p.selectSubsong(last));
        CHECK_EQ(p.subsong(), last);
        CHECK_EQ(p.subsongs(), count);  // the song list stays the same
        const double lastLen = p.seconds();
        Song s;
        CHECK(p.buildDisplay(s));
        peakOf(p, 0.5);
        CHECK(p.selectSubsong(0));
        CHECK_EQ(p.subsong(), 0);
        CHECK_EQ(p.subsongs(), count);
        CHECK_NEAR(p.seconds(), firstLen, 0.001);  // measured again: as on loading
        CHECK(peakOf(p, 2.0) > 0.01f);
        std::printf("  %ls: %d songs, first %.1f s, last %.1f s\n", name.c_str(), p.subsongs(), p.seconds(), lastLen);
        refusesOutOfRange(p);
        ++tested;
    }
    if (!tested) std::printf("SKIP subsong_tfmx_real_songs (no TFMX file with several songs)\n");
}

TEST(subsong_hippel_real_songs) {
    namespace fs = std::filesystem;
    const fs::path dir = modulesDir();
    if (!fs::exists(dir)) {
        std::printf("SKIP subsong_hippel_real_songs (no modules folder)\n");
        return;
    }
    int tested = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (tested >= 3) break;
        if (!e.is_regular_file()) continue;
        std::wstring ext = e.path().extension().wstring();
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext != L".hip" && ext != L".hipc" && ext != L".hip7" && ext != L".fc" && ext != L".fc14" && ext != L".smod")
            continue;
        const auto d = readFile(e.path());
        if (!HippelPlayer::detect(d.data(), d.size())) continue;
        HippelPlayer p;
        p.setRate(48000);
        if (!p.load(d.data(), d.size())) continue;
        CHECK(p.subsongs() >= 1);
        CHECK_EQ(p.subsong(), 0);
        CHECK(p.selectSubsong(0));
        refusesOutOfRange(p);
        if (p.subsongs() < 2) continue;
        const int last = p.subsongs() - 1;
        CHECK(p.selectSubsong(last));
        CHECK_EQ(p.subsong(), last);
        peakOf(p, 0.5);
        CHECK(p.selectSubsong(0));
        CHECK_EQ(p.subsong(), 0);
        CHECK(peakOf(p, 2.0) > 0.01f);
        std::printf("  %ls: %d songs\n", e.path().filename().wstring().c_str(), p.subsongs());
        refusesOutOfRange(p);
        ++tested;
    }
    if (!tested) std::printf("SKIP subsong_hippel_real_songs (no Hippel file with several songs)\n");
}

TEST(subsong_engine_without_chip_song) {
    Engine e;
    e.setSong(std::make_unique<Song>(makeEmptySong(4)));
    CHECK_EQ(e.chipSubsongs(), 0);
    CHECK_EQ(e.chipSubsong(), 0);
    CHECK(!e.selectChipSubsong(0));
    CHECK(!e.selectChipSubsong(1));
}

TEST(subsong_engine_select) {
    const auto f = threeTunePsid();
    Engine e;
    CHECK(e.loadSid(f.data(), f.size(), "", false));
    CHECK_EQ(e.chipSubsongs(), 3);
    CHECK_EQ(e.chipSubsong(), 1);
    Telemetry t;
    e.snapshot(t);
    CHECK_EQ(t.subsongs, 3);
    CHECK_EQ(t.subsong, 1);
    CHECK(e.selectChipSubsong(2));
    CHECK_EQ(e.chipSubsong(), 2);
    e.snapshot(t);
    CHECK_EQ(t.subsong, 2);
    CHECK(!t.playing);  // selecting does not start a stopped song
    CHECK(!e.selectChipSubsong(3));
    CHECK(!e.selectChipSubsong(-1));
    CHECK_EQ(e.chipSubsong(), 2);
    CHECK(e.tfmx());
    CHECK(e.song() != nullptr);
    CHECK(std::strncmp(e.song()->name, "THREE TUNES", 11) == 0);  // no per-subtune title: the name stays
    e.play();
    std::vector<float> buf(960 * 2);
    e.render(buf.data(), 960);
    CHECK(e.selectChipSubsong(0));
    e.snapshot(t);
    CHECK(t.playing);  // a playing song keeps playing, from the start of the new sub-song
    CHECK_EQ(t.playFrames, 0u);
    // a pattern song afterwards: no sub-songs
    e.setSong(std::make_unique<Song>(makeEmptySong(4)));
    CHECK_EQ(e.chipSubsongs(), 0);
    CHECK(!e.selectChipSubsong(0));
    e.snapshot(t);
    CHECK_EQ(t.subsongs, 0);
}

TEST(subsong_engine_select_while_rendering) {
    const auto f = threeTunePsid();
    Engine e;
    CHECK(e.loadSid(f.data(), f.size(), "", true));
    std::atomic<bool> stopFlag{false}, bad{false};
    std::thread a([&] {
        std::vector<float> buf(480 * 2);
        while (!stopFlag) e.render(buf.data(), 480);
    });
    std::thread b([&] {
        Telemetry t;
        while (!stopFlag) {
            e.snapshot(t);
            if (t.subsongs != 3 || t.subsong > 2) bad = true;
            float sc[64];
            e.scope(0, sc, 64);
        }
    });
    for (int i = 0; i < 200; ++i) {
        CHECK(e.selectChipSubsong(i % 3));
        CHECK(!e.selectChipSubsong(3 + i));
        CHECK(e.chipSubsongs() == 3);
        const Song* s = e.song();
        CHECK(s != nullptr);
    }
    stopFlag = true;
    a.join();
    b.join();
    CHECK(!bad);
    CHECK_EQ(e.chipSubsong(), 199 % 3);
}
