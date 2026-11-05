#include "core/mixer.h"

#include <cmath>
#include <cstring>

namespace {
constexpr float kVoiceGain = 0.5f;
constexpr float kHalfPi = 1.57079632679f;

inline float softclip(float x) {
    float a = std::fabs(x);
    if (a <= 0.8f) return x;
    float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}
}  // namespace

Mixer::Mixer(uint32_t sampleRate) : rate_(sampleRate), rampLen_((int)(sampleRate / 1000)) {}

void Mixer::setRate(uint32_t hz) {
    rate_ = hz ? hz : 48000;
    rampLen_ = (int)(rate_ / 1000);
    if (rampLen_ < 1) rampLen_ = 1;
}

void Mixer::setNumChannels(int n) { autoScale_ = std::sqrt(4.f / (float)(n < 4 ? 4 : n)); }

// Maps a virtual integer index to a real sample index, or -1 for silence.
int64_t Mixer::mapIndex(const Voice& v, int64_t vi) {
    if (vi < 0) return -1;
    const int64_t len = v.s->length;
    switch (v.loop) {
        case LoopType::None:
            return vi < len ? vi : -1;
        case LoopType::Forward:
            if (vi < v.loopStart + v.loopLen) return vi;
            return v.loopStart + (vi - v.loopStart) % v.loopLen;
        case LoopType::PingPong: {
            if (vi < v.loopStart) return vi;
            int64_t o = (vi - v.loopStart) % v.period;
            return o <= v.loopLen - 1 ? v.loopStart + o : v.loopStart + v.period - o;
        }
    }
    return -1;
}

void Mixer::trigger(int idx, const Sample* s, uint32_t offset) {
    if (idx < 0 || idx >= kMaxVoices) return;
    Voice& v = voices_[idx];
    if (ramp_ && v.active && (v.curL != 0 || v.curR != 0)) {
        Voice& g = ghosts_[idx];
        g = v;
        g.tgtL = g.tgtR = 0;
        g.rampLeft = rampLen_;
        g.dL = -g.curL / rampLen_;
        g.dR = -g.curR / rampLen_;
        g.stopping = true;
    }
    v.s = s;
    v.active = false;
    v.stopping = false;
    if (!s || s->length == 0) return;
    const bool has16 = s->is16 ? s->data16.size() >= s->length : s->data8.size() >= s->length;
    if (!has16) return;
    // sanitize loop
    v.loop = s->loop;
    v.loopStart = s->loopStart;
    v.loopLen = s->loopLength;
    if (v.loop != LoopType::None) {
        if (v.loopStart >= (int64_t)s->length || v.loopLen < 2) v.loop = LoopType::None;
        else if (v.loopStart + v.loopLen > (int64_t)s->length) v.loopLen = s->length - v.loopStart;
        if (v.loopLen < 2) v.loop = LoopType::None;
    }
    v.period = v.loop == LoopType::PingPong ? 2 * (v.loopLen - 1) : v.loopLen;
    if (offset >= s->length) {
        if (v.loop == LoopType::None) return;
        offset = (uint32_t)v.loopStart;
    }
    v.pos = offset;
    v.active = true;
    if (ramp_) {
        v.curL = v.curR = 0;
        startRamp(v);
    } else {
        v.curL = v.tgtL;
        v.curR = v.tgtR;
        v.rampLeft = 0;
    }
}

void Mixer::stopVoice(int idx) {
    if (idx < 0 || idx >= kMaxVoices) return;
    Voice& v = voices_[idx];
    if (!v.active) return;
    if (ramp_) {
        v.stopping = true;
        v.tgtL = v.tgtR = 0;
        startRamp(v);
    } else {
        v.active = false;
    }
}

void Mixer::reset() {
    for (int i = 0; i < kMaxVoices; ++i) {
        voices_[i] = Voice{};
        ghosts_[i] = Voice{};
    }
}

void Mixer::setFrequency(int idx, double hz) {
    if (idx < 0 || idx >= kMaxVoices) return;
    double st = hz / rate_;
    if (!(st >= 0) || st > 1e6) st = 0;  // guards NaN / absurd values
    voices_[idx].step = st;
}

void Mixer::setTargets(Voice& v) {
    float p = v.pan < 0 ? 0 : (v.pan > 1 ? 1 : v.pan);
    p = 0.5f + (p - 0.5f) * separation_;
    v.tgtL = kVoiceGain * v.vol * std::cos(p * kHalfPi);
    v.tgtR = kVoiceGain * v.vol * std::sin(p * kHalfPi);
}

void Mixer::startRamp(Voice& v) {
    v.rampLeft = rampLen_;
    v.dL = (v.tgtL - v.curL) / rampLen_;
    v.dR = (v.tgtR - v.curR) / rampLen_;
}

void Mixer::setVolume(int idx, float vol01, float pan01) {
    if (idx < 0 || idx >= kMaxVoices) return;
    Voice& v = voices_[idx];
    if (v.vol == vol01 && v.pan == pan01) return;
    v.vol = vol01;
    v.pan = pan01;
    if (v.stopping) return;
    setTargets(v);
    if (ramp_ && v.active) {
        startRamp(v);
    } else {
        v.curL = v.tgtL;
        v.curR = v.tgtR;
        v.rampLeft = 0;
    }
}

template <class T>
void Mixer::mixVoice(Voice& v, int idx, const T* data, float norm, float* out, uint32_t frames, float& peak) {
    const int64_t len = v.s->length;
    const int64_t linearEnd = v.loop == LoopType::None ? len : v.loopStart + v.loopLen;  // no wrap below this
    auto fetch = [&](int64_t vi) -> float {
        if (vi >= 0 && vi < linearEnd) return data[vi] * norm;
        int64_t m = mapIndex(v, vi);
        return m < 0 ? 0.f : data[m] * norm;
    };
    float* scope = scope_[idx];
    uint32_t sp = scopePos_[idx];
    const bool isGhost = &v == &ghosts_[idx];
    for (uint32_t f = 0; f < frames; ++f) {
        int64_t i = (int64_t)v.pos;
        if (v.loop == LoopType::None && i >= len) {
            v.active = false;
            break;
        }
        float t = (float)(v.pos - (double)i);
        float smp;
        switch (interp_) {
            case Interp::None:
                smp = fetch(i);
                break;
            case Interp::Linear: {
                float a = fetch(i), b = fetch(i + 1);
                smp = a + (b - a) * t;
                break;
            }
            default: {
                float p0 = fetch(i - 1), p1 = fetch(i), p2 = fetch(i + 1), p3 = fetch(i + 2);
                smp = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
                break;
            }
        }
        out[f * 2] += smp * v.curL;
        out[f * 2 + 1] += smp * v.curR;
        if (!isGhost) {
            float mono = smp * v.vol;
            float a = std::fabs(mono);
            if (a > peak) peak = a;
            scope[sp] = mono;
            sp = (sp + 1) & (kScopeLen - 1);
        }
        if (v.rampLeft > 0) {
            v.curL += v.dL;
            v.curR += v.dR;
            if (--v.rampLeft == 0) {
                v.curL = v.tgtL;
                v.curR = v.tgtR;
                if (v.stopping) {
                    v.active = false;
                    ++f;
                    break;
                }
            }
        }
        v.pos += v.step;
        if (v.loop != LoopType::None && v.pos >= (double)(v.loopStart + v.period)) {
            v.pos = (double)v.loopStart + std::fmod(v.pos - (double)v.loopStart, (double)v.period);
        }
    }
    if (!isGhost) scopePos_[idx] = sp;
}

void Mixer::mix(float* out, uint32_t frames) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    const float decay = (float)std::pow(0.5, (double)frames / (rate_ * 0.08));
    for (int i = 0; i < kMaxVoices; ++i) {
        float peak = 0;
        Voice* list[2] = {&ghosts_[i], &voices_[i]};
        for (Voice* v : list) {
            if (!v->active || !v->s) continue;
            if (v->s->is16)
                mixVoice(*v, i, v->s->data16.data(), 1.f / 32768.f, out, frames, peak);
            else
                mixVoice(*v, i, v->s->data8.data(), 1.f / 128.f, out, frames, peak);
        }
        if (!voices_[i].active) {
            // keep scope moving with silence so the display flattens
            for (uint32_t f = 0; f < frames && f < (uint32_t)kScopeLen; ++f) {
                scope_[i][scopePos_[i]] = 0;
                scopePos_[i] = (scopePos_[i] + 1) & (kScopeLen - 1);
            }
        }
        float d = vu_[i] * decay;
        vu_[i] = peak > d ? peak : d;
    }
    const float g = master_ * autoScale_;
    for (uint32_t k = 0; k < frames * 2; ++k) out[k] = softclip(out[k] * g);
}

bool Mixer::active(int v) const { return v >= 0 && v < kMaxVoices && voices_[v].active; }

double Mixer::position(int idx) const {
    if (idx < 0 || idx >= kMaxVoices) return 0;
    const Voice& v = voices_[idx];
    if (!v.s) return 0;
    int64_t i = (int64_t)v.pos;
    int64_t m = mapIndex(v, i);
    return m < 0 ? (double)v.s->length : (double)m + (v.pos - (double)i);
}

float Mixer::vu(int v) const { return (v >= 0 && v < kMaxVoices) ? vu_[v] : 0.f; }

void Mixer::scope(int v, float* dst, int n) const {
    if (v < 0 || v >= kMaxVoices || n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    uint32_t start = (scopePos_[v] - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = scope_[v][(start + k) & (kScopeLen - 1)];
}
