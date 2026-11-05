#include "core/sample_edit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
void range(const Sample& s, uint32_t& a, uint32_t& b) {
    if (a > b) std::swap(a, b);
    a = std::min(a, s.length);
    b = std::min(b, s.length);
}
int get(const Sample& s, uint32_t i) { return s.is16 ? s.data16[i] : s.data8[i]; }
void put(Sample& s, uint32_t i, long v) {
    if (s.is16)
        s.data16[i] = (int16_t)std::clamp(v, -32768L, 32767L);
    else
        s.data8[i] = (int8_t)std::clamp(v, -128L, 127L);
}
Sample converted(const Sample& clip, bool to16) {
    Sample c = clip;
    if (to16 && !c.is16) sampleTo16(c);
    if (!to16 && c.is16) sampleTo8(c);
    return c;
}
}  // namespace

void sampleFixLoop(Sample& s) {
    if (s.loop == LoopType::None) return;
    if (s.loopStart >= s.length || s.loopLength < 2) {
        s.loop = LoopType::None;
        s.loopStart = s.loopLength = 0;
        return;
    }
    if (s.loopStart + s.loopLength > s.length) s.loopLength = s.length - s.loopStart;
    if (s.loopLength < 2) {
        s.loop = LoopType::None;
        s.loopStart = s.loopLength = 0;
    }
}

Sample sampleCopy(const Sample& s, uint32_t a, uint32_t b) {
    range(s, a, b);
    Sample c = s;
    c.loop = LoopType::None;
    c.loopStart = c.loopLength = 0;
    c.length = b - a;
    if (s.is16)
        c.data16.assign(s.data16.begin() + a, s.data16.begin() + b);
    else
        c.data8.assign(s.data8.begin() + a, s.data8.begin() + b);
    return c;
}

Sample sampleCut(Sample& s, uint32_t a, uint32_t b) {
    range(s, a, b);
    Sample c = sampleCopy(s, a, b);
    const uint32_t n = b - a;
    if (!n) return c;
    if (s.is16)
        s.data16.erase(s.data16.begin() + a, s.data16.begin() + b);
    else
        s.data8.erase(s.data8.begin() + a, s.data8.begin() + b);
    s.length -= n;
    if (s.loopStart >= b)
        s.loopStart -= n;
    else if (s.loopStart > a)
        s.loopStart = a;
    sampleFixLoop(s);
    return c;
}

void samplePaste(Sample& s, uint32_t at, const Sample& clip) {
    if (!clip.length) return;
    at = std::min(at, s.length);
    const Sample c = converted(clip, s.is16);
    if (s.is16)
        s.data16.insert(s.data16.begin() + at, c.data16.begin(), c.data16.begin() + c.length);
    else
        s.data8.insert(s.data8.begin() + at, c.data8.begin(), c.data8.begin() + c.length);
    s.length += c.length;
    if (s.loop != LoopType::None && s.loopStart >= at) s.loopStart += c.length;
}

void sampleCrop(Sample& s, uint32_t a, uint32_t b) {
    range(s, a, b);
    Sample c = sampleCopy(s, a, b);
    const bool loopInside = s.loop != LoopType::None && s.loopStart >= a;
    c.loop = loopInside ? s.loop : LoopType::None;
    c.loopStart = loopInside ? s.loopStart - a : 0;
    c.loopLength = loopInside ? s.loopLength : 0;
    s = static_cast<Sample&&>(c);
    sampleFixLoop(s);
}

void sampleSilence(Sample& s, uint32_t a, uint32_t b) {
    range(s, a, b);
    for (uint32_t i = a; i < b; ++i) put(s, i, 0);
}

void sampleVolume(Sample& s, uint32_t a, uint32_t b, float startPct, float endPct) {
    range(s, a, b);
    const uint32_t n = b - a;
    for (uint32_t i = a; i < b; ++i) {
        const float t = n > 1 ? (float)(i - a) / (float)(n - 1) : 0.f;
        const float f = (startPct + (endPct - startPct) * t) / 100.f;
        put(s, i, std::lround(get(s, i) * f));
    }
}

void sampleReverse(Sample& s, uint32_t a, uint32_t b) {
    range(s, a, b);
    if (s.is16)
        std::reverse(s.data16.begin() + a, s.data16.begin() + b);
    else
        std::reverse(s.data8.begin() + a, s.data8.begin() + b);
}

void sampleNormalize(Sample& s, uint32_t a, uint32_t b) {
    range(s, a, b);
    int peak = 0;
    for (uint32_t i = a; i < b; ++i) peak = std::max(peak, std::abs(get(s, i)));
    if (!peak) return;
    const double scale = (s.is16 ? 32767.0 : 127.0) / peak;
    for (uint32_t i = a; i < b; ++i) put(s, i, std::lround(get(s, i) * scale));
}

void sampleTo16(Sample& s) {
    if (s.is16) return;
    s.data16.resize(s.length);
    for (uint32_t i = 0; i < s.length; ++i) s.data16[i] = (int16_t)(s.data8[i] * 256);
    s.data8.clear();
    s.data8.shrink_to_fit();
    s.is16 = true;
}

void sampleTo8(Sample& s) {
    if (!s.is16) return;
    s.data8.resize(s.length);
    for (uint32_t i = 0; i < s.length; ++i) s.data8[i] = (int8_t)(s.data16[i] >> 8);
    s.data16.clear();
    s.data16.shrink_to_fit();
    s.is16 = false;
}

// ---------------------------------------------------------------- sample editor tools

namespace {
void wholeIfEmpty(const Sample& s, uint32_t& a, uint32_t& b) {
    range(s, a, b);
    if (a == b) a = 0, b = s.length;
}
int maxValue(const Sample& s) { return s.is16 ? 32767 : 127; }
}  // namespace

double sampleRateHz(const Sample& s) { return 8363.0 * std::pow(2.0, (s.relativeNote + s.finetune / 128.0) / 12.0); }

void sampleSetRate(Sample& s, double hz) {
    if (!(hz > 0)) return;
    const double semis = 12.0 * std::log2(hz / 8363.0);
    int rel = (int)std::floor(semis + 1e-9);
    int ft = (int)std::lround((semis - rel) * 128.0);
    if (ft >= 128) {
        ++rel;
        ft -= 128;
    }
    s.relativeNote = (int8_t)std::clamp(rel, -96, 95);
    s.finetune = (int8_t)std::min(ft, 127);
}

void sampleResample(Sample& s, double factor) {
    if (!s.length || !(factor > 0)) return;
    const double want = s.length * factor;
    const uint32_t n = (uint32_t)std::clamp(std::lround(want), 1L, 16L * 1024 * 1024);
    Sample out = s;
    out.length = n;
    out.data8.clear();
    out.data16.clear();
    if (s.is16) out.data16.resize(n);
    else out.data8.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        const double p = i / factor;
        const uint32_t k = (uint32_t)p;
        const double f = p - k;
        const int a = get(s, std::min(k, s.length - 1)), b = get(s, std::min(k + 1, s.length - 1));
        put(out, i, std::lround(a + (b - a) * f));
    }
    out.loopStart = (uint32_t)std::lround(s.loopStart * factor);
    out.loopLength = (uint32_t)std::lround(s.loopLength * factor);
    sampleSetRate(out, sampleRateHz(s) * factor);
    sampleFixLoop(out);
    s = static_cast<Sample&&>(out);
}

double sampleDetectPitch(const Sample& s, uint32_t a, uint32_t b) {
    wholeIfEmpty(s, a, b);
    const uint32_t n = std::min<uint32_t>(b - a, 16384);
    if (n < 64) return 0;
    std::vector<float> x(n);
    for (uint32_t i = 0; i < n; ++i) x[i] = (float)get(s, a + i) / maxValue(s);
    const int maxLag = (int)std::min<uint32_t>(n / 2, 8192);
    auto corr = [&](int lag) {
        double acc = 0;
        for (uint32_t i = 0; i + lag < n; ++i) acc += (double)x[i] * x[i + lag];
        return acc / (n - lag);
    };
    const double r0 = corr(0);
    if (r0 < 1e-6) return 0;  // silence
    // the first strong peak after the correlation has dipped below zero is the period
    int lag = 1;
    while (lag < maxLag && corr(lag) > 0) ++lag;
    int best = -1;
    double bestV = 0;
    for (; lag < maxLag; ++lag) {
        const double v = corr(lag);
        if (v > bestV) {
            bestV = v;
            best = lag;
        } else if (best > 0 && v < bestV * 0.9 && bestV > 0.5 * r0) {
            break;  // past the first peak
        }
    }
    if (best < 2 || bestV < 0.3 * r0) return 0;
    // parabolic refinement of the peak
    const double l = corr(best - 1), c = bestV, r = corr(best + 1);
    const double den = l - 2 * c + r;
    const double shift = den != 0 ? 0.5 * (l - r) / den : 0;
    return 1.0 / (best + std::clamp(shift, -0.5, 0.5));
}

bool sampleTuneToC(Sample& s) {
    const double cycles = sampleDetectPitch(s, 0, s.length);
    if (cycles <= 0) return false;
    sampleSetRate(s, 261.6256 / cycles);  // rate / period = C-4
    return true;
}

void sampleMix(Sample& s, uint32_t at, const Sample& clipIn) {
    if (!clipIn.length) return;
    const Sample clip = converted(clipIn, s.is16 || (!s.length && clipIn.is16));
    if (!s.length) s.is16 = clip.is16;
    at = std::min(at, s.length);
    const uint32_t end = std::max(s.length, at + clip.length);
    if (s.is16) s.data16.resize(end, 0);
    else s.data8.resize(end, 0);
    s.length = end;
    for (uint32_t i = 0; i < clip.length; ++i) put(s, at + i, (long)get(s, at + i) + get(clip, i));
}

void sampleLowPass(Sample& s, uint32_t a, uint32_t b) {
    wholeIfEmpty(s, a, b);
    double y = a < s.length ? get(s, a) : 0;
    for (uint32_t i = a; i < b; ++i) {
        y += 0.35 * (get(s, i) - y);
        put(s, i, std::lround(y));
    }
}

void sampleHighPass(Sample& s, uint32_t a, uint32_t b) {
    wholeIfEmpty(s, a, b);
    double lp = 0;
    for (uint32_t i = a; i < b; ++i) {
        const int x = get(s, i);
        lp += 0.01 * (x - lp);
        put(s, i, std::lround(x - lp));
    }
}

void sampleEcho(Sample& s, uint32_t a, uint32_t b) {
    wholeIfEmpty(s, a, b);
    const uint32_t d = (uint32_t)std::max(1.0, sampleRateHz(s) / 8.0);
    std::vector<long> y(b - a);
    for (uint32_t i = a; i < b; ++i) {
        long v = get(s, i);
        if (i - a >= d) v += std::lround(0.4 * y[i - a - d]);
        y[i - a] = std::clamp(v, (long)-maxValue(s) - 1, (long)maxValue(s));
        put(s, i, y[i - a]);
    }
}

void sampleBoost(Sample& s, uint32_t a, uint32_t b) {
    wholeIfEmpty(s, a, b);
    const double m = maxValue(s), knee = m * 0.79;
    for (uint32_t i = a; i < b; ++i) {
        const double x = 2.0 * get(s, i), ax = std::fabs(x);
        const double y = ax <= knee ? ax : knee + (m - knee) * std::tanh((ax - knee) / (m - knee));
        put(s, i, std::lround(x < 0 ? -y : y));
    }
}

void sampleDraw(Sample& s, uint32_t x0, int v0, uint32_t x1, int v1) {
    if (!s.length) return;
    if (x0 > x1) {
        std::swap(x0, x1);
        std::swap(v0, v1);
    }
    for (uint32_t x = x0; x <= x1 && x < s.length; ++x) {
        const double f = x1 == x0 ? 1.0 : (double)(x - x0) / (x1 - x0);
        put(s, x, std::lround(v0 + (v1 - v0) * f));
    }
}

Sample sampleFromRecording(const float* mono, size_t frames, uint32_t rate) {
    Sample s;
    s.is16 = true;
    s.length = (uint32_t)frames;
    s.data16.resize(frames);
    for (size_t i = 0; i < frames; ++i) s.data16[i] = (int16_t)std::clamp(std::lround(mono[i] * 32767.0), -32768L, 32767L);
    sampleSetRate(s, rate);
    return s;
}
