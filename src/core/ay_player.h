// AY-3-8910 / YM2149 register dumps: YM (Atari ST, Amstrad CPC; YM2/YM3/YM3b/YM5/YM6, usually LHA packed),
// VTX (ZX Spectrum / CPC, LH5 packed) and PSG (raw register stream). The chip is emulated by ayumi (MIT).
// Playback only. Voices = the three AY channels.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/chip_player.h"

// LHA -lh5- decompression (also used inside VTX files). false on broken data.
bool lh5Decode(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);
// Unpacks an LHA archive's first member (-lh0- stored or -lh5-); false if this is not LHA.
bool lhaUnpack(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

class AyPlayer : public ChipPlayer {
public:
    AyPlayer();
    ~AyPlayer() override;
    AyPlayer(const AyPlayer&) = delete;
    AyPlayer& operator=(const AyPlayer&) = delete;

    static bool detect(const uint8_t* data, size_t size);
    bool load(const uint8_t* data, size_t size);

    void restart() override;
    void setRate(uint32_t hz) override;
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override { return 3; }
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;
    double seconds() const override;
    const char* format() const override;
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }
    const char* title() const;
    int frames() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
