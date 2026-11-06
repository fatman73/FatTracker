// Jochen Hippel family: Future Composer 1.3 (SMOD) / 1.4 (FC14), Hippel TFMX / COSO - playback only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/hippel_player.h"
#include "core/song.h"
#include "test.h"

namespace {
std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

float peakOf(HippelPlayer& p, double seconds) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
    }
    return peak;
}
}  // namespace

TEST(hippel_rejects_other_data) {
    std::vector<uint8_t> junk(4096, 0);
    std::memcpy(junk.data() + 1080, "M.K.", 4);  // a ProTracker module is not Future Composer
    CHECK(!HippelPlayer::detect(junk.data(), junk.size()));
    CHECK(!HippelPlayer::detect(nullptr, 0));
    HippelPlayer p;
    CHECK(!p.load(junk.data(), junk.size()));
}

TEST(hippel_real_modules_play) {
    const std::filesystem::path dir = std::filesystem::path(FT_SOURCE_DIR) / "modules";
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP hippel_real_modules_play (no modules folder)\n");
        return;
    }
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::wstring ext = e.path().extension().wstring();
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext != L".fc" && ext != L".smod" && ext != L".fc13" && ext != L".fc14" && ext != L".hip" && ext != L".hipc")
            continue;
        if (e.path().filename().wstring().find(L".diff.") != std::wstring::npos) continue;  // a patch, not a song
        const auto data = readAll(e.path());
        const bool found = HippelPlayer::detect(data.data(), data.size());
        CHECK(found);
        HippelPlayer p;
        p.setRate(48000);
        const bool ok = found && p.load(data.data(), data.size());
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s\n", e.path().filename().string().c_str());
            continue;
        }
        CHECK(p.voices() >= 4);
        CHECK(p.seconds() > 5);
        CHECK(peakOf(p, 3.0) > 0.02f);
        Song s;
        CHECK(p.buildDisplay(s));
        CHECK(s.numChannels >= 4);
        std::printf("  %s: %s, %d voices, %.1f s\n", e.path().filename().string().c_str(), p.format(), p.voices(),
                    p.seconds());
        ++n;
        // truncated copies must be rejected or played without crashing
        for (size_t cut : {data.size() / 2, data.size() / 3, (size_t)200}) {
            HippelPlayer q;
            q.setRate(48000);
            if (q.load(data.data(), cut)) peakOf(q, 0.5);
        }
    }
    std::printf("  hippel / future composer: %d modules\n", n);
    CHECK(n >= 2);
}
