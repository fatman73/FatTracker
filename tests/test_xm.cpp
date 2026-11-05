#include <cstring>

#include "build_xm.h"
#include "formats/formats.h"
#include "test.h"

namespace {
XmInstSpec oneSampleInst(std::vector<int16_t> pcm, bool is16) {
    XmInstSpec in;
    XmSampleSpec s;
    s.pcm = static_cast<std::vector<int16_t>&&>(pcm);
    s.is16 = is16;
    in.samples.push_back(s);
    return in;
}
}  // namespace

TEST(xm_header_fields) {
    auto b = buildXm(12, {0, 1, 0}, {makePattern(64, 12), makePattern(32, 12)}, {}, true, 3, 150);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& s = r.value();
    CHECK_EQ(s.numChannels, 12);
    CHECK_EQ(s.order.size(), 3u);
    CHECK_EQ(s.initialSpeed, 3);
    CHECK_EQ(s.initialBpm, 150);
    CHECK(s.freqMode == FreqMode::Linear);
    CHECK(s.quirks == Quirks::FT2);
    CHECK_EQ(s.patterns[1].rows, 32);
}

TEST(xm_packed_pattern_roundtrip) {
    Pattern p = makePattern(64, 12);
    p.at(0, 0) = {49, 1, 0x10 + 40, 0xA, 0x0F};
    p.at(5, 11) = {NOTE_OFF, 0, 0, 0, 0};
    auto b = buildXm(12, {0}, {p}, {}, true, 6, 125);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Pattern& q = r.value().patterns[0];
    for (int row = 0; row < 64; ++row)
        for (int c = 0; c < 12; ++c) {
            const Cell& a = p.at(row, c);
            const Cell& z = q.at(row, c);
            CHECK(a.note == z.note && a.instr == z.instr && a.vol == z.vol && a.fx == z.fx && a.param == z.param);
        }
}

TEST(xm_16bit_delta_decode) {
    std::vector<int16_t> pcm = {0, 1000, -1000, 32767, -32768};
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {oneSampleInst(pcm, true)}, true, 6, 125);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& sm = r.value().samples[0];
    CHECK(sm.is16);
    CHECK_EQ(sm.length, 5u);
    for (int i = 0; i < 5; ++i) CHECK_EQ(sm.data16[i], pcm[i]);
}

TEST(xm_8bit_delta_decode) {
    std::vector<int16_t> pcm = {0, 10, -10, 127, -128};
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {oneSampleInst(pcm, false)}, true, 6, 125);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& sm = r.value().samples[0];
    CHECK(!sm.is16);
    CHECK_EQ(sm.length, 5u);
    for (int i = 0; i < 5; ++i) CHECK_EQ(sm.data8[i], pcm[i]);
}

TEST(xm_instrument_map_global_indices) {
    XmInstSpec a = oneSampleInst({1, 2}, false);
    a.samples.push_back(a.samples[0]);
    XmInstSpec bI = oneSampleInst({3}, false);
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {a, bI}, true, 6, 125);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK_EQ(r.value().samples.size(), 3u);
    CHECK_EQ(r.value().instruments.size(), 2u);
    CHECK_EQ(r.value().instruments[1].sampleMap[0], 2);
}

TEST(xm_envelope_fields) {
    XmInstSpec in = oneSampleInst({0}, false);
    in.vol.pts[0] = {0, 64};
    in.vol.pts[1] = {10, 32};
    in.vol.pts[2] = {20, 0};
    in.vol.count = 3;
    in.vol.sustain = 1;
    in.vol.flags = ENV_ON | ENV_SUSTAIN;
    in.fadeout = 0x200;
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {in}, true, 6, 125);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Instrument& i = r.value().instruments[0];
    CHECK_EQ(i.volEnv.count, 3);
    CHECK_EQ(i.volEnv.pts[1].tick, 10);
    CHECK_EQ(i.volEnv.pts[1].value, 32);
    CHECK_EQ(i.volEnv.sustain, 1);
    CHECK_EQ(i.volEnv.flags, ENV_ON | ENV_SUSTAIN);
    CHECK_EQ(i.fadeout, 0x200);
}

TEST(xm_truncated_sample_data) {
    std::vector<int16_t> pcm(1000, 5);
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {oneSampleInst(pcm, true)}, true, 6, 125);
    b.resize(b.size() - 1000);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& s = r.value();
    CHECK(s.loadFlags & LOAD_TRUNCATED);
    CHECK_EQ(s.samples[0].data16.size(), (size_t)s.samples[0].length);
    CHECK_EQ(s.samples[0].data16[999], 0);
}

TEST(xm_huge_counts_rejected) {
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {}, true, 6, 125);
    b[70] = 0xFF;
    b[71] = 0xFF;
    auto r = loadModule(b.data(), b.size());
    CHECK(!r.ok());
    if (!r.ok()) CHECK(std::strcmp(r.error(), "BAD XM HEADER") == 0);
}

TEST(xm_33_channels_rejected) {
    auto b = buildXm(4, {0}, {makePattern(64, 4)}, {}, true, 6, 125);
    b[68] = 33;
    auto r = loadModule(b.data(), b.size());
    CHECK(!r.ok());
    if (!r.ok()) CHECK(std::strcmp(r.error(), "UNSUPPORTED CHANNELS") == 0);
}
