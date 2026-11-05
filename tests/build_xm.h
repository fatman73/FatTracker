// Builds XM files in memory for tests.
#pragma once
#include <cstdint>
#include <vector>

#include "core/song.h"

struct XmSampleSpec {
    std::vector<int16_t> pcm;  // for 8-bit samples values must fit int8
    bool is16 = false;
    LoopType loop = LoopType::None;
    uint32_t loopStart = 0, loopLen = 0;  // in samples
    uint8_t vol = 64;
    int8_t finetune = 0;
    int8_t relNote = 0;
    uint8_t pan = 128;
};

struct XmInstSpec {
    std::vector<XmSampleSpec> samples;
    uint8_t noteMap[96] = {};
    Envelope vol, pan;
    uint16_t fadeout = 0;
};

std::vector<uint8_t> buildXm(int channels, const std::vector<uint8_t>& order, const std::vector<Pattern>& pats,
                             const std::vector<XmInstSpec>& insts, bool linear, uint8_t speed, uint8_t bpm);
