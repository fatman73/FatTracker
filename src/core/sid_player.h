// Commodore 64 SID tunes (PSID / RSID files, e.g. from the HVSC): the 6502 player routine of the tune
// runs on an emulated C64 (cRSID by Hermit, see core/crsid/README.txt). Playback only.
// Voices are the SID voices (3 per chip, up to 3 chips; at most kMaxVoices are metered).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/chip_player.h"

class SidPlayer : public ChipPlayer {
public:
    SidPlayer();
    ~SidPlayer() override;
    SidPlayer(const SidPlayer&) = delete;
    SidPlayer& operator=(const SidPlayer&) = delete;

    static bool detect(const uint8_t* data, size_t size);
    bool load(const uint8_t* data, size_t size, int subtune = 0);  // 0 = the tune's default subtune

    void restart() override;
    void setRate(uint32_t hz) override;  // reloads the tune when it changes (the emulation is built per rate)
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override;
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;  // the mixed output (SID voices are not separable)
    double seconds() const override { return 0; }      // SID files carry no length
    const char* format() const override;
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }
    const char* title() const;  // from the SID header
    int subtunes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
