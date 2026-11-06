// ZX Spectrum Pro Tracker 3.x / Vortex Tracker II modules (.pt3) on the AY-3-8910 emulated by ayumi (MIT).
// Playback only. Voices = the three AY channels; 50 Hz frames.
//
// The player is a C++ port of PT3Play by Sergey Bulba (svbulba@gmail.com, https://bulba.untergrund.net/),
// by way of its C# port in PT3PlayBlazor by Ben Baker (https://github.com/benbaker76/PT3PlayBlazor,
// MIT licence; it contains modified code from ayfly, https://github.com/l29ah/ayfly). See pt3_player.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/chip_player.h"

class Pt3Player : public ChipPlayer {
public:
    Pt3Player();
    ~Pt3Player() override;
    Pt3Player(const Pt3Player&) = delete;
    Pt3Player& operator=(const Pt3Player&) = delete;

    // "ProTracker 3." or "Vortex Tracker II" at the start and room for the header
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
    const char* author() const;
    int frames() const;  // one pass of the song, in 50 Hz frames

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
