#include "core/hvl_player.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/hvl/hvl_replay.h"
#include "core/song.h"

namespace {
constexpr uint32_t kMinRate = 8000, kMaxRate = 384000;
constexpr uint32_t kMaxTick = kMaxRate / 50;  // samples of one player tick at most (speed 1)
constexpr int kAhxStereo = 2;                // AHX files carry no stereo separation: 50 %, as AHX itself
constexpr double kMaxSeconds = 30 * 60;      // the dry run gives up after half an hour
constexpr size_t kMaxFile = 2u << 20;        // the largest structures fit in under 1 MB
constexpr uint32_t kWrap = 0x280u << 16;     // a voice buffer (fixed point position)

// The replayer's wave tables are global: generated once, before any tune is parsed. A function-local static
// is initialised once even when several threads get here at the same time.
void initTables() {
    static const bool done = (hvl_InitReplayer(), true);
    (void)done;
}
}  // namespace

struct HvlPlayer::Impl {
    std::vector<uint8_t> file;  // our own copy: sub-songs and the dry run parse it again
    hvl_tune* ht = nullptr;
    uint32_t rate = 48000;
    int song = 0;
    double seconds = 0;
    char format[16] = {};
    char title[128] = {};
    // one player tick of output: the replayer's stereo mix and each metered voice on its own
    std::vector<int16_t> mix = std::vector<int16_t>((size_t)kMaxTick * 2);
    std::vector<float> voice[kMaxVoices];
    uint32_t tickLen = 0, tickPos = 0;
    float vu[kMaxVoices] = {};
    float scope[kMaxVoices][kScopeLen] = {};
    uint32_t scopePos = 0;

    Impl() {
        for (auto& v : voice) v.resize(kMaxTick);
    }
    ~Impl() { release(); }
    void release() {
        if (ht) hvl_FreeTune(ht);
        ht = nullptr;
    }
    hvl_tune* parse() const { return hvl_ParseTune(file.data(), (uint32)file.size(), rate, kAhxStereo); }
    int metered() const { return ht ? (ht->ht_Channels < kMaxVoices ? ht->ht_Channels : kMaxVoices) : 0; }

    void start() {  // the current sub-song from its start
        hvl_InitSubsong(ht, (uint32)song);
        tickLen = tickPos = 0;
    }

    // Song length: a fresh copy of the tune runs its player (no mixing) until the song loops.
    void measure() {
        seconds = 0;
        hvl_tune* t = parse();
        if (!t) return;
        if (hvl_InitSubsong(t, (uint32)song)) {
            const uint32_t perSecond = 50u * (t->ht_SpeedMultiplier ? t->ht_SpeedMultiplier : 1);
            const uint64_t cap = (uint64_t)(kMaxSeconds * perSecond);
            uint64_t n = 0;
            while (n < cap && !t->ht_SongEndReached) hvl_play_irq(t), ++n;
            seconds = (double)n / perSecond;
        }
        hvl_FreeTune(t);
    }

    // One player tick (hvl_DecodeFrame does speed-multiplier of these per 1/50 s): the tick, then the
    // replayer's mixer; each metered voice is then stepped the way the mixer stepped it, for the scopes.
    void tick() {
        const uint32_t sm = ht->ht_SpeedMultiplier ? ht->ht_SpeedMultiplier : 1;
        uint32_t samples = ht->ht_Frequency / 50 / sm;
        if (samples < 1) samples = 1;
        if (samples > kMaxTick) samples = kMaxTick;
        hvl_play_irq(ht);
        struct State {
            const int8* src;
            const int8* rsrc;
            uint32_t pos, delta, rpos, rdelta;
            int32_t vol;
        } st[kMaxVoices];
        const int nv = metered();
        for (int v = 0; v < nv; ++v) {
            const hvl_voice& vc = ht->ht_Voices[v];
            st[v] = {vc.vc_MixSource, vc.vc_RingMixSource, vc.vc_SamplePos, vc.vc_Delta, vc.vc_RingSamplePos,
                     vc.vc_RingDelta ? vc.vc_RingDelta : 1, vc.vc_VoiceVolume};
        }
        hvl_mixchunk(ht, samples, reinterpret_cast<int8*>(&mix[0]), reinterpret_cast<int8*>(&mix[1]), 4);
        for (int v = 0; v < nv; ++v) {
            State& s = st[v];
            float* out = voice[v].data();
            if (!s.src || !s.vol) {
                std::memset(out, 0, sizeof(float) * samples);
                continue;
            }
            for (uint32_t i = 0; i < samples; ++i) {
                if (s.pos >= kWrap) s.pos -= kWrap;
                int32_t j;
                if (s.rsrc) {
                    if (s.rpos >= kWrap) s.rpos -= kWrap;
                    j = ((s.src[s.pos >> 16] * s.rsrc[s.rpos >> 16]) >> 7) * s.vol;
                    s.rpos += s.rdelta;
                } else {
                    j = s.src[s.pos >> 16] * s.vol;
                }
                s.pos += s.delta;
                out[i] = (float)j / (128.f * 64.f);
            }
        }
        tickLen = samples;
        tickPos = 0;
    }
};

HvlPlayer::HvlPlayer() : impl_(new Impl) {}
HvlPlayer::~HvlPlayer() = default;

bool HvlPlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 16 || n > kMaxFile) return false;
    const bool ahx = d[0] == 'T' && d[1] == 'H' && d[2] == 'X';
    const bool hvl = d[0] == 'H' && d[1] == 'V' && d[2] == 'L';
    if (!(ahx || hvl) || d[3] > 1) return false;  // versions 0 and 1 of both
    const size_t posn = ((d[6] & 15) << 8) | d[7], trkl = d[10], trkn = d[11], insn = d[12], ssn = d[13];
    if (posn == 0 || posn > 1000 || trkl == 0 || trkl > 64 || insn > 64) return false;
    const bool blank0 = (d[6] & 0x80) != 0;  // track 0 is empty and not stored
    size_t off;
    if (ahx) {
        off = 14 + ssn * 2 + posn * 4 * 2 + (trkn + (blank0 ? 0 : 1)) * trkl * 3;
        if (off > n) return false;
    } else {
        const size_t chn = (d[8] >> 2) + 4;
        if (chn > 16 || d[15] > 4) return false;  // up to 16 channels, stereo separation 0..4
        off = 16 + ssn * 2 + posn * chn * 2;
        if (off > n) return false;
        for (size_t t = blank0 ? 1 : 0; t <= trkn; ++t)
            for (size_t r = 0; r < trkl; ++r) {
                if (off >= n) return false;
                off += d[off] == 0x3f ? 1 : 5;  // an empty row is one byte
            }
        if (off > n) return false;
    }
    for (size_t i = 0; i < insn; ++i) {  // 22 bytes and the play list
        if (off + 22 > n) return false;
        off += 22 + d[off + 21] * (size_t)(ahx ? 4 : 5);
        if (off > n) return false;
    }
    // the names follow the data and the header points at them (16 bits: the low bits in a file over 64 KB)
    return off < n && (size_t)((d[4] << 8) | d[5]) == (off & 0xFFFF);
}

bool HvlPlayer::load(const uint8_t* data, size_t size, int subsong) {
    initTables();
    Impl& m = *impl_;
    m.release();
    m.file.clear();
    if (!detect(data, size)) return false;
    m.file.assign(data, data + size);
    m.ht = m.parse();
    if (!m.ht) {
        m.file.clear();
        return false;
    }
    m.song = subsong > 0 && subsong <= m.ht->ht_SubsongNr ? subsong : 0;
    m.start();
    m.measure();
    if (m.file[0] == 'T') std::snprintf(m.format, sizeof m.format, "AHX");
    else if (m.ht->ht_Channels > 4) std::snprintf(m.format, sizeof m.format, "HVL %dCH", m.ht->ht_Channels);
    else std::snprintf(m.format, sizeof m.format, "HVL");
    std::memcpy(m.title, m.ht->ht_Name, sizeof m.title);
    m.title[sizeof m.title - 1] = 0;
    for (char& c : m.title)
        if (c && (c < 32 || c > 126)) c = ' ';
    clearMeters();
    return true;
}

void HvlPlayer::restart() {
    if (impl_->ht) impl_->start();
}

void HvlPlayer::setRate(uint32_t hz) {
    hz = hz ? hz : 48000;
    hz = hz < kMinRate ? kMinRate : (hz > kMaxRate ? kMaxRate : hz);
    Impl& m = *impl_;
    m.rate = hz;
    if (!m.ht || m.ht->ht_Frequency == hz) return;
    // the voices take the new rate at the next tick (their steps are worked out when a period is planted)
    m.ht->ht_Frequency = hz;
    m.ht->ht_FreqF = hz;
    for (int v = 0; v < m.ht->ht_Channels && v < MAX_CHANNELS; ++v) {
        hvl_voice& vc = m.ht->ht_Voices[v];
        vc.vc_PlantPeriod = 1;
        if (vc.vc_RingMixSource) vc.vc_RingPlantPeriod = 1;
    }
}

void HvlPlayer::clearMeters() {
    std::memset(impl_->vu, 0, sizeof impl_->vu);
    std::memset(impl_->scope, 0, sizeof impl_->scope);
}

void HvlPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.ht) return;
    const int nv = m.metered();
    const float k = gain * 2.f / 32768.f;
    for (uint32_t f = 0; f < frames; ++f) {
        if (m.tickPos >= m.tickLen) m.tick();
        out[2 * f] = m.mix[2 * m.tickPos] * k;
        out[2 * f + 1] = m.mix[2 * m.tickPos + 1] * k;
        const uint32_t sp = (m.scopePos + f) & (kScopeLen - 1);
        for (int v = 0; v < nv; ++v) m.scope[v][sp] = m.voice[v][m.tickPos];
        ++m.tickPos;
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
    // meters: each voice's volume (0..64), falling off slowly
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    for (int v = 0; v < nv; ++v) {
        const float level = m.ht->ht_Voices[v].vc_VoiceVolume / 64.f;
        const float d = m.vu[v] * decay;
        m.vu[v] = level > d ? level : d;
    }
}

int HvlPlayer::voices() const { return impl_->ht ? impl_->ht->ht_Channels : 0; }

float HvlPlayer::vu(int v) const { return v >= 0 && v < impl_->metered() ? impl_->vu[v] * 0.8f : 0.f; }

void HvlPlayer::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    if (v < 0 || v >= impl_->metered()) {  // no such voice (or not metered): a flat line
        for (int i = 0; i < n; ++i) dst[i] = 0;
        return;
    }
    const uint32_t start = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int i = 0; i < n; ++i) dst[i] = impl_->scope[v][(start + i) & (kScopeLen - 1)];
}

double HvlPlayer::seconds() const { return impl_->ht ? impl_->seconds : 0.0; }
const char* HvlPlayer::format() const { return impl_->ht ? impl_->format : ""; }
const char* HvlPlayer::title() const { return impl_->ht ? impl_->title : ""; }
int HvlPlayer::subsongs() const { return impl_->ht ? impl_->ht->ht_SubsongNr + 1 : 1; }
int HvlPlayer::subsong() const { return impl_->ht ? impl_->song : 0; }

bool HvlPlayer::selectSubsong(int i) {
    Impl& m = *impl_;
    if (!m.ht || i < 0 || i > m.ht->ht_SubsongNr) return false;
    m.song = i;
    m.start();
    m.measure();
    return true;
}

bool HvlPlayer::buildDisplay(Song& out) {
    if (!impl_->ht) return false;
    out = makeEmptySong(voices() < 4 ? 4 : voices());
    return true;
}
