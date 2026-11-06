// Amiga AHX (Abyss' Highest eXperience, "THX" files) and HivelyTracker (.hvl) modules through the original
// HivelyTracker replayer by Pete Gordon (BSD 3-clause, see core/hvl/README.txt). Playback only.
// Voices are the module's channels (4 for AHX, up to 16 for HVL); all are mixed, the first kMaxVoices are
// metered and scoped.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/chip_player.h"

class HvlPlayer : public ChipPlayer {
public:
    HvlPlayer();
    ~HvlPlayer() override;
    HvlPlayer(const HvlPlayer&) = delete;
    HvlPlayer& operator=(const HvlPlayer&) = delete;

    // "THX" + version 0/1 or "HVL" + version 0/1, with all the structures the header announces inside the file
    static bool detect(const uint8_t* data, size_t size);
    bool load(const uint8_t* data, size_t size, int subsong = 0);  // keeps its own copy of the data

    void restart() override;
    void setRate(uint32_t hz) override;  // cheap (safe on the audio thread): the voices take the new rate
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override;  // the module's channels (0 when nothing is loaded)
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;  // each voice's own output
    double seconds() const override;  // until the song loops (capped dry run)
    const char* format() const override;
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }
    const char* title() const;
    int subsongs() const override;  // the main song and the module's sub-songs
    int subsong() const override;
    bool selectSubsong(int i) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
