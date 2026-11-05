// DigiBooster 1.x ("DIGI Booster module") and DigiBooster Pro ("DBM0") loaders.
// Structure and effect conversion follow the OpenMPT DIGI / DBM loaders (BSD licence, OpenMPT Devs).
#include <cstring>

#include "core/reader.h"
#include "formats/formats_internal.h"

namespace {
void copyName(char* dst, const uint8_t* src, size_t n, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k < cap; ++i) {
        const char ch = (char)src[i];
        if (!ch) break;
        dst[k++] = (ch >= 32 && ch < 127) ? ch : ' ';
    }
    dst[k] = 0;
}

void setLoop(Sample& s, uint32_t start, uint32_t len, bool pingpong) {
    if (len < 2 || start >= s.length) {
        s.loop = LoopType::None;
        return;
    }
    if (len > s.length - start) len = s.length - start;
    s.loop = pingpong ? LoopType::PingPong : LoopType::Forward;
    s.loopStart = start;
    s.loopLength = len;
}

// ---------------------------------------------------------------- DigiBooster 1.x

// ProTracker cell (4 bytes) with the DigiBooster extras removed.
void digiCell(const uint8_t* c, Cell& cell) {
    const int period = ((c[0] & 0x0F) << 8) | c[1];
    cell.instr = (uint8_t)((c[0] & 0xF0) | (c[2] >> 4));
    cell.note = period ? modPeriodToNote(period) : NOTE_NONE;
    int fx = c[2] & 0x0F, p = c[3];
    if (fx == 0xE) {
        if ((p & 0xF0) == 0x30 || (p & 0xF0) == 0x80) fx = p = 0;  // backwards play, high offset
        else if (p == 0x40) p = 0xC0;                               // stop sample = cut
    } else if (fx == 0x8) {
        fx = p = 0;  // "robot"
    }
    cell.fx = (uint8_t)fx;
    cell.param = (uint8_t)p;
}

// ---------------------------------------------------------------- DigiBooster Pro

// DBM effect -> our effect number (XM numbering), -1 = dropped.
int dbmEffect(int cmd, int& p) {
    switch (cmd) {
        case 0x0: return p ? 0x0 : -1;
        case 0x1: case 0x2: case 0x3: case 0x4: case 0x7: case 0x8: case 0x9: case 0xB: case 0xC: case 0xD:
            return cmd;
        case 0x5: case 0x6: case 0xA:  // volume slide: the "up" nibble has priority
            if ((p & 0xF0) != 0 && (p & 0xF0) != 0xF0 && (p & 0x0F) != 0x0F) p &= 0xF0;
            return cmd;
        case 0xE:
            switch (p & 0xF0) {
                case 0x30: case 0x50: case 0x70: return -1;  // backwards, channel on/off, coarse offset
                case 0x40: p = 0xC0; return 0xE;            // turn sound off = cut now
                default: return 0xE;
            }
        case 0xF: return 0xF;
        case 0x10: p = p > 64 ? 64 : p; return 16;  // G global volume
        case 0x11: return 17;                         // H global volume slide
        case 0x14: return 20;                         // K key off
        case 0x15: return 21;                         // L envelope position
        case 0x19: return 25;                         // P panning slide
        default: return -1;                           // echo DSP and unknown
    }
}

// Puts an effect into the volume column when it has an equivalent there.
bool toVolumeColumn(Cell& c, int fx, int p) {
    if (c.vol) return false;
    if (fx == 0xC) {
        c.vol = (uint8_t)(0x10 + (p > 64 ? 64 : p));
        return true;
    }
    if (fx == 0x8) {
        c.vol = (uint8_t)(0xC0 + (p >> 4));
        return true;
    }
    if (fx == 0xA && p && ((p & 0x0F) == 0 || (p & 0xF0) == 0)) {
        c.vol = (uint8_t)((p & 0xF0) ? 0x70 + (p >> 4) : 0x60 + (p & 0x0F));
        return true;
    }
    return false;
}

void envelope(ByteReader& r, Envelope& e, bool scale) {
    const uint8_t flags = r.u8(), segs = r.u8(), sus1 = r.u8(), loopB = r.u8(), loopE = r.u8(), sus2 = r.u8();
    int n = segs + 1;
    if (n > 12) n = 12;
    e.count = (uint8_t)n;
    e.flags = 0;
    if (segs) {
        if (flags & 0x01) e.flags |= ENV_ON;
        if (flags & 0x0A) e.flags |= ENV_SUSTAIN;
        if (flags & 0x04) e.flags |= ENV_LOOP;
    }
    e.sustain = (flags & 0x0A) == 0x08 ? sus2 : ((flags & 0x0A) == 0x02 ? sus1 : (sus1 < sus2 ? sus1 : sus2));
    e.loopStart = loopB;
    e.loopEnd = loopE;
    if (e.sustain >= n) e.sustain = (uint8_t)(n - 1);
    if (e.loopEnd >= n) e.loopEnd = (uint8_t)(n - 1);
    if (e.loopStart > e.loopEnd) e.loopStart = e.loopEnd;
    for (int i = 0; i < 32; ++i) {
        const uint16_t tick = r.u16be();
        int v = r.u16be();
        if (i >= n) continue;
        if (scale) v = (v + 128) / 4;  // panning envelopes are -128..128 in DigiBooster Pro 3
        e.pts[i].tick = tick;
        e.pts[i].value = (uint8_t)(v < 0 ? 0 : (v > 64 ? 64 : v));
    }
}
}  // namespace

bool digiDetect(const uint8_t* d, size_t n) { return n >= 610 && std::memcmp(d, "DIGI Booster module\0", 20) == 0; }

Result<Song> digiLoad(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    r.seek(25);
    const int channels = r.u8();
    const bool packed = r.u8() != 0;
    r.skip(19);
    const int lastPat = r.u8(), lastOrd = r.u8();
    if (channels < 1 || channels > 8 || lastOrd > 127) return Result<Song>::fail("BAD DIGIBOOSTER FILE");
    uint8_t orders[128];
    r.bytes(orders, 128);
    uint32_t len[31], ls[31], ll[31];
    uint8_t vol[31], ft[31];
    for (auto& v : len) v = r.u32be();
    for (auto& v : ls) v = r.u32be();
    for (auto& v : ll) v = r.u32be();
    r.bytes(vol, 31);
    r.bytes(ft, 31);
    Song s;
    s.quirks = Quirks::PT23;
    s.freqMode = FreqMode::Amiga;
    s.numChannels = (uint8_t)(channels < 4 ? 4 : channels);
    setDefaultPanning(s);
    uint8_t name[32];
    r.bytes(name, 32);
    copyName(s.name, name, 32, 20);
    s.samples.resize(31);
    for (int i = 0; i < 31; ++i) {
        uint8_t n[30];
        r.bytes(n, 30);
        copyName(s.samples[i].name, n, 30, 22);
    }
    for (int i = 0; i <= lastOrd; ++i) s.order.push_back(orders[i]);
    for (int p = 0; p <= lastPat; ++p) {
        Pattern pat = makePattern(64, s.numChannels);
        if (packed) {
            const size_t chunk = r.u16be(), start = r.pos();
            uint8_t mask[64];
            r.bytes(mask, 64);
            for (int row = 0; row < 64; ++row)
                for (int ch = 0; ch < channels; ++ch)
                    if (mask[row] & (0x80 >> ch)) {
                        uint8_t c[4];
                        r.bytes(c, 4);
                        digiCell(c, pat.at(row, ch));
                    }
            r.seek(start + chunk);
        } else {
            for (int ch = 0; ch < channels; ++ch)  // unpacked patterns are stored channel by channel
                for (int row = 0; row < 64; ++row) {
                    uint8_t c[4];
                    r.bytes(c, 4);
                    digiCell(c, pat.at(row, ch));
                }
        }
        if (r.overrun()) {
            s.loadFlags |= LOAD_TRUNCATED;
            s.patterns.push_back(static_cast<Pattern&&>(pat));
            break;
        }
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }
    for (int i = 0; i < 31; ++i) {
        Sample& sm = s.samples[i];
        uint32_t n = len[i];
        if (n > r.remaining()) {
            if (n) s.loadFlags |= LOAD_TRUNCATED;
            n = (uint32_t)r.remaining();
        }
        sm.length = n;
        sm.data8.resize(n);
        r.bytes(sm.data8.data(), n);
        sm.volume = (uint8_t)(vol[i] > 64 ? 64 : vol[i]);
        sm.finetune = (int8_t)((ft[i] & 0x0F) < 8 ? (ft[i] & 0x0F) * 16 : ((ft[i] & 0x0F) - 16) * 16);
        if (ll[i]) setLoop(sm, ls[i], ll[i], false);
    }
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, s.numChannels));
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}

bool dbmDetect(const uint8_t* d, size_t n) { return n >= 16 && std::memcmp(d, "DBM0", 4) == 0; }

Result<Song> dbmLoad(const uint8_t* data, size_t size) {
    const int trackerHi = data[4];
    // chunk directory
    struct Chunk {
        size_t off = 0, len = 0;
        bool found = false;
    } name, info, song, inst, venv, penv, patt, smpl;
    for (size_t pos = 8; pos + 8 <= size;) {
        const uint8_t* id = data + pos;
        const size_t len = ((size_t)data[pos + 4] << 24) | (data[pos + 5] << 16) | (data[pos + 6] << 8) | data[pos + 7];
        const size_t body = pos + 8;
        const size_t avail = body <= size ? size - body : 0;
        Chunk c{body, len < avail ? len : avail, true};
        if (!std::memcmp(id, "NAME", 4)) name = c;
        else if (!std::memcmp(id, "INFO", 4)) info = c;
        else if (!std::memcmp(id, "SONG", 4)) song = c;
        else if (!std::memcmp(id, "INST", 4)) inst = c;
        else if (!std::memcmp(id, "VENV", 4)) venv = c;
        else if (!std::memcmp(id, "PENV", 4)) penv = c;
        else if (!std::memcmp(id, "PATT", 4)) patt = c;
        else if (!std::memcmp(id, "SMPL", 4)) smpl = c;
        if (len > avail) break;
        pos = body + len;
    }
    if (!info.found || info.len < 10) return Result<Song>::fail("BAD DIGIBOOSTER PRO FILE");
    ByteReader r(data, size);
    r.seek(info.off);
    const int numInst = r.u16be(), numSmp = r.u16be();
    r.u16be();  // songs
    const int numPat = r.u16be(), channels = r.u16be();
    if (channels < 1 || numSmp > 255 || numInst > 255) return Result<Song>::fail("BAD DIGIBOOSTER PRO FILE");

    Song s;
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Amiga;
    int ch = channels;
    if (ch > kMaxChannels) {
        ch = kMaxChannels;
        s.loadFlags |= LOAD_TRUNCATED;
    }
    s.numChannels = (uint8_t)(ch < 4 ? 4 : ch);
    setDefaultPanning(s);
    if (name.found) copyName(s.name, data + name.off, name.len, 20);

    // first song = the order list
    if (song.found && song.len >= 46) {
        r.seek(song.off);
        uint8_t sn[44];
        r.bytes(sn, 44);
        if (!s.name[0]) copyName(s.name, sn, 44, 20);
        const int n = r.u16be();
        for (int i = 0; i < n && !r.overrun() && r.pos() + 2 <= song.off + song.len; ++i) {
            const uint16_t p = r.u16be();
            if (p > 255) {
                s.loadFlags |= LOAD_TRUNCATED;
                continue;
            }
            if (s.order.size() < 256) s.order.push_back((uint8_t)p);
        }
    }

    // raw samples (shared by instruments)
    std::vector<Sample> raw(numSmp);
    if (smpl.found) {
        r.seek(smpl.off);
        for (int i = 0; i < numSmp; ++i) {
            const uint32_t flags = r.u32be();
            uint32_t len = r.u32be();
            if (r.overrun()) break;
            Sample& sm = raw[i];
            if (!(flags & 7)) continue;
            const int bytes = (flags & 4) ? 4 : (flags & 2) ? 2 : 1;
            if ((size_t)len * bytes > r.remaining()) {
                s.loadFlags |= LOAD_TRUNCATED;
                len = (uint32_t)(r.remaining() / bytes);
            }
            sm.length = len;
            if (bytes == 1) {
                sm.data8.resize(len);
                r.bytes(sm.data8.data(), len);
            } else {
                sm.is16 = true;
                sm.data16.resize(len);
                for (uint32_t k = 0; k < len; ++k) {
                    if (bytes == 2) {
                        sm.data16[k] = (int16_t)r.u16be();
                    } else {
                        sm.data16[k] = (int16_t)(r.u32be() >> 16);
                    }
                }
            }
        }
    }

    // instruments -> one sample each (volume, rate, loop and panning belong to the instrument)
    s.samples.resize(numInst);
    if (inst.found) {
        r.seek(inst.off);
        for (int i = 0; i < numInst; ++i) {
            uint8_t nm[30];
            r.bytes(nm, 30);
            const int smp = r.u16be(), vol = r.u16be();
            const uint32_t rate = r.u32be(), ls = r.u32be(), ll = r.u32be();
            const int pan = (int16_t)r.u16be();
            const int flags = r.u16be();
            if (r.overrun()) break;
            Sample& sm = s.samples[i];
            if (smp >= 1 && smp <= numSmp) sm = raw[smp - 1];
            copyName(sm.name, nm, 30, 22);
            sm.volume = (uint8_t)(vol > 64 ? 64 : vol);
            const int p = pan + 128;
            sm.pan = (uint8_t)(p < 0 ? 0 : (p > 255 ? 255 : p));
            samplePitchFromRate(sm, rate * 8303.0 / 8363.0);
            if (ll && (flags & 3)) setLoop(sm, ls, ll, (flags & 2) != 0);
        }
    }

    // envelopes -> XM instruments (only when there are any)
    if (venv.found || penv.found) {
        s.instruments.resize(numInst);
        for (int i = 0; i < numInst; ++i) {
            Instrument& in = s.instruments[i];
            for (auto& m : in.sampleMap) m = (uint16_t)i;
            std::memcpy(in.name, s.samples[i].name, sizeof in.name);
        }
        auto readEnvs = [&](const Chunk& c, bool pan) {
            r.seek(c.off);
            const int n = r.u16be();
            for (int k = 0; k < n && r.pos() + 136 <= c.off + c.len; ++k) {
                const int ins = r.u16be();
                Envelope e;
                envelope(r, e, pan && trackerHi > 2);
                if (ins >= 1 && ins <= numInst) (pan ? s.instruments[ins - 1].panEnv : s.instruments[ins - 1].volEnv) = e;
            }
        };
        if (venv.found) readEnvs(venv, false);
        if (penv.found) readEnvs(penv, true);
        for (auto& in : s.instruments)
            if (!(in.volEnv.flags & ENV_ON)) in.fadeout = 0xFFF;  // note off cuts the sample without an envelope
    }

    // patterns
    if (patt.found) {
        r.seek(patt.off);
        for (int p = 0; p < numPat && p < 256; ++p) {
            int rows = r.u16be();
            const uint32_t packedSize = r.u32be();
            if (r.overrun()) break;
            const size_t start = r.pos(), end = start + packedSize;
            if (rows < 1) rows = 1;
            if (rows > 256) {
                rows = 256;
                s.loadFlags |= LOAD_TRUNCATED;
            }
            Pattern pat = makePattern(rows, s.numChannels);
            int row = 0;
            while (r.pos() < end && r.pos() < size && row < rows) {
                const int c = r.u8();
                if (!c) {
                    ++row;
                    continue;
                }
                const int b = r.u8();
                Cell dummy;
                Cell& cell = c <= ch ? pat.at(row, c - 1) : dummy;
                if (b & 0x01) {
                    const int n = r.u8();
                    if (n == 0x1F) cell.note = NOTE_OFF;
                    else if (n > 0 && n < 0xFE) {
                        const int x = (n >> 4) * 12 + (n & 0x0F) + 1;
                        cell.note = (uint8_t)(x < 1 ? 1 : (x > 96 ? 96 : x));
                    }
                }
                if (b & 0x02) cell.instr = r.u8();
                int c1 = 0, p1 = 0, c2 = 0, p2 = 0;
                if (b & 0x04) c2 = r.u8();
                if (b & 0x08) p2 = r.u8();
                if (b & 0x10) c1 = r.u8();
                if (b & 0x20) p1 = r.u8();
                const int f1 = (b & 0x30) ? dbmEffect(c1, p1) : -1;
                const int f2 = (b & 0x0C) ? dbmEffect(c2, p2) : -1;
                int fx[2] = {f1, f2}, pr[2] = {p1, p2};
                // volume column: set volume and panning first, then single-direction volume slides
                for (int pass = 0; pass < 2; ++pass)
                    for (int k = 0; k < 2; ++k)
                        if (fx[k] >= 0 && (pass == 0) == (fx[k] != 0xA) && toVolumeColumn(cell, fx[k], pr[k])) fx[k] = -1;
                // one effect column left: global commands (speed, jumps) win
                int pick = fx[0] >= 0 ? 0 : 1;
                if (fx[0] >= 0 && fx[1] >= 0 && (fx[1] == 0xF || fx[1] == 0xB || fx[1] == 0xD)) pick = 1;
                if (fx[pick] >= 0) {
                    cell.fx = (uint8_t)fx[pick];
                    cell.param = (uint8_t)pr[pick];
                }
            }
            r.seek(end);
            s.patterns.push_back(static_cast<Pattern&&>(pat));
        }
        if (numPat > 256) s.loadFlags |= LOAD_TRUNCATED;
    }
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, s.numChannels));
    if (s.order.empty()) s.order.push_back(0);
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}
