// Amiga "synth" formats whose replay routines drive the four Paula channels directly (JamCracker,
// SidMon, Delta Music, SoundMon, ...). Each format ports its replayer from NostalgicPlayer
// (Thomas Neumann, MIT licence) onto AmigaChannel, which keeps NostalgicPlayer's IChannel calls
// (PlaySample / SetLoop / SetAmigaPeriod / SetAmigaVolume / Mute) so the ports stay close to the source.
// Playback only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
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

// Companion files of a song (IFF SMUS instruments and samples): fills `out` with the file called `name` (a plain
// file name, never a path) and returns true, or returns false when there is no such file.
using AmigaFileResolver = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;

// A name a song may ask for: not empty, at most 255 bytes, no control characters, no \ / : and no "..".
bool isPlainFileName(const std::string& name);
// Resolver over the files of one folder (a score's own folder): a plain name matches a regular file there,
// case-insensitively (an exact match first), else one in its "Instruments" subfolder (where NostalgicPlayer
// looks). Files over 16 MB are not read.
AmigaFileResolver folderFileResolver(const std::wstring& dir);

class AmigaSynthPlayer : public ChipPlayer {
public:
    // The format behind these bytes (one of the ported replayers), or null. Formats with companion files
    // (needsFiles) are not opened here.
    static std::unique_ptr<AmigaSynthPlayer> open(const uint8_t* data, size_t size);
    static bool detect(const uint8_t* data, size_t size);
    // Formats that need companion files (IFF SMUS); openWithFiles loads them through `files` (and opens the
    // others like open). On failure `error` (if given) says why, e.g. which instrument file is missing.
    static bool needsFiles(const uint8_t* data, size_t size);
    static std::unique_ptr<AmigaSynthPlayer> openWithFiles(const uint8_t* data, size_t size,
                                                           const AmigaFileResolver& files,
                                                           std::string* error = nullptr);

    void restart() override;
    void setRate(uint32_t hz) override { rate_ = hz ? hz : 48000; }
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override { return voices_; }
    float vu(int v) const override { return v >= 0 && v < voices_ ? vu_[v] : 0.f; }
    void scope(int v, float* dst, int n) const override;
    double seconds() const override { return seconds_; }
    const char* format() const override { return format_.c_str(); }
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }
    int subsongs() const override { return subsongs_; }
    int subsong() const override { return subsong_; }
    bool selectSubsong(int i) override;  // restarts on that sub-song and measures it

protected:
    // Paula channels 0-3; 7 voice formats (Digital Mugician 2) set voices_ = 7 and voices 3..6 are mixed
    // into channel 3, each at a quarter (as HippelPlayer does).
    AmigaChannel ch_[kMaxVoices];
    int voices_ = 4;
    // false: voices past the fourth are mixed into channel 3 at a quarter each (Digital Mugician 2);
    // true: every voice is a channel of its own, panned like Paula channel v % 4 (IFF SMUS tracks)
    bool ownChannels_ = false;
    std::string format_;
    double tickHz_ = 50.0;  // replay calls per second (VBlank); playTick may change it (CIA tempo)
    bool endReached_ = false;  // set by playTick when the song wrapped around

    int subsongs_ = 1, subsong_ = 0;  // formats with sub-songs set subsongs_; initSound plays subsong_

    virtual void initSound() = 0;  // song start state (of sub-song subsong_)
    virtual void playTick() = 0;   // one replay call
    // true: voice v plays on the left (Paula channels 0 and 3; Art Of Noise 8 voices pans its own way)
    virtual bool leftSide(int v) const {
        const int pc = ownChannels_ ? (v & 3) : v < 4 ? v : 3;
        return pc == 0 || pc == 3;
    }
    // dry run: song length in seconds (stops at the first wrap, or after maxSeconds); returns the ticks run
    long measure(double maxSeconds = 30 * 60);
    void selectLongestSubsong();   // starts on the longest sub-song (the first of equally long ones)

private:
    uint32_t rate_ = 48000;
    double tickLeft_ = 0, seconds_ = 0;
    float vu_[kMaxVoices] = {};
    float scope_[kMaxVoices][kScopeLen] = {};
    uint32_t scopePos_ = 0;
};
