// Amiga 3.5" DD floppy drive sounds for Pure mode: motor, head steps, disk insert.
// Sounds are synthesized unless the user supplies samples. Timing follows a DD drive:
// 300 rpm, 11 264 bytes per cylinder, 3 ms per head step.
#pragma once
#include <cstdint>
#include <vector>

#include "core/song.h"

class Floppy {
public:
    Floppy();
    void setRate(uint32_t hz);
    // User sounds (copied); nullptr keeps the built-in synthesis for that sound.
    void setSounds(const Sample* motor, const Sample* step, const Sample* insert);

    double read(uint32_t bytes);   // queues a file read; returns seconds until the data is in
    double write(uint32_t bytes);  // queues a file write
    double dir();                  // queues a directory read
    void insert();                 // disk inserted: clunk + recalibration
    double busySeconds() const;    // until the queued accesses finish (motor run-down excluded)
    bool audible() const;
    void mix(float* stereo, uint32_t frames, float gain);  // adds to the buffer

private:
    struct Shot {
        const std::vector<float>* snd;
        uint64_t at;
    };
    double access(uint32_t bytes, bool file);
    uint64_t begin();               // start time of a new access (spins the motor up)
    void seek(uint64_t& t, int cyl);
    void rebuild();                 // (re)creates the sound buffers at the current rate
    uint64_t sec(double s) const { return (uint64_t)(s * rate_ + 0.5); }

    uint32_t rate_ = 48000;
    uint64_t now_ = 0, busyUntil_ = 0, motorOffAt_ = 0;
    bool motorOn_ = false;
    int cyl_ = 0;
    float motorLevel_ = 0;
    size_t motorPos_ = 0;
    std::vector<float> motor_, step_, insert_;
    Sample user_[3];
    bool hasUser_[3] = {};
    std::vector<Shot> shots_;  // scheduled and playing one-shot sounds
};
