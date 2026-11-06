// Amiga "synth" formats whose replay routines drive the four Paula channels directly (JamCracker,
// SidMon, Delta Music, SoundMon, ...). Each format ports its replayer from NostalgicPlayer
// (Thomas Neumann, MIT licence) onto AmigaChannel, which keeps NostalgicPlayer's IChannel calls
// (PlaySample / SetLoop / SetAmigaPeriod / SetAmigaVolume / Mute) so the ports stay close to the source.
// Playback only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/chip_player.h"

// One Paula channel as NostalgicPlayer's IChannel sees it. The sample memory stays owned by the format
// (wave buffers may be rewritten while they play, like on the Amiga).
struct AmigaChannel {
    void playSample(const int8_t* data, uint32_t start, uint32_t length);  // starts at once, no loop yet
    // Queued: replaces the sample (looping over itself) when the current buffer ends, or starts at once
    // when the channel is silent (NostalgicPlayer's SetSample).
    void setSample(const int8_t* data, uint32_t start, uint32_t length);
    void setLoop(uint32_t start, uint32_t length);                           // repeat part (same data)
    void setAmigaPeriod(uint32_t period) { period_ = period; }
    void setAmigaVolume(uint32_t volume) { volume_ = (volume > 64 ? 64 : volume) * 4; }  // 0..64
    void setVolume(uint32_t volume) { volume_ = volume > 256 ? 256 : volume; }            // 0..256 (IChannel.SetVolume)
    void mute() { active_ = false; }
    bool active() const { return active_; }
    uint32_t volume() const { return volume_ / 4; }  // 0..64
    float next(double rate);  // one output sample, volume applied

private:
    const int8_t* data_ = nullptr;
    const int8_t* nextData_ = nullptr;
    uint32_t end_ = 0, loopStart_ = 0, loopEnd_ = 0, nextStart_ = 0, nextLen_ = 0;
    double pos_ = 0;
    bool active_ = false, loop_ = false, pending_ = false;
    uint32_t period_ = 0, volume_ = 0;  // volume in 1/256
};

// Cuts [start, start + length) to a buffer of `size` bytes, as format code must before handing a range to
// AmigaChannel (NostalgicPlayer throws on ranges outside the array). False when nothing of it is left.
inline bool clipRange(size_t size, uint32_t& start, uint32_t& length) {
    if (start >= size || length == 0) return false;
    if (length > size - start) length = (uint32_t)(size - start);
    return true;
}

class AmigaSynthPlayer : public ChipPlayer {
public:
    // The format behind these bytes (one of the ported replayers), or null.
    static std::unique_ptr<AmigaSynthPlayer> open(const uint8_t* data, size_t size);
    static bool detect(const uint8_t* data, size_t size);

    void restart() override;
    void setRate(uint32_t hz) override { rate_ = hz ? hz : 48000; }
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override { return 4; }
    float vu(int v) const override { return v >= 0 && v < 4 ? vu_[v] : 0.f; }
    void scope(int v, float* dst, int n) const override;
    double seconds() const override { return seconds_; }
    const char* format() const override { return format_.c_str(); }
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }

protected:
    AmigaChannel ch_[4];
    std::string format_;
    double tickHz_ = 50.0;  // replay calls per second (VBlank)
    bool endReached_ = false;  // set by playTick when the song wrapped around

    virtual void initSound() = 0;  // song start state
    virtual void playTick() = 0;   // one replay call
    void measure();                // dry run: song length in seconds (stops at the first wrap)

private:
    uint32_t rate_ = 48000;
    double tickLeft_ = 0, seconds_ = 0;
    float vu_[4] = {};
    float scope_[4][kScopeLen] = {};
    uint32_t scopePos_ = 0;
};
