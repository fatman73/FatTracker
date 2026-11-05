// Period and frequency tables (ProTracker 2.3 / FastTracker 2).
#pragma once
#include <cstdint>

// [finetune index 0..15 (0..7, then -8..-1)][PT note C-1..B-3]
extern const uint16_t kPtPeriods[16][36];
extern const uint8_t kVibratoSine[32];

constexpr double kPalClock = 3546895.0;
double ptPeriodToHz(uint32_t period);
int ptFinetuneIndex(int8_t sampleFinetune);  // Sample::finetune (MOD*16) -> 0..15
int ptNoteToXm(int ptIndex);                 // 0..35 -> XM note 37..72
int xmNoteToPt(int xmNote);                  // XM note -> 0..35 or -1 if out of PT range

// note0: 0 = C-0 (relative note already added), finetune -128..127
uint32_t xmLinearPeriod(int note0, int finetune);
double xmLinearHz(uint32_t period);
uint32_t xmAmigaPeriod(int note0, int finetune);  // units: PT period * 4 (C-4 = 1712)
double xmAmigaHz(uint32_t period);
