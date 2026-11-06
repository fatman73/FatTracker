#include "formats/sample_io.h"

#include <cmath>
#include <cstring>

#include "core/reader.h"
#include "formats/formats_internal.h"

namespace {
// Sets relative note / finetune so the sample keeps its pitch at C-4 (8363 Hz reference).
void setPitchFromRate(Sample& s, uint32_t rate) {
    if (!rate) return;
    const double semis = 12.0 * std::log2(rate / 8363.0);
    int rel = (int)std::floor(semis + 1e-9);
    int ft = (int)std::lround((semis - rel) * 128.0);
    if (ft >= 128) {
        ++rel;
        ft -= 128;
    }
    s.relativeNote = (int8_t)(rel < -96 ? -96 : (rel > 95 ? 95 : rel));
    s.finetune = (int8_t)(ft > 127 ? 127 : ft);
}

uint32_t rateFromPitch(const Sample& s) {
    return (uint32_t)std::lround(8363.0 * std::pow(2.0, (s.relativeNote + s.finetune / 128.0) / 12.0));
}

// Interleaved PCM to a mono sample (channels averaged); 8-bit input stays 8-bit, deeper input
// becomes 16-bit. `bps` bytes per sample (1..4); integer samples are left-justified in them.
struct PcmFormat {
    uint32_t channels = 1, bps = 2;
    bool bigEndian = false;
    bool unsigned8 = false;  // WAV 8-bit is unsigned, AIFF 8-bit is signed
    bool isFloat = false;    // 32-bit IEEE float
};

Sample decodePcm(const uint8_t* pcm, uint32_t frames, const PcmFormat& f) {
    const uint32_t frame = f.bps * f.channels;
    Sample s;
    s.is16 = f.bps != 1;
    s.length = frames;
    if (s.is16)
        s.data16.resize(frames);
    else
        s.data8.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (uint32_t c = 0; c < f.channels; ++c) {
            const uint8_t* p = pcm + (size_t)i * frame + c * f.bps;
            uint32_t u = 0;  // the sample's bytes, most significant first, in the top of `u`
            for (uint32_t k = 0; k < f.bps; ++k) u |= (uint32_t)p[f.bigEndian ? k : f.bps - 1 - k] << (24 - 8 * k);
            double v;
            if (f.isFloat) {
                float x;
                std::memcpy(&x, &u, 4);
                v = x == x ? x : 0.0;  // NaN is silence
            } else {
                if (f.unsigned8 && f.bps == 1) u ^= 0x80000000u;
                v = (int32_t)u / 2147483648.0;
            }
            sum += v;
        }
        const double m = sum / f.channels;
        if (s.is16) {
            const double v = std::round(m * 32768.0);
            s.data16[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        } else {
            const double v = std::round(m * 128.0);
            s.data8[i] = (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v));
        }
    }
    return s;
}

Result<Sample> loadWav(ByteReader& r) {
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    bool haveFmt = false;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;
    uint32_t loopStart = 0, loopEnd = 0, loopType = 0;
    bool haveLoop = false;
    r.seek(12);
    while (r.remaining() >= 8) {
        uint8_t id[4];
        r.bytes(id, 4);
        const uint32_t len = r.u32le();
        const size_t body = r.pos();
        if (!std::memcmp(id, "fmt ", 4) && len >= 16) {
            format = r.u16le();
            channels = r.u16le();
            rate = r.u32le();
            r.u32le();
            r.u16le();
            bits = r.u16le();
            if (format == 0xFFFE && len >= 26) {
                r.skip(8);
                format = r.u16le();
            }
            haveFmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            pcm = r.data() + body;
            pcmBytes = len < r.remaining() ? len : r.remaining();
        } else if (!std::memcmp(id, "smpl", 4) && len >= 60) {
            r.skip(28);
            const uint32_t loops = r.u32le();
            r.u32le();
            if (loops) {
                r.u32le();
                loopType = r.u32le();
                loopStart = r.u32le();
                loopEnd = r.u32le();
                haveLoop = true;
            }
        }
        r.seek(body + len + (len & 1));
        if (r.overrun()) break;
    }
    if (!haveFmt || !pcm || channels == 0 || channels > 8) return Result<Sample>::fail("BAD WAV FILE");
    const bool isFloat = format == 3 && bits == 32;
    if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !isFloat)
        return Result<Sample>::fail("UNSUPPORTED WAV FORMAT");
    PcmFormat pf;
    pf.channels = channels;
    pf.bps = bits / 8;
    pf.unsigned8 = true;
    pf.isFloat = isFloat;
    const uint32_t frames = (uint32_t)(pcmBytes / (pf.bps * channels));
    if (frames == 0) return Result<Sample>::fail("EMPTY SAMPLE FILE");
    Sample s = decodePcm(pcm, frames, pf);
    setPitchFromRate(s, rate);
    if (haveLoop && loopEnd >= loopStart && loopEnd < frames) {
        s.loop = loopType == 1 ? LoopType::PingPong : LoopType::Forward;
        s.loopStart = loopStart;
        s.loopLength = loopEnd - loopStart + 1;
        if (s.loopLength < 2) s.loop = LoopType::None;
    }
    return Result<Sample>::ok(static_cast<Sample&&>(s));
}

// 80-bit IEEE 754 extended (big-endian), as AIFF stores the sample rate. 0 when not a sane rate.
double extended80(const uint8_t* p) {
    const int exp = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) mant = (mant << 8) | p[2 + i];
    if ((p[0] & 0x80) || exp == 0x7FFF || !mant) return 0;
    const double v = std::ldexp((double)mant, exp - 16383 - 63);
    return v >= 1.0 && v <= 1e7 ? v : 0;
}

// AIFF and AIFF-C (compression NONE/twos, sowt, fl32/FL32). Big-endian IFF chunks: COMM, SSND,
// and the INST sustain loop whose ends are MARK markers.
Result<Sample> loadAiff(ByteReader& r, bool aifc) {
    uint32_t channels = 0, frameCount = 0, bits = 0;
    double rate = 0;
    PcmFormat pf;
    pf.bigEndian = true;
    bool haveComm = false;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;
    struct Marker {
        uint16_t id;
        uint32_t pos;
    };
    std::vector<Marker> markers;
    uint16_t loopMode = 0, loopBegin = 0, loopEnd = 0;
    r.seek(12);
    while (r.remaining() >= 8) {
        uint8_t id[4];
        r.bytes(id, 4);
        const uint32_t len = r.u32be();
        const size_t body = r.pos();
        const bool whole = len <= r.remaining();
        if (!std::memcmp(id, "COMM", 4)) {
            if (!whole || len < 18) return Result<Sample>::fail("BAD AIFF FILE");
            channels = r.u16be();
            frameCount = r.u32be();
            bits = r.u16be();
            uint8_t ext[10];
            r.bytes(ext, 10);
            rate = extended80(ext);
            if (aifc) {
                if (len < 22) return Result<Sample>::fail("BAD AIFF FILE");
                uint8_t c[4];
                r.bytes(c, 4);
                if (!std::memcmp(c, "sowt", 4)) {
                    pf.bigEndian = false;
                } else if (!std::memcmp(c, "fl32", 4) || !std::memcmp(c, "FL32", 4)) {
                    pf.isFloat = true;
                } else if (std::memcmp(c, "NONE", 4) && std::memcmp(c, "twos", 4)) {
                    return Result<Sample>::fail("UNSUPPORTED AIFF-C COMPRESSION");
                }
            }
            haveComm = true;
        } else if (!std::memcmp(id, "SSND", 4)) {
            if (len < 8 || r.remaining() < 8) return Result<Sample>::fail("BAD AIFF FILE");
            const uint32_t offset = r.u32be();
            r.u32be();  // block size
            const size_t avail = whole ? len - 8 : r.remaining();  // a cut-off last chunk keeps what is there
            if (offset > avail) return Result<Sample>::fail("BAD AIFF FILE");
            pcm = r.data() + body + 8 + offset;
            pcmBytes = avail - offset;
        } else if (!std::memcmp(id, "MARK", 4) && whole && len >= 2) {
            const size_t end = body + len;
            uint32_t n = r.u16be();
            markers.clear();
            while (n-- && r.pos() + 7 <= end) {
                Marker m;
                m.id = r.u16be();
                m.pos = r.u32be();
                const uint8_t nameLen = r.u8();
                r.skip(nameLen + ((nameLen & 1) ? 0 : 1));  // count byte + text padded to an even size
                if (r.pos() > end) break;
                markers.push_back(m);
            }
        } else if (!std::memcmp(id, "INST", 4) && whole && len >= 20) {
            r.skip(8);  // base note, detune, note and velocity range, gain
            loopMode = r.u16be();
            loopBegin = r.u16be();
            loopEnd = r.u16be();
        }
        if (!whole) break;
        r.seek(body + len + (len & 1));
        if (r.overrun()) break;
    }
    if (!haveComm || !pcm || channels == 0 || channels > 8) return Result<Sample>::fail("BAD AIFF FILE");
    if (bits < 1 || bits > 32 || (pf.isFloat && bits != 32)) return Result<Sample>::fail("UNSUPPORTED AIFF FORMAT");
    pf.channels = channels;
    pf.bps = (bits + 7) / 8;
    const size_t avail = pcmBytes / (pf.bps * channels);
    const uint32_t frames = frameCount < avail ? frameCount : (uint32_t)avail;
    if (frames == 0) return Result<Sample>::fail("EMPTY SAMPLE FILE");
    Sample s = decodePcm(pcm, frames, pf);
    setPitchFromRate(s, (uint32_t)std::lround(rate));
    if (loopMode == 1 || loopMode == 2) {
        const Marker* b = nullptr;
        const Marker* e = nullptr;
        for (const Marker& m : markers) {
            if (m.id == loopBegin) b = &m;
            if (m.id == loopEnd) e = &m;
        }
        // markers sit between frames: the loop is [begin, end)
        if (b && e && e->pos <= frames && e->pos >= b->pos + 2) {
            s.loop = loopMode == 2 ? LoopType::PingPong : LoopType::Forward;
            s.loopStart = b->pos;
            s.loopLength = e->pos - b->pos;
        }
    }
    return Result<Sample>::ok(static_cast<Sample&&>(s));
}

Result<Sample> load8svx(ByteReader& r) {
    r.seek(12);
    uint32_t oneShot = 0, repeat = 0, volume = 0x10000;
    uint16_t rate = 8363;
    bool haveHeader = false;
    Sample s;
    while (r.remaining() >= 8) {
        uint8_t id[4];
        r.bytes(id, 4);
        const uint32_t len = r.u32be();
        const size_t body = r.pos();
        if (!std::memcmp(id, "VHDR", 4) && len >= 20) {
            oneShot = r.u32be();
            repeat = r.u32be();
            r.u32be();
            rate = r.u16be();
            r.u8();
            if (r.u8() != 0) return Result<Sample>::fail("COMPRESSED 8SVX NOT SUPPORTED");
            volume = r.u32be();
            haveHeader = true;
        } else if (!std::memcmp(id, "NAME", 4)) {
            r.bytes(s.name, len < 22 ? len : 22);
            s.name[22] = 0;
        } else if (!std::memcmp(id, "BODY", 4)) {
            const uint32_t n = (uint32_t)(len < r.remaining() ? len : r.remaining());
            s.length = n;
            s.data8.resize(n);
            r.bytes(s.data8.data(), n);
        }
        r.seek(body + len + (len & 1));
        if (r.overrun()) break;
    }
    if (!haveHeader) return Result<Sample>::fail("BAD IFF 8SVX FILE");
    if (s.length == 0) return Result<Sample>::fail("EMPTY SAMPLE FILE");
    s.volume = (uint8_t)(volume >= 0x10000 ? 64 : volume * 64 / 0x10000);
    setPitchFromRate(s, rate);
    if (repeat >= 2 && oneShot < s.length) {
        s.loop = LoopType::Forward;
        s.loopStart = oneShot;
        s.loopLength = repeat;
        if (s.loopStart + s.loopLength > s.length) s.loopLength = s.length - s.loopStart;
    }
    return Result<Sample>::ok(static_cast<Sample&&>(s));
}

void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }
void le16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}
void le32(std::vector<uint8_t>& b, uint32_t v) {
    le16(b, v & 0xFFFF);
    le16(b, v >> 16);
}
void be16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}
void be32(std::vector<uint8_t>& b, uint32_t v) {
    be16(b, v >> 16);
    be16(b, v & 0xFFFF);
}
}  // namespace

Result<Sample> loadSampleFile(const uint8_t* data, size_t size) {
    if (!data || size < 12) return Result<Sample>::fail("NOT A SAMPLE FILE");
    ByteReader r(data, size);
    if (!std::memcmp(data, "RIFF", 4) && !std::memcmp(data + 8, "WAVE", 4)) return loadWav(r);
    if (!std::memcmp(data, "FORM", 4) && !std::memcmp(data + 8, "8SVX", 4)) return load8svx(r);
    if (!std::memcmp(data, "FORM", 4) && !std::memcmp(data + 8, "AIFF", 4)) return loadAiff(r, false);
    if (!std::memcmp(data, "FORM", 4) && !std::memcmp(data + 8, "AIFC", 4)) return loadAiff(r, true);
    return Result<Sample>::fail("NOT A SAMPLE FILE");
}

std::vector<uint8_t> saveSampleWav(const Sample& s) {
    std::vector<uint8_t> b;
    const uint32_t bps = s.is16 ? 2 : 1, data = s.length * bps, rate = rateFromPitch(s);
    const bool loop = s.loop != LoopType::None && s.loopLength >= 2;
    tag(b, "RIFF");
    le32(b, 4 + 24 + 8 + data + (data & 1) + (loop ? 8 + 60 : 0));
    tag(b, "WAVE");
    tag(b, "fmt ");
    le32(b, 16);
    le16(b, 1);
    le16(b, 1);
    le32(b, rate);
    le32(b, rate * bps);
    le16(b, bps);
    le16(b, bps * 8);
    tag(b, "data");
    le32(b, data);
    for (uint32_t i = 0; i < s.length; ++i) {
        if (s.is16)
            le16(b, (uint16_t)s.data16[i]);
        else
            b.push_back((uint8_t)(s.data8[i] + 128));
    }
    if (data & 1) b.push_back(0);
    if (loop) {
        tag(b, "smpl");
        le32(b, 60);
        for (int i = 0; i < 7; ++i) le32(b, i == 3 ? 60 : 0);  // midi unity note 60
        le32(b, 1);
        le32(b, 0);
        le32(b, 0);
        le32(b, s.loop == LoopType::PingPong ? 1 : 0);
        le32(b, s.loopStart);
        le32(b, s.loopStart + s.loopLength - 1);
        le32(b, 0);
        le32(b, 0);
    }
    return b;
}

std::vector<uint8_t> saveSample8svx(const Sample& s) {
    std::vector<uint8_t> body(s.length);
    for (uint32_t i = 0; i < s.length; ++i) body[i] = (uint8_t)(s.is16 ? (s.data16[i] >> 8) : s.data8[i]);
    const bool loop = s.loop != LoopType::None && s.loopLength >= 2;
    std::vector<uint8_t> b;
    char name[22] = {};
    std::memcpy(name, s.name, 22);
    const uint32_t nameLen = 22;
    tag(b, "FORM");
    be32(b, 4 + 8 + 20 + 8 + nameLen + 8 + s.length + (s.length & 1));
    tag(b, "8SVX");
    tag(b, "VHDR");
    be32(b, 20);
    be32(b, loop ? s.loopStart : s.length);
    be32(b, loop ? s.loopLength : 0);
    be32(b, 32);
    const uint32_t rate = rateFromPitch(s);
    be16(b, rate > 65535 ? 65535 : rate);
    b.push_back(1);
    b.push_back(0);
    be32(b, (uint32_t)s.volume * 0x10000 / 64);
    tag(b, "NAME");
    be32(b, nameLen);
    b.insert(b.end(), name, name + nameLen);
    tag(b, "BODY");
    be32(b, s.length);
    b.insert(b.end(), body.begin(), body.end());
    if (s.length & 1) b.push_back(0);
    return b;
}

void samplePitchFromRate(Sample& s, double rateHz) {
    if (rateHz <= 0) return;
    const double semis = 12.0 * std::log2(rateHz / 8363.0);
    int rel = (int)std::floor(semis + 1e-9);
    int ft = (int)std::lround((semis - rel) * 128.0);
    if (ft >= 128) {
        ++rel;
        ft -= 128;
    }
    s.relativeNote = (int8_t)(rel < -96 ? -96 : (rel > 95 ? 95 : rel));
    s.finetune = (int8_t)(ft > 127 ? 127 : ft);
}
