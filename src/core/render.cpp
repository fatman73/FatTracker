#include "core/render.h"

#include <cmath>

#include "core/player.h"

size_t renderSong(const Song& s, uint32_t rate, Interp interp, bool ramps, float maxSeconds, std::vector<float>& out) {
    Mixer* m = new Mixer(rate);  // large object (scope buffers): keep it off the stack
    m->setInterp(interp);
    m->setRamp(ramps);
    Player* p = new Player(*m);
    p->setSong(&s);
    p->play();
    const size_t maxFrames = (size_t)((double)maxSeconds * rate);
    const uint32_t block = 480;
    out.clear();
    size_t frames = 0;
    while (frames < maxFrames && p->playing() && !p->loopedOnce()) {
        uint32_t n = (uint32_t)(maxFrames - frames < block ? maxFrames - frames : block);
        out.resize((frames + n) * 2);
        p->render(out.data() + frames * 2, n);
        frames += n;
    }
    delete p;
    delete m;
    return frames;
}

Sample renderPatternSample(const Song& s, int pattern, uint32_t rate) {
    Sample out;
    if (pattern < 0 || pattern >= (int)s.patterns.size() || !rate) return out;
    Song one = s;
    one.order.assign(1, (uint8_t)pattern);
    one.restartPos = 0;
    std::vector<float> st;
    const size_t frames = renderSong(one, rate, Interp::Linear, true, 60.f, st);
    out.is16 = true;
    out.length = (uint32_t)frames;
    out.data16.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const float m = (st[i * 2] + st[i * 2 + 1]) * 0.5f * 32767.f;
        out.data16[i] = (int16_t)(m > 32767.f ? 32767 : (m < -32768.f ? -32768 : std::lround(m)));
    }
    return out;
}

double correlation(const float* a, size_t na, const float* b, size_t nb) {
    size_t n = na < nb ? na : nb;
    if (n == 0) return 0;
    double sa = 0, sb = 0;
    for (size_t i = 0; i < n; ++i) {
        sa += a[i];
        sb += b[i];
    }
    double ma = sa / n, mb = sb / n, cov = 0, va = 0, vb = 0;
    for (size_t i = 0; i < n; ++i) {
        double x = a[i] - ma, y = b[i] - mb;
        cov += x * y;
        va += x * x;
        vb += y * y;
    }
    if (va <= 0 || vb <= 0) return (va <= 0 && vb <= 0) ? 1.0 : 0.0;
    return cov / std::sqrt(va * vb);
}
