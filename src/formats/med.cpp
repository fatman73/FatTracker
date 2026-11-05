// OctaMED / MED Soundstudio loader (MMD0, MMD1, MMD2, MMD3).
// Structure and effect conversion follow the OpenMPT MED loader (BSD licence, OpenMPT Devs).
// The module becomes an ordinary ProTracker-style Song: synth instruments play their first
// waveform as a loop, hybrid instruments their sample, multi-octave IFF instruments their first octave.
#include <cmath>
#include <cstring>

#include "core/reader.h"
#include "formats/formats_internal.h"

namespace {
// offset + length inside the file, computed without 32-bit wrap-around
bool fits(size_t size, uint64_t off, uint64_t len) { return off <= size && len <= size - off; }

constexpr uint8_t FLAG_VOLHEX = 0x10, FLAG_8CHANNEL = 0x40;
constexpr uint8_t FLAG2_BMASK = 0x1F, FLAG2_BPM = 0x20, FLAG2_MIX = 0x80;
constexpr int TYPE_SYNTH = -1, TYPE_HYBRID = -2;
constexpr int S_16 = 0x10, S_STEREO = 0x20, S_DELTA = 0x40, S_PACKED = 0x80;

struct Ctx {
    bool volHex = false, vol7bit = false, is8Ch = false, bpmMode = false, mix = false;
    int rowsPerBeat = 4;
};

double medTempoToBpm(int tempo, const Ctx& c) {
    if (c.bpmMode && !c.is8Ch) {
        if (tempo < 7) return 111.5;
        return tempo * c.rowsPerBeat / 4.0;
    }
    if (c.is8Ch && tempo > 0) {
        static const uint8_t kTempos[10] = {179, 164, 152, 141, 131, 123, 116, 110, 104, 99};
        return kTempos[(tempo > 10 ? 10 : tempo) - 1];
    }
    if (!c.mix && tempo > 0 && tempo <= 10) return (6.0 * 1773447.0 / 14500.0) / tempo;  // SoundTracker compatible
    if (c.mix && tempo < 8) return 157.86;
    return tempo / 0.264;
}

uint8_t bpmByte(double bpm) {
    const long v = std::lround(bpm);
    return (uint8_t)(v < 32 ? 32 : (v > 255 ? 255 : v));
}

void setFx(Cell& c, int fx, int param) {
    c.fx = (uint8_t)fx;
    c.param = (uint8_t)param;
}

// MED command -> ProTracker/XM effect in the cell (cell numbering: Exy = fx 14, param xy).
void convertEffect(Cell& c, int cmd, int p, const Ctx& ctx) {
    const int lo = p < 0x0F ? p : 0x0F;
    switch (cmd) {
        case 0x00: if (p) setFx(c, 0x0, p); break;
        case 0x01: if (p) setFx(c, 0x1, p); break;
        case 0x02: if (p) setFx(c, 0x2, p); break;
        case 0x03: setFx(c, 0x3, p); break;
        case 0x04: setFx(c, 0x4, (p & 0xF0) | ((p & 0x0F) * 2 > 0x0F ? 0x0F : (p & 0x0F) * 2)); break;  // twice as deep
        case 0x05: if (p) setFx(c, 0x5, p); else setFx(c, 0x3, 0); break;
        case 0x06: if (p) setFx(c, 0x6, p); else setFx(c, 0x4, 0); break;
        case 0x07: setFx(c, 0x7, p); break;
        case 0x08: break;  // hold and decay
        case 0x09: if (p > 0) setFx(c, 0xF, p < 0x20 ? p : 0x1F); break;  // secondary tempo = speed (F20+ would be BPM)
        case 0x0A: if (p) setFx(c, 0xA, p); break;
        case 0x0B: setFx(c, 0xB, p); break;
        case 0x0C:
            if (!ctx.volHex && p < 0x99) setFx(c, 0xC, (p >> 4) * 10 + (p & 0x0F));
            else if (ctx.volHex && !ctx.vol7bit) setFx(c, 0xC, (p & 0x7F) > 64 ? 64 : (p & 0x7F));
            else if (ctx.volHex) setFx(c, 0xC, ((p & 0x7F) + 1) / 2);
            break;
        case 0x0D: if (p) setFx(c, 0xA, p); break;
        case 0x0E: break;  // synth jump
        case 0x0F:
            if (p == 0) setFx(c, 0xD, 0);
            else if (p <= 0xF0) setFx(c, 0xF, p < 3 ? 0x70 : bpmByte(medTempoToBpm(p, ctx)));
            else if (p == 0xF1) setFx(c, 0xE, 0x93);
            else if (p == 0xF2) setFx(c, 0xE, 0xD3);
            else if (p == 0xF3) setFx(c, 0xE, 0x92);
            else if (p == 0xF8 || p == 0xF9) setFx(c, 0xE, 0xF9 - p);  // LED filter off / on
            else if (p == 0xFF && c.note == NOTE_NONE) c.note = NOTE_OFF;
            break;
        case 0x11: setFx(c, 0xE, 0x10 | lo); break;
        case 0x12: setFx(c, 0xE, 0x20 | lo); break;
        case 0x14: setFx(c, 0x4, p); break;
        case 0x15: setFx(c, 0xE, 0x50 | (p & 0x0F)); break;
        case 0x16: setFx(c, 0xE, 0x60 | lo); break;
        case 0x18: setFx(c, 0xE, 0xC0 | lo); break;
        case 0x19: setFx(c, 0x9, p); break;
        case 0x1A: setFx(c, 0xE, 0xA0 | lo); break;
        case 0x1B: setFx(c, 0xE, 0xB0 | lo); break;
        case 0x1D: setFx(c, 0xD, ((p / 10) << 4) | (p % 10)); break;  // break row is hex in MED, BCD in PT
        case 0x1E: setFx(c, 0xE, 0xE0 | lo); break;
        case 0x1F:
            if (p & 0xF0) setFx(c, 0xE, 0xD0 | (p >> 4));
            else if (p & 0x0F) setFx(c, 0xE, 0x90 | p);
            break;
        case 0x2E:
            if (p <= 0x10 || p >= 0xF0) {
                int pan = (((p ^ 0x80) - 0x70) * 8);
                setFx(c, 0x8, pan < 0 ? 0 : (pan > 255 ? 255 : pan));
            }
            break;
        default: break;
    }
}

// Sample data of one instrument (8 / 16 bit big endian, optional delta, stereo -> left channel).
void readSampleData(ByteReader& r, Sample& s, uint32_t bytes, int type) {
    const bool is16 = (type & S_16) != 0;
    uint32_t frames = is16 ? bytes / 2 : bytes;
    // stereo samples: the length is per channel and the left channel comes first
    const size_t avail = r.remaining();
    const size_t need = (size_t)frames * (is16 ? 2 : 1);
    if (need > avail) frames = (uint32_t)(avail / (is16 ? 2 : 1));
    s.is16 = is16;
    s.length = frames;
    if (is16) {
        s.data16.resize(frames);
        int16_t acc = 0;
        for (uint32_t i = 0; i < frames; ++i) {
            int16_t v = (int16_t)r.u16be();
            if (type & S_DELTA) v = acc = (int16_t)(acc + v);
            s.data16[i] = v;
        }
    } else {
        s.data8.resize(frames);
        int8_t acc = 0;
        for (uint32_t i = 0; i < frames; ++i) {
            int8_t v = (int8_t)r.u8();
            if (type & S_DELTA) v = acc = (int8_t)(acc + v);
            s.data8[i] = v;
        }
    }
}

void setLoop(Sample& s, uint32_t start, uint32_t len, bool pingpong = false) {
    if (len < 2 || start >= s.length) {
        s.loop = LoopType::None;
        return;
    }
    if (len > s.length - start) len = s.length - start;
    s.loop = len >= 2 ? (pingpong ? LoopType::PingPong : LoopType::Forward) : LoopType::None;
    s.loopStart = start;
    s.loopLength = len;
}

void copyName(char* dst, const uint8_t* src, size_t n, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k < cap; ++i) {
        const char ch = (char)src[i];
        if (!ch) break;
        dst[k++] = (ch >= 32 && ch < 127) ? ch : ' ';
    }
    dst[k] = 0;
}
}  // namespace

bool medDetect(const uint8_t* d, size_t n) {
    return n >= 52 && d[0] == 'M' && d[1] == 'M' && d[2] == 'D' && d[3] >= '0' && d[3] <= '3';
}

Result<Song> medLoad(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    if (!medDetect(data, size)) return Result<Song>::fail("NOT A MODULE");
    const int version = data[3] - '0';
    r.seek(8);
    const uint32_t songOff = r.u32be();
    r.skip(4);
    const uint32_t blockArr = r.u32be();
    r.skip(4);
    const uint32_t sampleArr = r.u32be();
    r.skip(4);
    const uint32_t expOff = r.u32be();
    if (songOff < 52 || songOff + 63 * 8 + 284 > size || blockArr < 52 || blockArr >= size)
        return Result<Song>::fail("BAD OCTAMED FILE");

    // song header: 63 sample headers, then the common part
    struct SmpHdr {
        uint16_t loopStart, loopLength;
        uint8_t midiCh, midiPreset, volume;
        int8_t transpose;
    } smp[63];
    r.seek(songOff);
    for (auto& h : smp) {
        h.loopStart = r.u16be();
        h.loopLength = r.u16be();
        h.midiCh = r.u8();
        h.midiPreset = r.u8();
        h.volume = r.u8();
        h.transpose = (int8_t)r.u8();
    }
    const uint16_t numBlocks = r.u16be();
    const uint16_t songLength = r.u16be();
    const size_t songArea = r.pos();
    uint8_t seq[256];
    r.bytes(seq, 256);
    const uint16_t defTempo = r.u16be();
    const int8_t playTranspose = (int8_t)r.u8();
    const uint8_t flags = r.u8(), flags2 = r.u8(), tempo2 = r.u8();
    r.skip(16);
    r.u8();  // master volume
    const uint8_t numSamples = r.u8();
    if (numSamples > 63 || numBlocks > 0x7FFF) return Result<Song>::fail("BAD OCTAMED FILE");

    Ctx ctx;
    ctx.volHex = (flags & FLAG_VOLHEX) != 0;
    ctx.is8Ch = (flags & FLAG_8CHANNEL) != 0;
    ctx.bpmMode = (flags2 & FLAG2_BPM) != 0;
    ctx.mix = version >= 2 && (flags2 & FLAG2_MIX) != 0;
    ctx.rowsPerBeat = 1 + (flags2 & FLAG2_BMASK);
    const bool hardwareMix = version < 2 || !(flags2 & FLAG2_MIX);

    Song s;
    s.quirks = Quirks::PT23;
    s.freqMode = FreqMode::Amiga;
    s.initialSpeed = (uint8_t)(tempo2 < 1 ? 1 : (tempo2 > 32 ? 32 : tempo2));
    s.initialBpm = bpmByte(medTempoToBpm(defTempo, ctx));

    // ---- expansion data: song name, instrument names and extensions
    uint32_t extOff = 0, infoOff = 0;
    uint16_t extEntries = 0, extSize = 0, infoEntries = 0, infoSize = 0;
    if (expOff && fits(size, expOff, 80)) {
        r.seek(expOff);
        r.skip(4);
        extOff = r.u32be();
        extEntries = r.u16be();
        extSize = r.u16be();
        r.skip(8);
        infoOff = r.u32be();
        infoEntries = r.u16be();
        infoSize = r.u16be();
        r.skip(8 + 4 + 4);
        const uint32_t nameOff = r.u32be(), nameLen = r.u32be();
        if (nameOff && nameLen && fits(size, nameOff, nameLen)) copyName(s.name, data + nameOff, nameLen, 20);
    }

    // ---- instruments
    s.samples.resize(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        Sample& sm = s.samples[i];
        sm.volume = (uint8_t)(smp[i].volume > 64 ? 64 : smp[i].volume);
        int transpose = smp[i].transpose;
        uint32_t loopStart = smp[i].loopStart * 2u, loopLen = smp[i].loopLength * 2u;
        bool hasLoop = smp[i].loopLength > 1;
        if (!sampleArr || sampleArr + 4u * (i + 1) > size) continue;
        r.seek(sampleArr + 4u * i);
        const uint32_t off = r.u32be();
        if (!off || off + 6 > size) continue;
        r.seek(off);
        const uint32_t len = r.u32be();
        const int16_t type = (int16_t)r.u16be();
        if (type == TYPE_SYNTH || type == TYPE_HYBRID) {
            // synth header (272 bytes) then the waveform pointers (relative to the instrument)
            r.seek(off + 6 + 14);
            const uint16_t numWf = r.u16be();
            r.seek(off + 6 + 272);
            const uint32_t wf0 = numWf > 0 && numWf <= 64 ? r.u32be() : 0;
            if (wf0 > 6 + 272 && fits(size, (uint64_t)off + wf0, 6)) {
                r.seek(off + wf0);
                if (type == TYPE_SYNTH) {
                    const uint32_t bytes = r.u16be() * 2u;
                    readSampleData(r, sm, bytes, 0);
                    setLoop(sm, 0, sm.length);
                    if (version <= 2) transpose -= 24;
                    std::memcpy(sm.name, "SYNTH", 6);
                } else {
                    const uint32_t hlen = r.u32be();
                    const int16_t htype = (int16_t)r.u16be();
                    if (htype == 0) {
                        readSampleData(r, sm, hlen, 0);
                        if (hasLoop) setLoop(sm, loopStart, loopLen);
                    }
                    std::memcpy(sm.name, "HYBRID", 7);
                }
            }
        } else if (type >= 0 && !(type & S_PACKED)) {
            const int octaveType = type & 0x0F;
            static const uint8_t kSamplesPerType[] = {1, 5, 3, 2, 4, 6, 7};
            uint32_t bytes = len;
            if (octaveType > 0 && octaveType < 7) {
                const int n = kSamplesPerType[octaveType];
                bytes = len / ((1u << n) - 1);  // first (highest) octave only
            }
            readSampleData(r, sm, bytes, type);
            if (hasLoop) setLoop(sm, loopStart, loopLen);
        }
        sm.relativeNote = (int8_t)(transpose < -96 ? -96 : (transpose > 95 ? 95 : transpose));
        // instrument extension: finetune, long loops, loop / ping-pong flags
        if (extOff && i < extEntries && extSize && fits(size, extOff, (uint64_t)extSize * (i + 1))) {
            const uint8_t* e = data + extOff + (size_t)extSize * i;
            if (extSize > 3) sm.finetune = (int8_t)(((int8_t)e[3]) * 16);
            if (extSize >= 18 && sm.length) {
                const uint32_t ls = ((uint32_t)e[10] << 24) | (e[11] << 16) | (e[12] << 8) | e[13];
                const uint32_t ll = ((uint32_t)e[14] << 24) | (e[15] << 16) | (e[16] << 8) | e[17];
                if (ll) {
                    loopStart = ls;
                    loopLen = ll;
                    hasLoop = true;
                    setLoop(sm, ls, ll);
                }
            }
            if (extSize > 5 && type >= 0) {
                const uint8_t f = e[5];
                if (f & 0x01) setLoop(sm, loopStart, loopLen, (f & 0x08) != 0);
                else if (extSize >= 18) sm.loop = LoopType::None;
            }
        }
        if (infoOff && i < infoEntries && infoSize && fits(size, infoOff, (uint64_t)infoSize * (i + 1)))
            copyName(sm.name, data + infoOff + (size_t)infoSize * i, infoSize < 40 ? infoSize : 40, 22);
    }

    // ---- order list
    if (version < 2) {
        for (int i = 0; i < songLength && i < 256; ++i) s.order.push_back(seq[i]);
        if (songLength > 256) s.loadFlags |= LOAD_TRUNCATED;
    } else {
        ByteReader h(data, size);
        h.seek(songArea);
        const uint32_t playSeqTable = h.u32be(), sectionTable = h.u32be();
        h.skip(4);
        h.u16be();  // tracks
        const uint16_t numPlaySeqs = h.u16be();
        for (int sec = 0; sec < songLength && s.order.size() < 256; ++sec) {
            if (sectionTable + 2u * (sec + 1) > size) break;
            h.seek(sectionTable + 2u * sec);
            const uint16_t ps = h.u16be();
            if (ps >= numPlaySeqs || playSeqTable + 4u * (ps + 1) > size) continue;
            h.seek(playSeqTable + 4u * ps);
            const uint32_t psOff = h.u32be();
            if (!psOff || psOff + 42 > size) continue;
            h.seek(psOff + 40);
            const uint16_t n = h.u16be();
            for (int k = 0; k < n && !h.overrun(); ++k) {
                if (s.order.size() >= 256) {  // the model holds 256 positions: stop reading here
                    s.loadFlags |= LOAD_TRUNCATED;
                    break;
                }
                const uint16_t b = h.u16be();
                if (b >= 0x8000) continue;  // play sequence commands
                if (b > 255) {
                    s.loadFlags |= LOAD_TRUNCATED;
                    continue;
                }
                s.order.push_back((uint8_t)b);
            }
        }
    }

    // ---- channel count: the widest block
    int tracks = 4;
    const int blocks = numBlocks > 256 ? 256 : numBlocks;
    if (numBlocks > 256) s.loadFlags |= LOAD_TRUNCATED;
    std::vector<uint32_t> blockOff(blocks, 0);
    for (int b = 0; b < blocks; ++b) {
        if (blockArr + 4u * (b + 1) > size) break;
        r.seek(blockArr + 4u * b);
        blockOff[b] = r.u32be();
        if (!blockOff[b] || blockOff[b] + 2 > size) {
            blockOff[b] = 0;
            continue;
        }
        r.seek(blockOff[b]);
        const int t = version < 1 ? r.u8() : r.u16be();
        if (t > tracks) tracks = t;
    }
    if (tracks > kMaxChannels) {
        tracks = kMaxChannels;
        s.loadFlags |= LOAD_TRUNCATED;
    }
    s.numChannels = (uint8_t)tracks;
    setDefaultPanning(s);

    // ---- blocks
    const int noteBase = 36 + playTranspose - (version > 2 ? 24 : 0);
    const int wrapAt = hardwareMix ? 73 : 85;  // notes above B-5 wrap down an octave on 4-channel hardware
    int lastInstr[kMaxChannels] = {};
    s.patterns.reserve(blocks);
    for (int b = 0; b < blocks; ++b) {
        int numTracks = 0, numRows = 1;
        bool vol7bit = false;
        if (blockOff[b]) {
            r.seek(blockOff[b]);
            if (version < 1) {
                numTracks = r.u8();
                numRows = r.u8() + 1;
            } else {
                numTracks = r.u16be();
                numRows = r.u16be() + 1;
                const uint32_t info = r.u32be();
                if (info && fits(size, info, 36)) vol7bit = data[info + 16] | data[info + 17] | data[info + 18] | data[info + 19];
            }
        }
        if (numRows > 256) {
            numRows = 256;
            s.loadFlags |= LOAD_TRUNCATED;
        }
        Pattern p = makePattern(numRows, tracks);
        if (!blockOff[b]) {
            s.patterns.push_back(static_cast<Pattern&&>(p));
            continue;
        }
        Ctx bctx = ctx;
        bctx.vol7bit = vol7bit;
        const int bytesPerCell = version < 1 ? 3 : 4;
        for (int row = 0; row < numRows; ++row)
            for (int t = 0; t < numTracks; ++t) {
                uint8_t c[4] = {};
                if (r.remaining() < (size_t)bytesPerCell) {
                    s.loadFlags |= LOAD_TRUNCATED;
                    row = numRows;
                    break;
                }
                r.bytes(c, bytesPerCell);
                if (t >= tracks) continue;
                Cell& cell = p.at(row, t);
                int note = 0, cmd, param;
                if (version < 1) {
                    if (c[0] & 0x3F) note = (c[0] & 0x3F) + noteBase;
                    cell.instr = (uint8_t)((c[1] >> 4) | ((c[0] & 0x80) >> 3) | ((c[0] & 0x40) >> 1));
                    cmd = c[1] & 0x0F;
                    param = c[2];
                } else {
                    if (c[0] & 0x7F) note = (c[0] & 0x7F) + noteBase;
                    else if (c[0] == 0x80) cell.note = NOTE_OFF;
                    if (c[1] & 0x3F) cell.instr = (uint8_t)(c[1] & 0x3F);
                    cmd = c[2];
                    param = c[3];
                }
                if (cell.instr) lastInstr[t] = cell.instr;
                if (note > 0) {
                    const int ins = lastInstr[t];
                    const int st = ins > 0 && ins <= numSamples ? smp[ins - 1].transpose : 0;
                    const int real = note + st;
                    if (real >= wrapAt) note -= ((real - wrapAt + 12) / 12) * 12;
                    if (note < 1) note = 1 + (note % 12 + 12) % 12;
                    if (note > 96) note = 85 + (note - 85) % 12;
                    cell.note = (uint8_t)note;
                }
                if (cmd || param) convertEffect(cell, cmd, param, bctx);
            }
        s.patterns.push_back(static_cast<Pattern&&>(p));
    }
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, tracks));
    if (s.order.empty()) s.order.push_back(0);
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}
