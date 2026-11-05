// FastTracker 2 instrument processing: envelopes, fadeout, autovibrato.
#include <cmath>

#include "core/player.h"

namespace {
uint8_t envValue(const Envelope& e, uint16_t t) {
    if (e.count == 0) return 64;
    if (t <= e.pts[0].tick) return e.pts[0].value;
    for (int i = 0; i + 1 < e.count; ++i) {
        const EnvPoint& a = e.pts[i];
        const EnvPoint& b = e.pts[i + 1];
        if (t >= a.tick && t < b.tick) {
            int span = b.tick - a.tick;
            if (span <= 0) return b.value;
            return (uint8_t)(a.value + (b.value - a.value) * (int)(t - a.tick) / span);
        }
    }
    return e.pts[e.count - 1].value;
}

void envAdvance(const Envelope& e, uint16_t& t, bool keyOn) {
    if (keyOn && (e.flags & ENV_SUSTAIN) && t == e.pts[e.sustain].tick) return;
    ++t;
    if ((e.flags & ENV_LOOP) && t >= e.pts[e.loopEnd].tick) t = e.pts[e.loopStart].tick;
    const uint16_t last = e.pts[e.count - 1].tick;
    if (t > last) t = last;
}

// FT2 autovibrato sine: 256 entries of +-64, starting negative.
struct SineTab {
    int8_t v[256];
};
SineTab makeSine() {
    SineTab s{};
    for (int i = 0; i < 256; ++i) s.v[i] = (int8_t)std::lround(-64.0 * std::sin(i * 6.283185307179586 / 256.0));
    return s;
}
const SineTab kAutoVibSine = makeSine();
}  // namespace

void Player::instrumentTick(int c) {
    ChannelState& ch = ch_[c];
    if (ch.instrument < 0 || !song_ || ch.instrument >= (int)song_->instruments.size()) return;
    const Instrument& in = song_->instruments[ch.instrument];
    if (in.volEnv.flags & ENV_ON) {
        ch.envVol = envValue(in.volEnv, ch.volEnvTick);
        envAdvance(in.volEnv, ch.volEnvTick, ch.keyOn);
    }
    if (in.panEnv.flags & ENV_ON) {
        ch.envPan = envValue(in.panEnv, ch.panEnvTick);
        envAdvance(in.panEnv, ch.panEnvTick, ch.keyOn);
    }
    if (!ch.keyOn) {
        ch.fadeVol -= in.fadeout;
        if (ch.fadeVol < 0) ch.fadeVol = 0;
    }
    if (in.vibDepth) {
        // amplitude in 1/256 depth units, ramped in over `sweep` ticks (ft2 autoVibrato)
        int amp = in.vibDepth << 8;
        if (in.vibSweep && ch.avSweep < in.vibSweep) {
            amp = amp * ch.avSweep / in.vibSweep;
            ++ch.avSweep;
        }
        ch.avPos = (uint8_t)(ch.avPos + in.vibRate);  // FT2 advances before sampling
        const uint8_t pos = ch.avPos;
        int v;
        switch (in.vibType & 3) {
            case 1: v = pos > 127 ? 64 : -64; break;                    // square
            case 2: v = (((pos >> 1) + 64) & 127) - 64; break;          // ramp up
            case 3: v = (((0 - (pos >> 1)) + 64) & 127) - 64; break;    // ramp down
            default: v = kAutoVibSine.v[pos]; break;                    // sine
        }
        ch.avDelta = (v * amp) >> 14;  // peak = +-depth period units
    }
}
