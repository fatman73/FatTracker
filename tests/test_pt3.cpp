// ZX Spectrum Pro Tracker 3 / Vortex Tracker II modules (.pt3) on ayumi. Playback only.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/pt3_player.h"
#include "core/song.h"
#include "test.h"

namespace {
float peakOf(Pt3Player& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}

void put16(std::vector<uint8_t>& f, size_t at, size_t v) {
    f[at] = (uint8_t)(v & 0xFF);
    f[at + 1] = (uint8_t)(v >> 8);
}

// One position, one pattern: channel A sets ornament 0 + sample 1, volume 15 and plays a note held for
// 64 rows; B and C stay silent. Speed 3: the song is 64 * 3 frames = 3.84 s long.
std::vector<uint8_t> tinyPt3() {
    std::vector<uint8_t> f(0xC9, 0);
    std::string name = "ProTracker 3.5 compilation of ";
    std::string title = "TINY TUNE", author = "TESTER";
    title.resize(32, ' ');
    author.resize(32, ' ');
    name += title + " by " + author + " ";
    std::memcpy(f.data(), name.data(), 0x63);
    f[0x63] = 0;  // note table
    f[0x64] = 3;  // speed
    f[0x65] = 1;  // positions
    f[0x66] = 0;  // loop position
    f.push_back(0x00);  // position list: pattern 0
    f.push_back(0xFF);
    const size_t patTable = f.size();
    f.resize(f.size() + 6, 0);
    const size_t patA = f.size();
    const uint8_t a[] = {0xF0, 0x02, 0xB1, 64, 0xCF, 0x50 + 48, 0x00};
    f.insert(f.end(), a, a + sizeof a);
    const size_t patB = f.size();
    const uint8_t b[] = {0xB1, 64, 0xD0, 0x00};
    f.insert(f.end(), b, b + sizeof b);
    const size_t patC = f.size();
    f.insert(f.end(), b, b + sizeof b);
    const size_t sample = f.size();
    const uint8_t s[] = {0, 1, 0x00, 0x8F, 0x00, 0x00};  // loop 0, length 1: tone on, noise off, amplitude 15
    f.insert(f.end(), s, s + sizeof s);
    const size_t orn = f.size();
    const uint8_t o[] = {0, 1, 0};  // loop 0, length 1: +0 semitones
    f.insert(f.end(), o, o + sizeof o);
    put16(f, 0x67, patTable);
    put16(f, patTable, patA);
    put16(f, patTable + 2, patB);
    put16(f, patTable + 4, patC);
    put16(f, 0x69 + 2 * 1, sample);
    put16(f, 0xA9, orn);
    return f;
}

double secondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}
}  // namespace

TEST(pt3_tiny_module_plays_channel_a) {
    const auto f = tinyPt3();
    CHECK(Pt3Player::detect(f.data(), f.size()));
    Pt3Player p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_NEAR(p.seconds(), 3.84, 0.05);
    CHECK(std::strcmp(p.title(), "TINY TUNE") == 0);
    CHECK(std::strcmp(p.author(), "TESTER") == 0);
    CHECK(peakOf(p, 0.5) > 0.05f);
    CHECK(p.vu(0) > 0.1f);   // channel A sounds
    CHECK(p.vu(1) < 0.01f);  // B and C only have empty rows
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

TEST(pt3_vortex_header_detected) {
    auto f = tinyPt3();
    const char vt[] = "Vortex Tracker II 1.0 module: ";
    std::memcpy(f.data(), vt, 30);
    CHECK(Pt3Player::detect(f.data(), f.size()));
    Pt3Player p;
    p.setRate(44100);
    CHECK(p.load(f.data(), f.size()));
    CHECK(peakOf(p, 0.3) > 0.05f);
}

TEST(pt3_rejects_other_data) {
    std::vector<uint8_t> junk(2048, 0);
    std::memcpy(junk.data() + 1080, "M.K.", 4);
    CHECK(!Pt3Player::detect(junk.data(), junk.size()));
    Pt3Player p;
    CHECK(!p.load(junk.data(), junk.size()));
    CHECK(!p.load(nullptr, 0));
    const auto f = tinyPt3();
    CHECK(!Pt3Player::detect(f.data(), 0x40));  // the tag alone, without a header
    CHECK(!p.load(f.data(), 0xC9));             // header without a position list
}

TEST(pt3_broken_modules_refused_or_safe) {
    const auto good = tinyPt3();
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t cut = 0; cut <= good.size(); ++cut) {  // every truncation: refused or played without overrun
        Pt3Player p;
        p.setRate(48000);
        if (p.load(good.data(), cut)) peakOf(p, 0.1);
    }
    {  // no positions
        auto f = good;
        f[0x65] = 0;
        Pt3Player p;
        CHECK(!p.load(f.data(), f.size()));
    }
    {  // pattern table far past the end of the file
        auto f = good;
        put16(f, 0x67, 0xFFF0);
        Pt3Player p;
        CHECK(!p.load(f.data(), f.size()));
    }
    {  // a channel's pattern points outside the file
        auto f = good;
        const size_t table = f[0x67] | f[0x68] << 8;
        put16(f, table + 2, 0xF000);
        Pt3Player p;
        CHECK(!p.load(f.data(), f.size()));
    }
    {  // sample and ornament pointers outside the file, a loop position past the end: plays silence safely
        auto f = good;
        put16(f, 0x69 + 2, 0xFFFE);
        put16(f, 0xA9, 0xFFFF);
        f[0x66] = 200;
        Pt3Player p;
        p.setRate(48000);
        if (p.load(f.data(), f.size())) peakOf(p, 0.3);
    }
    {  // a pattern that never ends a row (no note / rest / stop command): capped, not an endless loop
        auto f = good;
        const size_t table = f[0x67] | f[0x68] << 8;
        const size_t patA = f[table] | f[table + 1] << 8;
        for (size_t i = patA; i < f.size(); ++i) f[i] = 0x20;  // noise base commands only
        Pt3Player p;
        p.setRate(48000);
        if (p.load(f.data(), f.size())) peakOf(p, 0.3);
    }
    uint32_t seed = 12345;  // random byte damage after the header tag
    for (int k = 0; k < 300; ++k) {
        auto f = good;
        for (int n = 0; n < 6; ++n) {
            seed = seed * 1103515245u + 12345u;
            const size_t at = 0x63 + (seed >> 8) % (f.size() - 0x63);
            seed = seed * 1103515245u + 12345u;
            f[at] = (uint8_t)(seed >> 16);
        }
        Pt3Player p;
        p.setRate(48000);
        if (p.load(f.data(), f.size())) peakOf(p, 0.1);
    }
    CHECK(secondsSince(t0) < 20.0);  // the length dry run is capped
}

TEST(pt3_real_files_play) {
    std::filesystem::path dir = "C:\\development\\FatTracker\\modules\\Spectrum";
    std::error_code ec0;
    if (!std::filesystem::exists(dir, ec0)) dir = std::filesystem::path(FT_SOURCE_DIR) / "modules" / "Spectrum";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::printf("SKIP pt3_real_files_play (no modules\\Spectrum folder)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".pt3") continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        CHECK(Pt3Player::detect(d.data(), d.size()));
        Pt3Player p;
        p.setRate(48000);
        const bool ok = p.load(d.data(), d.size());
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        CHECK(p.seconds() > 10.0);
        const float pk = peakOf(p, 3.0);
        CHECK(pk > 0.02f);
        std::printf("  %s: %s \"%s\" by \"%s\", %.1f s, peak %.2f\n", e.path().filename().string().c_str(), p.format(),
                    p.title(), p.author(), p.seconds(), pk);
        ++n;
        for (size_t cut = 0x60; cut < d.size(); cut += d.size() / 9 + 1) {  // truncated: refused or played
            Pt3Player q;
            q.setRate(48000);
            if (q.load(d.data(), cut)) peakOf(q, 0.1);
        }
    }
    if (n == 0) std::printf("SKIP pt3_real_files_play (no .pt3 files)\n");
    std::printf("  pt3: %d files\n", n);
}
