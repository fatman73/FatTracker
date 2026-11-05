// Builds MOD / Soundtracker files in memory for tests.
#pragma once
#include <cstdint>
#include <vector>

struct ModCell {
    int ptNoteIdx;  // -1 none, 0..35 (C-1..B-3, finetune-0 periods)
    int sample;
    int fx;
    int param;
};

struct ModSample {
    int lenWords;
    int finetune;  // -8..7
    int volume;
    int repStartWords;
    int repLenWords;
    std::vector<int8_t> data;
};

// patterns: [pattern][row*channels + c], 64 rows each
std::vector<uint8_t> buildMod(const char sig[4], int channels, const std::vector<uint8_t>& order,
                              const std::vector<std::vector<ModCell>>& patterns,
                              const std::vector<ModSample>& samples);
// 15-sample Soundtracker (4 channels, no signature)
std::vector<uint8_t> buildSoundtracker(const std::vector<uint8_t>& order,
                                       const std::vector<std::vector<ModCell>>& patterns,
                                       const std::vector<ModSample>& samples);
