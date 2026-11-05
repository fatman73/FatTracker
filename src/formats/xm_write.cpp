// XM writer (FastTracker 2 format 0x0104), the native save format of FatTracker.
#include <cstring>

#include "formats/writers.h"

namespace {
struct W {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back((uint8_t)v); }
    void u16(int v) {
        u8(v);
        u8(v >> 8);
    }
    void u32(uint32_t v) {
        u16((int)(v & 0xFFFF));
        u16((int)(v >> 16));
    }
    void str(const char* s, int n) {
        int l = 0;
        while (l < n && s[l]) ++l;
        for (int i = 0; i < n; ++i) u8(i < l ? s[i] : 0);
    }
    void put32At(size_t pos, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[pos + i] = (uint8_t)(v >> (8 * i));
    }
};

void envelope(W& w, const Envelope& e) {
    for (int i = 0; i < 12; ++i) {
        w.u16(e.pts[i].tick);
        w.u16(e.pts[i].value);
    }
}

void sampleHeader(W& w, const Sample& s) {
    const uint32_t bps = s.is16 ? 2 : 1;
    w.u32(s.length * bps);
    w.u32(s.loop == LoopType::None ? 0 : s.loopStart * bps);
    w.u32(s.loop == LoopType::None ? 0 : s.loopLength * bps);
    w.u8(s.volume);
    w.u8((uint8_t)s.finetune);
    const uint8_t loop = s.loop == LoopType::Forward ? 1 : (s.loop == LoopType::PingPong ? 2 : 0);
    w.u8(loop | (s.is16 ? 0x10 : 0));
    w.u8(s.pan);
    w.u8((uint8_t)s.relativeNote);
    w.u8(0);
    w.str(s.name, 22);
}

void sampleData(W& w, const Sample& s) {
    if (s.is16) {
        int16_t old = 0;
        for (uint32_t i = 0; i < s.length; ++i) {
            const int16_t v = i < s.data16.size() ? s.data16[i] : 0;
            w.u16((uint16_t)(int16_t)(v - old));
            old = v;
        }
    } else {
        int8_t old = 0;
        for (uint32_t i = 0; i < s.length; ++i) {
            const int8_t v = i < s.data8.size() ? s.data8[i] : 0;
            w.u8((uint8_t)(int8_t)(v - old));
            old = v;
        }
    }
}

// One XM instrument with the given global samples; map[n] = local sample index.
void instrument(W& w, const Song& s, const char* name, const std::vector<int>& samples, const uint8_t* map,
                const Instrument* src) {
    const size_t start = w.b.size();
    w.u32(0);  // size, patched
    w.str(name, 22);
    w.u8(0);
    w.u16((int)samples.size());
    if (!samples.empty()) {
        w.u32(40);
        for (int n = 0; n < 96; ++n) w.u8(map[n]);
        Envelope none{};
        envelope(w, src ? src->volEnv : none);
        envelope(w, src ? src->panEnv : none);
        w.u8(src ? src->volEnv.count : 0);
        w.u8(src ? src->panEnv.count : 0);
        w.u8(src ? src->volEnv.sustain : 0);
        w.u8(src ? src->volEnv.loopStart : 0);
        w.u8(src ? src->volEnv.loopEnd : 0);
        w.u8(src ? src->panEnv.sustain : 0);
        w.u8(src ? src->panEnv.loopStart : 0);
        w.u8(src ? src->panEnv.loopEnd : 0);
        w.u8(src ? src->volEnv.flags : 0);
        w.u8(src ? src->panEnv.flags : 0);
        w.u8(src ? src->vibType : 0);
        w.u8(src ? src->vibSweep : 0);
        w.u8(src ? src->vibDepth : 0);
        w.u8(src ? src->vibRate : 0);
        w.u16(src ? src->fadeout : 0);
        for (int i = 0; i < 22; ++i) w.u8(0);  // reserved: header size 263 like FT2
    }
    w.put32At(start, (uint32_t)(w.b.size() - start));
    for (int idx : samples) sampleHeader(w, s.samples[idx]);
    for (int idx : samples) sampleData(w, s.samples[idx]);
}
}  // namespace

std::vector<uint8_t> saveXm(const Song& s) {
    W w;
    w.str("Extended Module: ", 17);
    w.str(s.name, 20);
    w.u8(0x1A);
    w.str("FatTracker", 20);
    w.u16(0x0104);
    w.u32(276);
    w.u16((int)s.order.size());
    w.u16(s.restartPos);
    w.u16(s.numChannels);
    w.u16((int)s.patterns.size());
    const bool useInstruments = !s.instruments.empty();
    const int numInst = useInstruments ? (int)s.instruments.size() : (int)s.samples.size();
    w.u16(numInst > 128 ? 128 : numInst);
    w.u16(s.freqMode == FreqMode::Linear ? 1 : 0);
    w.u16(s.initialSpeed);
    w.u16(s.initialBpm);
    for (int i = 0; i < 256; ++i) w.u8(i < (int)s.order.size() ? s.order[i] : 0);

    for (const Pattern& p : s.patterns) {
        w.u32(9);
        w.u8(0);
        w.u16(p.rows);
        const size_t sizePos = w.b.size();
        w.u16(0);
        const size_t start = w.b.size();
        for (int r = 0; r < p.rows; ++r)
            for (int c = 0; c < s.numChannels; ++c) {
                const Cell cell = c < p.channels ? p.at(r, c) : Cell{};
                uint8_t flags = 0x80;
                if (cell.note) flags |= 1;
                if (cell.instr) flags |= 2;
                if (cell.vol) flags |= 4;
                if (cell.fx) flags |= 8;
                if (cell.param) flags |= 16;
                w.u8(flags);
                if (flags & 1) w.u8(cell.note);
                if (flags & 2) w.u8(cell.instr);
                if (flags & 4) w.u8(cell.vol);
                if (flags & 8) w.u8(cell.fx);
                if (flags & 16) w.u8(cell.param);
            }
        const size_t packed = w.b.size() - start;
        w.b[sizePos] = (uint8_t)packed;
        w.b[sizePos + 1] = (uint8_t)(packed >> 8);
    }

    for (int i = 0; i < numInst && i < 128; ++i) {
        uint8_t map[96] = {};
        std::vector<int> samples;
        if (useInstruments) {
            const Instrument& in = s.instruments[i];
            for (int n = 0; n < 96; ++n) {
                const uint16_t g = in.sampleMap[n];
                if (g >= s.samples.size()) continue;
                int local = -1;
                for (size_t k = 0; k < samples.size(); ++k)
                    if (samples[k] == g) local = (int)k;
                if (local < 0 && samples.size() < 16) {
                    local = (int)samples.size();
                    samples.push_back(g);
                }
                map[n] = (uint8_t)(local < 0 ? 0 : local);
            }
            instrument(w, s, in.name, samples, map, &in);
        } else {
            if (s.samples[i].length > 0) samples.push_back(i);
            instrument(w, s, s.samples[i].name, samples, map, nullptr);
        }
    }
    return w.b;
}
