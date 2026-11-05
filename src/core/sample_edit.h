// Sample editing operations on ranges [a, b) (clamped and ordered automatically).
#pragma once
#include <cstdint>

#include "core/song.h"

Sample sampleCopy(const Sample& s, uint32_t a, uint32_t b);
Sample sampleCut(Sample& s, uint32_t a, uint32_t b);           // returns the removed part
void samplePaste(Sample& s, uint32_t at, const Sample& clip);  // converts clip to s's bit depth
void sampleCrop(Sample& s, uint32_t a, uint32_t b);
void sampleSilence(Sample& s, uint32_t a, uint32_t b);
void sampleVolume(Sample& s, uint32_t a, uint32_t b, float startPercent, float endPercent);
void sampleReverse(Sample& s, uint32_t a, uint32_t b);
void sampleNormalize(Sample& s, uint32_t a, uint32_t b);
void sampleTo16(Sample& s);
void sampleTo8(Sample& s);
void sampleFixLoop(Sample& s);  // clamps the loop to the sample
