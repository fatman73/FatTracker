#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "core/render.h"
#include "formats/formats.h"
#include "formats/wav.h"
#include "player_util.h"
#include "test.h"

namespace {
std::vector<float> sine(size_t frames) {
    std::vector<float> v(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        v[i * 2] = (float)std::sin(i * 0.1) * 0.8f;
        v[i * 2 + 1] = (float)std::cos(i * 0.1) * 0.5f;
    }
    return v;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::vector<uint8_t> d;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return d;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    d.resize(n > 0 ? (size_t)n : 0);
    if (n > 0) std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    return d;
}
}  // namespace

TEST(wav_roundtrip_16) {
    auto src = sine(100);
    auto bytes = writeWav(src.data(), 100, 44100, false);
    auto r = readWav(bytes.data(), bytes.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK_EQ(r.value().rate, 44100u);
    CHECK_EQ(r.value().channels, 2);
    CHECK_EQ(r.value().samples.size(), 200u);
    for (size_t i = 0; i < 200; ++i) CHECK_NEAR(r.value().samples[i], src[i], 2.0 / 32768);
}

TEST(wav_roundtrip_float) {
    auto src = sine(100);
    auto bytes = writeWav(src.data(), 100, 48000, true);
    auto r = readWav(bytes.data(), bytes.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK_EQ(r.value().rate, 48000u);
    for (size_t i = 0; i < 200; ++i) CHECK_EQ(r.value().samples[i], src[i]);
}

TEST(render_stops_at_loop) {
    Song s = testSong(4, 64, Quirks::PT23, 2);
    s.order = {0, 1};
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    std::vector<float> out;
    size_t frames = renderSong(s, 48000, Interp::None, false, 600.f, out);
    CHECK_NEAR((double)frames, 128.0 * 6 * 960, 960);
    CHECK_EQ(out.size(), frames * 2);
}

TEST(render_cli_terminates_on_infinite_song) {
    {
        Song s = testSong(4, 64);
        for (int r = 0; r < 64; ++r) s.patterns[0].at(r, 0) = {0, 0, 0, 0xB, 0x00};
        std::vector<float> out;
        size_t frames = renderSong(s, 48000, Interp::None, false, 600.f, out);
        CHECK(frames < 48000);
    }
    {
        Song s = testSong(4, 64);
        s.initialSpeed = 31;
        s.initialBpm = 32;
        std::vector<float> out;
        size_t frames = renderSong(s, 48000, Interp::None, false, 2.f, out);
        CHECK_NEAR((double)frames, 96000.0, 480);
    }
}

TEST(ref_modules_match) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(FT_SOURCE_DIR) / "tests" / "ref";
    int compared = 0;
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        for (auto& e : fs::directory_iterator(dir, ec)) {
            auto ext = e.path().extension().wstring();
            if (ext != L".mod" && ext != L".xm" && ext != L".MOD" && ext != L".XM") continue;
            fs::path ref = e.path();
            ref += L".ref.wav";
            if (!fs::exists(ref, ec)) continue;
            auto mod = readFile(e.path());
            auto wav = readFile(ref);
            auto song = loadModule(mod.data(), mod.size());
            auto w = readWav(wav.data(), wav.size());
            CHECK(song.ok() && w.ok());
            if (!song.ok() || !w.ok()) continue;
            std::vector<float> out;
            renderSong(song.value(), w.value().rate, Interp::None, false, 600.f, out);
            double corr = correlation(out.data(), out.size(), w.value().samples.data(), w.value().samples.size());
            std::printf("  ref %ls: corr=%.4f\n", e.path().filename().c_str(), corr);
            CHECK(corr >= 0.98);
            ++compared;
        }
    }
    if (compared == 0) std::printf("SKIP ref_modules_match (no reference files)\n");
}
