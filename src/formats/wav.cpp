#include "formats/wav.h"

#include <cmath>
#include <cstring>

#include "core/reader.h"

namespace {
void put16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }
}  // namespace

std::vector<uint8_t> writeWav(const float* in, size_t frames, uint32_t rate, bool float32) {
    const uint32_t bps = float32 ? 4 : 2;
    const uint32_t dataBytes = (uint32_t)(frames * 2 * bps);
    std::vector<uint8_t> b;
    b.reserve(44 + dataBytes);
    tag(b, "RIFF");
    put32(b, 36 + dataBytes);
    tag(b, "WAVE");
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, float32 ? 3 : 1);
    put16(b, 2);
    put32(b, rate);
    put32(b, rate * 2 * bps);
    put16(b, 2 * bps);
    put16(b, bps * 8);
    tag(b, "data");
    put32(b, dataBytes);
    uint32_t seed = 0x12345678u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) / 16777216.f;
    };
    for (size_t i = 0; i < frames * 2; ++i) {
        float v = in[i];
        if (float32) {
            uint32_t u;
            std::memcpy(&u, &v, 4);
            put32(b, u);
        } else {
            float d = (rnd() - rnd()) / 32768.f;  // TPDF, +-1 LSB
            float x = (v + d) * 32767.f;
            long s = std::lround(x);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            put16(b, (uint32_t)(uint16_t)(int16_t)s);
        }
    }
    return b;
}

Result<WavData> readWav(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    uint8_t id[4];
    r.bytes(id, 4);
    if (std::memcmp(id, "RIFF", 4) != 0) return Result<WavData>::fail("NOT A WAV FILE");
    r.u32le();
    r.bytes(id, 4);
    if (std::memcmp(id, "WAVE", 4) != 0) return Result<WavData>::fail("NOT A WAV FILE");
    WavData w;
    uint16_t format = 0, bits = 0;
    bool haveFmt = false;
    while (r.remaining() >= 8) {
        r.bytes(id, 4);
        uint32_t len = r.u32le();
        size_t next = r.pos() + len + (len & 1);
        if (std::memcmp(id, "fmt ", 4) == 0) {
            format = r.u16le();
            w.channels = r.u16le();
            w.rate = r.u32le();
            r.u32le();
            r.u16le();
            bits = r.u16le();
            if (format == 0xFFFE && len >= 26) {  // WAVE_FORMAT_EXTENSIBLE: subformat GUID first word
                r.u16le();
                r.u16le();
                r.u32le();
                format = r.u16le();
            }
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!haveFmt || w.channels == 0) return Result<WavData>::fail("BAD WAV FILE");
            size_t avail = len < r.remaining() ? len : r.remaining();
            if (format == 1 && bits == 16) {
                w.samples.resize(avail / 2);
                for (auto& s : w.samples) s = (int16_t)r.u16le() / 32768.f;
            } else if (format == 1 && bits == 8) {
                w.samples.resize(avail);
                for (auto& s : w.samples) s = ((int)r.u8() - 128) / 128.f;
            } else if (format == 1 && bits == 24) {
                w.samples.resize(avail / 3);
                for (auto& s : w.samples) {
                    int32_t v = r.u8() | (r.u8() << 8) | (r.u8() << 16);
                    if (v & 0x800000) v |= ~0xFFFFFF;
                    s = v / 8388608.f;
                }
            } else if (format == 3 && bits == 32) {
                w.samples.resize(avail / 4);
                for (auto& s : w.samples) {
                    uint32_t u = r.u32le();
                    std::memcpy(&s, &u, 4);
                }
            } else {
                return Result<WavData>::fail("UNSUPPORTED WAV FORMAT");
            }
            return Result<WavData>::ok(static_cast<WavData&&>(w));
        }
        r.seek(next);
    }
    return Result<WavData>::fail("BAD WAV FILE");
}
