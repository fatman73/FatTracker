// Offline rendering of a whole song (fttest render, WAV export).
#pragma once
#include <cstddef>
#include <vector>

#include "core/mixer.h"
#include "core/song.h"

// Plays from order 0 until the song loops or ends, or maxSeconds elapse. Returns frames.
size_t renderSong(const Song& s, uint32_t rate, Interp interp, bool ramps, float maxSeconds,
                  std::vector<float>& outStereo);

// Pearson correlation of two interleaved buffers over the shorter length.
double correlation(const float* a, size_t na, const float* b, size_t nb);
