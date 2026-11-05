#include <chrono>
#include <cmath>

#include "core/mixer.h"
#include "test.h"

namespace {
Sample make8(std::vector<int8_t> d, LoopType lt = LoopType::None, uint32_t ls = 0, uint32_t ll = 0) {
    Sample s;
    s.length = (uint32_t)d.size();
    s.data8 = static_cast<std::vector<int8_t>&&>(d);
    s.loop = lt;
    s.loopStart = ls;
    s.loopLength = ll;
    return s;
}

void setup(Mixer& m, Interp in = Interp::None) {
    m.setRamp(false);
    m.setInterp(in);
    m.setNumChannels(4);
}
}  // namespace

TEST(mixer_nearest_reproduces_sample) {
    Mixer m(48000);
    setup(m);
    Sample s = make8({0, 64, 127, -128});
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    float out[8];
    m.mix(out, 4);
    const float L[] = {0.f, 0.25f, 0.49609375f, -0.5f};
    for (int i = 0; i < 4; ++i) {
        CHECK_NEAR(out[i * 2], L[i], 1e-6);
        CHECK_NEAR(out[i * 2 + 1], 0.f, 1e-6);
    }
}

TEST(mixer_half_speed_linear) {
    Mixer m(48000);
    setup(m, Interp::Linear);
    Sample s = make8({0, 100});
    m.trigger(0, &s, 0);
    m.setFrequency(0, 24000);
    m.setVolume(0, 1.f, 0.f);
    float out[4];
    m.mix(out, 2);
    CHECK_NEAR(out[2], 50.0 / 128 * 0.5, 1e-4);
}

TEST(mixer_forward_loop_wraps) {
    Mixer m(48000);
    setup(m);
    std::vector<int8_t> d;
    for (int i = 0; i < 10; ++i) d.push_back((int8_t)(i * 10));
    Sample s = make8(d, LoopType::Forward, 4, 6);
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    float out[32];
    m.mix(out, 16);
    const int idx[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 4, 5, 6, 7, 8, 9};
    for (int i = 0; i < 16; ++i) CHECK_NEAR(out[i * 2], idx[i] * 10 / 128.0 * 0.5, 1e-6);
}

TEST(mixer_pingpong_loop) {
    Mixer m(48000);
    setup(m);
    Sample s = make8({0, 10, 20, 30, 40, 50}, LoopType::PingPong, 2, 4);
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    float out[26];
    m.mix(out, 13);
    const int idx[] = {0, 1, 2, 3, 4, 5, 4, 3, 2, 3, 4, 5, 4};
    for (int i = 0; i < 13; ++i) CHECK_NEAR(out[i * 2], idx[i] * 10 / 128.0 * 0.5, 1e-6);
}

TEST(mixer_degenerate_loops) {
    Mixer m(48000);
    setup(m, Interp::Cubic);
    Sample a = make8({1, 2, 3, 4}, LoopType::Forward, 0, 0);
    Sample b = make8({1, 2, 3, 4}, LoopType::Forward, 3, 1);
    Sample c = make8({1, 2, 3, 4}, LoopType::Forward, 10, 4);
    Sample d = make8({1}, LoopType::PingPong, 0, 1);
    Sample e = make8({}, LoopType::Forward, 0, 0);
    Sample* all[] = {&a, &b, &c, &d, &e};
    for (int v = 0; v < 5; ++v) {
        m.trigger(v, all[v], 0);
        m.setFrequency(v, 12345.0 * (v + 1));
        m.setVolume(v, 1.f, 0.5f);
    }
    std::vector<float> buf(100000 * 2 + 16, 0.f);
    for (int i = 0; i < 16; ++i) buf[100000 * 2 + i] = 777.f;
    m.mix(buf.data(), 100000);
    bool finite = true;
    for (int i = 0; i < 100000 * 2; ++i) finite = finite && std::isfinite(buf[i]);
    CHECK(finite);
    for (int i = 0; i < 16; ++i) CHECK_EQ(buf[100000 * 2 + i], 777.f);
    for (int v = 0; v < 5; ++v) CHECK(!m.active(v));
}

TEST(mixer_offset_past_end) {
    Mixer m(48000);
    setup(m);
    Sample a = make8({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    m.trigger(0, &a, 1000);
    CHECK(!m.active(0));
    Sample b = make8({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, LoopType::Forward, 2, 8);
    m.trigger(1, &b, 1000);
    m.setFrequency(1, 48000);
    m.setVolume(1, 1.f, 0.f);
    float out[2];
    m.mix(out, 1);
    CHECK_NEAR(out[0], 3 / 128.0 * 0.5, 1e-6);
}

TEST(mixer_ramp_no_click) {
    Mixer m(48000);
    m.setInterp(Interp::None);
    m.setNumChannels(4);
    m.setRamp(true);
    Sample s = make8(std::vector<int8_t>(1000, 127));
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    float out[200];
    m.mix(out, 100);
    CHECK(std::fabs(out[0]) < 0.02f);
    CHECK_NEAR(out[48 * 2], 0.49609375, 1e-3);
}

TEST(mixer_pan_law) {
    Mixer m(48000);
    setup(m);
    Sample s = make8(std::vector<int8_t>(10, 64));
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.5f);
    float out[2];
    m.mix(out, 1);
    CHECK_NEAR(out[0], 0.5 * 0.5 * 0.70710678, 1e-4);
    CHECK_NEAR(out[1], 0.5 * 0.5 * 0.70710678, 1e-4);
}

TEST(mixer_vu_decay) {
    Mixer m(48000);
    setup(m);
    Sample s = make8(std::vector<int8_t>(48000, -128));
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    std::vector<float> buf(3840 * 2);
    m.mix(buf.data(), 480);
    CHECK_NEAR(m.vu(0), 1.0, 0.01);
    m.stopVoice(0);
    m.mix(buf.data(), 3840);
    CHECK_NEAR(m.vu(0), 0.5, 0.02);
}

TEST(mixer_softclip) {
    Mixer m(48000);
    setup(m);
    Sample full = make8(std::vector<int8_t>(100, 127));
    for (int v = 0; v < Mixer::kMaxVoices; ++v) {
        m.trigger(v, &full, 0);
        m.setFrequency(v, 48000);
        m.setVolume(v, 1.f, 0.f);
    }
    float out[20];
    m.mix(out, 10);
    for (int i = 0; i < 10; ++i) CHECK(std::fabs(out[i * 2]) <= 1.0f);

    Mixer m2(48000);
    setup(m2);
    Sample half = make8(std::vector<int8_t>(100, 64));
    for (int v = 0; v < 2; ++v) {
        m2.trigger(v, &half, 0);
        m2.setFrequency(v, 48000);
        m2.setVolume(v, 1.f, 0.f);
    }
    m2.mix(out, 1);
    CHECK_NEAR(out[0], 0.5, 1e-6);
}

TEST(mixer_scope_returns_last_samples) {
    Mixer m(48000);
    setup(m);
    std::vector<int8_t> d(2000);
    for (int i = 0; i < 2000; ++i) d[i] = (int8_t)((i % 256) - 128);
    Sample s = make8(d);
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    std::vector<float> buf(4000);
    m.mix(buf.data(), 2000);
    float sc[4];
    m.scope(0, sc, 4);
    for (int k = 0; k < 4; ++k) CHECK_NEAR(sc[k], d[1996 + k] / 128.0, 1e-6);
}

TEST(mixer_reset_drops_all_voices) {
    Mixer m(48000);
    m.setInterp(Interp::None);
    m.setNumChannels(4);
    m.setRamp(true);
    Sample s = make8(std::vector<int8_t>(1000, 100));
    m.trigger(0, &s, 0);
    m.setFrequency(0, 48000);
    m.setVolume(0, 1.f, 0.f);
    float buf[200];
    m.mix(buf, 100);
    m.trigger(0, &s, 0);  // creates a fading ghost of the previous note
    m.stopVoice(0);       // voice keeps ramping down
    m.reset();            // must drop voices and ghosts immediately (song is about to be freed)
    m.mix(buf, 100);
    bool silent = true;
    for (float v : buf) silent = silent && v == 0.f;
    CHECK(silent);
    CHECK(!m.active(0));
}

TEST(mixer_perf_32_voices_cubic) {
#ifdef NDEBUG
    Mixer m(48000);
    m.setInterp(Interp::Cubic);
    m.setRamp(true);
    m.setNumChannels(32);
    Sample s;
    s.is16 = true;
    s.length = 100000;
    s.data16.resize(100000);
    for (int i = 0; i < 100000; ++i) s.data16[i] = (int16_t)(std::sin(i * 0.01) * 20000);
    s.loop = LoopType::Forward;
    s.loopStart = 1000;
    s.loopLength = 99000;
    for (int v = 0; v < 32; ++v) {
        m.trigger(v, &s, 0);
        m.setFrequency(v, 8000.0 + v * 1500.0);
        m.setVolume(v, 0.8f, v / 31.f);
    }
    std::vector<float> buf(480 * 2);
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 1000; ++b) m.mix(buf.data(), 480);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  mixer perf: 10 s of 32 voices in %.3f s\n", sec);
    CHECK(sec < 0.5);
#endif
}
