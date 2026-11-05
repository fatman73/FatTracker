// Offline rendering of a whole song (fttest render, WAV export).
#pragma once
#include <cstddef>
#include <vector>

#include "core/mixer.h"
#include "core/song.h"

// Plays from order 0 until the song loops or ends, or maxSeconds elapse. Returns frames.
size_t renderSong(const Song& s, uint32_t rate, Interp interp, bool ramps, float maxSeconds,
                  std::vector<float>& outStereo);

// PAT2SMP: one pass through `pattern` as a mono 16-bit sample at `rate` (pitch fields left at 0).
// An empty sample when the pattern does not exist. At most 60 seconds.
Sample renderPatternSample(const Song& s, int pattern, uint32_t rate);

// Pearson correlation of two interleaved buffers over the shorter length.
double correlation(const float* a, size_t na, const float* b, size_t nb);
