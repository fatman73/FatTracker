// Game_Music_Emu formats: SNES SPC, Atari 8-bit SAP, NES NSF, ZX AY, Mega Drive VGM, ... - playback only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/gme_player.h"
#include "core/song.h"
#include "test.h"

namespace {
float peakOf(GmePlayer& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}
}  // namespace

TEST(gme_rejects_other_data) {
    std::vector<uint8_t> mod(4096, 0);
    std::memcpy(mod.data() + 1080, "M.K.", 4);
    CHECK(!GmePlayer::detect(mod.data(), mod.size()));
    std::vector<uint8_t> fake(64, 0);
    std::memcpy(fake.data(), "SNES-SPC700 Sound File Data", 27);  // the tag without the 64 KB of APU memory
    GmePlayer p;
    if (GmePlayer::detect(fake.data(), fake.size())) CHECK(!p.load(fake.data(), fake.size()));
}

TEST(gme_real_files_play) {
    const char* dirs[] = {"SNES", "Atari8", "NES", "Spectrum", "MegaDrive"};
    int n = 0;
    for (const char* sub : dirs) {
        const std::filesystem::path dir = std::filesystem::path(FT_SOURCE_DIR) / "modules" / sub;
        if (!std::filesystem::exists(dir)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            std::ifstream in(e.path(), std::ios::binary);
            const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
            if (!GmePlayer::detect(d.data(), d.size())) continue;
            GmePlayer p;
            p.setRate(48000);
            const bool ok = p.load(d.data(), d.size());
            CHECK(ok);
            if (!ok) {
                std::printf("  FAIL %s\n", e.path().filename().string().c_str());
                continue;
            }
            const float pk = peakOf(p, 3.0);
            CHECK(pk > 0.01f);
            Song s;
            CHECK(p.buildDisplay(s));
            std::printf("  %s: %s \"%s\", %d voices, peak %.2f\n", e.path().filename().string().c_str(), p.format(),
                        p.title(), p.voices(), pk);
            ++n;
            for (size_t cut : {d.size() / 2, (size_t)300}) {  // cut files: refused or played, never a crash
                GmePlayer q;
                q.setRate(48000);
                if (q.load(d.data(), cut)) peakOf(q, 0.2);
            }
        }
    }
    std::printf("  gme: %d files\n", n);
}
