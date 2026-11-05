// TFMX playback (Chris Huelsbeck's "The Final Musicsystem eXtended", e.g. Turrican II).
// TFMX is not pattern/sample based like a tracker module: macros drive the Paula voices directly,
// so it gets its own player. The decoder is adapted from libtfmxaudiodecoder (GPL, see core/tfmx);
// this class supplies the emulated Paula voices and renders them at the output rate.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

struct Song;

class TfmxPlayer {
public:
    static constexpr int kMaxVoices = 8;   // 4 Paula channels, 8 in TFMX 7V
    static constexpr int kScopeLen = 1024;

    TfmxPlayer();
    ~TfmxPlayer();
    TfmxPlayer(const TfmxPlayer&) = delete;
    TfmxPlayer& operator=(const TfmxPlayer&) = delete;

    // mdat = song data ("TFMX-SONG ..."), smpl = sample data file. false if not playable.
    static bool detect(const uint8_t* mdat, size_t size);
    bool load(const uint8_t* mdat, size_t mdatSize, const uint8_t* smpl, size_t smplSize, int song = 0);
    void restart();                                     // from the start of the song
    void setRate(uint32_t hz);
    void clearMeters();  // VU and scopes to zero (playback stopped)
    void render(float* stereo, uint32_t frames, float gain);  // overwrites the buffer

    int voices() const;
    float vu(int v) const;
    void scope(int v, float* dst, int n) const;
    int songs() const;
    int step() const;        // current track step, counted from the first one of the song
    int steps() const;       // number of track steps of the song
    double seconds() const;  // song length (one pass) found by the decoder
    const char* format() const;
    // TFMX tracks unrolled into tracker patterns for the screen (1 tick = 1 row, 64 rows a pattern,
    // one column per used track): note, macro as instrument, note volume in the volume column.
    bool buildDisplay(Song& out);
    int tick() const;  // display row being played (pattern = tick / 64)
    static constexpr int kDisplayRows = 64;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
