#include "ui/theme.h"

Color mixColor(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

// "Hybryda": the ProTracker 2.3F palette (light grey panels, blue digits, BLUE pattern text)
// rendered as a modern UI: rounded soft-gradient panels, sunken fields, smooth fonts, glow.
const Theme& themeHybrid() {
    static const Theme t = {
        rgb(0x1B1F26),  // background
        rgb(0xB9C0CB),  // panel
        rgb(0xF3F6FA),  // panelLight
        rgb(0x5B6372),  // panelDark
        rgb(0x1B1F27),  // text
        rgb(0x1D4ED8),  // digits
        rgb(0x252A33),  // label
        rgb(0x0A0E15),  // patternBg
        rgb(0x4F7DF0),  // patternText
        rgb(0xEEF3FB),  // patternNote
        rgb(0x7FB0FF),  // patternSample
        rgb(0x6EE0A0),  // patternVol
        rgb(0xFFC56B),  // patternFx
        rgb(0x7D879A),  // rowNumber
        rgb(0xDCE6F5),  // rowBar
        rgb(0x143FB8),  // rowBarText
        rgb(0xFF5A5A),  // cursor
        rgb(0x2FE06A),  // vuLow
        rgb(0xF5E14A),  // vuMid
        rgb(0xFF4A3D),  // vuHigh
        rgb(0x05080D),  // scopeBg
        rgb(0x46FF72),  // scopeLine
        rgb(0x1B1F27),  // status
        1.0f,           // bevelGradient
        0.55f,          // glow
        7.f,            // corner
    };
    return t;
}
