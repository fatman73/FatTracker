// Colors and theme parameters (no Direct2D dependency).
#pragma once
#include <cstdint>

struct Color {
    float r, g, b, a;
};

constexpr Color rgb(uint32_t hex, float a = 1.f) {
    return {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a};
}
Color mixColor(Color a, Color b, float t);
Color withAlpha(Color c, float a);

struct Theme {
    Color background, panel, panelLight, panelDark, text, digits, label;
    Color patternBg, patternText, patternNote, patternSample, patternVol, patternFx, rowNumber;
    Color rowBar, rowBarText, cursor, vuLow, vuMid, vuHigh, scopeBg, scopeLine, status;
    float bevelGradient;  // 0..1
    float glow;           // 0..1
    float corner;         // logical px
};

const Theme& themeHybrid();
