// WAV writing (16-bit PCM with TPDF dither, or 32-bit float) and reading (for tests/compare).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/result.h"

std::vector<uint8_t> writeWav(const float* interleavedStereo, size_t frames, uint32_t rate, bool float32);

struct WavData {
    uint32_t rate = 0;
    uint16_t channels = 0;
    std::vector<float> samples;  // interleaved
};
Result<WavData> readWav(const uint8_t* data, size_t size);
