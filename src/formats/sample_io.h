// Loading and saving single samples: WAV (8/16/24-bit PCM, 32-bit float; stereo is mixed
// to mono; 'smpl' loop chunk), IFF 8SVX and AIFF / AIFF-C (8..32-bit PCM, 'sowt', 'fl32';
// INST sustain loop).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/result.h"
#include "core/song.h"

Result<Sample> loadSampleFile(const uint8_t* data, size_t size);
std::vector<uint8_t> saveSampleWav(const Sample& s);    // keeps 8/16 bit, writes the loop
std::vector<uint8_t> saveSample8svx(const Sample& s);   // 8-bit (16-bit is converted)
