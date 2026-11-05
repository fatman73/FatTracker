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
    const uint32_t bps = bits / 8, frame = bps * channels;
    const uint32_t frames = (uint32_t)(pcmBytes / frame);
    Sample s;
    s.is16 = bits != 8;
    s.length = frames;
    if (s.is16)
        s.data16.resize(frames);
    else
        s.data8.resize(frames);
    for (uint32_t f = 0; f < frames; ++f) {
        double sum = 0;
        for (uint32_t c = 0; c < channels; ++c) {
            const uint8_t* p = pcm + (size_t)f * frame + c * bps;
            double v;
            if (bits == 8) v = (p[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t)(p[0] | (p[1] << 8)) / 32768.0;
            else if (bits == 24) {
                int32_t x = p[0] | (p[1] << 8) | (p[2] << 16);
                if (x & 0x800000) x |= ~0xFFFFFF;
                v = x / 8388608.0;
            } else if (isFloat) {
                float x;
                std::memcpy(&x, p, 4);
                v = x;
            } else {
                v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) / 2147483648.0;
            }
            sum += v;
        }
        const double m = sum / channels;
        if (s.is16) {
            long v = std::lround(m * 32768.0);
            s.data16[f] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        } else {
            long v = std::lround(m * 128.0);
            s.data8[f] = (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v));
        }
    }
    if (frames == 0) return Result<Sample>::fail("EMPTY SAMPLE FILE");
    setPitchFromRate(s, rate);
    if (haveLoop && loopEnd >= loopStart && loopEnd < frames) {
        s.loop = loopType == 1 ? LoopType::PingPong : LoopType::Forward;
        s.loopStart = loopStart;
        s.loopLength = loopEnd - loopStart + 1;
        if (s.loopLength < 2) s.loop = LoopType::None;
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
