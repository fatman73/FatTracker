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

// ---- sample editor tools (the selection [a, b) or the whole sample when a == b)
double sampleRateHz(const Sample& s);         // playback rate at C-4 (from relative note + finetune)
void sampleSetRate(Sample& s, double hz);     // relative note + finetune for a C-4 rate
void sampleResample(Sample& s, double factor);  // length x factor (linear), pitch and loop kept
double sampleDetectPitch(const Sample& s, uint32_t a, uint32_t b);  // cycles per sample, 0 = none
// C-4 plays the fundamental found in [a, b) (whole sample when a == b) as C (261.63 Hz)
bool sampleTuneToC(Sample& s, uint32_t a = 0, uint32_t b = 0);
// ProTracker songs ignore the relative note: resample the data so it plays its rate at 8363 Hz
void sampleBakeRate(Sample& s);
void sampleMix(Sample& s, uint32_t at, const Sample& clip);  // adds clip at `at`, grows the sample
void sampleLowPass(Sample& s, uint32_t a, uint32_t b);
void sampleHighPass(Sample& s, uint32_t a, uint32_t b);
void sampleEcho(Sample& s, uint32_t a, uint32_t b);   // echoes of [a, b): 1/8 s, 40 %, ringing to the end
void sampleBoost(Sample& s, uint32_t a, uint32_t b);  // +6 dB with a soft knee
void sampleDraw(Sample& s, uint32_t x0, int v0, uint32_t x1, int v1);  // pencil line, sample units
Sample sampleFromRecording(const float* mono, size_t frames, uint32_t rate);  // 16-bit, tuned to the rate
