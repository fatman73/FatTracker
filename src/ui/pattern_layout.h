// Pattern view geometry and cell text (no Direct2D dependency).
#pragma once
#include <string_view>

#include "core/song.h"

struct PatternLayout {
    int visible;
    float rowNumW, chanW, glyph, gap;
    int firstChannel;
};

PatternLayout computePatternLayout(int viewMode, int numChannels, int firstChannel, float areaW);
// 10 chars + NUL: note(3) instr(2) vol(2) fx(3).
void formatCell(const Cell& c, bool ptOctaves, char out[11]);
