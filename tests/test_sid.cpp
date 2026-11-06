// Commodore 64 SID tunes (PSID / RSID) on the emulated C64 - playback only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/sid_player.h"
#include "core/song.h"
#include "test.h"

namespace {
// PSID v2 at $1000. init: volume 15, voice 1 sawtooth with the gate on (attack 0, sustain 15). play: RTS.
std::vector<uint8_t> tinyPsid() {
    std::vector<uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[5] = 2, f[7] = 0x7C;
    f[8] = 0x10, f[9] = 0x00;    // load
    f[0xA] = 0x10, f[0xB] = 0x00;  // init
    f[0xC] = 0x10, f[0xD] = 0x20;  // play
    f[0xF] = 1, f[0x11] = 1;
    std::memcpy(f.data() + 0x16, "TINY SAW", 8);
    const uint8_t code[] = {
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,  // LDA #$0F  STA $D418 (volume)
        0xA9, 0x00, 0x8D, 0x05, 0xD4,  // attack / decay 0
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,  // sustain 15
        0xA9, 0x00, 0x8D, 0x00, 0xD4,  // frequency lo
        0xA9, 0x10, 0x8D, 0x01, 0xD4,  // frequency hi (~250 Hz)
        0xA9, 0x21, 0x8D, 0x04, 0xD4,  // sawtooth + gate
        0x60};                         // RTS
    f.insert(f.end(), code, code + sizeof code);
    f.resize(0x7C + 0x20, 0x60);  // play at $1020: RTS
    return f;
}

float peakOf(SidPlayer& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}
}  // namespace

TEST(sid_detect) {
    const auto f = tinyPsid();
    CHECK(SidPlayer::detect(f.data(), f.size()));
    CHECK(!SidPlayer::detect(f.data(), 0x40));  // header cut
    std::vector<uint8_t> bad = f;
    bad[7] = 0x20;  // impossible header size
    CHECK(!SidPlayer::detect(bad.data(), bad.size()));
    std::vector<uint8_t> amiga(4096, 0);
    std::memcpy(amiga.data(), "SIDMON", 6);  // Amiga SidMon files may be called .sid too
    CHECK(!SidPlayer::detect(amiga.data(), amiga.size()));
}

TEST(sid_tiny_tune_sounds) {
    const auto f = tinyPsid();
    SidPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.voices(), 3);
    CHECK(std::strcmp(p.title(), "TINY SAW") == 0);
    CHECK(std::strncmp(p.format(), "C64 PSID", 8) == 0);
    CHECK(peakOf(p, 0.5) > 0.05f);  // the 6502 init routine reached the SID
    CHECK(p.vu(0) > 0.1f);          // voice 1 envelope is up
    CHECK(p.vu(1) < 0.01f);         // voice 2 never started
    float sc[64];
    p.scope(3, sc, 64);  // the C64 has three voices: the fourth scope stays flat
    float mx = 0;
    for (float v : sc) mx = std::fabs(v) > mx ? std::fabs(v) : mx;
    CHECK(mx == 0.f);
    Song s;
    CHECK(p.buildDisplay(s));
    CHECK_EQ(s.numChannels, 4);
    p.setRate(44100);  // the C64 is rebuilt for the new rate and still plays
    CHECK(peakOf(p, 0.3) > 0.05f);
}

TEST(sid_hostile_files) {
    auto f = tinyPsid();
    f[8] = 0xFF, f[9] = 0xF0;  // load at $FFF0: the data runs past the C64's 64 KB
    f.resize(0x7C + 4000, 0xEA);
    SidPlayer p;
    if (p.load(f.data(), f.size())) peakOf(p, 0.2);
    auto g = tinyPsid();
    g[8] = g[9] = 0;  // PRG style load address, but no data after the header
    g.resize(0x7C);
    CHECK(!SidPlayer::detect(g.data(), g.size()));
    auto h = tinyPsid();
    h[0xA] = 0x10, h[0xB] = 0x00;
    h[0x7C] = 0x4C, h[0x7D] = 0x00, h[0x7E] = 0x10;  // init is an endless JMP loop: init gives up, no hang
    SidPlayer q;
    if (q.load(h.data(), h.size())) peakOf(q, 0.1);
}

TEST(sid_real_tunes_play) {
    const std::filesystem::path dir = std::filesystem::path(FT_SOURCE_DIR) / "modules" / "C64";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP sid_real_tunes_play (no modules/C64 folder)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        if (!SidPlayer::detect(d.data(), d.size())) continue;
        SidPlayer p;
        p.setRate(48000);
        CHECK(p.load(d.data(), d.size()));
        const float pk = peakOf(p, 2.0);
        CHECK(pk > 0.01f);
        std::printf("  %s: %s \"%s\", %d voices, peak %.2f\n", e.path().filename().string().c_str(), p.format(), p.title(),
                    p.voices(), pk);
        ++n;
    }
    std::printf("  c64 sid: %d tunes\n", n);
}

TEST(sid_pitch_does_not_depend_on_the_device_rate) {
    // cRSID keeps its rate in 16 bits: 96 kHz must not wrap into a slow, low tune
    const auto f = tinyPsid();
    auto crossings = [&](uint32_t rate) {
        SidPlayer p;
        p.setRate(rate);
        p.load(f.data(), f.size());
        std::vector<float> buf((size_t)rate * 2);
        p.render(buf.data(), rate, 0.45f);  // one second
        int n = 0;
        for (uint32_t i = 1; i < rate; ++i)
            if (buf[2 * (i - 1)] < 0 && buf[2 * i] >= 0) ++n;
        return n;
    };
    const int at48 = crossings(48000), at96 = crossings(96000);
    CHECK(at48 > 50);
    CHECK_NEAR((double)at96, (double)at48, at48 * 0.05 + 2);
}
