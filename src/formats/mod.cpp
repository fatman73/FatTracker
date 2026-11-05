// ProTracker MOD (31 samples, 1-32 channels) and Soundtracker (15 samples) loader.
#include <cmath>
#include <cstring>

#include "core/reader.h"
#include "core/tables.h"
#include "formats/formats_internal.h"

namespace {

int modChannelsFromSig(const uint8_t* s) {
    auto is = [&](const char* t) { return std::memcmp(s, t, 4) == 0; };
    if (is("M.K.") || is("M!K!") || is("M&K!") || is("FLT4") || is("4CHN")) return 4;
    if (is("FLT8") || is("8CHN") || is("OKTA") || is("CD81")) return 8;
    auto dig = [](uint8_t c) { return c >= '0' && c <= '9'; };
    if (dig(s[0]) && s[1] == 'C' && s[2] == 'H' && s[3] == 'N') return s[0] - '0';
    if (dig(s[0]) && dig(s[1]) && s[2] == 'C' && (s[3] == 'H' || s[3] == 'N')) return (s[0] - '0') * 10 + (s[1] - '0');
    return 0;
}

uint8_t periodToNote(int period) {
    if (period <= 0) return NOTE_NONE;
    if (period >= 108 && period <= 907) {
        int best = 0, bestDist = 1 << 30;
        for (int i = 0; i < 36; ++i) {
            int d = std::abs(kPtPeriods[0][i] - period);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        // values below C-1 (e.g. 907) round to C-1 only if close; otherwise use log
        if (bestDist <= kPtPeriods[0][best] / 30) return (uint8_t)ptNoteToXm(best);
    }
    int n = 49 + (int)std::lround(12.0 * std::log2(428.0 / period));
    if (n < 1) n = 1;
    if (n > 96) n = 96;
    return (uint8_t)n;
}

struct Header {
    int numSamples;
    int channels;
    size_t patternOffset;
};

Result<Song> loadCommon(const uint8_t* data, size_t size, const Header& h) {
    ByteReader r(data, size);
    Song s;
    s.numChannels = (uint8_t)h.channels;
    s.quirks = Quirks::PT23;
    s.freqMode = FreqMode::Amiga;
    setDefaultPanning(s);
    r.bytes(s.name, 20);
    s.name[20] = 0;

    struct Raw {
        uint32_t len, repStart, repLen;
    };
    Raw raw[31] = {};
    s.samples.resize(h.numSamples);
    for (int i = 0; i < h.numSamples; ++i) {
        Sample& sm = s.samples[i];
        r.bytes(sm.name, 22);
        sm.name[22] = 0;
        raw[i].len = (uint32_t)r.u16be() * 2;
        uint8_t ft = r.u8() & 0x0F;
        sm.finetune = (int8_t)((ft > 7 ? ft - 16 : ft) * 16);
        uint8_t vol = r.u8();
        sm.volume = vol > 64 ? 64 : vol;
        raw[i].repStart = r.u16be();
        raw[i].repLen = r.u16be();
    }
    uint8_t songLen = r.u8();
    r.u8();  // restart byte (ignored by PT / tempo in Soundtracker)
    uint8_t order[128];
    r.bytes(order, 128);
    if (songLen == 0 || songLen > 128) songLen = songLen ? 128 : 1;
    s.order.assign(order, order + songLen);

    size_t patternBytes = (size_t)64 * h.channels * 4;
    int maxAll = 0, maxSong = 0;
    for (int i = 0; i < 128; ++i)
        if (order[i] > maxAll && order[i] < 128) maxAll = order[i];
    for (int i = 0; i < songLen; ++i)
        if (order[i] > maxSong) maxSong = order[i];
    int numPatterns = maxAll + 1;
    if (h.patternOffset + (size_t)numPatterns * patternBytes > size) numPatterns = maxSong + 1;
    for (auto& o : s.order)
        if (o >= numPatterns) o = 0;

    r.seek(h.patternOffset);
    s.patterns.reserve(numPatterns);
    for (int p = 0; p < numPatterns; ++p) {
        Pattern pat = makePattern(64, h.channels);
        for (int row = 0; row < 64; ++row) {
            for (int c = 0; c < h.channels; ++c) {
                uint8_t b0 = r.u8(), b1 = r.u8(), b2 = r.u8(), b3 = r.u8();
                Cell& cell = pat.at(row, c);
                cell.instr = (uint8_t)((b0 & 0xF0) | (b2 >> 4));
                cell.note = periodToNote(((b0 & 0x0F) << 8) | b1);
                cell.fx = b2 & 0x0F;
                cell.param = b3;
            }
        }
        s.patterns.push_back(static_cast<Pattern&&>(pat));
    }
    if (r.overrun()) s.loadFlags |= LOAD_TRUNCATED;

    const bool soundtracker = h.numSamples == 15;
    for (int i = 0; i < h.numSamples; ++i) {
        Sample& sm = s.samples[i];
        sm.length = raw[i].len;
        sm.data8.resize(sm.length);
        size_t before = r.remaining();
        r.bytes(sm.data8.data(), sm.length);
        if (before < sm.length) s.loadFlags |= LOAD_TRUNCATED;
        uint32_t start = soundtracker ? raw[i].repStart : raw[i].repStart * 2;
        uint32_t len = raw[i].repLen * 2;
        if (raw[i].repLen > 1 && start < sm.length) {
            if (start + len > sm.length) len = sm.length - start;
            if (len >= 2) {
                sm.loop = LoopType::Forward;
                sm.loopStart = start;
                sm.loopLength = len;
            }
        }
    }
    return Result<Song>::ok(static_cast<Song&&>(s));
}

}  // namespace

bool modDetect(const uint8_t* data, size_t size) { return size >= 1084 && modChannelsFromSig(data + 1080) > 0; }

Result<Song> modLoad(const uint8_t* data, size_t size) {
    int ch = modChannelsFromSig(data + 1080);
    if (ch < 1 || ch > kMaxChannels) return Result<Song>::fail("UNSUPPORTED CHANNELS");
    return loadCommon(data, size, {31, ch, 1084});
}

bool stDetect(const uint8_t* data, size_t size) {
    if (size < 600 + 1024) return false;
    for (int i = 0; i < 15; ++i) {
        const uint8_t* sh = data + 20 + i * 30;
        if (sh[25] > 64) return false;  // volume
        if (sh[24] > 15) return false;  // finetune byte (0 in Soundtracker)
        for (int k = 0; k < 22; ++k)
            if (sh[k] != 0 && (sh[k] < 32 || sh[k] > 126)) return false;
    }
    uint8_t songLen = data[470];
    if (songLen < 1 || songLen > 128) return false;
    int maxPat = 0;
    for (int i = 0; i < 128; ++i) {
        if (data[472 + i] >= 64) return false;
        if (data[472 + i] > maxPat) maxPat = data[472 + i];
    }
    return size >= 600 + 1024 * (size_t)(maxPat + 1);
}

Result<Song> stLoad(const uint8_t* data, size_t size) { return loadCommon(data, size, {15, 4, 600}); }
