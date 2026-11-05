#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "audio/engine.h"
#include "player_util.h"
#include "test.h"

namespace {
std::unique_ptr<Song> noteSong() {
    auto s = std::make_unique<Song>(testSong(6, 64));
    s->patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    return s;
}
}  // namespace

TEST(engine_render_without_audio) {
    Engine e;
    e.setSong(noteSong());
    e.play();
    std::vector<float> buf(960 * 2);
    e.render(buf.data(), 960);
    float mx = 0;
    for (float v : buf) mx = v > mx ? v : mx;
    CHECK(mx > 0.f);
    Telemetry t;
    e.snapshot(t);
    CHECK(t.playing);
    CHECK_EQ(t.row, 0);
    CHECK_EQ(t.numChannels, 6);
    CHECK_EQ(t.playFrames, 960u);
    CHECK(t.ch[0].active);
    CHECK(t.ch[0].vu > 0.f);
}

TEST(engine_snapshot_consistent) {
    Engine e;
    e.setSong(noteSong());
    e.play();
    std::atomic<bool> bad{false};
    std::thread a([&] {
        std::vector<float> buf(480 * 2);
        for (int i = 0; i < 1000; ++i) e.render(buf.data(), 480);
    });
    std::thread b([&] {
        Telemetry t;
        for (int i = 0; i < 100000; ++i) {
            e.snapshot(t);
            if (t.numChannels != 6 || t.row >= 64) bad = true;
        }
    });
    a.join();
    b.join();
    CHECK(!bad);
}

TEST(engine_setsong_while_rendering) {
    Engine e;
    e.setSong(noteSong());
    e.play();
    std::atomic<bool> stopFlag{false};
    std::thread a([&] {
        std::vector<float> buf(480 * 2);
        while (!stopFlag) e.render(buf.data(), 480);
    });
    for (int i = 0; i < 100; ++i) {
        e.setSong(noteSong());
        e.play();
    }
    stopFlag = true;
    a.join();
    CHECK(e.song() != nullptr);
}

TEST(engine_audio_device_optional) {
    Engine e;  // no song: the device only receives silence
    if (e.startAudio()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        Telemetry t;
        e.snapshot(t);
        std::printf("  audio device open: %u Hz\n", t.rate);
        CHECK(e.audioStatus() == nullptr);
        CHECK(t.rate >= 8000);
    } else {
        std::printf("  audio device not available: %s\n", e.audioStatus());
        CHECK(e.audioStatus() != nullptr);
    }
}

TEST(engine_no_song_renders_silence) {
    Engine e;
    std::vector<float> buf(480 * 2, 1.f);
    e.render(buf.data(), 480);
    bool zero = true;
    for (float v : buf) zero = zero && v == 0.f;
    CHECK(zero);
}
