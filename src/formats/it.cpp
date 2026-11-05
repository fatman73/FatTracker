// Impulse Tracker (IT) loader: packed patterns, instruments with envelopes, IT214 / IT215 compressed samples.
// Structures and the sample decompression follow OpenMPT (BSD licence, OpenMPT Devs).
// Not carried over (the player has no equivalent): new note actions, pitch / filter envelopes, resonant
// filters, instrument / channel global volume, keyboard transposition, sample auto-vibrato.
#include <cstring>

#include "core/reader.h"
#include "formats/formats_internal.h"

namespace {
constexpr uint16_t kNoSample = 0xFFFF;
constexpr size_t kSampleBudget = (size_t)1 << 28;  // bytes of sample data per song

uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int itNote(int n) { return n - 11 < 1 ? 1 : (n - 11 > 96 ? 96 : n - 11); }  // IT C-5 (60) plays at C5 speed: our C-4

// ---- IT214 / IT215 decompression: blocks of 0x8000 bytes of output, each a u16 size + LSB-first bits
struct BitIn {
    const uint8_t* p;
    size_t n, bit = 0;
    bool read(int width, uint32_t& v) {
        if (bit + (size_t)width > n * 8) return false;
        v = 0;
        for (int i = 0; i < width; ++i, ++bit)
            if (p[bit >> 3] & (1 << (bit & 7))) v |= 1u << i;
        return true;
    }
};

// returns false when the data ran out before all frames were decoded
// work: block bytes the song may still decode (samples sharing data cannot multiply the work)
bool decompress(const uint8_t* data, size_t size, size_t pos, Sample& s, uint32_t frames, bool is16, bool it215,
                size_t& work) {
    const int defWidth = is16 ? 17 : 9, fetchA = is16 ? 4 : 3, lowerB = is16 ? -8 : -4, upperB = is16 ? 7 : 3;
    const uint32_t blockFrames = is16 ? 0x4000 : 0x8000;
    uint32_t written = 0;
    while (written < frames) {
        if (pos + 2 > size) return false;
        const size_t bytes = le16(data + pos);
        pos += 2;
        if (!bytes) continue;
        if (bytes > work) return false;
        work -= bytes;
        BitIn in{data + pos, pos + bytes > size ? size - pos : bytes};
        pos += bytes;
        uint32_t left = frames - written < blockFrames ? frames - written : blockFrames;
        const uint32_t startWritten = written;
        int width = defWidth;
        uint32_t mem1 = 0, mem2 = 0;
        auto write = [&](int v, int topBit) {
            if (v & topBit) v -= topBit << 1;
            mem1 += (uint32_t)v;
            mem2 += mem1;
            const uint32_t out = it215 ? mem2 : mem1;
            if (is16) s.data16[written] = (int16_t)(uint16_t)(out & 0xFFFF);
            else s.data8[written] = (int8_t)(uint8_t)(out & 0xFF);
            ++written;
            --left;
        };
        auto change = [&](int w) {
            ++w;
            if (w >= width) ++w;
            width = w;
        };
        while (left > 0 && width <= defWidth) {
            uint32_t uv;
            if (!in.read(width, uv)) break;
            const int v = (int)uv, topBit = 1 << (width - 1);
            if (width <= 6) {  // mode A
                if (v == topBit) {
                    uint32_t w;
                    if (!in.read(fetchA, w)) break;
                    change((int)w);
                } else {
                    write(v, topBit);
                }
            } else if (width < defWidth) {  // mode B
                if (v >= topBit + lowerB && v <= topBit + upperB) change(v - (topBit + lowerB));
                else write(v, topBit);
            } else {  // mode C
                if (v & topBit) width = (v & ~topBit) + 1;
                else write(v & ~topBit, 0);
            }
        }
        if (written == startWritten) return false;  // a block that decodes nothing is garbage
        if (pos > size) return false;
    }
    return true;
}

// IT envelope (up to 25 nodes) -> ours (12): keep evenly spread nodes, move loop / sustain to the nearest one
void convertEnvelope(Envelope& e, int flags, int num, const int* ticks, const int* vals, int lpb, int lpe, int slb) {
    e = Envelope{};
    if (num > 25) num = 25;
    if (num < 0) num = 0;
    int idx[12];
    const int keep = num > 12 ? 12 : num;
    for (int j = 0; j < keep; ++j) idx[j] = num > 12 ? (j * (num - 1) + 5) / 11 : j;
    auto nearest = [&](int node) {
        int best = 0;
        for (int j = 1; j < keep; ++j)
            if (std::abs(idx[j] - node) < std::abs(idx[best] - node)) best = j;
        return (uint8_t)best;
    };
    uint16_t last = 0;
    for (int j = 0; j < keep; ++j) {
        int t = ticks[idx[j]];
        if (t < last) t = last;  // ticks must not go backwards
        e.pts[j].tick = (uint16_t)t;
        e.pts[j].value = (uint8_t)(vals[idx[j]] < 0 ? 0 : (vals[idx[j]] > 64 ? 64 : vals[idx[j]]));
        last = (uint16_t)t;
    }
    e.count = (uint8_t)keep;
    e.flags = (uint8_t)(((flags & 1) ? ENV_ON : 0) | ((flags & 2) ? ENV_LOOP : 0) | ((flags & 4) ? ENV_SUSTAIN : 0));
    e.loopStart = nearest(lpb);
    e.loopEnd = nearest(lpe);
    e.sustain = nearest(slb);
    if (e.count == 0) e.flags = 0;
    if (lpb > lpe || lpe >= num) e.flags &= (uint8_t)~ENV_LOOP;
    if (slb >= num) e.flags &= (uint8_t)~ENV_SUSTAIN;
}

// one IT envelope block (new instrument format), pan / pitch values are -32..32
void readEnvelope(const uint8_t* p, Envelope& e, int offset) {
    int ticks[25], vals[25];
    for (int i = 0; i < 25; ++i) {
        vals[i] = (int8_t)p[6 + i * 3] + offset;
        ticks[i] = le16(p + 7 + i * 3);
    }
    convertEnvelope(e, p[0], p[1], ticks, vals, p[2], p[3], p[4]);
}

void loadInstrument(const uint8_t* p, bool oldFormat, int numSamples, Instrument& in) {
    pcName(in.name, p + 32, 26, 22);
    for (int k = 0; k < 120; ++k) {
        const int note = k - 11;
        if (note < 1 || note > 96) continue;
        const int smp = p[64 + k * 2 + 1];
        in.sampleMap[note - 1] = smp >= 1 && smp <= numSamples ? (uint16_t)(smp - 1) : kNoSample;
    }
    if (oldFormat) {  // IT 1.x: volume envelope only, nodes as (tick, value) until tick 0xFF
        const int fade = le16(p + 24);
        in.fadeout = (uint16_t)((fade > 512 ? 512 : fade) * 64);
        int ticks[25], vals[25], num = 0;
        for (; num < 25 && p[504 + num * 2] != 0xFF; ++num) {
            ticks[num] = p[504 + num * 2];
            vals[num] = p[505 + num * 2];
        }
        convertEnvelope(in.volEnv, p[17], num, ticks, vals, p[18], p[19], p[20]);
    } else {
        const int fade = le16(p + 20);
        in.fadeout = (uint16_t)((fade > 1024 ? 1024 : fade) * 32);
        readEnvelope(p + 304, in.volEnv, 0);
        readEnvelope(p + 386, in.panEnv, 32);
    }
}

struct PackedCell {
    int note = -1, ins = 0, vol = -1, cmd = 0, param = 0;
};

void convertCell(const PackedCell& pc, Cell& c) {
    bool cut = false;
    if (pc.note >= 0) {
        if (pc.note < 120) c.note = (uint8_t)itNote(pc.note);
        else if (pc.note == 254) cut = true;
        else c.note = NOTE_OFF;  // 255 note off, the rest note fade
    }
    c.instr = (uint8_t)pc.ins;
    if (pc.cmd) pcEffect(c, pc.cmd, pc.param, true);
    auto freeFx = [&](int fx, int p) {
        if (c.fx || c.param) return;
        c.fx = (uint8_t)fx;
        c.param = (uint8_t)p;
    };
    const int v = pc.vol;
    if (v >= 0 && v <= 64) c.vol = (uint8_t)(0x10 + v);
    else if (v >= 65 && v <= 74) c.vol = (uint8_t)(0x90 + v - 65);   // fine up
    else if (v >= 75 && v <= 84) c.vol = (uint8_t)(0x80 + v - 75);   // fine down
    else if (v >= 85 && v <= 94) c.vol = (uint8_t)(0x70 + v - 85);   // slide up
    else if (v >= 95 && v <= 104) c.vol = (uint8_t)(0x60 + v - 95);  // slide down
    else if (v >= 105 && v <= 114) freeFx(0x2, (v - 105) * 4);       // pitch down
    else if (v >= 115 && v <= 124) freeFx(0x1, (v - 115) * 4);       // pitch up
    else if (v >= 128 && v <= 192) c.vol = (uint8_t)(0xC0 + ((v - 128) * 4 > 255 ? 15 : (v - 128) / 4));
    else if (v >= 193 && v <= 202) {  // tone portamento
        static const int tab[10] = {0, 1, 4, 8, 16, 32, 64, 96, 128, 255};
        if (!c.fx && !c.param) freeFx(0x3, tab[v - 193]);
        else c.vol = (uint8_t)(0xF0 + (tab[v - 193] >> 4 > 15 ? 15 : tab[v - 193] >> 4));
    } else if (v >= 203 && v <= 212) {
        c.vol = (uint8_t)(0xB0 + v - 203);  // vibrato depth
    }
    if (cut) {
        if (!c.fx && !c.param) {
            c.fx = 0xE;
            c.param = 0xC0;
        } else {
            c.note = NOTE_OFF;
        }
    }
}

// Walks one packed pattern; cb(row, channel, cell) for every cell with data.
template <class F>
void walkPattern(const uint8_t* data, size_t size, uint32_t off, int& rowsOut, F cb) {
    rowsOut = 64;
    if (!off || (size_t)off + 8 > size) return;
    const size_t len = le16(data + off);
    int rows = le16(data + off + 2);
    rowsOut = rows < 1 ? 64 : (rows > 256 ? 256 : rows);
    size_t p = off + 8;
    const size_t end = p + len > size ? size : p + len;
    uint8_t mask[64] = {};
    PackedCell last[64];
    for (int row = 0; row < rowsOut && p < end;) {
        const int cv = data[p++];
        if (!cv) {
            ++row;
            continue;
        }
        const int ch = (cv - 1) & 63;
        if (cv & 0x80) {
            if (p >= end) break;
            mask[ch] = data[p++];
        }
        const int m = mask[ch];
        PackedCell c;
        if (m & 1) {
            if (p >= end) break;
            c.note = last[ch].note = data[p++];
        }
        if (m & 2) {
            if (p >= end) break;
            c.ins = last[ch].ins = data[p++];
        }
        if (m & 4) {
            if (p >= end) break;
            c.vol = last[ch].vol = data[p++];
        }
        if (m & 8) {
            if (p + 1 >= end) break;
            c.cmd = last[ch].cmd = data[p++];
            c.param = last[ch].param = data[p++];
        }
        if (m & 16) c.note = last[ch].note;
        if (m & 32) c.ins = last[ch].ins;
        if (m & 64) c.vol = last[ch].vol;
        if (m & 128) c.cmd = last[ch].cmd, c.param = last[ch].param;
        cb(row, ch, c);
    }
}
}  // namespace

bool itDetect(const uint8_t* d, size_t n) { return n >= 4 && std::memcmp(d, "IMPM", 4) == 0; }

Result<Song> itLoad(const uint8_t* data, size_t size) {
    if (size < 192) return Result<Song>::fail("BAD IT FILE");
    const int ordNum = le16(data + 32), insNum = le16(data + 34), smpNum = le16(data + 36), patNum = le16(data + 38);
    const int cmwt = le16(data + 42), flags = le16(data + 44);
    if (ordNum == 0 || ordNum > 1024 || insNum > 1024 || smpNum > 4000 || patNum > 4000)
        return Result<Song>::fail("BAD IT FILE");
    const size_t tables = 192 + (size_t)ordNum + 4 * ((size_t)insNum + smpNum + patNum);
    if (tables > size) return Result<Song>::fail("BAD IT FILE");
    const uint8_t* insOff = data + 192 + ordNum;
    const uint8_t* smpOff = insOff + 4 * insNum;
    const uint8_t* patOff = smpOff + 4 * smpNum;

    Song s;
    s.quirks = Quirks::FT2;
    s.freqMode = (flags & 8) ? FreqMode::Linear : FreqMode::Amiga;
    pcName(s.name, data + 4, 26, 20);
    const int gv = data[48], speed = data[50], tempo = data[51];
    s.globalVolume = (uint8_t)((gv > 128 ? 128 : gv) / 2);
    s.initialSpeed = (uint8_t)(speed < 1 ? 6 : (speed > 31 ? 31 : speed));
    s.initialBpm = (uint8_t)(tempo < 32 ? 125 : tempo);
    const bool stereo = (flags & 1) != 0;
    for (int i = 0; i < kMaxChannels; ++i) {
        const int p = data[64 + i] & 0x7F;
        s.channelPan[i] = !stereo || p > 64 ? 0x80 : (uint8_t)(p * 4 > 255 ? 255 : p * 4);
    }
    const std::vector<int> remap = pcOrders(s, data + 192, ordNum);

    // patterns: channel count = the highest channel with data (4..32)
    const int pats = patNum > 256 ? 256 : patNum;
    if (patNum > 256) s.loadFlags |= LOAD_TRUNCATED;
    int maxCh = 3;
    for (int p = 0; p < pats; ++p) {
        int rows;
        walkPattern(data, size, le32(patOff + 4 * p), rows, [&](int, int ch, const PackedCell&) {
            if (ch > maxCh) maxCh = ch;
        });
    }
    if (maxCh >= kMaxChannels) {
        maxCh = kMaxChannels - 1;
        s.loadFlags |= LOAD_TRUNCATED;
    }
    s.numChannels = (uint8_t)(maxCh + 1);
    for (int p = 0; p < pats; ++p) {
        const uint32_t off = le32(patOff + 4 * p);
        int rows;
        walkPattern(data, size, off, rows, [](int, int, const PackedCell&) {});
        Pattern pat = makePattern(rows, s.numChannels);
        walkPattern(data, size, off, rows, [&](int row, int ch, const PackedCell& c) {
            if (ch < s.numChannels) convertCell(c, pat.at(row, ch));
        });
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }

    // samples
    size_t budget = kSampleBudget, work = 4 * size + 65536;
    s.samples.resize(smpNum);
    for (int i = 0; i < smpNum; ++i) {
        const uint32_t off = le32(smpOff + 4 * i);
        if ((size_t)off + 80 > size || std::memcmp(data + off, "IMPS", 4) != 0) continue;
        const uint8_t* h = data + off;
        Sample& sm = s.samples[i];
        pcName(sm.name, h + 20, 26, 22);
        const int gvl = h[17] > 64 ? 64 : h[17], sflags = h[18], cvt = h[46], dfp = h[47];
        sm.volume = (uint8_t)(h[19] > 64 ? 64 : h[19]);
        sm.hasPan = (dfp & 0x80) != 0;
        if (sm.hasPan) sm.pan = (uint8_t)((dfp & 0x7F) * 4 > 255 ? 255 : (dfp & 0x7F) * 4);
        const uint32_t c5 = le32(h + 60);
        samplePitchFromRate(sm, c5 ? c5 : 8363);
        if (!(sflags & 1)) continue;
        const bool is16 = (sflags & 2) != 0, packed = (sflags & 8) != 0;
        uint32_t frames = le32(h + 48);
        const uint32_t ptr = le32(h + 72);
        if (ptr >= size) {
            s.loadFlags |= LOAD_TRUNCATED;
            continue;
        }
        const size_t bps = is16 ? 2 : 1;
        if ((size_t)frames * bps > budget) {
            frames = (uint32_t)(budget / bps);
            s.loadFlags |= LOAD_TRUNCATED;
        }
        if (packed) {
            const size_t most = (size - ptr) * 8;  // a compressed frame takes at least one bit
            if (frames > most) frames = (uint32_t)most;
            sm.is16 = is16;
            sm.length = frames;
            if (is16) sm.data16.assign(frames, 0);
            else sm.data8.assign(frames, 0);
            if (!decompress(data, size, ptr, sm, frames, is16, (cvt & 4) != 0, work)) s.loadFlags |= LOAD_TRUNCATED;
        } else {
            ByteReader dr(data, size);
            dr.seek(ptr);
            pcReadPcm(dr, sm, frames, is16, !(cvt & 1));
            if (sm.length < frames) s.loadFlags |= LOAD_TRUNCATED;
            if (cvt & 4) {  // delta PCM
                int acc = 0;
                for (uint32_t k = 0; k < sm.length; ++k) {
                    if (is16) sm.data16[k] = (int16_t)(acc += sm.data16[k]);
                    else sm.data8[k] = (int8_t)(acc += sm.data8[k]);
                }
            }
        }
        budget -= (size_t)sm.length * bps;
        if (gvl < 64)  // sample global volume always scales the sample
            for (uint32_t k = 0; k < sm.length; ++k) {
                if (is16) sm.data16[k] = (int16_t)(sm.data16[k] * gvl / 64);
                else sm.data8[k] = (int8_t)(sm.data8[k] * gvl / 64);
            }
        // the normal loop wins; a sustain loop alone becomes the loop
        bool bidi = false;
        if (sflags & 0x10) {
            pcLoop(sm, le32(h + 52), le32(h + 56));
            bidi = (sflags & 0x40) != 0;
        } else if (sflags & 0x20) {
            pcLoop(sm, le32(h + 64), le32(h + 68));
            bidi = (sflags & 0x80) != 0;
        }
        if (bidi && sm.loop != LoopType::None) sm.loop = LoopType::PingPong;
    }

    // instruments
    if (flags & 4) {
        for (int i = 0; i < insNum; ++i) {
            Instrument in;
            for (auto& m : in.sampleMap) m = kNoSample;
            const uint32_t off = le32(insOff + 4 * i);
            if ((size_t)off + 554 <= size && std::memcmp(data + off, "IMPI", 4) == 0)
                loadInstrument(data + off, cmwt < 0x200, smpNum, in);
            s.instruments.push_back(in);
        }
        if (s.instruments.empty()) {
            Instrument none;
            for (auto& m : none.sampleMap) m = kNoSample;
            s.instruments.push_back(none);
        }
    }

    pcRemapJumps(s, remap);
    // orders naming missing patterns play an empty pattern (as in Impulse Tracker)
    for (uint8_t o : s.order)
        while (o >= s.patterns.size()) s.patterns.push_back(makePattern(64, s.numChannels));
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, s.numChannels));
    if (s.order.empty()) s.order.push_back(0);
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}
