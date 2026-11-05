#include "core/sample_edit.h"

#include <algorithm>
#include <cmath>

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
