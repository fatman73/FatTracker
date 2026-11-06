// Amiga synth formats ported from NostalgicPlayer (JamCracker, ...): playback only.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/amiga_synth.h"
#include "core/song.h"
#include "test.h"

namespace {
float peakOf(AmigaSynthPlayer& p, double seconds, float* vu0 = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (vu0 && i == 2) *vu0 = p.vu(0);
    }
    return peak;
}
void be16(std::vector<uint8_t>& f, int v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void be32(std::vector<uint8_t>& f, uint32_t v) { be16(f, (int)(v >> 16)), be16(f, (int)(v & 0xFFFF)); }

// JamCracker: 1 looped 32 byte square sample, 1 pattern of 4 rows: row 0 channel 0 plays note 13 with it.
std::vector<uint8_t> tinyJam() {
    std::vector<uint8_t> f = {'B', 'e', 'E', 'p'};
    be16(f, 1);
    for (int i = 0; i < 31; ++i) f.push_back(i < 4 ? "sqr"[i % 3] : 0);
    f.push_back(1);  // looped
    be32(f, 32), be32(f, 0);
    be16(f, 1);  // patterns
    be16(f, 4), be32(f, 0);
    be16(f, 1), be16(f, 0);  // song: pattern 0
    for (int row = 0; row < 4; ++row)
        for (int ch = 0; ch < 4; ++ch) {
            const bool on = row == 0 && ch == 0;
            const uint8_t note[8] = {(uint8_t)(on ? 13 : 0), 0, 0, 0, 0, 0, 0, 0};
            f.insert(f.end(), note, note + 8);
        }
    for (int i = 0; i < 32; ++i) f.push_back(i < 16 ? 100 : (uint8_t)-100);
    return f;
}
}  // namespace

TEST(jamcracker_tiny_song_plays) {
    const auto f = tinyJam();
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "JAMCRACKER", 10) == 0);
    CHECK(p->seconds() > 0.2 && p->seconds() < 2.0);  // 4 rows at speed 6
    float vu0 = 0;
    CHECK(peakOf(*p, 0.5, &vu0) > 0.05f);
    CHECK(vu0 > 0.1f);
    CHECK(p->vu(1) < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

TEST(jamcracker_broken_files) {
    auto f = tinyJam();
    for (size_t cut = 6; cut < f.size() - 32; cut += 7) CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
    auto g = tinyJam();
    g[4 + 2 + 40 + 2 + 6 + 2 + 1] = 5;  // song refers to pattern 5 of 1
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    auto h = tinyJam();
    const size_t notes = 4 + 2 + 40 + 2 + 6 + 2 + 2;
    h[notes] = 250;      // period index far past the table
    h[notes + 1] = 100;  // instrument 100 of 1
    h[notes + 3] = 0xFF;
    auto p = AmigaSynthPlayer::open(h.data(), h.size());
    CHECK(p != nullptr);
    if (p) peakOf(*p, 0.3);
}

TEST(jamcracker_huge_sample_size_is_not_allocated) {
    // the last sample may be cut short, but its header size must not become a 4 GB allocation
    auto f = tinyJam();
    const size_t sizeAt = 4 + 2 + 31 + 1;
    f[sizeAt] = f[sizeAt + 1] = f[sizeAt + 2] = f[sizeAt + 3] = 0xFF;
    const auto t0 = std::chrono::steady_clock::now();
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s < 1.0);
    if (p) peakOf(*p, 0.2);
}

TEST(amiga_synth_real_modules_play) {
    const std::filesystem::path dir = std::filesystem::path(FT_SOURCE_DIR) / "modules";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP amiga_synth_real_modules_play (no modules folder)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file() || e.file_size() > (8u << 20)) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        if (!AmigaSynthPlayer::detect(d.data(), d.size())) continue;
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        p->setRate(48000);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.02f);
        std::printf("  %s: %s, %.1f s, peak %.2f\n", e.path().filename().string().c_str(), p->format(), p->seconds(), pk);
        ++n;
    }
    std::printf("  amiga synth: %d modules\n", n);
}
