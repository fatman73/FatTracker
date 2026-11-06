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

// AIFF / AIFF-C file in memory. `pcm` is the raw SSND data (already in the file's byte order).
// `compression` empty = plain AIFF; otherwise AIFF-C with that compression type.
std::vector<uint8_t> aiff(int channels, int bits, uint32_t rate, const std::vector<uint8_t>& pcm,
                          const char* compression = nullptr, uint32_t ssndOffset = 0) {
    std::vector<uint8_t> b;
    auto u16 = [&](uint32_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); };
    auto u32 = [&](uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    const bool aifc = compression != nullptr;
    const uint32_t frameBytes = (uint32_t)channels * ((bits + 7) / 8);
    const uint32_t frames = frameBytes ? (uint32_t)pcm.size() / frameBytes : 0;
    tag("FORM");
    u32(0);  // patched below
    tag(aifc ? "AIFC" : "AIFF");
    if (aifc) {
        tag("FVER");
        u32(4);
        u32(0xA2805140);
    }
    tag("COMM");
    u32(aifc ? 18 + 4 + 2 : 18);
    u16(channels);
    u32(frames);
    u16(bits);
    // 80-bit IEEE extended sample rate
    int e = 0;
    uint32_t r = rate;
    while (r > 1) { r >>= 1; ++e; }
    u16(16383 + e);
    const uint64_t mant = (uint64_t)rate << (63 - e);
    u32((uint32_t)(mant >> 32));
    u32((uint32_t)mant);
    if (aifc) {
        tag(compression);
        b.push_back(0);  // empty pascal string + pad byte
        b.push_back(0);
    }
    tag("SSND");
    u32(8 + ssndOffset + (uint32_t)pcm.size());
    u32(ssndOffset);
    u32(0);
    for (uint32_t i = 0; i < ssndOffset; ++i) b.push_back(0xEE);
    b.insert(b.end(), pcm.begin(), pcm.end());
    if (pcm.size() & 1) b.push_back(0);
    const uint32_t formLen = (uint32_t)b.size() - 8;
    b[4] = (uint8_t)(formLen >> 24); b[5] = (uint8_t)(formLen >> 16); b[6] = (uint8_t)(formLen >> 8); b[7] = (uint8_t)formLen;
    return b;
}

void appendBe16(std::vector<uint8_t>& b, int v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
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

TEST(sample_load_aiff_8_mono) {
    // AIFF 8-bit PCM is signed.
    auto b = aiff(1, 8, 8363, {0x80, 0x00, 0x7F, 0x10});
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Sample& s = r.value();
    CHECK(!s.is16);
    CHECK_EQ(s.length, 4u);
    CHECK(s.data8 == (std::vector<int8_t>{-128, 0, 127, 16}));
    CHECK_EQ(s.relativeNote, 0);
    CHECK_EQ(s.finetune, 0);
    CHECK(s.loop == LoopType::None);
}

TEST(sample_load_aiff_16_stereo_mix_and_pitch) {
    // big-endian frames: (1000, 3000), (-2000, -4000)
    std::vector<uint8_t> pcm;
    for (int v : {1000, 3000, -2000, -4000}) appendBe16(pcm, v & 0xFFFF);
    auto b = aiff(2, 16, 16726, pcm, nullptr, 4);  // with an SSND offset
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Sample& s = r.value();
    CHECK(s.is16);
    CHECK_EQ(s.length, 2u);
    CHECK_EQ(s.data16[0], 2000);
    CHECK_EQ(s.data16[1], -3000);
    CHECK_EQ(s.relativeNote, 12);
    CHECK_EQ(s.finetune, 0);
}

TEST(sample_load_aiff_24_bit) {
    auto b = aiff(1, 24, 44100, {0x7F, 0xFF, 0xFF, 0x80, 0x00, 0x00});
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok() && r.value().is16 && r.value().length == 2u && r.value().data16[0] == 32767 &&
          r.value().data16[1] == -32768);
}

TEST(sample_load_aifc_sowt_and_none) {
    std::vector<uint8_t> le;  // little-endian 16-bit
    for (int v : {1234, -1234, 32767}) { le.push_back((uint8_t)v); le.push_back((uint8_t)(v >> 8)); }
    auto b = aiff(1, 16, 22050, le, "sowt");
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok());
    if (r.ok()) {
        CHECK_EQ(r.value().length, 3u);
        CHECK(r.value().data16 == (std::vector<int16_t>{1234, -1234, 32767}));
    }
    std::vector<uint8_t> be;
    for (int v : {1234, -1234}) appendBe16(be, v & 0xFFFF);
    auto n = aiff(1, 16, 22050, be, "NONE");
    auto rn = loadSampleFile(n.data(), n.size());
    CHECK(rn.ok() && rn.value().data16 == (std::vector<int16_t>{1234, -1234}));
}

TEST(sample_load_aifc_float) {
    std::vector<uint8_t> pcm;
    for (float f : {0.5f, -0.25f}) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        for (int sh = 24; sh >= 0; sh -= 8) pcm.push_back((uint8_t)(u >> sh));
    }
    auto b = aiff(1, 32, 44100, pcm, "fl32");
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok());
    if (r.ok()) {
        CHECK(r.value().is16);
        CHECK_EQ(r.value().data16[0], 16384);
        CHECK_EQ(r.value().data16[1], -8192);
    }
}

TEST(sample_load_aiff_loop_from_mark_inst) {
    auto b = aiff(1, 8, 8363, {0, 1, 2, 3, 4, 5, 6, 7});
    // MARK: two markers, id 1 at frame 2, id 2 at frame 6 (the loop is frames 2..5)
    std::vector<uint8_t> mark;
    appendBe16(mark, 2);
    for (int m = 0; m < 2; ++m) {
        appendBe16(mark, m + 1);
        appendBe16(mark, 0);
        appendBe16(mark, m ? 6 : 2);
        mark.push_back(1);  // pascal string "A" (2 bytes: even)
        mark.push_back('A');
    }
    std::vector<uint8_t> inst = {60, 0, 0, 127, 1, 127};
    appendBe16(inst, 0);                                      // gain
    appendBe16(inst, 1); appendBe16(inst, 1); appendBe16(inst, 2);  // sustain loop: forward, markers 1..2
    appendBe16(inst, 0); appendBe16(inst, 0); appendBe16(inst, 0);  // release loop: none
    auto chunk = [&](const char* id, const std::vector<uint8_t>& body) {
        b.insert(b.end(), id, id + 4);
        const uint32_t n = (uint32_t)body.size();
        b.push_back((uint8_t)(n >> 24)); b.push_back((uint8_t)(n >> 16)); b.push_back((uint8_t)(n >> 8)); b.push_back((uint8_t)n);
        b.insert(b.end(), body.begin(), body.end());
    };
    chunk("MARK", mark);
    chunk("INST", inst);
    const uint32_t formLen = (uint32_t)b.size() - 8;
    b[4] = (uint8_t)(formLen >> 24); b[5] = (uint8_t)(formLen >> 16); b[6] = (uint8_t)(formLen >> 8); b[7] = (uint8_t)formLen;
    auto r = loadSampleFile(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK(r.value().loop == LoopType::Forward);
    CHECK_EQ(r.value().loopStart, 2u);
    CHECK_EQ(r.value().loopLength, 4u);
}

TEST(sample_load_aiff_rejects_bad_input) {
    // truncated in the COMM chunk
    auto good = aiff(1, 16, 8363, {0, 1, 0, 2});
    for (size_t cut = 12; cut < 40; ++cut) {
        std::vector<uint8_t> t(good.begin(), good.begin() + cut);
        CHECK(!loadSampleFile(t.data(), t.size()).ok());
    }
    // every prefix and random garbage after the header: no crash
    for (size_t cut = 0; cut <= good.size(); ++cut) (void)loadSampleFile(good.data(), cut);
    uint32_t seed = 12345;
    for (int i = 0; i < 200; ++i) {
        std::vector<uint8_t> g = good;
        for (size_t k = 12; k < g.size(); ++k) {
            seed = seed * 1103515245u + 12345u;
            if ((seed >> 16) % 4 == 0) g[k] = (uint8_t)(seed >> 24);
        }
        (void)loadSampleFile(g.data(), g.size());
    }
    // absurd chunk size, zero channels, bad bit depth, unknown compression, no SSND
    auto huge = good;
    huge[16] = 0xFF; huge[17] = 0xFF; huge[18] = 0xFF; huge[19] = 0xF0;  // COMM length
    CHECK(!loadSampleFile(huge.data(), huge.size()).ok());
    auto noCh = aiff(0, 16, 8363, {0, 1, 0, 2});
    CHECK(!loadSampleFile(noCh.data(), noCh.size()).ok());
    auto bits = aiff(1, 40, 8363, {0, 1, 0, 2, 0});
    CHECK(!loadSampleFile(bits.data(), bits.size()).ok());
    auto comp = aiff(1, 16, 8363, {0, 1, 0, 2}, "ima4");
    CHECK(!loadSampleFile(comp.data(), comp.size()).ok());
    auto empty = aiff(1, 16, 8363, {});
    CHECK(!loadSampleFile(empty.data(), empty.size()).ok());
    std::vector<uint8_t> junk = {'F', 'O', 'R', 'M', 0, 0, 0, 4, 'A', 'I', 'F', 'F'};
    CHECK(!loadSampleFile(junk.data(), junk.size()).ok());
}
