#include "ui/pattern_layout.h"

namespace {
constexpr float kCellGlyphs = 11.75f;  // 10 glyphs + 3 gaps of 0.25 + 1 glyph margin
constexpr float kRowNumGlyphs = 3.5f;
constexpr float kDenseGlyphs = 11.25f;  // full cell with a half-glyph margin (32-channel view)
constexpr float kMaxGlyph = 13.f;  // max character advance (logical units)
const char kHex[] = "0123456789ABCDEF";
}  // namespace

PatternLayout computePatternLayout(int viewMode, int numChannels, int firstChannel, float areaW) {
    PatternLayout l{};
    if (numChannels < 1) numChannels = 1;
    // the view always shows `viewMode` columns; columns past the song's channels stay empty
    l.visible = viewMode < 1 ? 1 : viewMode;
    // the 32-channel view uses a tighter margin so the font can stay a little larger
    const float cellGlyphs = viewMode >= 32 ? kDenseGlyphs : kCellGlyphs;
    float g = areaW / (l.visible * cellGlyphs + kRowNumGlyphs);
    l.glyph = g > kMaxGlyph ? kMaxGlyph : g;
    l.rowNumW = kRowNumGlyphs * l.glyph;
    // columns share the full width; the cell text is centered inside its column
    l.chanW = (areaW - l.rowNumW) / l.visible;
    l.gap = 0.25f * l.glyph;
    int maxFirst = numChannels - l.visible;
    if (maxFirst < 0) maxFirst = 0;
    l.firstChannel = firstChannel < 0 ? 0 : (firstChannel > maxFirst ? maxFirst : firstChannel);
    return l;
}

void formatCell(const Cell& c, bool ptOctaves, char out[11]) {
    static const char* const names = "C-C#D-D#E-F-F#G-G#A-A#B-";
    if (c.note == NOTE_NONE) {
        out[0] = out[1] = out[2] = '-';
    } else if (c.note == NOTE_OFF) {
        out[0] = out[1] = out[2] = '=';
    } else {
        int n = c.note - 1;
        int oct = n / 12 - (ptOctaves ? 2 : 0);
        out[0] = names[(n % 12) * 2];
        out[1] = names[(n % 12) * 2 + 1];
        out[2] = (char)('0' + (oct < 0 ? 0 : (oct > 9 ? 9 : oct)));
    }
    if (c.instr) {
        out[3] = kHex[c.instr >> 4];
        out[4] = kHex[c.instr & 15];
    } else {
        out[3] = out[4] = '.';
    }
    const uint8_t v = c.vol;
    if (v >= 0x10 && v <= 0x50) {
        out[5] = kHex[(v - 0x10) >> 4];
        out[6] = kHex[(v - 0x10) & 15];
    } else if (v >= 0x60) {
        out[5] = "-+DUSVPLRM"[(v >> 4) - 6];
        out[6] = kHex[v & 15];
    } else {
        out[5] = out[6] = '.';
    }
    if (c.fx == 0 && c.param == 0) {
        out[7] = out[8] = out[9] = '.';
    } else {
        out[7] = c.fx < 10 ? (char)('0' + c.fx) : (char)('A' + c.fx - 10);
        out[8] = kHex[c.param >> 4];
        out[9] = kHex[c.param & 15];
    }
    out[10] = 0;
}
