// ZX Spectrum AY tracker modules on the AY-3-8910 emulated by ayumi (MIT). Playback only; voices = the three
// AY channels, 50 Hz frames.
//
//  - Sound Tracker 1.x compiled / ST Song Compiler (.stc; Modland's .st13 files are the same format);
//  - Sound Tracker 1.1 source modules (Modland .st11: Ay_Emul's ZXAYST11 wrapper around the tracker's own file,
//    also accepted without the wrapper), converted to the compiled form when loaded;
//  - Pro Tracker 2.x (.pt2);
//  - ASC Sound Master 0.x / 1.x (.asc);
//  - SQ-Tracker compiled (.sqt).
//
// The replayers are C++ ports of Sergey Bulba's players from his AY Emulator by way of ayfly (GPL v2 or later),
// with the format checks of ZXTune (LGPL v3) as a model for the structural detection. See zx_trackers.cpp and
// zx_trackers_README.txt.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/chip_player.h"

class ZxTrackerPlayer : public ChipPlayer {
public:
    enum class Kind { None, Stc, St1, Pt2, Asc, Sqt };

    ZxTrackerPlayer();
    ~ZxTrackerPlayer() override;
    ZxTrackerPlayer(const ZxTrackerPlayer&) = delete;
    ZxTrackerPlayer& operator=(const ZxTrackerPlayer&) = delete;

    // These formats have no tags: a file counts only when its whole structure (header geometry, tables,
    // every position's patterns) checks out. None when it is not one of them.
    static Kind identify(const uint8_t* data, size_t size);
    static bool detect(const uint8_t* data, size_t size) { return identify(data, size) != Kind::None; }
    bool load(const uint8_t* data, size_t size);  // keeps its own copy of the data

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
    Kind kind() const;
    const char* title() const;
    const char* author() const;
    int frames() const;  // one pass of the song, in 50 Hz frames

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
