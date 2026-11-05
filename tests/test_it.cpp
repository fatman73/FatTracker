// Stage 4: Impulse Tracker (IT) - header, packed patterns, instruments / envelopes, IT214 / IT215 samples.
#include <cstring>
#include <vector>

#include "formats/formats.h"
#include "test.h"

namespace {
struct Out {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back((uint8_t)v); }
    void u16(int v) { u8(v), u8(v >> 8); }
    void u32(uint32_t v) { u16((int)(v & 0xFFFF)), u16((int)(v >> 16)); }
    void str(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) u8(i < std::strlen(s) ? s[i] : 0);
    }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
    void put32(size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[at + i] = (uint8_t)(v >> (8 * i));
    }
};

// LSB-first bit writer: the IT214 / IT215 compressed sample stream.
struct Bits {
    std::vector<uint8_t> b;
    int used = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            if (used % 8 == 0) b.push_back(0);
            if ((v >> i) & 1) b.back() |= (uint8_t)(1 << (used % 8));
            ++used;
        }
    }
    // one compressed block: byte count, then the bits
    std::vector<uint8_t> block() const {
        std::vector<uint8_t> out{(uint8_t)b.size(), (uint8_t)(b.size() >> 8)};
        out.insert(out.end(), b.begin(), b.end());
        return out;
    }
};

struct ItCell {
    int row, ch, note = -1, ins = 0, vol = -1, cmd = 0, param = 0;
};

// One 64-row pattern packed with a mask byte per cell; sameNoteRow uses the "last note" mask bit.
std::vector<uint8_t> itPattern(const std::vector<ItCell>& cells, int sameNoteRow) {
    Out p;
    for (int row = 0; row < 64; ++row) {
        for (const auto& c : cells) {
            if (c.row != row) continue;
            int mask = 0;
            if (c.note >= 0) mask |= row == sameNoteRow ? 16 : 1;
            if (c.ins) mask |= 2;
            if (c.vol >= 0) mask |= 4;
            if (c.cmd) mask |= 8;
            p.u8((c.ch + 1) | 0x80);
            p.u8(mask);
            if (mask & 1) p.u8(c.note);
            if (mask & 2) p.u8(c.ins);
            if (mask & 4) p.u8(c.vol);
            if (mask & 8) p.u8(c.cmd), p.u8(c.param);
        }
        p.u8(0);
    }
    return p.b;
}

struct Packed {
    std::vector<uint8_t> data;
    int frames = 0;
    bool is16 = false, it215 = false;
};

// IT: speed 4, tempo 150, global volume 128, linear slides, stereo; channel 0 hard left, channel 1 hard
// right. Sample 1: signed 8-bit, 32 frames (100 at frame 10), loop 4..20, sample global volume 32.
// Pattern 0: row 0 ch 0 C-5 sample 1 volume 32 A04; row 1 ch 2 note off C10 (hex: row 16);
// row 2 ch 0 "same note" + volume column fine up 3; row 3 ch 1 X80; row 4 ch 1 V80; row 5 ch 0 note cut.
// withInstrument: one new-format instrument, C-5 -> sample 2, the rest -> sample 1, volume envelope
// (0,64) (10,32) (20,0) with sustain on node 1, pan envelope -32 -> +32, fadeout 8.
// packed: sample 1 holds this compressed data instead.
std::vector<uint8_t> tinyIt(bool withInstrument = false, const Packed* packed = nullptr) {
    Out w;
    const int numIns = withInstrument ? 1 : 0, numSmp = withInstrument ? 2 : 1;
    w.str("IMPM", 4), w.str("tiny it", 26);
    w.u16(0x1004);
    w.u16(2), w.u16(numIns), w.u16(numSmp), w.u16(1);
    w.u16(0x0214), w.u16(0x0214);
    w.u16(1 | 8 | (withInstrument ? 4 : 0));  // stereo, linear slides, instruments
    w.u16(0);
    w.u8(128), w.u8(48), w.u8(4), w.u8(150), w.u8(128), w.u8(0);
    w.u16(0), w.u32(0), w.u32(0);
    for (int i = 0; i < 64; ++i) w.u8(i == 0 ? 0 : (i == 1 ? 64 : 32));
    for (int i = 0; i < 64; ++i) w.u8(64);
    w.u8(0), w.u8(255);
    const size_t offs = w.b.size();
    w.zeros(4 * (numIns + numSmp + 1));
    if (withInstrument) {
        w.put32(offs, (uint32_t)w.b.size());
        const size_t ins = w.b.size();
        w.str("IMPI", 4), w.str("tiny.iti", 13);
        w.u8(0), w.u8(0), w.u8(0), w.u16(8);
        w.u8(0), w.u8(60), w.u8(128), w.u8(32), w.u8(0), w.u8(0);
        w.u16(0x0214), w.u8(2), w.u8(0);
        w.str("lead", 26);
        w.u8(0), w.u8(0), w.u8(0), w.u8(0), w.u16(0);
        for (int n = 0; n < 120; ++n) w.u8(n), w.u8(n == 60 ? 2 : 1);
        w.u8(1 | 4), w.u8(3), w.u8(0), w.u8(0), w.u8(1), w.u8(1);
        const int vals[3] = {64, 32, 0}, ticks[3] = {0, 10, 20};
        for (int i = 0; i < 25; ++i) w.u8(i < 3 ? vals[i] : 0), w.u16(i < 3 ? ticks[i] : 0);
        w.u8(0);
        w.u8(1), w.u8(2), w.u8(0), w.u8(0), w.u8(0), w.u8(0);
        for (int i = 0; i < 25; ++i) w.u8(i == 0 ? -32 : (i == 1 ? 32 : 0)), w.u16(i == 1 ? 8 : 0);
        w.u8(0);
        w.zeros(82 + 4);
        CHECK_EQ(w.b.size() - ins, 554u);
    }
    std::vector<size_t> hdr;
    for (int k = 0; k < numSmp; ++k) {
        w.put32(offs + 4 * (numIns + k), (uint32_t)w.b.size());
        hdr.push_back(w.b.size());
        const bool comp = packed && k == 0;
        w.str("IMPS", 4), w.str("tiny.its", 13);
        w.u8(32);
        w.u8(1 | 0x10 | (comp ? 8 : 0) | (comp && packed->is16 ? 2 : 0));
        w.u8(48);
        w.str(k ? "second" : "first", 26);
        w.u8(1 | (comp && packed->it215 ? 4 : 0));
        w.u8(k ? 0x80 | 16 : 0);
        w.u32(comp ? (uint32_t)packed->frames : 32), w.u32(4), w.u32(20), w.u32(8363);
        w.u32(0), w.u32(0), w.u32(0);
        w.u8(0), w.u8(0), w.u8(0), w.u8(0);
    }
    w.put32(offs + 4 * (numIns + numSmp), (uint32_t)w.b.size());
    const auto pat = itPattern({{0, 0, 60, 1, 32, 1, 4},
                                {1, 2, 255, 0, -1, 3, 0x10},
                                {2, 0, 60, 0, 65 + 3},
                                {3, 1, -1, 0, -1, 24, 0x80},
                                {4, 1, -1, 0, -1, 22, 0x80},
                                {5, 0, 254}},
                               2);
    w.u16((int)pat.size()), w.u16(64), w.u32(0);
    w.b.insert(w.b.end(), pat.begin(), pat.end());
    for (int k = 0; k < numSmp; ++k) {
        w.put32(hdr[k] + 72, (uint32_t)w.b.size());
        if (packed && k == 0) w.b.insert(w.b.end(), packed->data.begin(), packed->data.end());
        else
            for (int i = 0; i < 32; ++i) w.u8(i == 10 ? 100 : (k ? -20 : 0));
    }
    return w.b;
}

// 8-bit stream: 9-bit values 0 +10 +10 (mode C), switch to 3 bits, +3 -1 (mode A), escape back to 9 bits, -30.
Packed packed8(bool it215) {
    Bits b;
    b.put(0, 9), b.put(10, 9), b.put(10, 9);
    b.put(0x100 | 2, 9);       // mode C: width = 2 + 1
    b.put(3, 3), b.put(7, 3);  // +3, -1
    b.put(4, 3), b.put(7, 3);  // mode A escape: width 7 + 1 -> 8, >= 3 -> 9
    b.put(256 - 30, 9);        // -30
    return {b.block(), 6, false, it215};
}

const Sample* loadSample0(const std::vector<uint8_t>& f, Song& keep) {
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok() || r.value().samples.empty()) return nullptr;
    keep = static_cast<Song&&>(r.value());
    return &keep.samples[0];
}
}  // namespace

TEST(it_sample_mode_mapping) {
    const auto f = tinyIt();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK(std::strcmp(s.name, "tiny it") == 0);
    CHECK_EQ(s.numChannels, 4);
    CHECK_EQ(s.initialSpeed, 4);
    CHECK_EQ(s.initialBpm, 150);
    CHECK_EQ(s.globalVolume, 64);
    CHECK(s.freqMode == FreqMode::Linear);
    CHECK(s.instruments.empty());
    CHECK_EQ(s.order.size(), 1u);
    CHECK_EQ(s.channelPan[0], 0);
    CHECK_EQ(s.channelPan[1], 255);
    const auto& p = s.patterns[0];
    CHECK_EQ(p.at(0, 0).note, 49);  // IT C-5 plays at the sample's C-5 speed: our C-4
    CHECK_EQ(p.at(0, 0).instr, 1);
    CHECK_EQ(p.at(0, 0).vol, 0x10 + 32);
    CHECK_EQ(p.at(0, 0).fx, 0xF);
    CHECK_EQ(p.at(0, 0).param, 4);
    CHECK_EQ(p.at(1, 2).note, NOTE_OFF);
    CHECK_EQ(p.at(1, 2).fx, 0xD);
    CHECK_EQ(p.at(1, 2).param, 0x16);  // IT breaks are hex, the player reads BCD
    CHECK_EQ(p.at(2, 0).note, 49);     // "same note as last time"
    CHECK_EQ(p.at(2, 0).vol, 0x93);    // fine volume up 3
    CHECK_EQ(p.at(3, 1).fx, 0x8);
    CHECK_EQ(p.at(3, 1).param, 0x80);
    CHECK_EQ(p.at(4, 1).fx, 16);  // global volume: IT 0..128 -> 0..64
    CHECK_EQ(p.at(4, 1).param, 0x40);
    CHECK_EQ(p.at(5, 0).note, 0);  // note cut -> EC0
    CHECK_EQ(p.at(5, 0).fx, 0xE);
    CHECK_EQ(p.at(5, 0).param, 0xC0);
    if (s.samples.empty()) return;
    const Sample& sm = s.samples[0];
    CHECK(std::strcmp(sm.name, "first") == 0);
    CHECK_EQ(sm.length, 32u);
    CHECK_EQ(sm.volume, 48);
    CHECK_EQ(sm.data8[10], 50);  // sample global volume 32 / 64 is baked into the data
    CHECK(sm.loop == LoopType::Forward);
    CHECK_EQ(sm.loopStart, 4u);
    CHECK_EQ(sm.loopLength, 16u);
    CHECK_EQ(sm.relativeNote, 0);
    CHECK(!sm.hasPan);
}

TEST(it_instrument_mode_mapping) {
    const auto f = tinyIt(true);
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK_EQ(s.instruments.size(), 1u);
    CHECK_EQ(s.samples.size(), 2u);
    if (s.instruments.size() != 1 || s.samples.size() != 2) return;
    const Instrument& in = s.instruments[0];
    CHECK(std::strcmp(in.name, "lead") == 0);
    CHECK_EQ(in.sampleMap[49 - 1], 1);  // IT C-5 -> sample 2
    CHECK_EQ(in.sampleMap[0], 0);
    CHECK_EQ(in.fadeout, 8 * 32);
    CHECK_EQ(in.volEnv.count, 3);
    CHECK_EQ(in.volEnv.flags, ENV_ON | ENV_SUSTAIN);
    CHECK_EQ(in.volEnv.sustain, 1);
    CHECK_EQ(in.volEnv.pts[1].tick, 10);
    CHECK_EQ(in.volEnv.pts[1].value, 32);
    CHECK_EQ(in.panEnv.count, 2);
    CHECK_EQ(in.panEnv.pts[0].value, 0);   // -32
    CHECK_EQ(in.panEnv.pts[1].value, 64);  // +32
    CHECK_EQ(in.panEnv.pts[1].tick, 8);
    CHECK(s.samples[1].hasPan);
    CHECK_EQ(s.samples[1].pan, 64);
}

TEST(it214_8bit_decompression) {
    const Packed pk = packed8(false);
    Song keep;
    const Sample* s = loadSample0(tinyIt(false, &pk), keep);
    if (!s) return;
    CHECK_EQ(s->length, 6u);
    const int want[6] = {0, 10, 20, 23, 22, -8};
    for (int i = 0; i < 6 && i < (int)s->data8.size(); ++i) CHECK_NEAR(s->data8[i] * 2, want[i], 1);  // gv 32: halved
}

TEST(it215_8bit_decompression) {
    const Packed pk = packed8(true);  // second integration: 0 10 30 53 75 323 (wraps to 67)
    Song keep;
    const Sample* s = loadSample0(tinyIt(false, &pk), keep);
    if (!s) return;
    const int want[6] = {0, 10, 30, 53, 75, 67};
    for (int i = 0; i < 6 && i < (int)s->data8.size(); ++i) CHECK_NEAR(s->data8[i] * 2, want[i], 1);
}

TEST(it214_16bit_decompression) {
    Bits b;
    b.put(1000, 17), b.put(65536 - 3000, 17);  // mode C: +1000, -3000
    const Packed pk{b.block(), 2, true, false};
    Song keep;
    const Sample* s = loadSample0(tinyIt(false, &pk), keep);
    if (!s) return;
    CHECK(s->is16);
    CHECK_EQ(s->length, 2u);
    if (s->data16.size() < 2) return;
    CHECK_EQ(s->data16[0], 500);  // gv 32: halved
    CHECK_EQ(s->data16[1], -1000);
}

TEST(it_survives_truncation_and_garbage) {
    const Packed pk = packed8(true);
    for (const auto& f : {tinyIt(), tinyIt(true), tinyIt(false, &pk)}) {
        for (size_t cut = 4; cut < f.size(); cut += 23) {
            auto r = loadModule(f.data(), cut);
            (void)r;
        }
        std::vector<uint8_t> g = f;
        for (size_t i = 200; i < g.size(); i += 7) g[i] ^= 0xA5;
        auto r = loadModule(g.data(), g.size());
        (void)r;
    }
}
