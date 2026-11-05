#include <cstring>

#include "core/sample_edit.h"
#include "formats/sample_io.h"
#include "test.h"

namespace {
Sample s8(std::vector<int8_t> d) {
    Sample s;
    s.length = (uint32_t)d.size();
    s.data8 = static_cast<std::vector<int8_t>&&>(d);
    return s;
}
std::vector<uint8_t> wav(int bits, int channels, uint32_t rate, const std::vector<int>& frames) {
    std::vector<uint8_t> b;
    auto u16 = [&](int v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16((int)(v & 0xFFFF)); u16((int)(v >> 16)); };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    const uint32_t bps = bits / 8, data = (uint32_t)frames.size() * bps;
    tag("RIFF"); u32(36 + data); tag("WAVE"); tag("fmt "); u32(16); u16(1); u16(channels); u32(rate);
    u32(rate * channels * bps); u16(channels * bps); u16(bits); tag("data"); u32(data);
    for (int v : frames) {
        if (bits == 8) b.push_back((uint8_t)(v + 128));
        else if (bits == 16) u16(v & 0xFFFF);
        else { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)(v >> 16)); }
    }
    return b;
}
}  // namespace

TEST(sample_cut_copy_paste) {
    Sample s = s8({0, 1, 2, 3, 4, 5, 6, 7});
    s.loop = LoopType::Forward;
    s.loopStart = 6;
    s.loopLength = 2;
    Sample clip = sampleCut(s, 2, 4);
    CHECK_EQ(s.length, 6u);
    CHECK(s.data8 == (std::vector<int8_t>{0, 1, 4, 5, 6, 7}));
    CHECK_EQ(s.loopStart, 4u);  // loop moved with its data
    CHECK_EQ(clip.length, 2u);
    samplePaste(s, 1, clip);
    CHECK(s.data8 == (std::vector<int8_t>{0, 2, 3, 1, 4, 5, 6, 7}));
    CHECK_EQ(s.loopStart, 6u);
}

TEST(sample_paste_converts_bit_depth) {
    Sample s = s8({10, 20});
    Sample clip;
    clip.is16 = true;
    clip.length = 1;
    clip.data16 = {-32768};
    samplePaste(s, 2, clip);
    CHECK_EQ(s.length, 3u);
    CHECK_EQ(s.data8[2], -128);
}

TEST(sample_crop_reverse_volume_normalize) {
    Sample s = s8({0, 10, 20, 30, 40, 50});
    sampleCrop(s, 1, 5);
    CHECK(s.data8 == (std::vector<int8_t>{10, 20, 30, 40}));
    sampleReverse(s, 0, 4);
    CHECK(s.data8 == (std::vector<int8_t>{40, 30, 20, 10}));
    sampleVolume(s, 0, 4, 50.f, 50.f);
    CHECK(s.data8 == (std::vector<int8_t>{20, 15, 10, 5}));
    sampleNormalize(s, 0, 4);
    CHECK_EQ(s.data8[0], 127);
}

TEST(sample_bit_depth_conversion) {
    Sample s = s8({-128, 0, 127});
    sampleTo16(s);
    CHECK(s.is16);
    CHECK(s.data16 == (std::vector<int16_t>{-32768, 0, 32512}));
    sampleTo8(s);
    CHECK(!s.is16);
    CHECK(s.data8 == (std::vector<int8_t>{-128, 0, 127}));
}

TEST(sample_ops_clamp_ranges) {
    Sample s = s8({1, 2, 3});
    sampleCut(s, 2, 100);
    CHECK_EQ(s.length, 2u);
    sampleReverse(s, 5, 0);  // swapped and clamped to [0, 2)
    CHECK(s.data8 == (std::vector<int8_t>{2, 1}));
    Sample empty;
    sampleNormalize(empty, 0, 0);
    CHECK_EQ(empty.length, 0u);
}

TEST(sample_load_wav_16_stereo_mix_and_pitch) {
    auto b = wav(16, 2, 16726, {1000, 3000, -2000, -4000});
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Sample& s = r.value();
    CHECK(s.is16);
    CHECK_EQ(s.length, 2u);
    CHECK_EQ(s.data16[0], 2000);
    CHECK_EQ(s.data16[1], -3000);
    CHECK_EQ(s.relativeNote, 12);  // 16726 Hz = one octave above 8363 Hz
    CHECK_EQ(s.finetune, 0);
}

TEST(sample_load_wav_8_and_24) {
    auto b8 = wav(8, 1, 8363, {-128, 0, 127});
    auto r8 = loadSampleFile(b8.data(), b8.size());
    CHECK(r8.ok() && !r8.value().is16 && r8.value().data8[0] == -128 && r8.value().data8[2] == 127);
    auto b24 = wav(24, 1, 8363, {0x7FFFFF, -0x800000});
    auto r24 = loadSampleFile(b24.data(), b24.size());
    CHECK(r24.ok() && r24.value().is16 && r24.value().data16[0] == 32767 && r24.value().data16[1] == -32768);
}

TEST(sample_8svx_and_wav_roundtrip) {
    Sample s = s8({0, 50, -50, 100});
    s.loop = LoopType::Forward;
    s.loopStart = 2;
    s.loopLength = 2;
    s.volume = 32;
    auto iff = saveSample8svx(s);
    auto r = loadSampleFile(iff.data(), iff.size());
    CHECK(r.ok());
    if (r.ok()) {
        CHECK(r.value().data8 == s.data8);
        CHECK(r.value().loop == LoopType::Forward);
        CHECK_EQ(r.value().loopStart, 2u);
        CHECK_EQ(r.value().volume, 32);
    }
    auto w = saveSampleWav(s);
    auto r2 = loadSampleFile(w.data(), w.size());
    CHECK(r2.ok() && r2.value().data8 == s.data8 && r2.value().loopStart == 2u && r2.value().loop == LoopType::Forward);
}

TEST(sample_load_rejects_garbage) {
    std::vector<uint8_t> junk(100, 0x55);
    auto r = loadSampleFile(junk.data(), junk.size());
    CHECK(!r.ok());
    std::vector<uint8_t> trunc = wav(16, 1, 8363, {1, 2, 3});
    trunc.resize(30);
    auto r2 = loadSampleFile(trunc.data(), trunc.size());
    CHECK(!r2.ok());
}
