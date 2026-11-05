// FastTracker 2 XM loader (version 0x0104).
#include <cstring>

#include "core/reader.h"
#include "formats/formats_internal.h"

namespace {

constexpr uint16_t kNoSample = 0xFFFF;

void readEnvelope(ByteReader& r, Envelope& e) {
    for (int i = 0; i < 12; ++i) {
        e.pts[i].tick = r.u16le();
        uint16_t v = r.u16le();
        e.pts[i].value = (uint8_t)(v > 64 ? 64 : v);
    }
}

void fixEnvelope(Envelope& e) {
    if (e.count > 12) e.count = 12;
    if (e.count == 0) e.flags &= (uint8_t)~ENV_ON;
    if (e.sustain >= e.count) e.flags &= (uint8_t)~ENV_SUSTAIN;
    if (e.loopStart >= e.count || e.loopEnd >= e.count || e.loopStart > e.loopEnd) e.flags &= (uint8_t)~ENV_LOOP;
}

}  // namespace

bool xmDetect(const uint8_t* data, size_t size) {
    return size >= 60 && std::memcmp(data, "Extended Module: ", 17) == 0;
}

Result<Song> xmLoad(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    Song s;
    s.quirks = Quirks::FT2;
    r.seek(17);
    r.bytes(s.name, 20);
    s.name[20] = 0;
    r.seek(60);
    uint32_t headerSize = r.u32le();
    uint16_t songLen = r.u16le();
    uint16_t restart = r.u16le();
    uint16_t channels = r.u16le();
    uint16_t numPatterns = r.u16le();
    uint16_t numInstruments = r.u16le();
    uint16_t flags = r.u16le();
    uint16_t speed = r.u16le();
    uint16_t bpm = r.u16le();
    if (r.overrun()) return Result<Song>::fail("BAD XM HEADER");
    if (channels > kMaxChannels) return Result<Song>::fail("UNSUPPORTED CHANNELS");
    if (channels == 0 || songLen > 256 || numPatterns > 256 || numInstruments > 128 || headerSize < 20)
        return Result<Song>::fail("BAD XM HEADER");

    s.numChannels = (uint8_t)channels;
    s.freqMode = (flags & 1) ? FreqMode::Linear : FreqMode::Amiga;
    s.initialSpeed = (uint8_t)(speed == 0 ? 6 : (speed > 31 ? 31 : speed));
    s.initialBpm = (uint8_t)(bpm < 32 ? 125 : (bpm > 255 ? 255 : bpm));
    for (int c = 0; c < kMaxChannels; ++c) s.channelPan[c] = 128;
    uint8_t order[256];
    r.bytes(order, 256);
    if (songLen == 0) songLen = 1;
    s.order.assign(order, order + songLen);
    s.restartPos = (uint8_t)(restart < songLen ? restart : 0);

    const size_t allocCap = size * 4 + 65536;
    size_t allocated = 0;

    r.seek(60 + (size_t)headerSize);
    s.patterns.reserve(numPatterns);
    for (int p = 0; p < numPatterns; ++p) {
        size_t start = r.pos();
        uint32_t phLen = r.u32le();
        r.u8();  // packing type
        uint16_t rows = r.u16le();
        uint16_t packed = r.u16le();
        if (rows == 0 || rows > 256) rows = 64;
        Pattern pat = makePattern(rows, channels);
        allocated += pat.cells.size() * sizeof(Cell);
        r.seek(start + (phLen < 9 ? 9 : phLen));
        size_t dataEnd = r.pos() + packed;
        if (packed) {
            for (int row = 0; row < rows; ++row) {
                for (int c = 0; c < channels; ++c) {
                    if (r.pos() >= dataEnd) break;
                    Cell cell;
                    uint8_t b = r.u8();
                    if (b & 0x80) {
                        if (b & 1) cell.note = r.u8();
                        if (b & 2) cell.instr = r.u8();
                        if (b & 4) cell.vol = r.u8();
                        if (b & 8) cell.fx = r.u8();
                        if (b & 16) cell.param = r.u8();
                    } else {
                        cell.note = b;
                        cell.instr = r.u8();
                        cell.vol = r.u8();
                        cell.fx = r.u8();
                        cell.param = r.u8();
                    }
                    if (cell.note > NOTE_OFF) cell.note = NOTE_NONE;
                    if (cell.fx > 35) cell.fx = cell.param = 0;
                    pat.at(row, c) = cell;
                }
            }
        }
        r.seek(dataEnd);
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, channels));
    for (auto& o : s.order)
        if (o >= s.patterns.size()) o = 0;

    s.instruments.resize(numInstruments);
    for (int i = 0; i < numInstruments; ++i) {
        Instrument& in = s.instruments[i];
        for (auto& m : in.sampleMap) m = kNoSample;
        size_t start = r.pos();
        uint32_t instSize = r.u32le();
        r.bytes(in.name, 22);
        in.name[22] = 0;
        r.u8();  // type
        uint16_t numSamples = r.u16le();
        if (numSamples > 16) numSamples = 16;
        if (numSamples == 0 || r.overrun()) {
            r.seek(start + (instSize < 29 ? 29 : instSize));
            continue;
        }
        uint32_t sampleHeaderSize = r.u32le();
        uint8_t map[96];
        r.bytes(map, 96);
        readEnvelope(r, in.volEnv);
        readEnvelope(r, in.panEnv);
        in.volEnv.count = r.u8();
        in.panEnv.count = r.u8();
        in.volEnv.sustain = r.u8();
        in.volEnv.loopStart = r.u8();
        in.volEnv.loopEnd = r.u8();
        in.panEnv.sustain = r.u8();
        in.panEnv.loopStart = r.u8();
        in.panEnv.loopEnd = r.u8();
        in.volEnv.flags = r.u8() & 7;
        in.panEnv.flags = r.u8() & 7;
        in.vibType = r.u8();
        in.vibSweep = r.u8();
        in.vibDepth = r.u8();
        in.vibRate = r.u8();
        in.fadeout = r.u16le();
        fixEnvelope(in.volEnv);
        fixEnvelope(in.panEnv);
        r.seek(start + (instSize < 29 ? 29 : instSize));

        const size_t base = s.samples.size();
        for (int n = 0; n < 96; ++n) in.sampleMap[n] = map[n] < numSamples ? (uint16_t)(base + map[n]) : kNoSample;

        struct Raw {
            uint32_t len, ls, ll;
            uint8_t type;
        };
        Raw raw[16];
        for (int k = 0; k < numSamples; ++k) {
            size_t sh = r.pos();
            Sample sm;
            raw[k].len = r.u32le();
            raw[k].ls = r.u32le();
            raw[k].ll = r.u32le();
            uint8_t v = r.u8();
            sm.volume = v > 64 ? 64 : v;
            sm.finetune = (int8_t)r.u8();
            raw[k].type = r.u8();
            sm.pan = r.u8();
            sm.relativeNote = (int8_t)r.u8();
            r.u8();
            r.bytes(sm.name, 22);
            sm.name[22] = 0;
            r.seek(sh + (sampleHeaderSize < 40 ? 40 : sampleHeaderSize));
            s.samples.push_back(static_cast<Sample&&>(sm));
        }
        for (int k = 0; k < numSamples; ++k) {
            Sample& sm = s.samples[base + k];
            sm.is16 = (raw[k].type & 0x10) != 0;
            uint32_t bytes = raw[k].len;
            if (bytes > allocCap - allocated) {
                bytes = (uint32_t)(allocCap - allocated);
                s.loadFlags |= LOAD_TRUNCATED;
            }
            uint32_t bps = sm.is16 ? 2 : 1;
            sm.length = bytes / bps;
            allocated += bytes;
            if (r.remaining() < (size_t)sm.length * bps) s.loadFlags |= LOAD_TRUNCATED;
            if (sm.is16) {
                sm.data16.resize(sm.length);
                int16_t old = 0;
                for (uint32_t j = 0; j < sm.length; ++j) {
                    bool have = r.remaining() >= 2;
                    old = (int16_t)(old + (int16_t)r.u16le());
                    sm.data16[j] = have ? old : 0;
                }
            } else {
                sm.data8.resize(sm.length);
                int8_t old = 0;
                for (uint32_t j = 0; j < sm.length; ++j) {
                    bool have = r.remaining() >= 1;
                    old = (int8_t)(old + (int8_t)r.u8());
                    sm.data8[j] = have ? old : 0;
                }
            }
            if (bytes < raw[k].len) r.skip(raw[k].len - bytes);
            uint32_t ls = raw[k].ls / bps, ll = raw[k].ll / bps;
            uint8_t lt = raw[k].type & 3;
            if (lt && ll >= 2 && ls < sm.length) {
                if (ls + ll > sm.length) ll = sm.length - ls;
                if (ll >= 2) {
                    sm.loop = lt == 2 ? LoopType::PingPong : LoopType::Forward;
                    sm.loopStart = ls;
                    sm.loopLength = ll;
                }
            }
        }
    }
    if (r.overrun()) s.loadFlags |= LOAD_TRUNCATED;
    return Result<Song>::ok(static_cast<Song&&>(s));
}
