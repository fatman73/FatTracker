#include "ui/theme.h"

Color mixColor(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

namespace {
// Order of the fields below = Theme member order.
const Theme kPresets[] = {
    // 0 "Hybryda": ProTracker 2.3F palette (grey panels, blue digits, BLUE pattern text), modern finish
    {rgb(0x8A929E), rgb(0xB9C0CB), rgb(0xF3F6FA), rgb(0x5B6372), rgb(0x1B1F27), rgb(0x1D4ED8), rgb(0x252A33),
     rgb(0x0A0E15), rgb(0x4F7DF0), rgb(0xEEF3FB), rgb(0x7FB0FF), rgb(0x6EE0A0), rgb(0xFFC56B), rgb(0x7D879A),
     rgb(0xDCE6F5), rgb(0x143FB8), rgb(0xFF5A5A), rgb(0x2FE06A), rgb(0xF5E14A), rgb(0xFF4A3D), rgb(0x323945),
     rgb(0x46FF72), rgb(0x1B1F27), 1.0f, 0.55f, 7.f},
    // 1 Classic ProTracker 2.3: flat grey bevels, magenta pattern text, no glow
    {rgb(0x9DA3AC), rgb(0xBBBBBB), rgb(0xFFFFFF), rgb(0x666666), rgb(0x000000), rgb(0x0000BB), rgb(0x222222),
     rgb(0x000000), rgb(0xBB66CC), rgb(0xFFFFFF), rgb(0x8888FF), rgb(0x66DD88), rgb(0xFFDD66), rgb(0x888888),
     rgb(0xDDDDDD), rgb(0x0000BB), rgb(0xFF3333), rgb(0x22CC44), rgb(0xEEDD22), rgb(0xEE2222), rgb(0x000000),
     rgb(0x33FF55), rgb(0x000000), 0.f, 0.f, 0.f},
    // 2 ProTracker 3.61 blue
    {rgb(0x1E4F72), rgb(0x2F7FA8), rgb(0x8FD3F0), rgb(0x123850), rgb(0xE8F6FF), rgb(0xFFFFFF), rgb(0xE8F6FF),
     rgb(0x082238), rgb(0x7BE36B), rgb(0xF2FFE0), rgb(0xFFE35A), rgb(0x9BE7FF), rgb(0xFFE35A), rgb(0x8FC6E0),
     rgb(0x2D86B8), rgb(0xFFFFFF), rgb(0xFF6B6B), rgb(0x3CE06B), rgb(0xF0E040), rgb(0xFF4A3D), rgb(0x061A2B),
     rgb(0xFFE35A), rgb(0xFFFFFF), 0.7f, 0.5f, 5.f},
    // 3 Neo-Amiga: dark graphite, colourful syntax, strong glow
    {rgb(0x14171C), rgb(0x2A313C), rgb(0x46505F), rgb(0x0B0E12), rgb(0xDCE3EE), rgb(0x59A6FF), rgb(0xC9D3E3),
     rgb(0x07090C), rgb(0x5B7FB8), rgb(0xE6EDF7), rgb(0x59A6FF), rgb(0x38E8A0), rgb(0xFFB86B), rgb(0x5F6B80),
     rgb(0x1F3A66), rgb(0xFFFFFF), rgb(0xFF4D6D), rgb(0x38E8A0), rgb(0xFFD166), rgb(0xFF4D6D), rgb(0x0B0E12),
     rgb(0x38E8A0), rgb(0xDCE3EE), 0.8f, 0.85f, 9.f},
};
const char* const kNames[] = {"HYBRYDA", "CLASSIC PT 2.3", "PT 3.61 BLUE", "NEO-AMIGA", "CUSTOM"};
constexpr int kCount = (int)(sizeof kPresets / sizeof kPresets[0]);
int g_current = 0;
Theme g_custom = kPresets[0];
const char* const kColorNames[kThemeColors] = {
    "BACKGROUND", "PANEL",   "PANEL LIGHT", "PANEL DARK", "TEXT",     "DIGITS",      "LABELS",  "PATTERN BG",
    "PATTERN",    "NOTES",   "SAMPLES",     "VOLUME",     "EFFECTS",  "ROW NUMBERS", "ROW BAR", "ROW BAR TEXT",
    "CURSOR",     "VU LOW",  "VU MID",      "VU HIGH",    "SCOPE BG", "SCOPE LINE",  "STATUS"};
}  // namespace

const Theme& themeHybrid() { return kPresets[0]; }
int themeCount() { return kCount; }
const char* themeName(int i) { return i >= 0 && i <= kCount ? kNames[i] : ""; }
const Theme& themePreset(int i) { return i >= 0 && i < kCount ? kPresets[i] : g_custom; }
const Theme& currentTheme() { return g_current >= kCount ? g_custom : kPresets[g_current]; }
int currentThemeIndex() { return g_current; }
void setCurrentTheme(int i) { g_current = i < 0 ? 0 : (i > kCount ? kCount : i); }
Theme& customTheme() { return g_custom; }
const char* themeColorName(int i) { return i >= 0 && i < kThemeColors ? kColorNames[i] : ""; }
Color& themeColor(Theme& t, int i) { return (&t.background)[i < 0 ? 0 : (i >= kThemeColors ? kThemeColors - 1 : i)]; }
