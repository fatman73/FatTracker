// Stage 3: Amiga floppy drive sounds (Pure mode).
#include <cmath>
#include <vector>

#include "core/floppy.h"
#include "test.h"

namespace {
float runPeak(Floppy& f, double seconds, float gain = 1.f) {
    const uint32_t frames = (uint32_t)(seconds * 48000);
    std::vector<float> buf(2 * 4800);
    float peak = 0;
    for (uint32_t done = 0; done < frames; done += 4800) {
        const uint32_t n = frames - done < 4800 ? frames - done : 4800;
        std::fill(buf.begin(), buf.end(), 0.f);
        f.mix(buf.data(), n, gain);
        for (uint32_t i = 0; i < 2 * n; ++i) peak = std::fabs(buf[i]) > peak ? std::fabs(buf[i]) : peak;
    }
    return peak;
}

Sample constant8(int8_t v, uint32_t len) {
    Sample s;
    s.length = len;
    s.data8.assign(len, v);
    return s;
}
}  // namespace

TEST(floppy_read_time_matches_model) {
    Floppy f;
    f.setRate(48000);
    // spin-up 0.5 + 40 steps to the directory (3 ms) + settle 15 ms + one revolution 0.2
    // + step to cylinder 41 (3 + 15 ms) + 3 cylinders x 0.4 s + 2 more steps (3 + 15 ms)
    const double expect = 0.5 + 0.12 + 0.015 + 0.2 + 0.018 + 1.2 + 2 * 0.018;
    CHECK_NEAR(f.read(11264 * 3), expect, 0.005);
    CHECK_NEAR(f.busySeconds(), expect, 0.005);
    runPeak(f, 1.0);
    CHECK_NEAR(f.busySeconds(), expect - 1.0, 0.005);
}

TEST(floppy_makes_sound_then_falls_silent) {
    Floppy f;
    f.setRate(48000);
    const double t = f.read(1000);
    CHECK(runPeak(f, t) > 0.01f);
    runPeak(f, 3.0);  // motor stops 2 s after the last access and spins down
    CHECK(!f.audible());
    CHECK(runPeak(f, 0.5) == 0.f);
}

TEST(floppy_user_step_sample_used) {
    Floppy f;
    f.setRate(48000);
    Sample silence = constant8(0, 1000), step = constant8(64, 10);  // 10 frames at 8363 Hz ~ 1.2 ms
    f.setSounds(&silence, &step, nullptr);
    f.dir();
    CHECK_NEAR(runPeak(f, 1.0), 0.5, 0.01);
}

TEST(floppy_read_is_capped) {
    Floppy f;
    f.setRate(48000);
    CHECK(f.read(880 * 1024) <= 12.0 + 1e-6);
}

TEST(floppy_gain_scales_output) {
    Floppy a, b;
    a.setRate(48000);
    b.setRate(48000);
    a.dir();
    b.dir();
    const float pa = runPeak(a, 1.0, 1.f), pb = runPeak(b, 1.0, 0.5f);
    CHECK(pa > 0.f);
    CHECK_NEAR(pb, pa * 0.5f, 1e-4);
}
