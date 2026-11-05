#include "core/tfmx_player.h"

#include <cmath>
#include <cstring>

#include "core/tfmx/PaulaVoice.h"
#include "core/tfmx/TFMXDecoder.h"

namespace {
constexpr double kPaulaClock = 3546895.0;  // PAL

// One Paula DMA channel: plays 8-bit samples between start and end, then the repeat part,
// counting the loops (TFMX macros wait on those), sample-and-hold like the hardware.
class Voice : public tfmxaudiodecoder::PaulaVoice {
public:
    void on() override {
        takeNextBuf();
        on_ = true;
    }
    void off() override {
        on_ = false;
        paula.period = 0;
        paula.volume = 0;
        loops_ = 0;
    }
    void takeNextBuf() override {
        if (!on_) {
            loops_ = 0;
            cur_ = paula.start;
            uint32_t len = (uint32_t)paula.length << 1;
            if (!len) len = 1;  // Paula would play 65536 words
            end_ = cur_ ? cur_ + len : nullptr;
            frac_ = 0;
        }
        rep_ = paula.start;
        uint32_t rlen = (uint32_t)paula.length << 1;
        if (!rlen) rlen = 1;
        repEnd_ = rep_ ? rep_ + rlen : nullptr;
    }
    uword getLoopCount() override { return loops_; }

    // next output sample (-1..1, volume applied), step = source samples per output sample
    float next(double rate) {
        if (!on_ || !cur_ || paula.period < 2) return 0.f;
        const float s = (cur_ < end_ ? (int8_t)*cur_ : 0) / 128.f * (paula.volume > 64 ? 64 : paula.volume) / 64.f;
        frac_ += kPaulaClock / paula.period / rate;
        while (frac_ >= 1.0) {
            frac_ -= 1.0;
            if (++cur_ >= end_) {
                cur_ = rep_;
                end_ = repEnd_;
                ++loops_;
                if (!cur_) break;
            }
        }
        return s;
    }

private:
    bool on_ = false;
    const uint8_t *cur_ = nullptr, *end_ = nullptr, *rep_ = nullptr, *repEnd_ = nullptr;
    double frac_ = 0;
    uword loops_ = 0;
};
}  // namespace

struct TfmxPlayer::Impl {
    tfmxaudiodecoder::TFMXDecoder dec;
    Voice voice[kMaxVoices];
    uint32_t rate = 48000;
    double tickLeft = 0;  // output samples left in the current 50 Hz (or BPM) tick
    bool loaded = false;
    float vu[kMaxVoices] = {};
    float scope[kMaxVoices][kScopeLen] = {};
    uint32_t scopePos = 0;
};

TfmxPlayer::TfmxPlayer() : impl_(new Impl) {}
TfmxPlayer::~TfmxPlayer() = default;

bool TfmxPlayer::detect(const uint8_t* d, size_t n) {
    auto at = [&](const char* tag) {
        const size_t k = std::strlen(tag);
        return n >= k && std::memcmp(d, tag, k) == 0;
    };
    return at("TFMX-SONG ") || at("tfmxsong") || (n >= 5 && std::memcmp(d, "TFMX", 4) == 0 && d[4] == 0x20) ||
           at("TFMXPAK ") || at("TFHD") || at("TFMX-MOD");
}

bool TfmxPlayer::load(const uint8_t* mdat, size_t mdatSize, const uint8_t* smpl, size_t smplSize, int song) {
    impl_->loaded = false;
    if (!mdat || mdatSize < 0x200 || mdatSize > (1u << 30) || smplSize > (1u << 30)) return false;
    auto& dec = impl_->dec;
    if (smpl && smplSize) dec.setSamples(smpl, (udword)smplSize);
    for (int v = 0; v < kMaxVoices; ++v) dec.setPaulaVoice((ubyte)v, &impl_->voice[v]);
    // init() copies the data and runs the song once to find its length
    if (!dec.init(const_cast<uint8_t*>(mdat), (udword)mdatSize, song)) return false;
    dec.setLooping(true);
    impl_->loaded = true;
    restart();
    return true;
}

void TfmxPlayer::restart() {
    if (!impl_->loaded) return;
    for (auto& v : impl_->voice) v.off();
    impl_->dec.restart();
    impl_->tickLeft = 0;
}

void TfmxPlayer::setRate(uint32_t hz) { impl_->rate = hz ? hz : 48000; }

void TfmxPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.loaded) return;
    const int nv = voices();
    if (nv > 4) gain *= 0.6f;  // TFMX 7V: four virtual voices share Paula channel 3
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    float peak[kMaxVoices] = {};
    for (uint32_t f = 0; f < frames; ++f) {
        if (m.tickLeft <= 0) {
            int ms = m.dec.run();  // one player tick (macros, patterns, modulation)
            if (ms <= 0) ms = 20;
            m.tickLeft += ms * m.rate / 1000.0;
        }
        m.tickLeft -= 1;
        float l = 0, r = 0;
        for (int v = 0; v < nv; ++v) {
            const float s = m.voice[v].next(m.rate);
            // Paula wiring: channels 0 and 3 left, 1 and 2 right (7V voices 4-7 share channel 3)
            const int pc = v < 4 ? v : 3;
            const bool left = pc == 0 || pc == 3;
            l += s * (left ? 1.f : 0.3f);
            r += s * (left ? 0.3f : 1.f);
            const float a = std::fabs(s);
            if (a > peak[v]) peak[v] = a;
            m.scope[v][(m.scopePos + f) & (kScopeLen - 1)] = s;
        }
        out[2 * f] = l * gain;
        out[2 * f + 1] = r * gain;
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
    for (int v = 0; v < kMaxVoices; ++v) {
        const float d = m.vu[v] * decay;
        m.vu[v] = peak[v] > d ? peak[v] : d;
    }
}

int TfmxPlayer::voices() const { return impl_->loaded ? (int)impl_->dec.getVoices() : 0; }
float TfmxPlayer::vu(int v) const { return v >= 0 && v < kMaxVoices ? impl_->vu[v] : 0.f; }

void TfmxPlayer::scope(int v, float* dst, int n) const {
    if (v < 0 || v >= kMaxVoices || n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    const uint32_t start = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = impl_->scope[v][(start + k) & (kScopeLen - 1)];
}

int TfmxPlayer::songs() const { return impl_->loaded ? impl_->dec.getSongs() : 0; }
int TfmxPlayer::step() const { return impl_->loaded ? impl_->dec.currentStep() - impl_->dec.firstStep() : 0; }
int TfmxPlayer::steps() const { return impl_->loaded ? impl_->dec.lastStep() - impl_->dec.firstStep() + 1 : 0; }
double TfmxPlayer::seconds() const { return impl_->loaded ? impl_->dec.getDuration() / 1000.0 : 0.0; }
const char* TfmxPlayer::format() const { return impl_->loaded ? impl_->dec.formatLabel().c_str() : ""; }
