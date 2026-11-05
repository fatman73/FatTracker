// FastTracker 2 instrument processing: envelopes, fadeout, autovibrato.
#include "core/player.h"
#include "core/tables.h"

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
    if (in.vibDepth && in.vibRate) {
        int depth = in.vibDepth;
        if (in.vibSweep && ch.avSweep < in.vibSweep) {
            depth = depth * ch.avSweep / in.vibSweep;
            ++ch.avSweep;
        }
        const uint8_t pos = ch.avPos;
        int v;
        switch (in.vibType & 3) {
            case 0: v = (pos & 128) ? -kVibratoSine[(pos >> 2) & 31] : kVibratoSine[(pos >> 2) & 31]; break;
            case 1: v = (pos & 128) ? -255 : 255; break;
            case 2: v = 255 - pos * 2; break;
            default: v = pos * 2 - 255; break;
        }
        ch.avDelta = (v * depth) >> 6;
        ch.avPos = (uint8_t)(ch.avPos + in.vibRate);
    }
}
