// Jochen Hippel's formats and the ones derived from them: Future Composer 1.0-1.3 (SMOD) and 1.4 (FC14),
// Hippel TFMX / TFMX 7V, COSO and MCMD. Like TFMX they drive the Paula voices from their own sequences
// (frequency and volume macros), so they get their own player: playback only.
// The decoder is from libtfmxaudiodecoder by Michael Schwendt (GPL v2+, see core/hippel/README.txt).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/chip_player.h"

class HippelPlayer : public ChipPlayer {
public:
    HippelPlayer();
    ~HippelPlayer() override;
    HippelPlayer(const HippelPlayer&) = delete;
    HippelPlayer& operator=(const HippelPlayer&) = delete;

    static bool detect(const uint8_t* data, size_t size);
    bool load(const uint8_t* data, size_t size, int song = 0);  // one file holds song and samples

    void restart() override;
    void setRate(uint32_t hz) override;
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override;
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;
    double seconds() const override;
    const char* format() const override;
    bool buildDisplay(Song& out) override;  // empty patterns, one column per voice (meters and scopes move)
    int tick() const override { return 0; }
    int subsongs() const override;
    int subsong() const override;
    bool selectSubsong(int i) override;  // loads the file again (the decoder measures the song while loading)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
