#include "build_xm.h"

#include <cstring>

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
        int l = (int)std::strlen(s);
        for (int i = 0; i < n; ++i) u8(i < l ? s[i] : 0);
    }
    void put32At(size_t pos, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[pos + i] = (uint8_t)(v >> (8 * i));
    }
};

void env(W& w, const Envelope& e) {
    for (int i = 0; i < 12; ++i) {
        w.u16(e.pts[i].tick);
        w.u16(e.pts[i].value);
    }
}
}  // namespace

std::vector<uint8_t> buildXm(int channels, const std::vector<uint8_t>& order, const std::vector<Pattern>& pats,
                             const std::vector<XmInstSpec>& insts, bool linear, uint8_t speed, uint8_t bpm) {
    W w;
    w.str("Extended Module: ", 17);
    w.str("xm test", 20);
    w.u8(0x1A);
    w.str("FatTracker test", 20);
    w.u16(0x0104);
    w.u32(276);
    w.u16((int)order.size());
    w.u16(0);
    w.u16(channels);
    w.u16((int)pats.size());
    w.u16((int)insts.size());
    w.u16(linear ? 1 : 0);
    w.u16(speed);
    w.u16(bpm);
    for (int i = 0; i < 256; ++i) w.u8(i < (int)order.size() ? order[i] : 0);

    for (const Pattern& p : pats) {
        w.u32(9);
        w.u8(0);
        w.u16(p.rows);
        size_t sizePos = w.b.size();
        w.u16(0);
        size_t start = w.b.size();
        for (int r = 0; r < p.rows; ++r) {
            for (int c = 0; c < channels; ++c) {
                const Cell& cell = p.at(r, c);
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
        }
        size_t packed = w.b.size() - start;
        w.b[sizePos] = (uint8_t)packed;
        w.b[sizePos + 1] = (uint8_t)(packed >> 8);
    }

    for (const XmInstSpec& in : insts) {
        size_t instStart = w.b.size();
        w.u32(0);  // patched below
        w.str("inst", 22);
        w.u8(0);
        w.u16((int)in.samples.size());
        if (!in.samples.empty()) {
            w.u32(40);
            for (int i = 0; i < 96; ++i) w.u8(in.noteMap[i]);
            env(w, in.vol);
            env(w, in.pan);
            w.u8(in.vol.count);
            w.u8(in.pan.count);
            w.u8(in.vol.sustain);
            w.u8(in.vol.loopStart);
            w.u8(in.vol.loopEnd);
            w.u8(in.pan.sustain);
            w.u8(in.pan.loopStart);
            w.u8(in.pan.loopEnd);
            w.u8(in.vol.flags);
            w.u8(in.pan.flags);
            w.u8(0);  // vib type
            w.u8(0);
            w.u8(0);
            w.u8(0);
            w.u16(in.fadeout);
            w.u16(0);
            for (int i = 0; i < 22; ++i) w.u8(0);
        }
        w.put32At(instStart, (uint32_t)(w.b.size() - instStart));
        for (const XmSampleSpec& s : in.samples) {
            uint32_t bps = s.is16 ? 2 : 1;
            w.u32((uint32_t)s.pcm.size() * bps);
            w.u32(s.loopStart * bps);
            w.u32(s.loopLen * bps);
            w.u8(s.vol);
            w.u8((uint8_t)s.finetune);
            uint8_t type = (uint8_t)s.loop | (s.is16 ? 0x10 : 0);
            w.u8(type);
            w.u8(s.pan);
            w.u8((uint8_t)s.relNote);
            w.u8(0);
            w.str("smp", 22);
        }
        for (const XmSampleSpec& s : in.samples) {
            int old = 0;
            for (int16_t v : s.pcm) {
                int d = v - old;
                old = v;
                if (s.is16)
                    w.u16(d & 0xFFFF);
                else
                    w.u8(d & 0xFF);
            }
        }
    }
    return w.b;
}
