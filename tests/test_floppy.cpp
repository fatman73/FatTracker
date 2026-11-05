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

TEST(floppy_read_lasts_five_seconds) {
    // user: loading a file plays the drive head for about 5 seconds, whatever the size
    for (uint32_t bytes : {1000u, 11264u * 3, 880u * 1024}) {
        Floppy f;
        f.setRate(48000);
        CHECK_NEAR(f.read(bytes), 5.0, 0.001);
        CHECK_NEAR(f.busySeconds(), 5.0, 0.001);
        runPeak(f, 1.0);
        CHECK_NEAR(f.busySeconds(), 4.0, 0.001);
    }
}

TEST(floppy_reads_do_not_queue) {
    Floppy f;
    f.setRate(48000);
    f.read(100000);
    runPeak(f, 1.0);
    CHECK_NEAR(f.read(100000), 5.0, 0.001);  // a new load restarts the access, it does not wait
    for (int i = 0; i < 10; ++i) f.dir();     // folder reads while busy are ignored
    CHECK_NEAR(f.busySeconds(), 5.0, 0.001);
}

TEST(floppy_cancel_stops_the_access) {
    Floppy f;
    f.setRate(48000);
    f.read(100000);
    runPeak(f, 0.7);
    f.cancel();
    CHECK(f.busySeconds() == 0.0);
    runPeak(f, 3.0);
    CHECK(!f.audible());
}

TEST(floppy_rate_change_keeps_remaining_time) {
    Floppy f;
    f.setRate(48000);
    f.read(1000);
    runPeak(f, 1.0);
    f.setRate(44100);
    CHECK_NEAR(f.busySeconds(), 4.0, 0.001);
}

TEST(floppy_rejects_absurd_user_sound) {
    Floppy f;
    f.setRate(48000);
    Sample slow = constant8(64, 500000);
    slow.relativeNote = -96;  // ~33 Hz: would expand 1500x
    f.setSounds(nullptr, &slow, nullptr);
    CHECK(f.stepFrames() < 48000);  // built-in click used instead
    Sample longStep = constant8(64, 200000);  // 24 s at 8363 Hz: trimmed
    f.setSounds(nullptr, &longStep, nullptr);
    CHECK(f.stepFrames() <= 24000);
}

TEST(floppy_steps_do_not_pile_up) {
    Floppy f;
    f.setRate(48000);
    Sample silence = constant8(0, 1000), step = constant8(64, 2000);  // 0.24 s, overlaps many steps
    f.setSounds(&silence, &step, nullptr);
    f.dir();
    CHECK(runPeak(f, 1.5) <= 0.51f);
}

TEST(floppy_reset_silences_everything) {
    Floppy f;
    f.setRate(48000);
    f.insert();
    runPeak(f, 0.2);
    f.reset();
    CHECK(!f.audible());
    CHECK(f.busySeconds() == 0.0);
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
