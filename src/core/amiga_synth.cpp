#include "core/amiga_synth.h"

#include <cmath>
#include <cstring>

#include "core/amiga_formats.h"
#include "core/song.h"

namespace {
constexpr double kPaulaClock = 3546895.0;  // PAL
}

// ---------------------------------------------------------------- AmigaChannel
void AmigaChannel::playSample(const int8_t* data, uint32_t start, uint32_t length) {
    data_ = data;
    pos_ = start;
    end_ = start + length;
    loop_ = pending_ = false;
    active_ = data && length > 0;
}

void AmigaChannel::setSample(const int8_t* data, uint32_t start, uint32_t length) {
    nextData_ = data;
    nextStart_ = start;
    nextLen_ = length;
    pending_ = data && length > 0;
}

void AmigaChannel::setLoop(uint32_t start, uint32_t length) {
    loopStart_ = start;
    loopEnd_ = start + length;
    loop_ = length > 0;
}

float AmigaChannel::next(double rate) {
    if (!active_ || !data_ || period_ < 2) return 0.f;
    const uint32_t i = (uint32_t)pos_;
    const float s = (i < end_ ? data_[i] : 0) / 128.f * volume_ / 64.f;
    pos_ += kPaulaClock / period_ / rate;
    if ((uint32_t)pos_ >= end_) {
        if (pending_) {  // a queued sample replaces the loop (Paula reloads at the end of the buffer)
            data_ = nextData_;
            pos_ = nextStart_ + (pos_ - end_);
            end_ = nextStart_ + nextLen_;
            loopStart_ = nextStart_;
            loopEnd_ = end_;
            loop_ = true;
            pending_ = false;
        } else if (loop_ && loopEnd_ > loopStart_) {
            const double over = pos_ - end_;
            end_ = loopEnd_;
            pos_ = loopStart_ + std::fmod(over, (double)(loopEnd_ - loopStart_));
        } else {
            active_ = false;
        }
    }
    return s;
}

// ---------------------------------------------------------------- AmigaSynthPlayer
std::unique_ptr<AmigaSynthPlayer> AmigaSynthPlayer::open(const uint8_t* d, size_t n) {
    if (!d || n < 8 || n > (64u << 20)) return nullptr;
    if (auto p = openJamCracker(d, n)) return p;
    return nullptr;
}

bool AmigaSynthPlayer::detect(const uint8_t* d, size_t n) {
    return d && n >= 8 && std::memcmp(d, "BeEp", 4) == 0;  // JamCracker
}

void AmigaSynthPlayer::restart() {
    for (auto& c : ch_) c = AmigaChannel{};
    endReached_ = false;
    tickLeft_ = 0;
    initSound();
}

void AmigaSynthPlayer::clearMeters() {
    std::memset(vu_, 0, sizeof vu_);
    std::memset(scope_, 0, sizeof scope_);
}

void AmigaSynthPlayer::measure() {
    restart();
    long ticks = 0;
    const long limit = (long)(tickHz_ * 60 * 30);  // 30 minutes at most
    while (!endReached_ && ticks < limit) {
        playTick();
        ++ticks;
    }
    seconds_ = ticks / tickHz_;
    restart();
}

void AmigaSynthPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    float peak[4] = {};
    for (uint32_t f = 0; f < frames; ++f) {
        if (tickLeft_ <= 0) {
            playTick();
            endReached_ = false;  // keeps looping
            tickLeft_ += rate_ / tickHz_;
        }
        tickLeft_ -= 1;
        float l = 0, r = 0;
        for (int v = 0; v < 4; ++v) {
            const float s = ch_[v].next(rate_);
            const bool left = v == 0 || v == 3;  // Paula: 0 and 3 left, 1 and 2 right
            l += s * (left ? 1.f : 0.3f);
            r += s * (left ? 0.3f : 1.f);
            const float a = std::fabs(s);
            if (a > peak[v]) peak[v] = a;
            scope_[v][(scopePos_ + f) & (kScopeLen - 1)] = s;
        }
        out[2 * f] = l * gain;
        out[2 * f + 1] = r * gain;
    }
    scopePos_ = (scopePos_ + frames) & (kScopeLen - 1);
    const float decay = (float)std::pow(0.5, (double)frames / (rate_ * 0.08));
    for (int v = 0; v < 4; ++v) {
        const float d = vu_[v] * decay;
        vu_[v] = peak[v] > d ? peak[v] : d;
    }
}

void AmigaSynthPlayer::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    if (v < 0 || v >= 4) {
        for (int k = 0; k < n; ++k) dst[k] = 0;
        return;
    }
    const uint32_t start = (scopePos_ - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = scope_[v][(start + k) & (kScopeLen - 1)];
}

bool AmigaSynthPlayer::buildDisplay(Song& out) {
    out = makeEmptySong(4);
    return true;
}
