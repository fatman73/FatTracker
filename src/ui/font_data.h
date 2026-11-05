// FatTracker's own 8x8 glyph set in the spirit of ProTracker (bold, blocky).
// Row bitmasks, bit 7 = leftmost column. Codes 1..6 are UI arrows:
// 1 up, 2 down, 3 left, 4 right, 5 filled up, 6 filled down.
#pragma once
#include <cstdint>

extern const uint8_t kGlyphs[128][8];
