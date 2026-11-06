// Console and 8-bit computer music through Game_Music_Emu (LGPL, see core/gme/README.txt):
// SNES SPC, Sega Mega Drive / Master System VGM (and GYM), Atari 8-bit SAP (POKEY), ZX Spectrum / CPC AY
// (with Z80), NES NSF / NSFE, Game Boy GBS, PC Engine HES, MSX KSS. Playback only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/chip_player.h"

class GmePlayer : public ChipPlayer {
public:
    GmePlayer();
    ~GmePlayer() override;
    GmePlayer(const GmePlayer&) = delete;
    GmePlayer& operator=(const GmePlayer&) = delete;

    static bool detect(const uint8_t* data, size_t size);
    bool load(const uint8_t* data, size_t size, int track = 0);

    void restart() override;
    void setRate(uint32_t hz) override;
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override;
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;
    double seconds() const override;
    const char* format() const override;
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }
    const char* title() const;
    int tracks() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
