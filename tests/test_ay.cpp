// AY-3-8910 / YM2149 register dumps (YM, VTX, PSG) on ayumi; LHA -lh5- unpacking. Playback only.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/ay_player.h"
#include "core/song.h"
#include "test.h"

namespace {
float peakOf(AyPlayer& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}

// PSG: channel A tone 0x100 at volume 15 for 50 frames, then the end mark.
std::vector<uint8_t> tinyPsg() {
    std::vector<uint8_t> f(16, 0);
    std::memcpy(f.data(), "PSG\x1A", 4);
    const uint8_t setup[] = {0, 0x00, 1, 0x01, 7, 0x3E, 8, 15};
    f.insert(f.end(), setup, setup + sizeof setup);
    for (int i = 0; i < 50; ++i) f.push_back(0xFF);
    f.push_back(0xFD);
    return f;
}
}  // namespace

TEST(ay_psg_tone_plays) {
    const auto f = tinyPsg();
    CHECK(AyPlayer::detect(f.data(), f.size()));
    AyPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.frames(), 50);
    CHECK_NEAR(p.seconds(), 1.0, 0.01);
    CHECK(peakOf(p, 0.5) > 0.05f);
    CHECK(p.vu(0) > 0.3f);   // channel A sounds
    CHECK(p.vu(1) < 0.01f);  // B and C were never given a volume
    Song s;
    CHECK(p.buildDisplay(s));
}

TEST(ay_rejects_other_data) {
    std::vector<uint8_t> junk(2048, 0);
    std::memcpy(junk.data() + 1080, "M.K.", 4);
    CHECK(!AyPlayer::detect(junk.data(), junk.size()));
    AyPlayer p;
    CHECK(!p.load(junk.data(), junk.size()));
    // a broken -lh5- stream is refused, not unpacked into garbage or past its buffer
    std::vector<uint8_t> lha(64, 0xA5);
    lha[0] = 22, lha[2] = '-', lha[3] = 'l', lha[4] = 'h', lha[5] = '5', lha[6] = '-';
    lha[7] = 40, lha[11] = 0x00, lha[12] = 0x10;  // packed 40, unpacked 4096
    lha[20] = 0, lha[21] = 0;
    std::vector<uint8_t> out;
    lhaUnpack(lha.data(), lha.size(), out);
    CHECK(!p.load(lha.data(), lha.size()));
}

TEST(ay_vtx_header_of_an_amstrad_tune_is_detected) {
    // VTX: "ay", stereo, loop, chip clock 1 MHz (Amstrad CPC), 50 Hz, year, unpacked size, five strings
    std::vector<uint8_t> v = {'a', 'y', 1, 0, 0, 0x40, 0x42, 0x0F, 0x00, 50, 0xCE, 0x07, 0x78, 0x05, 0, 0};
    for (const char* s : {"title", "author", "", "", ""}) {
        v.insert(v.end(), s, s + std::strlen(s));
        v.push_back(0);
    }
    v.resize(v.size() + 64, 0x55);
    CHECK(AyPlayer::detect(v.data(), v.size()));
}

TEST(ay_truncated_lha_is_refused) {
    const auto dir = std::filesystem::path(FT_SOURCE_DIR) / "modules" / "AtariST";
    if (!std::filesystem::exists(dir)) return;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        std::vector<uint8_t> out;
        if (!lhaUnpack(d.data(), d.size(), out)) continue;
        CHECK(!lhaUnpack(d.data(), d.size() * 2 / 3, out));  // two thirds of the stream: refused, not padded with zeros
    }
}

TEST(ay_real_ym_files_play) {
    const std::filesystem::path dir = std::filesystem::path(FT_SOURCE_DIR) / "modules" / "AtariST";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP ay_real_ym_files_play (no modules/AtariST folder)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
        if (!AyPlayer::detect(d.data(), d.size())) continue;
        AyPlayer p;
        p.setRate(48000);
        const bool ok = p.load(d.data(), d.size());
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        CHECK(p.frames() > 50);
        const float pk = peakOf(p, 2.0);
        CHECK(pk > 0.02f);
        std::printf("  %s: %s \"%s\", %.1f s, peak %.2f\n", e.path().filename().string().c_str(), p.format(), p.title(),
                    p.seconds(), pk);
        ++n;
        for (size_t cut = 24; cut < d.size(); cut += d.size() / 7 + 1) {  // truncated archives: refused or played
            AyPlayer q;
            q.setRate(48000);
            if (q.load(d.data(), cut)) peakOf(q, 0.1);
        }
    }
    std::printf("  ym: %d files\n", n);
}
