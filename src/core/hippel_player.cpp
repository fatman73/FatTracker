#include "core/hippel_player.h"

#include <cmath>
#include <cstring>
#include <string>

#include "core/hippel/HippelDecoder.h"
#include "core/hippel/PaulaVoice.h"
#include "core/song.h"

namespace {
constexpr double kPaulaClock = 3546895.0;  // PAL

// One Paula DMA channel (same model as the TFMX player's): start / length, then the repeat part,
// counting the loops the sequences may wait on, sample-and-hold like the hardware.
class Voice : public hippeldec::PaulaVoice {
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
            if (!len) len = 1;
            end_ = cur_ ? cur_ + len : nullptr;
            frac_ = 0;
        }
        rep_ = paula.start;
        uint32_t rlen = (uint32_t)paula.length << 1;
        if (!rlen) rlen = 1;
        repEnd_ = rep_ ? rep_ + rlen : nullptr;
    }
    uword getLoopCount() override { return loops_; }

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

struct HippelPlayer::Impl {
    hippeldec::HippelDecoder dec;
    Voice voice[kMaxVoices];
    uint32_t rate = 48000;
    double tickLeft = 0;
    bool loaded = false;
    std::string format;
    float vu[kMaxVoices] = {};
    float scope[kMaxVoices][kScopeLen] = {};
    uint32_t scopePos = 0;
};

HippelPlayer::HippelPlayer() : impl_(new Impl) {}
HippelPlayer::~HippelPlayer() = default;

bool HippelPlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 16 || n > (1u << 26)) return false;
    hippeldec::HippelDecoder dec;
    return dec.detect(const_cast<uint8_t*>(d), (udword)n);
}

bool HippelPlayer::load(const uint8_t* data, size_t size, int song) {
    impl_->loaded = false;
    if (!data || size < 16 || size > (1u << 26)) return false;
    auto& dec = impl_->dec;
    for (int v = 0; v < kMaxVoices; ++v) dec.setPaulaVoice((ubyte)v, &impl_->voice[v]);
    // init() copies the data and dry-runs the song once to find its length
    if (!dec.init(const_cast<uint8_t*>(data), (udword)size, song)) return false;
    dec.setLoopMode(true);
    impl_->format = dec.getFormatName();
    impl_->loaded = true;
    restart();
    return true;
}

void HippelPlayer::restart() {
    if (!impl_->loaded) return;
    for (auto& v : impl_->voice) v.off();
    impl_->dec.restart();
    impl_->tickLeft = 0;
}

void HippelPlayer::setRate(uint32_t hz) { impl_->rate = hz ? hz : 48000; }

void HippelPlayer::clearMeters() {
    std::memset(impl_->vu, 0, sizeof impl_->vu);
    std::memset(impl_->scope, 0, sizeof impl_->scope);
}

void HippelPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.loaded) return;
    const int nv = voices();
    // 7 voice formats: voices 3..6 are mixed into Paula channel 3 (each at a quarter)
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    float peak[kMaxVoices] = {};
    for (uint32_t f = 0; f < frames; ++f) {
        if (m.tickLeft <= 0) {
            int ms = m.dec.run();  // one player tick
            if (ms <= 0) ms = 20;
            m.tickLeft += ms * m.rate / 1000.0;
        }
        m.tickLeft -= 1;
        float l = 0, r = 0;
        for (int v = 0; v < nv; ++v) {
            const float s = m.voice[v].next(m.rate) * (nv > 4 && v >= 3 ? 0.25f : 1.f);
            const int pc = v < 4 ? v : 3;  // Paula wiring: 0 and 3 left, 1 and 2 right
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

int HippelPlayer::voices() const {
    if (!impl_->loaded) return 0;
    const int n = (int)impl_->dec.getVoices();
    return n < 1 ? 4 : (n > kMaxVoices ? kMaxVoices : n);
}
float HippelPlayer::vu(int v) const { return v >= 0 && v < kMaxVoices ? impl_->vu[v] : 0.f; }

void HippelPlayer::scope(int v, float* dst, int n) const {
    if (v < 0 || v >= kMaxVoices || n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    const uint32_t start = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = impl_->scope[v][(start + k) & (kScopeLen - 1)];
}

double HippelPlayer::seconds() const { return impl_->loaded ? impl_->dec.getDuration() / 1000.0 : 0.0; }
const char* HippelPlayer::format() const { return impl_->loaded ? impl_->format.c_str() : ""; }

bool HippelPlayer::buildDisplay(Song& out) {
    if (!impl_->loaded) return false;
    const int ch = voices() < 4 ? 4 : voices();
    out = makeEmptySong(ch);
    return true;
}
