// Software mixer: 32 channel voices + 2 preview voices, float, stereo.
#pragma once
#include <cstdint>

#include "core/song.h"

enum class Interp : uint8_t { None, Linear, Cubic };

class Mixer {
public:
    static constexpr int kMaxVoices = 34;  // 32 channels + 2 preview (32, 33)
    static constexpr int kScopeLen = 1024;

    explicit Mixer(uint32_t sampleRate = 48000);
    void setRate(uint32_t hz);
    uint32_t rate() const { return rate_; }
    void setInterp(Interp i) { interp_ = i; }
    void setRamp(bool on) { ramp_ = on; }
    void setMasterGain(float g) { master_ = g; }
    void setNumChannels(int n);

    void trigger(int v, const Sample* s, uint32_t offset);
    void stopVoice(int v);
    void reset();  // drops every voice and ghost at once (call before freeing samples)
    void setFrequency(int v, double hz);
    void setVolume(int v, float vol01, float pan01);  // pan 0 = left, 1 = right
    void mix(float* outStereo, uint32_t frames);      // overwrites the buffer

    bool active(int v) const;
    double position(int v) const;  // actual sample position (loop-mapped)
    float vu(int v) const;
    void scope(int v, float* dst, int n) const;

private:
    struct Voice {
        const Sample* s = nullptr;
        bool active = false;
        bool stopping = false;
        double pos = 0;  // virtual position (monotonic inside loops, see mapIndex)
        double step = 0;
        float vol = 0, pan = 0.5f;
        float curL = 0, curR = 0, tgtL = 0, tgtR = 0, dL = 0, dR = 0;
        int rampLeft = 0;
        // sanitized loop
        LoopType loop = LoopType::None;
        int64_t loopStart = 0, loopLen = 0, period = 0;
    };
    template <class T>
    void mixVoice(Voice& v, int idx, const T* data, float norm, float* out, uint32_t frames, float& peak);
    static int64_t mapIndex(const Voice& v, int64_t vi);
    void setTargets(Voice& v);
    void startRamp(Voice& v);

    uint32_t rate_;
    int rampLen_;
    Interp interp_ = Interp::Cubic;
    bool ramp_ = true;
    float master_ = 1.f;
    float autoScale_ = 1.f;
    Voice voices_[kMaxVoices];
    Voice ghosts_[kMaxVoices];  // fading-out copies of retriggered voices
    float vu_[kMaxVoices] = {};
    float scope_[kMaxVoices][kScopeLen] = {};
    uint32_t scopePos_[kMaxVoices] = {};
};
