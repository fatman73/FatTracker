// Stage 5: TFMX playback (Turrican II mdat + smpl pairs from modules/).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/song.h"
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

TEST(tfmx_display_song_shows_all_tracks) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules" / "Chris Huelsbeck" / "TFMX";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        std::printf("SKIP tfmx_display (no Turrican files)\n");
        return;
    }
    const auto mdat = readAll(dir / L"mdat.turrican 2 level 0-intro"), smpl = readAll(dir / L"smpl.turrican 2 level 0-intro");
    TfmxPlayer p;
    CHECK(p.load(mdat.data(), mdat.size(), smpl.data(), smpl.size()));
    Song s;
    CHECK(p.buildDisplay(s));
    std::printf("  intro display: %d channels, %d patterns\n", s.numChannels, (int)s.patterns.size());
    CHECK(s.numChannels >= 7);  // the intro is TFMX 7V
    CHECK(s.patterns.size() > 2);
    CHECK_EQ(s.order.size(), s.patterns.size());
    int notes = 0;
    for (const Pattern& pat : s.patterns) {
        CHECK_EQ(pat.rows, 64);
        for (const Cell& c : pat.cells) notes += c.note ? 1 : 0;
    }
    CHECK(notes > 100);
    // the display follows playback: after some seconds the tick has moved on
    CHECK_EQ(p.tick(), 0);
    peakOf(p, 3.0);
    CHECK(p.tick() > 10);
}

TEST(tfmx_absurd_tempo_does_not_hang) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules" / "Chris Huelsbeck" / "TFMX";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    auto mdat = readAll(dir / L"mdat.turrican 2 level 1-desert");
    const auto smpl = readAll(dir / L"smpl.turrican 2 level 1-desert");
    for (int song = 0; song < 32; ++song) mdat[0x180 + 2 * song] = mdat[0x181 + 2 * song] = 0xFF;  // BPM 65535
    const auto t0 = std::chrono::steady_clock::now();
    TfmxPlayer p;
    p.load(mdat.data(), mdat.size(), smpl.data(), smpl.size());
    std::vector<float> buf(2 * 4800);
    p.render(buf.data(), 4800, 0.5f);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 2.0);  // loading must not spin for minutes
}

TEST(tfmx_display_columns_are_voices) {
    // a column is a Paula voice (what the VU meters and scopes show), not a TFMX track:
    // a track sends each note to its own channel, so tracks would put notes in the wrong column
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules" / "Chris Huelsbeck" / "TFMX";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (_wcsnicmp(name.c_str(), L"mdat.", 5) != 0) continue;
        const auto mdat = readAll(e.path()), smpl = readAll(dir / (L"smpl." + name.substr(5)));
        TfmxPlayer p;
        CHECK(p.load(mdat.data(), mdat.size(), smpl.data(), smpl.size()));
        Song s;
        if (!p.buildDisplay(s)) continue;
        const int voices = p.voices();
        if (s.numChannels > (voices > 4 ? voices : 4)) std::printf("  %ls: %d columns for %d voices\n", name.c_str(), s.numChannels, voices);
        CHECK(s.numChannels <= (voices > 4 ? voices : 4));
    }
}
