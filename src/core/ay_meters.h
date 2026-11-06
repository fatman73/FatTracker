// VU meters and scopes of the three channels of an AY-3-8910 / YM2149 emulated by ayumi, from what each
// channel really puts out (ayumi's channel_out): shared by the AY / YM players (AyPlayer, Pt3Player,
// ZxTrackerPlayer, SndhPlayer). Written by render() on the audio thread, read by vu() / scope().
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/chip_player.h"

extern "C" {
#include "core/ayumi/ayumi.h"
}

struct AyMeters {
    static constexpr int kLen = ChipPlayer::kScopeLen;
    static constexpr float kScopeGain = 1.6f;  // a full volume square wave swings +-0.8
    static constexpr float kVuGain = 0.6f;     // a full volume channel reads 0.6 (as the other players' meters)

    float scope[3][kLen] = {};
    float vu[3] = {};
    uint32_t pos = 0;    // scope write position (start of the block being rendered)
    uint32_t count = 0;  // frames of the block so far
    float lo[3] = {}, hi[3] = {};

    void clear() {
        std::memset(scope, 0, sizeof scope);
        std::memset(vu, 0, sizeof vu);
    }
    void begin() {
        count = 0;
        for (int c = 0; c < 3; ++c) lo[c] = 2.f, hi[c] = -2.f;
    }
    // One output frame: each channel's level after ayumi_process() (silent: the chip is not heard).
    void sample(const ayumi& ay, bool silent = false) {
        const uint32_t at = (pos + count++) & (kLen - 1);
        for (int c = 0; c < 3; ++c) {
            const float x = silent ? 0.f : (float)ay.channel_out[c];
            scope[c][at] = x;
            lo[c] = x < lo[c] ? x : lo[c];
            hi[c] = x > hi[c] ? x : hi[c];
        }
    }
    // End of the block: the scopes show the wave around the middle of its swing (not the DAC's DC level),
    // the meters the swing itself (a volume level held without a tone, noise or envelope moving it is silent).
    void end(uint32_t rate) {
        const uint32_t first = count > (uint32_t)kLen ? count - kLen : 0;
        const float decay = (float)std::pow(0.5, (double)count / ((rate ? rate : 48000) * 0.08));
        for (int c = 0; c < 3; ++c) {
            const float mid = count ? (lo[c] + hi[c]) * 0.5f : 0.f;
            for (uint32_t k = first; k < count; ++k) {
                float& s = scope[c][(pos + k) & (kLen - 1)];
                s = (s - mid) * kScopeGain;
            }
            const float level = count ? hi[c] - lo[c] : 0.f;
            const float d = vu[c] * decay;
            vu[c] = level > d ? level : d;
        }
        pos = (pos + count) & (kLen - 1);
        count = 0;
    }

    float vuOf(int c) const { return c >= 0 && c < 3 ? vu[c] * kVuGain : 0.f; }
    void scopeOf(int c, float* dst, int n) const {
        if (n <= 0) return;
        if (n > kLen) n = kLen;
        if (c < 0 || c >= 3) {
            for (int k = 0; k < n; ++k) dst[k] = 0;
            return;
        }
        const uint32_t from = (pos - (uint32_t)n) & (kLen - 1);
        for (int k = 0; k < n; ++k) dst[k] = scope[c][(from + k) & (kLen - 1)];
    }
};
