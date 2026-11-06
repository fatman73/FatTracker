#include "ui/pure_layout.h"

#include "ui/pixel_canvas.h"

namespace pure {
namespace {
constexpr int kLabelW = 63, kValueX = 63, kValueW = 35, kArrowX = 98, kArrowW = 11;
constexpr int kGridX = 120, kGridW = 62, kSideX = 306;
const char* const kLabels[12] = {"PLAY",   "STOP",     "MOD2WAV", "PATTERN", "CLEAR",    "PAT2SMP",
                                 "EDIT",   "EDIT OP.", "POS ED.", "RECORD",  "DISK OP.", "SAMPLER"};
}  // namespace

// LENGTH, REPEAT and REPLEN show five hex digits: their value boxes are wider, the labels narrower.
// FINETUNE shows at most two characters ("-8"): its label takes 6 px of the value box to keep its margins.
constexpr int kFineW = 6;
Box counterLabel(int row) {
    if (row == 0) return {0, 0, 37, kRowH};  // room for the I / D boxes
    return {0, row * kRowH, row >= 6 ? kLabelW - 8 : row == 3 ? kLabelW + kFineW : kLabelW, kRowH};
}
Box counterValue(int row) {
    if (row >= 6) return {kValueX - 8, row * kRowH, kValueW + 8, kRowH};
    if (row == 3) return {kValueX + kFineW, row * kRowH, kValueW - kFineW, kRowH};
    return {kValueX, row * kRowH, kValueW, kRowH};
}
const char* counterText(int row) {
    static const char* const kText[kCounterRows] = {"POS", "PATTERN", "LENGTH", "FINETUNE", "SAMPLE",
                                                    "VOLUME", "LENGTH", "REPEAT", "REPLEN"};
    return row >= 0 && row < kCounterRows ? kText[row] : "";
}
Box counterArrow(int row, int dir) { return {kArrowX + dir * kArrowW, row * kRowH, kArrowW, kRowH}; }
Box posButton(int i) { return {37 + i * 13, 0, 13, kRowH}; }  // 13 px: 3 px of face around the letter
Box gridButton(int i) { return {kGridX + (i % 3) * kGridW, (i / 3) * kRowH, kGridW, kRowH}; }
Box sideButton(int i) { return {kSideX, i * kRowH, 14, kRowH}; }
Box scopeTitle() { return {kGridX, 4 * kRowH, kSideX - kGridX, kRowH}; }
Box scope(int i) { return {kGridX + i * 47 + 8, 5 * kRowH, 39, 3 * kRowH}; }
Box creditLine() { return {kGridX, 8 * kRowH, 124, kRowH}; }
const char* machineName(int model) { return model == 2 ? "A1200" : "A500"; }
int machineNameX() { return kGridX + 4; }
// "A1200" ends 39 px after machineNameX(): the LEDs start 4 px after it, then DF0, then the "?" box
Box machineLed(int i) { return {kGridX + 47 + i * 18, 8 * kRowH + 3, 14, 5}; }
int machineDriveX() { return kGridX + 83; }
Box creditHint() { return {kGridX + 124 - 13, 8 * kRowH, 13, kRowH}; }
Box exitButton() { return {kGridX + 124, 8 * kRowH, 320 - kGridX - 124, kRowH}; }
Box songRow() { return {0, 9 * kRowH, 320, kRowH}; }
Box sampleRow() { return {0, 10 * kRowH, 283, kRowH}; }  // 22 characters from x 106 keep 2 px off its edge
Box loadButton() { return {283, 10 * kRowH, 37, kRowH}; }
Box infoArea() { return {0, 11 * kRowH, 320, kPatternY - 11 * kRowH}; }
Box editSkipBox() { return {5, 11 * kRowH + 4, 22, kRowH}; }  // 3 px clear of the TEMPO up arrow
Box tempoArrow(int dir) { return {dir ? 78 : 30, 130, 9, 8}; }
Box track(int i) { return {kTrackX0 + i * kTrackPitch, kPatternY, kTrackW, 256 - kPatternY}; }
Box rowNumbers() { return {kRowNumX, kPatternY, kRowNumW, 256 - kPatternY}; }
Box opArea() { return {kGridX, 4 * kRowH, 320 - kGridX, 5 * kRowH}; }
Box opTitle() { return {kGridX, 4 * kRowH, kSideX - kGridX - 40, kRowH}; }  // "A" stays right of EXIT
Box opExit() { return {kSideX - 40, 4 * kRowH, 40, kRowH}; }
const char* opScreenTitle(int screen) {
    static const char* const kTitles[kOpScreens] = {"TRANSPOSE", "EDIT OPTIONS", "SAMPLE OPS"};
    return screen >= 0 && screen < kOpScreens ? kTitles[screen] : "";
}

const char* recModeLabel(bool step) { return step ? "REC STEP" : "REC LIVE"; }

namespace {
constexpr int kY1 = 5 * kRowH, kY2 = 6 * kRowH, kY3 = 7 * kRowH, kY4 = 8 * kRowH, kX = kGridX;
const OpItem kTranspose[] = {
    {{kX, kY1, 50, kRowH}, "TRACK", kOpTrack, kOpButton},       {{kX + 50, kY1, 50, kRowH}, "PATT", kOpPattern, kOpButton},
    {{kX + 100, kY1, 50, kRowH}, "SONG", kOpSong, kOpButton},   {{kX + 150, kY1, 50, kRowH}, "BLOCK", kOpBlock, kOpButton},
    {{kX, kY2, 50, kRowH}, "-12", kOpTrM12, kOpButton},         {{kX + 50, kY2, 50, kRowH}, "-1", kOpTrM1, kOpButton},
    {{kX + 100, kY2, 50, kRowH}, "+1", kOpTrP1, kOpButton},     {{kX + 150, kY2, 50, kRowH}, "+12", kOpTrP12, kOpButton},
    {{kX, kY3, 100, kRowH}, "ONLY SAMPLE", kOpOnlySample, kOpButton},
    {{kX + 100, kY3, 56, kRowH}, "EXCH", kOpExchange, kOpCaption}, {{kX + 156, kY3, 22, kRowH}, "", kOpExchange, kOpValue},
    {{kX + 178, kY3, 11, kRowH}, "", kOpExchange, kOpUp},       {{kX + 189, kY3, 11, kRowH}, "", kOpExchange, kOpDown},
    {{kX, kY4, 100, kRowH}, "REPLACE", kOpReplace, kOpButton},  {{kX + 100, kY4, 100, kRowH}, "SWAP", kOpSwap, kOpButton},
};
// value rows: caption, value (3 digits), up, down (100 px)
constexpr int kOptionRows = 5;
struct OptionRow {
    int x, y;
    const char* label;
    int action;
};
const OptionRow kOptionRow[kOptionRows] = {{kX, kY1, "SKIP", kOpSkip},   {kX + 100, kY1, "OCT", kOpOctave},
                                           {kX, kY2, "QUANT", kOpQuant}, {kX + 100, kY2, "METRO", kOpMetro},
                                           {kX, kY3, "ROWS", kOpPatLen}};
const OpItem kOptionButtons[] = {{{kX, kY4, 100, kRowH}, "NEW PAT", kOpNewPat, kOpButton},
                                 {{kX + 100, kY4, 100, kRowH}, "CLONE PAT", kOpClonePat, kOpButton},
                                 {{kX + 100, kY3, 100, kRowH}, "REC LIVE", kOpRecMode, kOpButton}};  // label drawn per mode
OpItem optionItem(int i) {
    if (i >= kOptionRows * 4) return kOptionButtons[i - kOptionRows * 4];
    const OptionRow& r = kOptionRow[i / 4];
    switch (i % 4) {
        case 0: return {{r.x, r.y, 48, kRowH}, r.label, r.action, kOpCaption};
        case 1: return {{r.x + 48, r.y, 30, kRowH}, "", r.action, kOpValue};
        case 2: return {{r.x + 78, r.y, 11, kRowH}, "", r.action, kOpUp};
    }
    return {{r.x + 89, r.y, 11, kRowH}, "", r.action, kOpDown};
}
// sampler operations in the normal sampler's order (sampler.cpp)
enum { S_PLAY, S_PLAYSEL, S_STOP, S_CUT, S_COPY, S_PASTE, S_CROP, S_SILENCE, S_REVERSE, S_NORMALIZE, S_FADEIN,
       S_FADEOUT, S_VOLDN, S_VOLUP, S_BITS, S_LOOP, S_SHOWALL, S_ZOOMSEL, S_UNDO, S_EXIT, S_RESAMPLE, S_TUNE, S_MIX,
       S_LOWPASS, S_HIGHPASS, S_ECHO, S_BOOST, S_DRAW, S_RECORD, S_NEW };
const int kSampleOps[16] = {S_RESAMPLE, S_TUNE, S_MIX, S_ECHO, S_BOOST, S_LOWPASS, S_HIGHPASS, S_REVERSE,
                            S_NORMALIZE, S_FADEIN, S_FADEOUT, S_SILENCE, S_CROP, S_VOLDN, S_VOLUP, S_BITS};
const char* const kSamplerLabels[kSamplerButtons] = {  // at most 5 characters: 3 px of face around them
    "PLAY", "PLSEL", "STOP", "CUT", "COPY", "PASTE", "CROP", "CLEAR", "BACK", "NORM",
    "FD.IN", "FDOUT", "VOL -", "VOL +", "8/16", "LOOP", "ALL", "ZOOM", "UNDO", "EXIT",
    "RESMP", "TUNE", "MIX", "LOWP", "HIGHP", "ECHO", "BOOST", "DRAW", "REC", "NEW"};
}  // namespace

int opCount(int screen) {
    switch (screen) {
        case 0: return (int)(sizeof kTranspose / sizeof kTranspose[0]);
        case 1: return kOptionRows * 4 + 3;
        case 2: return 16;
    }
    return 0;
}

OpItem opItem(int screen, int i) {
    if (i < 0 || i >= opCount(screen)) return {{0, 0, 0, 0}, "", kOpNone, kOpCaption};
    if (screen == 0) return kTranspose[i];
    if (screen == 1) return optionItem(i);
    return {{kX + (i % 4) * 50, kY1 + (i / 4) * kRowH, 50, kRowH}, kSamplerLabels[kSampleOps[i]], kOpSample + kSampleOps[i], kOpButton};
}

Box posLine(int i) { return {kGridX, 5 * kRowH + 1 + i * 7, 178, 7}; }
Box posScroll(int dir) { return {kGridX + 178, 5 * kRowH + dir * 22, 22, 22}; }
Box samplerWave() { return {2, kPatternY + 2, 316, 256 - kPatternY - 5 * kRowH - 4}; }  // inside its bevel
Box samplerButton(int i) {
    const int c = i % 6, r = i / 6;
    return {c * 320 / 6, 256 - 5 * kRowH + r * kRowH, (c + 1) * 320 / 6 - c * 320 / 6, kRowH};
}
const char* samplerLabel(int i) { return i >= 0 && i < kSamplerButtons ? kSamplerLabels[i] : ""; }

Box creditsTitle() { return {0, kPatternY, 258, kRowH}; }
Box creditsScroll(int dir) { return {258 + dir * 11, kPatternY, 11, kRowH}; }
Box creditsExit() { return {280, kPatternY, 40, kRowH}; }
Box creditsText() { return {2, kPatternY + kRowH + 2, 316, 256 - kPatternY - kRowH - 5}; }
int creditsRows() { return (creditsText().h - 2) / kCreditsPitch; }

const char* buttonLabel(int i) { return i >= 0 && i < 12 ? kLabels[i] : ""; }

int labelAdvance(char c) { return c == ' ' ? kLabelSpace : 8; }
namespace {
// first and one-past-last ink column of a caption drawn at x = 0, the shadow included
void labelInk(const char* s, int& l, int& r) {
    l = 1 << 20;
    r = 0;
    for (int x = 0; *s; x += labelAdvance(*s), ++s) {
        const uint8_t m = glyphColumns(*s, kSmall);
        for (int b = 0; b < 7; ++b)
            if (m & (0x40 >> b)) {
                l = x + b < l ? x + b : l;
                r = x + b + 2 > r ? x + b + 2 : r;
            }
    }
    if (r == 0) l = 0;
}
}  // namespace
int labelWidth(const char* s) {
    int l, r;
    labelInk(s, l, r);
    return r - l;
}
int labelX(const Box& b, const char* s) {
    int l, r;
    labelInk(s, l, r);
    return b.x + (b.w - (r - l)) / 2 - l;
}
void drawLabel(PixelCanvas& c, const Box& b, const char* s, uint32_t ink, uint32_t shadow) {
    const int y = b.y + (b.h - glyphRows(kSmall)) / 2;
    for (int x = labelX(b, s); *s; x += labelAdvance(*s), ++s) {
        const char one[2] = {*s, 0};
        c.textShadow(x, y, one, ink, shadow, kSmall);
    }
}
// glyphs use 6 of their 8 columns: the text ends 2 px before x + chars * 8
int valueX(const Box& b, int chars) { return b.x + b.w - chars * 8 - 1; }

int hitTest(int x, int y, View view, int opScreen) {
    if ((view == kViewEditOp || view == kViewPosEd) && opArea().contains(x, y) && !sideButton(4).contains(x, y)) {
        if (opTitle().contains(x, y)) return kOpTitle;
        if (opExit().contains(x, y)) return kOpExit;
        if (view == kViewEditOp) {
            for (int i = 0; i < opCount(opScreen); ++i)
                if (opItem(opScreen, i).box.contains(x, y)) return kOpItem + i;
        } else {
            for (int i = 0; i < kPosLines; ++i)
                if (posLine(i).contains(x, y)) return kPosLine + i;
            for (int d = 0; d < 2; ++d)
                if (posScroll(d).contains(x, y)) return d ? kPosDown : kPosUp;
        }
        return kNone;
    }
    if (view == kViewSampler && y >= kPatternY) {
        if (samplerWave().contains(x, y)) return kSmpWave;
        for (int i = 0; i < kSamplerButtons; ++i)
            if (samplerButton(i).contains(x, y)) return kSmpButton + i;
        return kNone;
    }
    if (view == kViewCredits && y >= kPatternY) {
        for (int d = 0; d < 2; ++d)
            if (creditsScroll(d).contains(x, y)) return d ? kCreditsDown : kCreditsUp;
        if (creditsExit().contains(x, y)) return kCreditsExit;
        return kCreditsText;
    }
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
    if (creditLine().contains(x, y)) return kCredits;
    if (songRow().contains(x, y)) return kSongName;
    if (sampleRow().contains(x, y)) return kSampleName;
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
