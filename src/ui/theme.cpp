#include "ui/theme.h"

Color mixColor(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

// "Hybryda": ProTracker 2.3F palette (light grey panels, blue digits, BLUE pattern text),
// rendered with soft light and glow.
const Theme& themeHybrid() {
    static const Theme t = {
        rgb(0x2B2F36),  // background
        rgb(0xB8BCC4),  // panel
        rgb(0xEEF1F5),  // panelLight
        rgb(0x5A606B),  // panelDark
        rgb(0x1A1C20),  // text
        rgb(0x1F4FD1),  // digits
        rgb(0x2A2D33),  // label
        rgb(0x04060A),  // patternBg
        rgb(0x3F6FE8),  // patternText
        rgb(0xF2F5FA),  // patternNote
        rgb(0x6FA0FF),  // patternSample
        rgb(0x5FD38A),  // patternVol
        rgb(0xFFC15E),  // patternFx
        rgb(0x8A93A6),  // rowNumber
        rgb(0xDCE3EE),  // rowBar
        rgb(0x1F4FD1),  // rowBarText
        rgb(0xFF5A5A),  // cursor
        rgb(0x2FE06A),  // vuLow
        rgb(0xF5E14A),  // vuMid
        rgb(0xFF4A3D),  // vuHigh
        rgb(0x000000),  // scopeBg
        rgb(0x46FF72),  // scopeLine
        rgb(0x1A1C20),  // status
        0.35f,          // bevelGradient
        0.5f,           // glow
        3.f,            // corner
    };
    return t;
}
