// Scream Tracker 3 (S3M), MultiTracker (MTM) and Composer 669 loaders.
// Structures and effect conversion follow the OpenMPT loaders (BSD licence, OpenMPT Devs).
#include <cstring>

#include "core/reader.h"
#include "formats/formats_internal.h"

void pcName(char* dst, const uint8_t* src, size_t n, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k < cap; ++i) {
        const char ch = (char)src[i];
        if (!ch) break;
        dst[k++] = (ch >= 32 && ch < 127) ? ch : ' ';
    }
    while (k && dst[k - 1] == ' ') --k;
    dst[k] = 0;
}

void pcLoop(Sample& s, uint32_t start, uint32_t end) {
    if (end > s.length) end = s.length;
    if (start >= end || end - start < 2) {
        s.loop = LoopType::None;
        return;
    }
    s.loop = LoopType::Forward;
    s.loopStart = start;
    s.loopLength = end - start;
}

std::vector<int> pcOrders(Song& s, const uint8_t* list, int n) {
    std::vector<int> remap((size_t)n + 1, 0);
    bool ended = false;
    for (int i = 0; i < n; ++i) {
        remap[i] = (int)s.order.size();  // a dropped entry jumps to the next kept one
        const int o = list[i];
        if (o == 0xFF) ended = true;
        if (ended || o == 0xFE) continue;
        if (s.order.size() >= 256) {
            s.loadFlags |= LOAD_TRUNCATED;
            ended = true;
            continue;
        }
        s.order.push_back((uint8_t)o);
    }
    remap[n] = (int)s.order.size();
    return remap;
}

void pcRemapJumps(Song& s, const std::vector<int>& remap) {
    for (auto& pat : s.patterns)
        for (auto& c : pat.cells)
            if (c.fx == 0xB) {
                const int to = c.param < (int)remap.size() ? remap[c.param] : (int)s.order.size();
                c.param = (uint8_t)(to > 255 ? 255 : to);
            }
}

// Reads `frames` of 8 / 16 bit (little endian) PCM, signed or unsigned.
void pcReadPcm(ByteReader& r, Sample& s, uint32_t frames, bool is16, bool isUnsigned) {
    const size_t bytes = (size_t)frames * (is16 ? 2 : 1);
    if (bytes > r.remaining()) frames = (uint32_t)(r.remaining() / (is16 ? 2 : 1));
    s.is16 = is16;
    s.length = frames;
    if (is16) {
        s.data16.resize(frames);
        for (uint32_t i = 0; i < frames; ++i) s.data16[i] = (int16_t)(r.u16le() ^ (isUnsigned ? 0x8000 : 0));
    } else {
        s.data8.resize(frames);
        for (uint32_t i = 0; i < frames; ++i) s.data8[i] = (int8_t)(r.u8() ^ (isUnsigned ? 0x80 : 0));
    }
}

namespace {
void setFx(Cell& c, int fx, int p) {
    c.fx = (uint8_t)fx;
    c.param = (uint8_t)p;
}
}  // namespace

// S3M / IT effect letter (1 = A) -> our XM style effect. IT: breaks are hex, global volume 0..128, X 0..255.
void pcEffect(Cell& c, int cmd, int p, bool it) {
    const int hi = p >> 4, lo = p & 15;
    switch (cmd) {
        case 1: if (p) setFx(c, 0xF, p > 0x1F ? 0x1F : p); break;  // A speed
        case 2: setFx(c, 0xB, p); break;                            // B jump
        case 3: setFx(c, 0xD, it ? ((p > 159 ? 159 : p) / 10) << 4 | (p > 159 ? 159 : p) % 10 : p); break;  // C break (player: BCD)
        case 4:                                                     // D volume slide (fine forms DxF / DFy)
            if (hi == 0xF && lo) setFx(c, 0xE, 0xB0 | lo);
            else if (lo == 0xF && hi) setFx(c, 0xE, 0xA0 | hi);
            else setFx(c, 0xA, p);
            break;
        case 5:  // E porta down (EFx fine, EEx extra fine)
            if (hi == 0xF) setFx(c, 0xE, 0x20 | lo);
            else if (hi == 0xE) setFx(c, 33, 0x20 | lo);
            else setFx(c, 0x2, p);
            break;
        case 6:  // F porta up
            if (hi == 0xF) setFx(c, 0xE, 0x10 | lo);
            else if (hi == 0xE) setFx(c, 33, 0x10 | lo);
            else setFx(c, 0x1, p);
            break;
        case 7: setFx(c, 0x3, p); break;   // G tone portamento
        case 8: setFx(c, 0x4, p); break;   // H vibrato
        case 9: setFx(c, 29, p); break;    // I tremor (XM T)
        case 10: setFx(c, 0x0, p); break;  // J arpeggio
        case 11: setFx(c, 0x6, p); break;  // K vibrato + volume slide
        case 12: setFx(c, 0x5, p); break;  // L tone portamento + volume slide
        case 15: setFx(c, 0x9, p); break;  // O sample offset
        case 17: setFx(c, 27, p); break;   // Q retrigger (XM R)
        case 18: setFx(c, 0x7, p); break;  // R tremolo
        case 19:                           // S special
            switch (hi) {
                case 0x0: setFx(c, 0xE, 0x00 | lo); break;  // filter
                case 0x1: setFx(c, 0xE, 0x30 | lo); break;  // glissando
                case 0x2: setFx(c, 0xE, 0x50 | lo); break;  // finetune
                case 0x3: setFx(c, 0xE, 0x40 | lo); break;  // vibrato waveform
                case 0x4: setFx(c, 0xE, 0x70 | lo); break;  // tremolo waveform
                case 0x8: setFx(c, 0x8, lo * 0x11); break;  // panning
                case 0xB: setFx(c, 0xE, 0x60 | lo); break;  // pattern loop
                case 0xC: setFx(c, 0xE, 0xC0 | lo); break;  // note cut
                case 0xD: setFx(c, 0xE, 0xD0 | lo); break;  // note delay
                case 0xE: setFx(c, 0xE, 0xE0 | lo); break;  // pattern delay
                default: break;
            }
            break;
        case 20: if (p >= 0x20) setFx(c, 0xF, p); break;            // T tempo (slides dropped)
        case 21: setFx(c, 0x4, (p & 0xF0) | (lo ? (lo / 4 ? lo / 4 : 1) : 0)); break;  // U fine vibrato
        case 22: setFx(c, 16, it ? (p > 128 ? 64 : p / 2) : (p > 64 ? 64 : p)); break;  // V global volume (XM G)
        case 23: setFx(c, 17, p); break;                            // W global volume slide (XM H)
        case 24: setFx(c, 0x8, it ? p : (p == 0xA4 ? 0x80 : (p >= 0x80 ? 0xFF : p * 2))); break;  // X (S3M 0..80, A4 surround)
        case 16:  // IT P: P0x slides right, Px0 left; XM P the other way round (fine forms dropped)
            if (it && !((hi == 0xF && lo) || (lo == 0xF && hi))) setFx(c, 25, (lo << 4) | hi);
            break;
        default: break;                                             // Y panbrello, Z midi
    }
}

namespace {
int clampNote(int n) { return n < 1 ? 1 : (n > 96 ? 96 : n); }
}  // namespace

// ---------------------------------------------------------------- S3M

bool s3mDetect(const uint8_t* d, size_t n) { return n >= 48 && std::memcmp(d + 44, "SCRM", 4) == 0; }

Result<Song> s3mLoad(const uint8_t* data, size_t size) {
    if (size < 96 || data[29] != 16) return Result<Song>::fail("BAD S3M FILE");
    ByteReader r(data, size);
    Song s;
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Amiga;
    pcName(s.name, data, 28, 20);
    r.seek(32);
    const int ordNum = r.u16le(), smpNum = r.u16le(), patNum = r.u16le();
    r.u16le();  // flags
    r.u16le();  // tracker
    const int format = r.u16le();
    r.skip(4);  // SCRM
    const int gv = r.u8(), speed = r.u8(), tempo = r.u8(), master = r.u8();
    r.u8();
    const int panTable = r.u8();
    r.seek(64);
    uint8_t chan[32];
    r.bytes(chan, 32);
    if (ordNum > 512 || smpNum > 255 || patNum > 512) return Result<Song>::fail("BAD S3M FILE");
    s.globalVolume = (uint8_t)(gv > 64 ? 64 : gv);
    s.initialSpeed = (uint8_t)(speed < 1 ? 6 : (speed > 31 ? 31 : speed));
    s.initialBpm = (uint8_t)(tempo < 32 ? 125 : tempo);
    int channels = 4;
    for (int i = 0; i < 32; ++i)  // 0..15 PCM / AdLib; ModPlug wrote 16..31 for PCM channels 17..32
        if ((chan[i] & 0x7F) < 32) channels = i + 1;
    s.numChannels = (uint8_t)channels;
    const bool stereo = (master & 0x80) != 0;
    for (int i = 0; i < 32; ++i) s.channelPan[i] = !stereo ? 0x80 : ((chan[i] & 8) ? 0xCC : 0x33);
    std::vector<uint8_t> orders(ordNum);
    if (ordNum) r.bytes(orders.data(), ordNum);
    std::vector<uint16_t> smpPara(smpNum), patPara(patNum);
    for (auto& v : smpPara) v = r.u16le();
    for (auto& v : patPara) v = r.u16le();
    if (panTable == 0xFC)
        for (int i = 0; i < 32; ++i) {
            const uint8_t b = r.u8();
            if ((b & 0x20) && stereo) s.channelPan[i] = (uint8_t)((b & 0x0F) * 17);
        }
    const std::vector<int> remap = pcOrders(s, orders.data(), ordNum);

    // samples: headers may share data, so the total is budgeted against the file size
    size_t budget = 2 * size + 65536;
    s.samples.resize(smpNum);
    for (int i = 0; i < smpNum; ++i) {
        const size_t off = (size_t)smpPara[i] * 16;
        if (off + 80 > size) continue;
        const uint8_t* h = data + off;
        Sample& sm = s.samples[i];
        sm.hasPan = false;
        pcName(sm.name, h + 48, 28, 22);
        if (h[0] != 1) continue;  // not PCM (empty / AdLib)
        ByteReader hr(data, size);
        hr.seek(off + 13);
        const uint32_t ptr = ((uint32_t)hr.u8() << 20) | ((uint32_t)hr.u16le() << 4);
        const uint32_t len = hr.u32le(), ls = hr.u32le(), le = hr.u32le();
        const int vol = hr.u8();
        hr.u8();
        const int pack = hr.u8(), flags = hr.u8();
        const uint32_t c5 = hr.u32le();
        if (pack) continue;  // packed samples were never used
        sm.volume = (uint8_t)(vol > 64 ? 64 : vol);
        samplePitchFromRate(sm, c5 ? c5 : 8363);
        if (ptr < size) {
            ByteReader dr(data, size);
            dr.seek(ptr);
            const size_t bps = (flags & 4) ? 2 : 1;
            const uint32_t most = (uint32_t)(budget / bps);
            pcReadPcm(dr, sm, len > most ? most : len, (flags & 4) != 0, format == 2);
            budget -= (size_t)sm.length * bps;
            if (sm.length < len) s.loadFlags |= LOAD_TRUNCATED;
        }
        if (flags & 1) pcLoop(sm, ls, le);
    }

    // patterns: 64 rows, packed (0 = end of row; channel | 0x20 note+instr | 0x40 volume | 0x80 effect)
    const int pats = patNum > 256 ? 256 : patNum;
    if (patNum > 256) s.loadFlags |= LOAD_TRUNCATED;
    for (int p = 0; p < pats; ++p) {
        Pattern pat = makePattern(64, channels);
        const size_t off = (size_t)patPara[p] * 16;
        if (patPara[p] && off + 2 <= size) {
            ByteReader pr(data, size);
            pr.seek(off + 2);
            for (int row = 0; row < 64 && !pr.overrun();) {
                const int what = pr.u8();
                if (!what) {
                    ++row;
                    continue;
                }
                Cell dummy;
                const int ch = what & 31;
                Cell& c = ch < channels ? pat.at(row, ch) : dummy;
                if (what & 0x20) {
                    const int note = pr.u8(), ins = pr.u8();
                    if (note == 0xFE) c.note = NOTE_OFF;
                    else if (note < 0xF0 && (note & 15) < 12) c.note = (uint8_t)clampNote((note >> 4) * 12 + (note & 15) + 1);
                    c.instr = (uint8_t)ins;
                }
                if (what & 0x40) {
                    const int v = pr.u8();
                    if (v != 0xFF) c.vol = (uint8_t)(0x10 + (v > 64 ? 64 : v));
                }
                if (what & 0x80) {
                    const int cmd = pr.u8(), info = pr.u8();
                    if (cmd) pcEffect(c, cmd, info, false);
                }
            }
        }
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }
    pcRemapJumps(s, remap);
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, channels));
    if (s.order.empty()) s.order.push_back(0);
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}

// ---------------------------------------------------------------- MTM

bool mtmDetect(const uint8_t* d, size_t n) { return n >= 66 && std::memcmp(d, "MTM", 3) == 0 && d[3] >= 0x10; }

Result<Song> mtmLoad(const uint8_t* data, size_t size) {
    if (size < 66) return Result<Song>::fail("BAD MTM FILE");
    ByteReader r(data, size);
    Song s;
    s.quirks = Quirks::PT23;
    s.freqMode = FreqMode::Amiga;
    pcName(s.name, data + 4, 20, 20);
    r.seek(24);
    const int numTracks = r.u16le(), lastPattern = r.u8(), lastOrder = r.u8();
    const int commentSize = r.u16le(), numSamples = r.u8();
    r.u8();
    r.u8();  // rows per track: always 64 in practice
    const int channels = r.u8();
    uint8_t pan[32];
    r.bytes(pan, 32);
    if (channels < 1 || channels > 32 || lastOrder > 127) return Result<Song>::fail("BAD MTM FILE");
    s.numChannels = (uint8_t)(channels < 4 ? 4 : channels);
    for (int i = 0; i < 32; ++i) s.channelPan[i] = (uint8_t)((pan[i] & 15) * 17);
    struct Hdr {
        uint32_t len, ls, le;
        int ft, vol, attr;
    };
    std::vector<Hdr> hdr(numSamples);
    s.samples.resize(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        uint8_t nm[22];
        r.bytes(nm, 22);
        pcName(s.samples[i].name, nm, 22, 22);
        hdr[i].len = r.u32le();
        hdr[i].ls = r.u32le();
        hdr[i].le = r.u32le();
        hdr[i].ft = (int8_t)r.u8();
        hdr[i].vol = r.u8();
        hdr[i].attr = r.u8();
    }
    uint8_t orders[128];
    r.bytes(orders, 128);
    const std::vector<int> remap = pcOrders(s, orders, lastOrder + 1);
    const size_t tracks = r.pos();
    r.skip((size_t)numTracks * 192);
    for (int p = 0; p <= lastPattern; ++p) {
        Pattern pat = makePattern(64, s.numChannels);
        for (int ch = 0; ch < 32; ++ch) {
            const int t = r.u16le();
            if (!t || t > numTracks || ch >= channels) continue;
            const size_t base = tracks + (size_t)(t - 1) * 192;
            if (base + 192 > size) continue;
            for (int row = 0; row < 64; ++row) {
                const uint8_t* b = data + base + row * 3;
                Cell& c = pat.at(row, ch);
                if (b[0] & 0xFC) c.note = (uint8_t)clampNote((b[0] >> 2) + 25);
                c.instr = (uint8_t)(((b[0] & 3) << 4) | (b[1] >> 4));
                int cmd = b[1] & 15, param = b[2];
                if (cmd == 0xA) param = (param & 0xF0) ? (param & 0xF0) : (param & 0x0F);
                else if (cmd == 0x8) cmd = param = 0;  // no 8xx in MultiTracker
                else if (cmd == 0xE) {
                    const int sub = param >> 4;
                    if (sub == 0 || sub == 3 || sub == 4 || sub == 6 || sub == 7 || sub == 0xF) cmd = param = 0;
                }
                c.fx = (uint8_t)cmd;
                c.param = (uint8_t)param;
            }
        }
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }
    r.skip(commentSize);
    for (int i = 0; i < numSamples; ++i) {
        Sample& sm = s.samples[i];
        const Hdr& h = hdr[i];
        sm.volume = (uint8_t)(h.vol > 64 ? 64 : h.vol);
        sm.finetune = (int8_t)((h.ft < -8 ? -8 : (h.ft > 7 ? 7 : h.ft)) * 16);
        if (h.len <= 2) {
            r.skip(h.len);
            continue;
        }
        const bool is16 = (h.attr & 1) != 0;
        pcReadPcm(r, sm, is16 ? h.len / 2 : h.len, is16, true);
        if (sm.length < (is16 ? h.len / 2 : h.len)) s.loadFlags |= LOAD_TRUNCATED;
        uint32_t ls = h.ls, le = h.le ? h.le - 1 : 0;
        if (is16) ls /= 2, le /= 2;
        if (le > 2 && ls + 4 < le) pcLoop(sm, ls, le);
    }
    pcRemapJumps(s, remap);
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, s.numChannels));
    if (s.order.empty()) s.order.push_back(0);
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}

// ---------------------------------------------------------------- Composer 669

bool c669Detect(const uint8_t* d, size_t n) {
    if (n < 0x1F1 || !((d[0] == 'i' && d[1] == 'f') || (d[0] == 'J' && d[1] == 'N'))) return false;
    if (d[0x6E] > 64 || d[0x6F] > 128 || d[0x70] >= 128) return false;
    for (int i = 0; i < 128; ++i) {
        const int o = d[0x71 + i], tempo = d[0xF1 + i], brk = d[0x171 + i];
        if ((o >= 128 && o < 0xFE) || (o < 128 && tempo == 0) || tempo > 15 || brk >= 64) return false;
    }
    return true;
}

Result<Song> c669Load(const uint8_t* data, size_t size) {
    if (!c669Detect(data, size)) return Result<Song>::fail("BAD 669 FILE");
    const bool extended = data[0] == 'J';
    const int numSamples = data[0x6E], numPatterns = data[0x6F];
    Song s = makeEmptySong(8);
    s.patterns.clear();
    s.order.clear();
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Amiga;
    s.initialSpeed = 4;
    s.initialBpm = 78;  // Composer 669 plays at 78 BPM
    pcName(s.name, data + 2, 20, 20);
    for (int i = 0; i < 8; ++i) s.channelPan[i] = (i & 1) ? 0xD0 : 0x30;
    for (int i = 0; i < 128; ++i)
        if (data[0x71 + i] < 128) s.order.push_back(data[0x71 + i]);
        else if (data[0x71 + i] == 0xFF) break;
    ByteReader r(data, size);
    r.seek(0x1F1);
    struct Hdr {
        uint32_t len, ls, le;
    };
    std::vector<Hdr> hdr(numSamples);
    s.samples.resize(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        s.samples[i].hasPan = false;
        uint8_t nm[13];
        r.bytes(nm, 13);
        pcName(s.samples[i].name, nm, 13, 22);
        hdr[i] = {r.u32le(), r.u32le(), r.u32le()};
    }
    for (int p = 0; p < numPatterns; ++p) {
        Pattern pat = makePattern(64, 8);
        for (int row = 0; row < 64; ++row)
            for (int ch = 0; ch < 8; ++ch) {
                const int b0 = r.u8(), b1 = r.u8(), eff = r.u8();
                Cell& c = pat.at(row, ch);
                if (b0 < 0xFE) {
                    c.note = (uint8_t)clampNote((b0 >> 2) + 25);
                    c.instr = (uint8_t)((((b0 & 3) << 4) | (b1 >> 4)) + 1);
                }
                if (b0 <= 0xFE) c.vol = (uint8_t)(0x10 + ((b1 & 15) * 64 + 8) / 15);
                if (eff == 0xFF) continue;
                const int cmd = eff >> 4, p4 = eff & 15;
                switch (cmd) {
                    case 0: setFx(c, 0x1, p4); break;               // slide up
                    case 1: setFx(c, 0x2, p4); break;               // slide down
                    case 2: setFx(c, 0x3, p4); break;               // slide to note
                    case 4: setFx(c, 0x0, p4 | (p4 << 4)); break;  // frequency vibrato ~ arpeggio
                    case 5: if (p4) setFx(c, 0xF, p4); break;       // speed
                    case 7: if (extended && c.note) setFx(c, 27, p4); break;  // retrigger
                    default: break;
                }
            }
        // the pattern's speed and break row live in the header: put them into free effect slots
        auto place = [&](int row, int fx, int param) {
            for (int ch = 0; ch < 8; ++ch)
                if (!pat.at(row, ch).fx && !pat.at(row, ch).param) {
                    setFx(pat.at(row, ch), fx, param);
                    return;
                }
        };
        if (p < 128) {
            if (data[0xF1 + p]) place(0, 0xF, data[0xF1 + p]);  // speed 0 would stop the song
            if (data[0x171 + p] < 63) place(data[0x171 + p], 0xD, 0);
        }
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }
    for (int i = 0; i < numSamples; ++i) {
        Sample& sm = s.samples[i];
        pcReadPcm(r, sm, hdr[i].len > (1u << 26) ? (1u << 26) : hdr[i].len, false, true);
        if (sm.length < hdr[i].len) s.loadFlags |= LOAD_TRUNCATED;
        if (hdr[i].le && !(hdr[i].le > sm.length && hdr[i].ls == 0)) pcLoop(sm, hdr[i].ls, hdr[i].le);
    }
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, 8));
    if (s.order.empty()) s.order.push_back(0);
    if (data[0x70] < s.order.size()) s.restartPos = data[0x70];
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    return Result<Song>::ok(static_cast<Song&&>(s));
}
