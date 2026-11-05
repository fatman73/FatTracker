// Stage 5: TFMX playback (Turrican II mdat + smpl pairs from modules/).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/tfmx_player.h"
#include "test.h"

namespace {
std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::vector<uint8_t> d;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return d;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    d.resize(n > 0 ? (size_t)n : 0);
    if (n > 0) std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    return d;
}

float peakOf(TfmxPlayer& p, double seconds) {
    std::vector<float> buf(2 * 4800);
    float peak = 0;
    for (double t = 0; t < seconds; t += 0.1) {
        p.render(buf.data(), 4800, 0.5f);
        for (float v : buf) peak = v > peak ? v : (-v > peak ? -v : peak);
    }
    return peak;
}
}  // namespace

TEST(tfmx_detect_and_reject) {
    std::vector<uint8_t> mod(4096, 0);
    std::memcpy(mod.data() + 1080, "M.K.", 4);
    CHECK(!TfmxPlayer::detect(mod.data(), mod.size()));
    std::vector<uint8_t> fake(4096, 0);
    std::memcpy(fake.data(), "TFMX-SONG ", 10);
    CHECK(TfmxPlayer::detect(fake.data(), fake.size()));
    TfmxPlayer p;
    CHECK(!p.load(fake.data(), fake.size(), nullptr, 0));  // no samples, no songs
    std::vector<float> buf(2 * 64, 1.f);
    p.render(buf.data(), 64, 1.f);  // an unloaded player renders silence
    CHECK(buf[10] == 0.f);
}

TEST(real_tfmx_turrican2_plays) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        std::printf("SKIP real_tfmx (no modules folder)\n");
        return;
    }
    int count = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::wstring name = e.path().filename().wstring();
        if (name.size() < 5 || _wcsnicmp(name.c_str(), L"mdat.", 5) != 0) continue;
        const fs::path smplPath = e.path().parent_path() / (L"smpl." + name.substr(5));
        const auto mdat = readAll(e.path()), smpl = readAll(smplPath);
        CHECK(TfmxPlayer::detect(mdat.data(), mdat.size()));
        TfmxPlayer p;
        p.setRate(48000);
        const bool ok = p.load(mdat.data(), mdat.size(), smpl.data(), smpl.size());
        if (!ok) std::printf("  TFMX FAIL %ls\n", name.c_str());
        CHECK(ok);
        if (!ok) continue;
        const float peak = peakOf(p, 10.0);
        std::printf("  %ls: %s, %d voices, %d songs, %.1f s, peak %.2f\n", name.c_str(), p.format(), p.voices(),
                    p.songs(), p.seconds(), peak);
        CHECK(peak > 0.01f);
        CHECK(p.voices() >= 4);
        ++count;
    }
    std::printf("  tfmx: %d modules\n", count);
}
