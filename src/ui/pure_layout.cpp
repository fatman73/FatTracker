#include "ui/pure_layout.h"

namespace pure {
namespace {
constexpr int kLabelW = 63, kValueX = 63, kValueW = 35, kArrowX = 98, kArrowW = 11;
constexpr int kGridX = 120, kGridW = 62, kSideX = 306;
const char* const kLabels[12] = {"PLAY",   "STOP",     "MOD2WAV", "PATTERN", "CLEAR",    "PAT2SMP",
                                 "EDIT",   "EDIT OP.", "POS ED.", "RECORD",  "DISK OP.", "SAMPLER"};
}  // namespace

// LENGTH, REPEAT and REPLEN show five hex digits: their value boxes are wider, the labels narrower.
Box counterLabel(int row) {
    if (row == 0) return {0, 0, 42, kRowH};
    return {0, row * kRowH, row >= 6 ? kLabelW - 8 : kLabelW, kRowH};
}
Box counterValue(int row) { return row >= 6 ? Box{kValueX - 8, row * kRowH, kValueW + 8, kRowH} : Box{kValueX, row * kRowH, kValueW, kRowH}; }
Box counterArrow(int row, int dir) { return {kArrowX + dir * kArrowW, row * kRowH, kArrowW, kRowH}; }
Box posButton(int i) { return {42 + i * 10, 0, i ? 11 : 10, kRowH}; }
Box gridButton(int i) { return {kGridX + (i % 3) * kGridW, (i / 3) * kRowH, kGridW, kRowH}; }
Box sideButton(int i) { return {kSideX, i * kRowH, 14, kRowH}; }
Box scopeTitle() { return {kGridX, 4 * kRowH, kSideX - kGridX, kRowH}; }
Box scope(int i) { return {kGridX + i * 47 + 8, 5 * kRowH, 39, 3 * kRowH}; }
Box creditLine() { return {kGridX, 8 * kRowH, 124, kRowH}; }
Box exitButton() { return {kGridX + 124, 8 * kRowH, 320 - kGridX - 124, kRowH}; }
Box songRow() { return {0, 9 * kRowH, 320, kRowH}; }
Box sampleRow() { return {0, 10 * kRowH, 284, kRowH}; }
Box loadButton() { return {284, 10 * kRowH, 36, kRowH}; }
Box infoArea() { return {0, 11 * kRowH, 320, kPatternY - 11 * kRowH}; }
Box tempoArrow(int dir) { return {dir ? 78 : 30, 130, 9, 8}; }
Box track(int i) { return {kTrackX0 + i * kTrackPitch, kPatternY, kTrackW, 256 - kPatternY}; }
Box rowNumbers() { return {kRowNumX, kPatternY, kRowNumW, 256 - kPatternY}; }
const char* buttonLabel(int i) { return i >= 0 && i < 12 ? kLabels[i] : ""; }
// glyphs use 6 of their 8 columns: the text ends 2 px before x + chars * 8
int valueX(const Box& b, int chars) { return b.x + b.w - chars * 8 - 1; }

int hitTest(int x, int y) {
    for (int r = 0; r < kCounterRows; ++r)
        for (int d = 0; d < 2; ++d)
            if (counterArrow(r, d).contains(x, y)) return kArrow + r * 2 + d;
    for (int i = 0; i < 2; ++i)
        if (posButton(i).contains(x, y)) return kPosIns + i;
    for (int i = 0; i < 12; ++i)
        if (gridButton(i).contains(x, y)) return kButton + i;
    for (int i = 0; i < 5; ++i)
        if (sideButton(i).contains(x, y)) return kSide + i;
    for (int i = 0; i < 4; ++i)
        if (scope(i).contains(x, y)) return kScope + i;
    if (loadButton().contains(x, y)) return kLoad;
    if (exitButton().contains(x, y)) return kExit;
    if (exitButton().contains(x, y)) return kExit;
    for (int d = 0; d < 2; ++d)
        if (tempoArrow(d).contains(x, y)) return d ? kTempoDown : kTempoUp;
    for (int i = 0; i < 4; ++i)
        if (track(i).contains(x, y)) return kTrack + i;
    if (rowNumbers().contains(x, y)) return kRowNumbers;
    return kNone;
}

bool rowAt(int y, int& rel) {
    if (y < kPatternY || y >= 256) return false;
    if (y < kBarY) rel = -((kBarY - 1 - y) / kPatternRowH + 1);
    else if (y < kBarY + kBarH) rel = 0;
    else rel = (y - kBarY - kBarH) / kPatternRowH + 1;
    return true;
}

int columnAt(int t, int x) {
    const int c = (x - (kTrackX0 + t * kTrackPitch + 2)) / 8;  // 8 characters "C#201A0F"
    if (x < kTrackX0 + t * kTrackPitch || c < 0 || c > 7) return -1;
    static const int kMap[8] = {0, 0, 0, 1, 2, 5, 6, 7};
    return kMap[c];
}
}  // namespace pure
