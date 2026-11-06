// Players for formats that drive the Paula voices themselves instead of being pattern/sample modules
// (TFMX, Future Composer, Jochen Hippel, ...): playback only. The engine talks to all of them through
// this interface; the screen shows a display song built by buildDisplay().
#pragma once
#include <cstdint>

struct Song;

class ChipPlayer {
public:
    static constexpr int kMaxVoices = 8;      // 4 Paula channels, more for 7 voice formats
    static constexpr int kScopeLen = 1024;
    static constexpr int kDisplayRows = 64;   // rows of a display pattern (1 tick = 1 row)

    virtual ~ChipPlayer() = default;
    virtual void restart() = 0;  // from the start of the song
    virtual void setRate(uint32_t hz) = 0;
    virtual void clearMeters() = 0;  // VU and scopes to zero (playback stopped)
    virtual void render(float* stereo, uint32_t frames, float gain) = 0;  // overwrites the buffer
    virtual int voices() const = 0;
    virtual float vu(int v) const = 0;
    virtual void scope(int v, float* dst, int n) const = 0;
    virtual double seconds() const = 0;  // song length (one pass)
    virtual const char* format() const = 0;
    virtual bool buildDisplay(Song& out) = 0;  // what the pattern view shows
    virtual int tick() const = 0;              // display row being played (pattern = tick / kDisplayRows)

    // Sub-songs (counted from 0). Selecting one restarts it and measures seconds() again where the player
    // measures lengths; an index out of range is refused and changes nothing. Formats without sub-songs
    // have one.
    virtual int subsongs() const { return 1; }
    virtual int subsong() const { return 0; }
    virtual bool selectSubsong(int i) { return i == 0; }
    virtual const char* subsongTitle() const { return ""; }  // the current sub-song's own name ("" = none)
};
