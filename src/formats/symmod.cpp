// Symphonie / Symphonie Pro ("SymM") loader.
// Chunk parsing, sample decoding, instrument processing and the position -> pattern expansion follow
// the OpenMPT SymMOD loader by Devin Acker and the OpenMPT Devs (BSD licence).
// The module becomes an FT2-style Song (linear frequencies, no XM instruments):
// - every Symphonie position (pattern, start row, length, transpose, speed) becomes its own pattern
//   (split into 256-row pieces when longer), repeated in the order list as often as it loops;
// - instrument volume, filters, fades, reversing and sustain repeats are baked into the sample data;
// - Symphonie pans channels hard left / right. FT2 takes the panning from the sample on every
//   instrument change, so an instrument played on both sides gets a second, right-panned sample;
// - continuous Symphonie slides (volume, pitch) are approximated per row like OpenMPT does,
//   volume slides in the volume column, pitch slides as 1xx/2xx/E1x/E2x;
// - DSP (echo, delay, filter), CV, add-pitch/add-volume, emphasis and sample vibrato are dropped;
//   virtual instruments play their first source sample, transwave instruments are silent.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <type_traits>
#include <vector>

#include "core/reader.h"
#include "formats/formats_internal.h"
#include "formats/sample_io.h"

namespace {
enum : int32_t {
    CH_NUM_CHANNELS = -1,
    CH_TRACK_LENGTH = -2,
    CH_PATTERN_SIZE = -3,
    CH_NUM_INSTRUMENTS = -4,
    CH_EVENT_SIZE = -5,
    CH_TEMPO = -6,
    CH_EXTERNAL_SAMPLES = -7,
    CH_POSITION_LIST = -10,
    CH_SAMPLE_FILE = -11,
    CH_EMPTY_SAMPLE = -12,
    CH_PATTERN_EVENTS = -13,
    CH_INSTRUMENT_LIST = -14,
    CH_SEQUENCES = -15,
    CH_INFO_TEXT = -16,
    CH_SAMPLE_PACKED = -17,
    CH_SAMPLE_PACKED16 = -18,
    CH_INFO_TYPE = -19,
    CH_INFO_BINARY = -20,
    CH_INFO_STRING = -21,
    CH_SAMPLE_BOOST = 10,
    CH_STEREO_DETUNE = 11,
    CH_STEREO_PHASE = 12,
};

// event commands
enum : uint8_t {
    EV_KEY_ON = 0,
    EV_VOL_SLIDE_UP,
    EV_VOL_SLIDE_DOWN,
    EV_PITCH_SLIDE_UP,
    EV_PITCH_SLIDE_DOWN,
    EV_REPLAY_FROM,
    EV_FROM_AND_PITCH,
    EV_SET_FROM_ADD,
    EV_FROM_ADD,
    EV_SET_SPEED,
    EV_ADD_PITCH,
    EV_ADD_VOLUME,
    EV_TREMOLO,
    EV_VIBRATO,
    EV_SAMPLE_VIB,
    EV_PITCH_SLIDE_TO,
    EV_RETRIG,
    EV_EMPHASIS,
    EV_ADD_HALF_TONE,
    EV_CV,
    EV_CV_ADD,
    EV_FILTER = 23,
    EV_DSP_ECHO,
    EV_DSP_DELAY,
};

// key-on "volume" values above 200 are commands
enum : uint8_t {
    VOL_COMMAND = 200,
    VOL_STOP_SAMPLE = 254,
    VOL_CONT_SAMPLE = 253,
    VOL_KEY_OFF = 251,
    VOL_SPEED_DOWN = 250,
    VOL_SPEED_UP = 249,
    VOL_SET_PITCH = 248,
    VOL_PITCH_UP = 247,
    VOL_PITCH_DOWN = 246,
    VOL_PITCH_UP2 = 245,
    VOL_PITCH_DOWN2 = 244,
    VOL_PITCH_UP3 = 243,
    VOL_PITCH_DOWN3 = 242,
};

constexpr int8_t TYPE_LOOP = 4, TYPE_SUSTAIN = 8;
constexpr uint8_t CHAN_STEREO_L = 1, CHAN_STEREO_R = 2;
constexpr uint8_t SF_REVERSE = 1, SF_MIRROR = 4, SF_NEW_LOOP = 16;
constexpr uint8_t IF_NO_TRANSPOSE = 1, IF_SYNC_PLAY = 4;
constexpr uint8_t FX_R = 27, FX_X = 33;
constexpr int kMaxRows = 1024;  // the longest Symphonie track

struct Event {
    uint8_t cmd = 0;
    int8_t note = -1;
    uint8_t param = 0, inst = 0;
};

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// 256-byte instrument record: 128 bytes name (or virtual / transwave header), then the settings.
struct SymInst {
    const uint8_t* b;
    int8_t type() const { return (int8_t)b[128]; }
    uint8_t loopStartHigh() const { return b[129]; }
    uint8_t loopLenHigh() const { return b[130]; }
    uint8_t repetitions() const { return b[131]; }
    uint8_t channel() const { return b[132]; }
    uint8_t volume() const { return b[134]; }
    int8_t finetune() const { return (int8_t)b[138]; }
    int8_t transpose() const { return (int8_t)b[139]; }
    uint8_t sampleFlags() const { return b[140]; }
    int8_t filter() const { return (int8_t)b[141]; }
    uint8_t instFlags() const { return b[142]; }
    uint8_t downsample() const { return b[143]; }
    uint16_t loopStartFine() const { return be16(b + 150); }
    uint16_t loopLenFine() const { return be16(b + 152); }
    uint8_t filterFlags() const { return b[160]; }
    uint8_t filterPoints() const { return b[161]; }
    uint8_t cutoff(int i) const { return b[162 + i * 2]; }
    uint8_t resonance(int i) const { return b[163 + i * 2]; }
    uint8_t volFadeFlag() const { return b[170]; }
    uint8_t volFadeFrom() const { return b[171]; }
    uint8_t volFadeTo() const { return b[172]; }

    // "ViRT" header shared by virtual and transwave instruments
    bool virtValid() const {
        return !std::memcmp(b, "ViRT", 4) && b[4] == 0 && be16(b + 6) <= 1 && be16(b + 12) == 0 && be16(b + 16) == 20;
    }
    bool isVirtual() const { return virtValid() && be16(b + 6) == 0 && be16(b + 14) <= 20 && be16(b + 18) == 4; }
    bool isTranswave() const { return virtValid() && be16(b + 6) == 1 && be16(b + 14) == 2 && be16(b + 18) == 10; }
    uint8_t firstSource() const { return b[20 + 3]; }  // instrument of the first note event
    bool isEmpty() const { return b[0] == 0 || type() < 0; }
};

struct Position {
    uint16_t loopNum = 0, pattern = 0, start = 0, length = 0, speed = 0;
    int16_t transpose = 0;
    bool operator<(const Position& o) const {
        return std::tie(pattern, start, length, transpose, speed) < std::tie(o.pattern, o.start, o.length, o.transpose, o.speed);
    }
};

// Reads a length-prefixed chunk, unpacking the "PACK\xFF\xFF" run-length scheme. A chunk that runs
// past the end of the file (or unpacks to the wrong size) comes back empty and sets `truncated`.
std::vector<uint8_t> decodeChunk(ByteReader& r, bool& truncated) {
    std::vector<uint8_t> out;
    const uint32_t packed = r.u32be();
    if (r.overrun() || packed > r.remaining()) {
        truncated = true;
        r.seek(r.size());
        return out;
    }
    const uint8_t* c = r.data() + r.pos();
    r.skip(packed);
    if (packed >= 10 && !std::memcmp(c, "PACK\xFF\xFF", 6)) {
        // the best ratio is six bytes turning into 255 * 4: never trust a size above 170:1
        const uint64_t maxLen = (uint64_t)(packed - 10) * 170;
        uint64_t want = be32(c + 6);
        if (want > maxLen) want = maxLen;
        out.assign((size_t)want, 0);
        size_t in = 10, off = 0, remain = (size_t)want;
        bool done = false;
        while (!done && in < packed) {
            const int8_t type = (int8_t)c[in++];
            switch (type) {
                case 0: {  // raw bytes
                    if (in >= packed) {
                        done = true;
                        break;
                    }
                    const size_t len = c[in++];
                    if (remain >= len && in + len <= packed) {
                        std::memcpy(out.data() + off, c + in, len);
                        in += len, off += len, remain -= len;
                    } else {
                        done = true;
                    }
                    break;
                }
                case 1: {  // a dword repeated
                    if (in + 5 > packed) {
                        done = true;
                        break;
                    }
                    size_t len = c[in];
                    if (remain >= len * 4) {
                        remain -= len * 4;
                        while (len--) {
                            std::memcpy(out.data() + off, c + in + 1, 4);
                            off += 4;
                        }
                        in += 5;
                    } else {
                        done = true;
                    }
                    break;
                }
                case 2:  // a dword twice
                    if (remain >= 8 && in + 4 <= packed) {
                        std::memcpy(out.data() + off, c + in, 4);
                        std::memcpy(out.data() + off + 4, c + in, 4);
                        in += 4, off += 8, remain -= 8;
                    } else {
                        done = true;
                    }
                    break;
                case 3: {  // zeros
                    if (in >= packed) {
                        done = true;
                        break;
                    }
                    const size_t len = c[in++];
                    if (remain >= len) {
                        off += len, remain -= len;
                    } else {
                        done = true;
                    }
                    break;
                }
                default:  // -1 = end, anything else is an error
                    done = true;
                    break;
            }
        }
        if (remain) {
            truncated = true;
            out.clear();
        }
    } else {
        out.assign(c, c + packed);
    }
    return out;
}

std::vector<uint8_t> decodeSample8(ByteReader& r, bool& truncated) {
    auto d = decodeChunk(r, truncated);
    uint8_t last = 0;
    for (auto& v : d) v = last = (uint8_t)(last + v);
    return d;
}

// 16-bit delta: 4096-byte blocks, first the low bytes then the high bytes of 2048 samples.
std::vector<uint8_t> decodeSample16(ByteReader& r, bool& truncated) {
    auto d = decodeChunk(r, truncated);
    uint8_t buf[4096];
    constexpr size_t half = sizeof buf / 2;
    for (size_t block = 0; block < d.size() / sizeof buf; ++block) {
        uint8_t* p = d.data() + block * sizeof buf;
        uint8_t last = 0;
        for (size_t i = 0; i < half; ++i) buf[i * 2 + 1] = last = (uint8_t)(last + p[i]);
        for (size_t i = 0; i < half; ++i) buf[i * 2] = last = (uint8_t)(last + p[i + half]);
        std::memcpy(p, buf, sizeof buf);
    }
    return d;
}

// Minimal AIFF reader (8/16-bit PCM, channels mixed to mono).
bool loadAiff(const uint8_t* d, size_t n, Sample& out) {
    if (n < 12 || std::memcmp(d, "FORM", 4) || std::memcmp(d + 8, "AIFF", 4)) return false;
    int channels = 0, bits = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;
    for (size_t pos = 12; pos + 8 <= n;) {
        const uint32_t len = be32(d + pos + 4);
        const size_t body = pos + 8, avail = n - body;
        const size_t l = len < avail ? len : avail;
        if (!std::memcmp(d + pos, "COMM", 4) && l >= 8) {
            channels = be16(d + body);
            bits = be16(d + body + 6);
        } else if (!std::memcmp(d + pos, "SSND", 4) && l >= 8) {
            const uint32_t skip = be32(d + body);
            if (skip <= l - 8) {
                pcm = d + body + 8 + skip;
                pcmBytes = l - 8 - skip;
            }
        }
        if (len > avail) break;
        pos = body + len + (len & 1);
    }
    if (!pcm || channels < 1 || channels > 8 || (bits != 8 && bits != 16)) return false;
    const size_t bps = (size_t)bits / 8, frame = bps * channels, frames = pcmBytes / frame;
    if (!frames) return false;
    out = Sample{};
    out.is16 = bits == 16;
    out.length = (uint32_t)frames;
    if (out.is16) out.data16.resize(frames);
    else out.data8.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) {
            const uint8_t* p = pcm + f * frame + c * bps;
            sum += out.is16 ? (int16_t)be16(p) : (int8_t)p[0];
        }
        if (out.is16) out.data16[f] = (int16_t)(sum / channels);
        else out.data8[f] = (int8_t)(sum / channels);
    }
    return true;
}

// A sample chunk is an IFF, WAV or AIFF file, or raw data: "MAESTRO" (16-bit, maybe stereo),
// "16BT" (16-bit) or plain signed 8-bit. Stereo raw data fills `right` as well.
void readSampleFile(const uint8_t* d, size_t n, Sample& out, Sample& right, bool& stereo) {
    stereo = false;
    auto r = loadSampleFile(d, n);
    if (r.ok()) {
        out = r.value();
        return;
    }
    if (loadAiff(d, n, out)) return;
    out = Sample{};
    size_t start = 0, zeroFrames = 0;
    bool is16 = true;
    if (n >= 7 && !std::memcmp(d, "MAESTRO", 7)) {
        stereo = n >= 16 && be32(d + 12) == 0;
        start = 24;
    } else if (n >= 4 && !std::memcmp(d, "16BT", 4)) {
        zeroFrames = 2;  // the header bytes play as silence
    } else {
        is16 = false;
    }
    if (start > n) start = n;
    const size_t bytes = n - start, frameBytes = (is16 ? 2u : 1u) * (stereo ? 2u : 1u), frames = bytes / frameBytes;
    out.is16 = is16;
    out.length = (uint32_t)frames;
    if (!is16) {
        out.data8.assign(reinterpret_cast<const int8_t*>(d + start), reinterpret_cast<const int8_t*>(d + start) + frames);
        return;
    }
    out.data16.resize(frames);
    if (stereo) {
        right = out;
    }
    for (size_t f = 0; f < frames; ++f) {
        const uint8_t* p = d + start + f * frameBytes;
        out.data16[f] = (int16_t)be16(p);
        if (stereo) right.data16[f] = (int16_t)be16(p + 2);
    }
    for (size_t f = 0; f < zeroFrames && f < frames; ++f) out.data16[f] = 0;
}

void to16(Sample& s) {
    if (s.is16) return;
    s.data16.resize(s.length);
    for (uint32_t i = 0; i < s.length; ++i) s.data16[i] = (int16_t)(s.data8[i] * 256);
    s.data8.clear();
    s.data8.shrink_to_fit();
    s.is16 = true;
}

int sampleAt(const Sample& s, size_t i) { return s.is16 ? s.data16[i] : s.data8[i]; }

// Loop points in sample frames (percentages of the sample length in the file).
void sampleLoop(const SymInst& in, const Sample& s, uint32_t& start, uint32_t& len) {
    start = len = 0;
    if (in.type() != TYPE_LOOP && in.type() != TYPE_SUSTAIN) return;
    uint64_t ls = in.loopStartHigh() < 100 ? in.loopStartHigh() : 100;
    uint64_t ll = in.loopLenHigh() < 100 ? in.loopLenHigh() : 100;
    const uint64_t n = s.length;
    if (in.sampleFlags() & SF_NEW_LOOP) {
        ls = (ls << 16) + in.loopStartFine();
        ll = (ll << 16) + in.loopLenFine();
        const double scale = (double)n / (100 << 16);
        ls = (uint64_t)(ls * scale);
        if (ls > n) ls = n;
        ll = (uint64_t)(ll * scale);
        if (ll > n - ls) ll = n - ls;
    } else if (n) {
        // the precision loss is the original's (UpdateRecalcLoop)
        ls = ((ls << 7) / 100) * (n >> 7);
        if (ls >= n) return;
        const uint64_t want = ((ll << 7) / 100) * (n >> 7);
        ll = want < n - ls ? want : n - ls;
        // shorten the loop until its end matches the start value
        const int threshold = s.is16 ? 6 * 256 : 6;
        const int atStart = sampleAt(s, (size_t)ls);
        while (ll) {
            const size_t e = (size_t)(ls + ll);
            if (e < n && std::abs(sampleAt(s, e) - atStart) < threshold) break;
            --ll;
        }
    }
    start = (uint32_t)ls;
    len = (uint32_t)ll;
}

template <typename T>
void normalize(std::vector<T>& d, int full) {
    int maxV = 0;
    for (T v : d) {
        const int a = v < 0 ? -(int)v : (int)v;
        if (a > maxV) maxV = a;
    }
    if (maxV >= full || maxV == 0) return;
    for (T& v : d) v = (T)((int)v * full / maxV);
}

// "Non-destructive" over-amplification with a hard knee (volume 101..199).
template <typename T>
void amplify(std::vector<T>& d, int gain, int bits) {
    const int32_t knee = 16384 * (200 - gain) / 100, kneeInv = 32768 - knee;
    const int32_t scale = 1 << (16 - bits);
    const int32_t lo = bits == 8 ? -128 : -32768, hi = bits == 8 ? 127 : 32767;
    for (T& v : d) {
        int32_t x = (int32_t)v * scale;
        if (x > knee) x = (x - knee) * knee / kneeInv + kneeInv;
        else if (x < -knee) x = (x + knee) * knee / kneeInv - kneeInv;
        else x = x * kneeInv / knee;
        x /= scale;
        v = (T)(x < lo ? lo : (x > hi ? hi : x));
    }
}

int16_t sat16(int64_t v) { return (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }

// Applies the instrument's settings to its sample: loop, tuning, the sample processing Symphonie
// does at load time and the instrument volume (baked into the data).
void convertInstrument(const SymInst& in, Sample& s, int extraDownsample) {
    uint32_t loopStart = 0, loopLen = 0;
    sampleLoop(in, s, loopStart, loopLen);
    s.loop = LoopType::None;
    s.loopStart = s.loopLength = 0;
    s.volume = 64;
    s.pan = 128;
    const double octaves = -(in.downsample() + extraDownsample) + in.transpose() / 12.0 + in.finetune() / (128.0 * 12.0);
    samplePitchFromRate(s, 40460.0 * std::pow(2.0, octaves));
    if (!s.length) return;

    if (in.sampleFlags() & SF_REVERSE) {
        if (s.is16) std::vector<int16_t>(s.data16.rbegin(), s.data16.rend()).swap(s.data16);
        else std::vector<int8_t>(s.data8.rbegin(), s.data8.rend()).swap(s.data8);
    }
    if (in.sampleFlags() & SF_MIRROR) {
        if (s.is16)
            for (auto& v : s.data16) v = (int16_t)(v == -32768 ? 32767 : -v);
        else
            for (auto& v : s.data8) v = (int8_t)(v == -128 ? 127 : -v);
    }
    const int filter = in.filter();
    const bool fade = in.volFadeFlag() == 2 && in.volFadeFrom() <= 100 && in.volFadeTo() <= 100;
    if (in.filterFlags() || fade || filter) to16(s);
    auto& d = s.data16;
    const size_t n = s.length;
    for (int i = 0; filter < 0 && i < -filter; ++i) {  // highpass
        int32_t mix = d[0];
        for (auto& v : d) {
            mix = (v - (mix >> 1)) >> 1;
            v = (int16_t)mix;
        }
    }
    if (fade) {
        int64_t amp = (int64_t)in.volFadeFrom() << 24;
        const int64_t inc = (int64_t)std::llround(((int)in.volFadeTo() - (int)in.volFadeFrom()) * (double)(1 << 24) / (double)n);
        for (auto& v : d) {
            v = (int16_t)std::llround((double)v * (double)amp / (double)(100LL << 24));
            amp += inc;
        }
    }
    if (in.filterFlags()) {  // resonant filter sweep
        const int pts = in.filterPoints();
        int64_t cut = (int64_t)in.cutoff(0) << 23, res = (int64_t)in.resonance(0) << 23;
        const int64_t cutStep = pts > 1 ? std::llround((in.cutoff(1) - in.cutoff(0)) * (double)(1 << 23) / (double)n) : 0;
        const int64_t resStep = pts > 1 ? std::llround((in.resonance(1) - in.resonance(0)) * (double)(1 << 23) / (double)n) : 0;
        const int hp = (in.filterFlags() & 2) ? 1 : 0;
        int64_t st[3] = {};
        for (auto& v : d) {
            const int64_t c = cut >> 23, r = res >> 23;
            cut += cutStep;
            res += resStep;
            st[2] = (v >> 1) - st[0];
            st[1] += (c * st[2]) >> 8;
            st[0] += (c * st[1]) >> 6;
            st[0] += (r * st[0]) >> 6;
            st[0] >>= 2;
            if (st[0] > (1LL << 40) || st[0] < -(1LL << 40)) st[0] = 0;  // runaway resonance
            if (st[1] > (1LL << 40) || st[1] < -(1LL << 40)) st[1] = 0;
            v = sat16(st[hp]);
        }
    }
    for (int i = 0; filter > 0 && i < filter; ++i) {  // lowpass
        int32_t mix = d[0];
        for (auto& v : d) {
            mix = (v + v + mix) / 3;
            v = (int16_t)mix;
        }
    }

    // Symphonie normalises every sample at load time
    if (s.is16) normalize(s.data16, 32767);
    else normalize(s.data8, 127);
    const int vol = in.volume();
    if (vol > 100 && vol < 200) {
        if (s.is16) amplify(s.data16, vol, 16);
        else amplify(s.data8, vol, 8);
    } else if (vol > 0 && vol < 100) {
        to16(s);  // keep the resolution of quiet 8-bit instruments
        const int gain = vol * 128 / 100;
        for (auto& v : s.data16) v = (int16_t)(v * gain / 128);
    }

    if (in.type() == TYPE_LOOP && loopLen >= 2) {
        s.loop = LoopType::Forward;
        s.loopStart = loopStart;
        s.loopLength = loopLen;
    } else if (in.type() == TYPE_SUSTAIN && in.repetitions() > 0 && loopLen > 0) {
        // the sustain loop plays `repetitions` extra times, then the rest of the sample
        const uint64_t newLen = (uint64_t)s.length + (uint64_t)loopLen * in.repetitions();
        if (newLen <= (64u << 20)) {
            const uint32_t end = loopStart + loopLen;
            auto unroll = [&](auto& v) {
                using V = std::remove_reference_t<decltype(v)>;
                V out;
                out.reserve((size_t)newLen);
                out.insert(out.end(), v.begin(), v.begin() + end);
                for (int i = 0; i < in.repetitions(); ++i) out.insert(out.end(), v.begin() + loopStart, v.begin() + end);
                out.insert(out.end(), v.begin() + end, v.end());
                v.swap(out);
            };
            if (s.is16) unroll(s.data16);
            else unroll(s.data8);
            s.length = (uint32_t)newLen;
        }
    }
}

// Instrument name without the Amiga path.
void instName(const SymInst& in, char* dst) {
    size_t len = 0;
    while (len < 128 && in.b[len]) ++len;
    size_t from = 0;
    for (size_t i = 0; i < len; ++i)
        if (in.b[i] == '/' || in.b[i] == ':') from = i + 1;
    if (from == len) from = 0;
    size_t k = 0;
    for (size_t i = from; i < len && k < 22; ++i) {
        const char ch = (char)in.b[i];
        dst[k++] = (ch >= 32 && ch < 127) ? ch : ' ';
    }
    dst[k] = 0;
}

struct ChanState {
    float vol = 64;              // Symphonie channel volume 0..64 (what the song wants)
    int playerVol = 64;          // what our player has after the converted cells
    float volSlide = 0;          // per tick
    float pitchSlide = 0;        // per tick, in 1/16 semitones
    float pitchSlideAmt = 0;     // not yet emitted pitch slide
    bool stopped = false;
    int lastNote = 0;            // our note numbering, 0 = none
    int lastInst = 0;            // Symphonie instrument + 1
    uint8_t lastVol = 64;
    uint8_t fromAdd = 0;
    bool retrigVibrato = false, retrigTremolo = false;
    uint8_t vibrato = 0, tremolo = 0, tonePortaAmt = 0;
    uint8_t sampleVibSpeed = 0, sampleVibDepth = 0;
    uint16_t sampleVibPhase = 0;
    int retrigRemain = 0, tonePortaRemain = 0;
};

// A converted cell before the instrument becomes a (panned) sample number.
struct XCell {
    int note = 0, inst = 0;  // inst: Symphonie instrument + 1
    uint8_t vol = 0, fx = 0, param = 0;
    bool hasFx = false;
    void setFx(uint8_t f, uint8_t p) {
        fx = f, param = p, hasFx = true;
    }
};

void setName(Sample& s, const char* n) {
    std::memset(s.name, 0, sizeof s.name);
    std::memcpy(s.name, n, std::strlen(n) < sizeof s.name - 1 ? std::strlen(n) : sizeof s.name - 1);
}

int roundi(float v) { return (int)std::lround(v); }
uint8_t speedParam(int speed) { return (uint8_t)(speed < 1 ? 1 : (speed > 31 ? 31 : speed)); }
}  // namespace

bool symDetect(const uint8_t* d, size_t n) { return n >= 16 && std::memcmp(d, "SymM", 4) == 0; }

Result<Song> symLoad(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    r.seek(4);
    const uint32_t version = r.u32be(), first = r.u32be(), fileCh = r.u32be();
    if (r.overrun() || version != 1 || first != 0xFFFFFFFFu || fileCh < 1 || fileCh > 256)
        return Result<Song>::fail("BAD SYMPHONIE FILE");

    Song s;
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Linear;
    bool truncated = false;
    int numCh = (int)fileCh;
    if (numCh > kMaxChannels) {
        numCh = kMaxChannels;
        truncated = true;
    }
    s.numChannels = (uint8_t)(numCh < 4 ? 4 : numCh);
    for (int c = 0; c < kMaxChannels; ++c) s.channelPan[c] = (c & 1) ? 255 : 0;  // hard stereo

    uint32_t trackLen = 0;
    bool external = false;
    std::vector<uint8_t> posData, seqData, evData, insData;
    std::vector<Sample> slots;  // samples in file order, slot i belongs to instrument i
    while (r.remaining() >= 4) {
        const int32_t type = (int32_t)r.u32be();
        switch (type) {
            case CH_NUM_CHANNELS:
            case CH_PATTERN_SIZE:
            case CH_NUM_INSTRUMENTS:
            case CH_STEREO_DETUNE:
            case CH_STEREO_PHASE:
            case CH_SAMPLE_BOOST:
                r.skip(4);
                break;
            case CH_TRACK_LENGTH:
                trackLen = r.u32be();
                if (trackLen > kMaxRows) return Result<Song>::fail("BAD SYMPHONIE FILE");
                break;
            case CH_EVENT_SIZE:
                if ((r.u32be() & 0xFFFF) != 4 && !r.overrun()) return Result<Song>::fail("BAD SYMPHONIE FILE");
                break;
            case CH_TEMPO: {
                uint32_t t = r.u32be();
                if (t > 800) t = 800;
                const long bpm = std::lround(1.24 * t);
                if (!r.overrun()) s.initialBpm = (uint8_t)(bpm < 32 ? 32 : (bpm > 255 ? 255 : bpm));
                break;
            }
            case CH_EXTERNAL_SAMPLES:
                r.skip(4);
                if (slots.empty()) external = true;
                break;
            case CH_POSITION_LIST:
            case CH_PATTERN_EVENTS:
            case CH_INSTRUMENT_LIST:
            case CH_SEQUENCES: {
                std::vector<uint8_t>& dst = type == CH_POSITION_LIST    ? posData
                                            : type == CH_PATTERN_EVENTS ? evData
                                            : type == CH_INSTRUMENT_LIST ? insData
                                                                         : seqData;
                if (dst.empty()) dst = decodeChunk(r, truncated);
                else r.skip(r.u32be());
                break;
            }
            case CH_SAMPLE_FILE:
            case CH_SAMPLE_PACKED:
            case CH_SAMPLE_PACKED16: {
                if (external || slots.size() >= insData.size() / 256) {
                    r.skip(r.u32be());
                    break;
                }
                std::vector<uint8_t> unpacked;
                const uint8_t* p = nullptr;
                size_t n = 0;
                if (type == CH_SAMPLE_FILE) {
                    const uint32_t len = r.u32be();
                    if (len > r.remaining()) {
                        truncated = true;
                        n = r.remaining();
                    } else {
                        n = len;
                    }
                    p = data + r.pos();
                    r.skip(n);
                } else {
                    unpacked = type == CH_SAMPLE_PACKED ? decodeSample8(r, truncated) : decodeSample16(r, truncated);
                    p = unpacked.data();
                    n = unpacked.size();
                }
                Sample smp, right;
                bool stereo = false;
                if (n) readSampleFile(p, n, smp, right, stereo);
                const size_t idx = slots.size();
                slots.push_back(static_cast<Sample&&>(smp));
                const size_t numIns = insData.size() / 256;
                if (stereo) {
                    slots.push_back(static_cast<Sample&&>(right));  // the right half belongs to the next instrument
                } else if (idx + 1 < numIns && SymInst{insData.data() + (idx + 1) * 256}.channel() == CHAN_STEREO_R) {
                    slots.emplace_back();  // a mono sample for a stereo pair: keep the slots aligned
                }
                break;
            }
            case CH_EMPTY_SAMPLE:
                slots.emplace_back();
                break;
            case CH_INFO_TEXT:
            case CH_INFO_TYPE:
            case CH_INFO_BINARY:
            case CH_INFO_STRING:
                (void)decodeChunk(r, truncated);
                break;
            default:  // unknown hunk (or garbage at the end): ignored, like OpenMPT
                break;
        }
    }
    if (r.overrun()) truncated = true;
    if (!trackLen || insData.size() < 256 || posData.size() < 32 || seqData.size() < 16 || evData.size() < 4)
        return Result<Song>::fail(truncated ? "TRUNCATED SYMPHONIE FILE" : "BAD SYMPHONIE FILE");

    // ---- instruments -> samples (one per instrument, panned per side on demand)
    size_t numIns = insData.size() / 256;
    if (numIns > 255) {
        numIns = 255;
        truncated = true;
    }
    auto inst = [&](size_t i) { return SymInst{insData.data() + i * 256}; };
    s.samples.resize(numIns);
    for (size_t i = 0; i < numIns; ++i) {
        const SymInst in = inst(i);
        Sample& sm = s.samples[i];
        if (in.isEmpty()) continue;
        if (in.isVirtual()) {
            const size_t src = in.firstSource();
            if (src < slots.size()) sm = slots[src];
            convertInstrument(in, sm, src < numIns ? inst(src).downsample() : 0);
            setName(sm, "Virtual");
            continue;
        }
        if (in.isTranswave()) {
            sm = Sample{};
            setName(sm, "Transwave");
            continue;
        }
        if (i < slots.size()) sm = slots[i];
        convertInstrument(in, sm, 0);
        instName(in, sm.name);
    }
    slots.clear();
    slots.shrink_to_fit();

    struct Sides {
        int16_t idx[2] = {-1, -1};
    };
    std::vector<Sides> sides(numIns);
    auto sampleFor = [&](int sym, int side) -> int {
        Sides& m = sides[sym];
        if (m.idx[side] >= 0) return m.idx[side];
        if (m.idx[side ^ 1] < 0 || s.samples[sym].length == 0) {
            if (m.idx[side ^ 1] < 0) s.samples[sym].pan = side ? 255 : 0;
            m.idx[side] = (int16_t)sym;
            return sym;
        }
        if (s.samples.size() >= 255) return sym;  // no room: this side gets the other side's panning
        Sample copy = s.samples[sym];
        copy.pan = side ? 255 : 0;
        s.samples.push_back(static_cast<Sample&&>(copy));
        m.idx[side] = (int16_t)(s.samples.size() - 1);
        return m.idx[side];
    };

    // ---- positions -> patterns
    std::vector<Position> positions(posData.size() / 32);
    for (size_t i = 0; i < positions.size(); ++i) {
        const uint8_t* p = posData.data() + i * 32;
        Position& ps = positions[i];
        ps.loopNum = be16(p + 4);
        ps.pattern = be16(p + 8);
        ps.start = be16(p + 10);
        ps.length = be16(p + 12);
        ps.speed = be16(p + 14);
        ps.transpose = (int16_t)be16(p + 16);
    }
    const size_t numEvents = evData.size() / 4;
    const size_t patternSize = (size_t)fileCh * trackLen;
    const size_t numPatterns = numEvents / patternSize;
    auto eventAt = [&](size_t i) {
        Event e;
        if (i < numEvents) {
            const uint8_t* p = evData.data() + i * 4;
            e.cmd = p[0], e.note = (int8_t)p[1], e.param = p[2], e.inst = p[3];
        }
        return e;
    };

    std::map<Position, std::vector<uint8_t>> patternMap;
    std::vector<ChanState> chs(numCh);
    int speed = 6;
    bool firstPos = true, full = false;
    for (size_t sq = 0; sq + 16 <= seqData.size() && !full; sq += 16) {
        const uint8_t* q = seqData.data() + sq;
        const uint16_t seqStart = be16(q), seqLen = be16(q + 2);
        const int16_t info = (int16_t)be16(q + 6), seqTranspose = (int16_t)be16(q + 8);
        if (info == 1) continue;
        if (info == -1) break;
        if (seqStart >= positions.size() || seqLen == 0 || seqLen > positions.size() - seqStart) continue;
        for (size_t pi = seqStart; pi < (size_t)seqStart + seqLen && !full; ++pi) {
            Position pos = positions[pi];
            pos.transpose = (int16_t)(pos.transpose + seqTranspose);
            if (pos.length == 0) continue;
            if (pos.speed) speed = pos.speed;
            if (firstPos) {
                s.initialSpeed = speedParam(speed);
                firstPos = false;
            }
            auto it = patternMap.find(pos);
            if (it == patternMap.end()) {
                int length = pos.length;
                if (length > kMaxRows) {
                    length = kMaxRows;
                    truncated = true;
                }
                const int pieces = (length + 255) / 256;
                if (s.patterns.size() + pieces > 256) {
                    truncated = true;
                    full = true;
                    break;
                }
                std::vector<uint8_t> ids;
                for (int k = 0; k < pieces; ++k) {
                    const int rows = length - k * 256 < 256 ? length - k * 256 : 256;
                    ids.push_back((uint8_t)s.patterns.size());
                    s.patterns.push_back(makePattern(rows, s.numChannels));
                }
                it = patternMap.emplace(pos, ids).first;
                if (pos.pattern < numPatterns && pos.start < trackLen) {
                    int patSpeed = speed;
                    // this may intentionally read into the next pattern
                    const size_t base = pos.pattern * patternSize + (size_t)pos.start * fileCh;
                    std::vector<XCell> row(numCh);
                    std::vector<bool> dup(numCh);
                    for (int rw = 0; rw < length; ++rw) {
                        for (auto& c : row) c = XCell{};
                        for (int c = 0; c < numCh; ++c) dup[c] = false;
                        bool applySync = false;
                        XCell syncCell;
                        for (int ch = 0; ch < numCh; ++ch) {
                            const Event ev = eventAt(base + (size_t)rw * fileCh + ch);
                            XCell& x = row[ch];
                            ChanState& st = chs[ch];
                            const bool evEmpty = ev.cmd == EV_KEY_ON && ev.note == -1 && ev.inst == 0 && ev.param == 0;
                            if (dup[ch] && !evEmpty) x = XCell{};  // a stereo copy gives way to a real event
                            const bool global = ev.cmd == EV_SET_SPEED || ev.cmd == EV_DSP_ECHO || ev.cmd == EV_DSP_DELAY ||
                                                (ev.cmd == EV_KEY_ON && (ev.param == VOL_SPEED_UP || ev.param == VOL_SPEED_DOWN));
                            if (applySync) {
                                applySync = false;
                                x = syncCell;
                                const ChanState& prev = chs[ch - 1];
                                st.vol = prev.vol;
                                st.playerVol = prev.playerVol;
                                st.lastVol = prev.lastVol;
                                if (!global) continue;
                            }

                            bool resumed = false;
                            int note = (ev.note >= 0 && ev.note <= 84) ? ev.note + 13 : -1;
                            int mapped = 0;
                            if (ev.inst < numIns) {
                                mapped = ev.inst + 1;
                                if (!(inst(ev.inst).instFlags() & IF_NO_TRANSPOSE) && note >= 0) note += pos.transpose;
                            } else if (ev.inst < insData.size() / 256) {
                                truncated = true;  // instruments above 255
                            }
                            if (note >= 0) note = note < 1 ? 1 : (note > 96 ? 96 : note);

                            switch (ev.cmd) {
                                case EV_KEY_ON:
                                    if (ev.param > VOL_COMMAND) {
                                        switch (ev.param) {
                                            case VOL_STOP_SAMPLE:  // pause: approximated as silence
                                                x.vol = 0x10;
                                                st.stopped = true;
                                                st.playerVol = 0;
                                                break;
                                            case VOL_CONT_SAMPLE:
                                                st.stopped = false;
                                                resumed = true;
                                                break;
                                            case VOL_KEY_OFF:
                                                x.note = NOTE_OFF;
                                                break;
                                            case VOL_SPEED_DOWN:
                                                if (patSpeed > 1) x.setFx(0xF, speedParam(--patSpeed));
                                                break;
                                            case VOL_SPEED_UP:
                                                if (patSpeed < 255) x.setFx(0xF, speedParam(++patSpeed));
                                                break;
                                            case VOL_SET_PITCH:
                                                if (note < 0) break;
                                                st.lastNote = note;
                                                if (mapped != st.lastInst) break;
                                                x.note = note;
                                                x.setFx(0x3, 0xFF);  // change the pitch, no new attack
                                                st.pitchSlide = 0;
                                                st.tonePortaRemain = 0;
                                                st.retrigVibrato = st.retrigTremolo = true;
                                                break;
                                            // fine slides of 2/64, 4/64 and 8/64 of a semitone
                                            case VOL_PITCH_UP: x.setFx(FX_X, 0x12); break;
                                            case VOL_PITCH_DOWN: x.setFx(FX_X, 0x22); break;
                                            case VOL_PITCH_UP2: x.setFx(FX_X, 0x14); break;
                                            case VOL_PITCH_DOWN2: x.setFx(FX_X, 0x24); break;
                                            case VOL_PITCH_UP3: x.setFx(FX_X, 0x18); break;
                                            case VOL_PITCH_DOWN3: x.setFx(FX_X, 0x28); break;
                                            default: break;
                                        }
                                    } else if (note >= 0 || ev.param < 100) {
                                        if (note >= 0) {
                                            x.note = st.lastNote = note;
                                            x.inst = st.lastInst = mapped;
                                            st.pitchSlide = 0;
                                            st.tonePortaRemain = 0;
                                            st.retrigVibrato = st.retrigTremolo = true;
                                        }
                                        if (ev.param > 0) {
                                            const int v = (int)std::lround(ev.param * 0.64);
                                            st.lastVol = (uint8_t)(v > 64 ? 64 : v);
                                            st.vol = st.lastVol;
                                            st.volSlide = 0;
                                        }
                                    }
                                    // stereo instruments play on this and the next channel
                                    if (ev.note > 0 && ch + 1 < numCh && !(ch & 1) && ev.inst < numIns &&
                                        inst(ev.inst).channel() == CHAN_STEREO_L) {
                                        XCell& next = row[ch + 1];
                                        next = x;
                                        if (next.inst && next.inst < (int)numIns) ++next.inst;
                                        ChanState& ns = chs[ch + 1];
                                        ns.lastVol = st.lastVol;
                                        ns.vol = st.vol;
                                        ns.volSlide = st.volSlide;
                                        ns.pitchSlide = st.pitchSlide;
                                        ns.pitchSlideAmt = st.pitchSlideAmt;
                                        ns.retrigRemain = st.retrigRemain;
                                        dup[ch + 1] = true;
                                    }
                                    break;
                                case EV_VOL_SLIDE_UP:
                                    st.volSlide = ev.param * 0.0333f;
                                    break;
                                case EV_VOL_SLIDE_DOWN:
                                    st.volSlide = ev.param * -0.0333f;
                                    break;
                                case EV_TREMOLO: {
                                    const int sp = (ev.inst >> 3) < 15 ? ev.inst >> 3 : 15;
                                    const int dp = (ev.param >> 3) < 15 ? ev.param >> 3 : 15;
                                    st.tremolo = (uint8_t)((sp << 4) | dp);
                                    st.retrigTremolo = st.tremolo != 0;
                                    break;
                                }
                                case EV_PITCH_SLIDE_UP:
                                case EV_PITCH_SLIDE_DOWN:
                                    st.pitchSlideAmt = 0;
                                    st.pitchSlide = ev.param * (ev.cmd == EV_PITCH_SLIDE_UP ? 0.0333f : -0.0333f);
                                    st.tonePortaRemain = 0;
                                    break;
                                case EV_PITCH_SLIDE_TO:
                                    if (note >= 0 && ev.param > 0) {
                                        const int dist = std::abs(note - st.lastNote) * 16;  // 1/16 semitones
                                        st.pitchSlide = 0;
                                        x.note = st.lastNote = note;
                                        int amt = dist / ev.param;
                                        amt = amt < 1 ? 1 : (amt > 255 ? 255 : amt);
                                        st.tonePortaAmt = (uint8_t)amt;
                                        x.setFx(0x3, st.tonePortaAmt);
                                        const int done = amt * (patSpeed - 1);
                                        st.tonePortaRemain = dist - (done < dist ? done : dist);
                                    }
                                    break;
                                case EV_VIBRATO: {
                                    const int sp = (ev.inst >> 3) < 15 ? ev.inst >> 3 : 15;
                                    const int dp = ev.param < 15 ? ev.param : 15;
                                    st.vibrato = (uint8_t)((sp << 4) | dp);
                                    st.retrigVibrato = st.vibrato != 0;
                                    break;
                                }
                                case EV_ADD_HALF_TONE:
                                    if (st.lastNote > 0) {
                                        int n = st.lastNote + (int8_t)ev.param;
                                        n = n < 1 ? 1 : (n > 96 ? 96 : n);
                                        x.note = st.lastNote = n;
                                        x.setFx(0x3, 0xFF);
                                    }
                                    st.tonePortaRemain = 0;
                                    break;
                                case EV_RETRIG:
                                    // plays the note <param> times every <inst>+1 ticks
                                    if (ev.param < 1) break;
                                    x.setFx(FX_R, (uint8_t)(ev.inst + 1 < 15 ? ev.inst + 1 : 15));
                                    st.retrigRemain = ev.param * (ev.inst + 1);
                                    break;
                                case EV_SET_SPEED:
                                    patSpeed = ev.param ? ev.param : 4;
                                    x.setFx(0xF, speedParam(patSpeed));
                                    break;
                                case EV_SET_FROM_ADD:
                                    st.fromAdd = ev.param;
                                    st.sampleVibSpeed = st.sampleVibDepth = 0;
                                    break;
                                case EV_FROM_ADD:
                                    st.fromAdd = (uint8_t)(st.fromAdd + ev.param);
                                    break;
                                case EV_SAMPLE_VIB:
                                    st.sampleVibSpeed = ev.inst;
                                    st.sampleVibDepth = ev.param;
                                    break;
                                case EV_FROM_AND_PITCH:
                                case EV_REPLAY_FROM: {
                                    if (ev.cmd == EV_FROM_AND_PITCH && note >= 0) st.lastNote = note;
                                    if (st.lastNote <= 0) break;
                                    x.note = st.lastNote;  // the sample restarts at the current note
                                    if (note >= 0 || ev.cmd == EV_FROM_AND_PITCH) x.inst = st.lastInst = mapped;
                                    if (ev.param || st.fromAdd || st.sampleVibDepth) {
                                        double vib = 0;
                                        if (st.sampleVibDepth)
                                            vib = st.sampleVibDepth *
                                                  (std::sin(st.sampleVibPhase * (6.283185307179586 / 1024.0) + 4.71238898038469) - 1.0) / 4.0;
                                        double pct = ev.param + st.fromAdd + vib;
                                        pct = pct < 0 ? 0 : (pct > 255 ? 255 : pct);
                                        // percent of the sample -> 9xx (256 frames per step)
                                        const int si = (x.inst ? x.inst : st.lastInst) - 1;
                                        if (si >= 0 && si < (int)numIns) {
                                            const double frames = std::round(pct) * s.samples[si].length / 100.0;
                                            const long o = std::lround(frames / 256.0);
                                            if (o > 0) x.setFx(0x9, (uint8_t)(o > 255 ? 255 : o));
                                        }
                                    }
                                    st.tonePortaRemain = 0;
                                    break;
                                }
                                default:  // add pitch / volume, emphasis, CV, filter and DSP: dropped
                                    break;
                            }

                            // any event that plays a note restarts the continuous effects
                            if (x.note > 0 && x.note != NOTE_OFF) st.stopped = false;
                            else if (st.stopped) continue;

                            if (st.retrigRemain) {
                                st.retrigRemain = (st.retrigRemain > patSpeed ? st.retrigRemain : patSpeed) - patSpeed;
                                if (!x.hasFx) x.setFx(FX_R, 0);
                            }

                            // volume: what the player will have after tick 0 of this cell...
                            int pv = st.playerVol;
                            if (x.inst) pv = 64;  // the instrument resets to the sample volume
                            if (x.note == NOTE_OFF) pv = 0;
                            if (x.vol >= 0x10 && x.vol <= 0x50) pv = x.vol - 0x10;
                            // ...and what Symphonie has by the end of it
                            const float vStart = st.vol;
                            if (st.volSlide != 0) {
                                st.vol += st.volSlide * patSpeed;
                                st.vol = st.vol < 0 ? 0 : (st.vol > 64 ? 64 : st.vol);
                            }
                            if (!x.vol && x.note != NOTE_OFF) {
                                if (st.volSlide == 0 || x.inst || resumed) {
                                    const int want = roundi(vStart);
                                    if (pv != want) {
                                        x.vol = (uint8_t)(0x10 + want);
                                        pv = want;
                                    }
                                } else {
                                    const int diff = roundi(st.vol) - pv, ad = std::abs(diff);
                                    if (patSpeed > 1 && ad >= patSpeed - 1) {
                                        int k = (int)std::lround((double)ad / (patSpeed - 1));
                                        k = k > 15 ? 15 : k;
                                        x.vol = (uint8_t)((diff > 0 ? 0x70 : 0x60) + k);
                                        pv += (diff > 0 ? 1 : -1) * k * (patSpeed - 1);
                                    } else if (ad >= 1) {
                                        const int k = ad > 15 ? 15 : ad;
                                        x.vol = (uint8_t)((diff > 0 ? 0x90 : 0x80) + k);
                                        pv += diff > 0 ? k : -k;
                                    }
                                    pv = pv < 0 ? 0 : (pv > 64 ? 64 : pv);
                                }
                            }
                            st.playerVol = pv;

                            // pitch slide in 1/16 semitones: 1xx/2xx per tick, E1x/E2x once
                            if (st.pitchSlide != 0) {
                                st.pitchSlideAmt += st.pitchSlide * patSpeed;
                                if (!x.hasFx) {
                                    const float a = st.pitchSlideAmt, aa = std::fabs(a);
                                    const bool up = a > 0;
                                    if (patSpeed > 1 && aa >= patSpeed - 1) {
                                        int k = (int)std::lround(aa / (patSpeed - 1));
                                        k = k > 0xFF ? 0xFF : k;
                                        x.setFx(up ? 0x1 : 0x2, (uint8_t)k);
                                        st.pitchSlideAmt -= (up ? 1 : -1) * (float)(k * (patSpeed - 1));
                                    } else if (aa >= 1) {
                                        int k = (int)std::lround(aa);
                                        k = k > 15 ? 15 : k;
                                        x.setFx(0xE, (uint8_t)((up ? 0x10 : 0x20) | k));
                                        st.pitchSlideAmt -= (up ? 1 : -1) * (float)k;
                                    }
                                }
                            }
                            // Symphonie's vibrato and tremolo keep going until switched off
                            if (!x.hasFx && st.vibrato) {
                                x.setFx(0x4, st.vibrato);
                                st.retrigVibrato = false;
                            }
                            if (!x.hasFx && st.tremolo) {
                                x.setFx(0x7, st.tremolo);
                                st.retrigTremolo = false;
                            }
                            if (x.fx != 0x3 && st.tonePortaRemain > 0) {
                                if (!x.hasFx) x.setFx(0x3, 0);
                                else if (!x.vol) x.vol = 0xF0;
                                const int done = st.tonePortaAmt * (patSpeed - 1);
                                st.tonePortaRemain -= done < st.tonePortaRemain ? done : st.tonePortaRemain;
                            }
                            st.sampleVibPhase = (uint16_t)((st.sampleVibPhase + st.sampleVibSpeed * patSpeed) & 1023);

                            if (!(ch & 1) && ch + 1 < numCh && st.lastInst > 0 && st.lastInst <= (int)numIns &&
                                (inst(st.lastInst - 1).instFlags() & IF_SYNC_PLAY)) {
                                syncCell = x;
                                applySync = true;
                                if (syncCell.inst && syncCell.inst < (int)numIns &&
                                    inst(st.lastInst - 1).channel() == CHAN_STEREO_L)
                                    ++syncCell.inst;
                            }
                        }
                        Pattern& pat = s.patterns[it->second[rw >> 8]];
                        for (int ch = 0; ch < numCh; ++ch) {
                            const XCell& x = row[ch];
                            Cell& c = pat.at(rw & 255, ch);
                            c.note = (uint8_t)x.note;
                            c.instr = x.inst ? (uint8_t)(sampleFor(x.inst - 1, ch & 1) + 1) : 0;
                            c.vol = x.vol;
                            c.fx = x.fx;
                            c.param = x.param;
                        }
                    }
                }
                // the position's speed on row 0 (or the first row with a free effect column)
                if (pos.speed) {
                    Pattern& p0 = s.patterns[it->second[0]];
                    bool placed = false;
                    for (int ch = 0; ch < numCh && !placed; ++ch)
                        placed = p0.at(0, ch).fx == 0xF && p0.at(0, ch).param != 0;
                    for (int rw = 0; rw < p0.rows && !placed; ++rw)
                        for (int ch = 0; ch < numCh && !placed; ++ch) {
                            Cell& c = p0.at(rw, ch);
                            if (c.fx == 0 && c.param == 0) {
                                c.fx = 0xF;
                                c.param = speedParam(pos.speed);
                                placed = true;
                            }
                        }
                }
            }
            const int loops = pos.loopNum > 1 ? pos.loopNum : 1;
            for (int l = 0; l < loops && !full; ++l)
                for (uint8_t id : it->second) {
                    if (s.order.size() >= 256) {
                        truncated = true;
                        full = true;
                        break;
                    }
                    s.order.push_back(id);
                }
        }
    }
    if (s.patterns.empty()) s.patterns.push_back(makePattern(64, s.numChannels));
    if (s.order.empty()) s.order.push_back(0);
    if (truncated) s.loadFlags |= LOAD_TRUNCATED;
    return Result<Song>::ok(static_cast<Song&&>(s));
}
