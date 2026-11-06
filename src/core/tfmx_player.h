// TFMX playback (Chris Huelsbeck's "The Final Musicsystem eXtended", e.g. Turrican II).
// TFMX is not pattern/sample based like a tracker module: macros drive the Paula voices directly,
// so it gets its own player. The decoder is adapted from libtfmxaudiodecoder (GPL, see core/tfmx);
// this class supplies the emulated Paula voices and renders them at the output rate.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/chip_player.h"

struct Song;

class TfmxPlayer : public ChipPlayer {
public:
    TfmxPlayer();
    ~TfmxPlayer() override;
    TfmxPlayer(const TfmxPlayer&) = delete;
    TfmxPlayer& operator=(const TfmxPlayer&) = delete;

    // mdat = song data ("TFMX-SONG ..."), smpl = sample data file. false if not playable.
    static bool detect(const uint8_t* mdat, size_t size);
    bool load(const uint8_t* mdat, size_t mdatSize, const uint8_t* smpl, size_t smplSize, int song = 0);
    void restart() override;
    void setRate(uint32_t hz) override;
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;

    int voices() const override;
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;
    int songs() const;
    int step() const;        // current track step, counted from the first one of the song
    int steps() const;       // number of track steps of the song
    double seconds() const override;
    const char* format() const override;
    // TFMX tracks unrolled into tracker patterns for the screen (1 tick = 1 row, 64 rows a pattern,
    // one column per used track): note, macro as instrument, note volume in the volume column.
    bool buildDisplay(Song& out) override;
    int tick() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
