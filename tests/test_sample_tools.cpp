// Sample editor tools: resample, tuning, mix, filters, echo, boost, drawing, recording.
#include <cmath>
#include <vector>

#include "core/render.h"
#include "core/sample_edit.h"
#include "test.h"

namespace {
constexpr double kPi = 3.14159265358979323846;

Sample sine8(uint32_t len, double period, int amp = 100) {
    Sample s;
    s.length = len;
    s.data8.resize(len);
    for (uint32_t i = 0; i < len; ++i) s.data8[i] = (int8_t)std::lround(amp * std::sin(2 * kPi * i / period));
    return s;
}

Sample flat8(uint32_t len, int v) {
    Sample s;
    s.length = len;
    s.data8.assign(len, (int8_t)v);
    return s;
}

int peak(const Sample& s, uint32_t a, uint32_t b) {
    int p = 0;
    for (uint32_t i = a; i < b; ++i) p = std::max(p, std::abs(s.is16 ? (int)s.data16[i] : (int)s.data8[i]));
    return p;
}
}  // namespace

TEST(sample_rate_roundtrip) {
    Sample s;
    for (double hz : {8363.0, 11025.0, 16726.0, 22050.0, 44100.0, 4181.5}) {
        sampleSetRate(s, hz);
        CHECK_NEAR(sampleRateHz(s), hz, hz * 0.002);
    }
}

TEST(sample_resample_keeps_pitch) {
    Sample s = sine8(1000, 50);
    s.loop = LoopType::Forward;
    s.loopStart = 100;
    s.loopLength = 400;
    sampleSetRate(s, 8363);
    sampleResample(s, 2.0);
    CHECK_EQ(s.length, 2000u);
    CHECK_NEAR(sampleRateHz(s), 16726.0, 16726.0 * 0.003);  // twice the rate: same pitch
    CHECK_EQ(s.loopStart, 200u);
    CHECK_EQ(s.loopLength, 800u);
    CHECK_NEAR(sampleDetectPitch(s, 0, s.length), 1.0 / 100, 0.0005);  // a period is now 100 samples
    sampleResample(s, 0.5);
    CHECK_EQ(s.length, 1000u);
    CHECK_NEAR(sampleRateHz(s), 8363.0, 8363.0 * 0.003);
}

TEST(sample_detect_pitch_and_tune_to_c) {
    Sample s = sine8(4000, 40);
    CHECK_NEAR(sampleDetectPitch(s, 0, s.length), 1.0 / 40, 0.0005);
    CHECK(sampleTuneToC(s));
    // C-4 plays the sample at sampleRateHz(s): the sine then sounds at rate / 40 = C (261.63 Hz)
    CHECK_NEAR(sampleRateHz(s) / 40.0, 261.63, 261.63 * 0.01);
    Sample noise = flat8(4000, 0);
    CHECK(!sampleTuneToC(noise));  // silence has no pitch
}

TEST(sample_mix_adds_and_extends) {
    Sample s = flat8(100, 100);
    const Sample clip = flat8(30, 50);
    sampleMix(s, 90, clip);
    CHECK_EQ(s.length, 120u);       // the clip runs past the end: the sample grows
    CHECK_EQ(s.data8[50], 100);     // before the clip: untouched
    CHECK_EQ(s.data8[95], 127);     // 100 + 50 saturates
    CHECK_EQ(s.data8[110], 50);     // past the old end: only the clip
}

TEST(sample_filters) {
    Sample alt = flat8(400, 0);
    for (uint32_t i = 0; i < 400; ++i) alt.data8[i] = (int8_t)(i & 1 ? 100 : -100);
    sampleLowPass(alt, 0, 400);
    CHECK(peak(alt, 200, 400) < 60);  // the highest frequency is damped
    Sample dc = flat8(4000, 100);
    sampleHighPass(dc, 0, 4000);
    CHECK(peak(dc, 3000, 4000) < 20);  // a constant offset is removed
}

TEST(sample_echo_repeats) {
    Sample s = flat8(4000, 0);
    sampleSetRate(s, 8000);
    s.data8[0] = 100;
    sampleEcho(s, 0, s.length);
    CHECK_EQ(s.data8[0], 100);
    CHECK_NEAR(s.data8[1000], 40, 2);  // 1/8 s later at 40 %
    CHECK_NEAR(s.data8[2000], 16, 2);  // and again
}

TEST(sample_boost_and_draw) {
    Sample s = flat8(10, 50);
    s.data8[5] = 120;
    sampleBoost(s, 0, 10);
    CHECK_NEAR(s.data8[0], 95, 8);  // about +6 dB
    CHECK(s.data8[5] <= 127 && s.data8[5] > 110);
    Sample d = flat8(40, 0);
    sampleDraw(d, 10, 0, 20, 100);
    CHECK_NEAR(d.data8[15], 50, 2);
    CHECK_EQ(d.data8[20], 100);
    sampleDraw(d, 30, 200, 30, 200);  // clamped to the 8-bit range
    CHECK_EQ(d.data8[30], 127);
}

TEST(sample_from_recording) {
    std::vector<float> rec(48000);
    for (size_t i = 0; i < rec.size(); ++i) rec[i] = 0.5f * (float)std::sin(2 * kPi * 440.0 * i / 48000.0);
    const Sample s = sampleFromRecording(rec.data(), rec.size(), 48000);
    CHECK(s.is16);
    CHECK_EQ(s.length, 48000u);
    CHECK_NEAR(sampleRateHz(s), 48000.0, 48000.0 * 0.003);
    CHECK_NEAR(peak(s, 0, s.length), 16384, 200);
}

TEST(sample_tools_on_empty_sample) {
    Sample e;
    sampleResample(e, 2.0);
    sampleTuneToC(e);
    sampleMix(e, 0, Sample{});
    sampleLowPass(e, 0, 0);
    sampleHighPass(e, 0, 0);
    sampleEcho(e, 0, 0);
    sampleBoost(e, 0, 0);
    sampleDraw(e, 0, 0, 5, 5);
    CHECK_EQ(e.length, 0u);
}

TEST(sample_bake_rate_for_protracker) {
    // ProTracker songs ignore the relative note: the rate has to live in the data
    Sample s = sine8(1000, 50);
    sampleSetRate(s, 16726);  // one octave up
    sampleBakeRate(s);
    CHECK_EQ(s.relativeNote, 0);
    CHECK_EQ(s.finetune, 0);
    CHECK_NEAR(s.length, 500, 2);  // half as many samples per second at 8363 Hz: same pitch
    CHECK_NEAR(sampleDetectPitch(s, 0, s.length), 1.0 / 25, 0.001);
}

TEST(sample_tune_uses_the_selection) {
    // silence first, then the note: tuning the selected note must work
    Sample s = flat8(30000, 0);
    for (uint32_t i = 20000; i < 30000; ++i) s.data8[i] = (int8_t)std::lround(100 * std::sin(2 * kPi * i / 40.0));
    CHECK(sampleTuneToC(s, 20000, 30000));
    CHECK_NEAR(sampleRateHz(s) / 40.0, 261.63, 261.63 * 0.01);
}

TEST(sample_echo_tail_runs_to_the_end) {
    Sample s = flat8(4000, 0);
    sampleSetRate(s, 8000);
    s.data8[0] = 100;
    sampleEcho(s, 0, 10);  // only the hit is selected: the echoes still ring out
    CHECK_NEAR(s.data8[1000], 40, 2);
}

TEST(pattern_renders_to_a_sample) {
    // PAT2SMP: one pass through a 4 row pattern at speed 6 / 125 BPM, mono 16 bit at the asked rate
    Song s = makeEmptySong(4);
    s.patterns[0] = makePattern(4, 4);
    s.order = {0};
    s.samples.resize(1);
    s.samples[0] = sine8(64, 16);
    s.samples[0].loop = LoopType::Forward;
    s.samples[0].loopStart = 0;
    s.samples[0].loopLength = 64;
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    const Sample out = renderPatternSample(s, 0, 16726);
    CHECK(out.is16);
    const double frames = 4 * 6 * 16726 * 2.5 / 125;
    CHECK_NEAR((double)out.length, frames, 6 * 16726 * 2.5 / 125);
    CHECK(peak(out, 0, out.length) > 1000);
    CHECK_EQ(out.relativeNote, 0);
    Song empty = makeEmptySong(4);
    CHECK_EQ(renderPatternSample(empty, 5, 16726).length, 0u);  // no such pattern
}
