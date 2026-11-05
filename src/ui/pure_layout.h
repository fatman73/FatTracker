// Geometry of the Pure mode screen: a ProTracker style Amiga lowres screen, 320x256 pixels.
// Pure C++ so the hit testing is unit tested.
#pragma once

namespace pure {
struct Box {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

constexpr int kRowH = 11;          // top area rows
constexpr int kCounterRows = 9;    // POS .. REPLEN
constexpr int kPatternY = 138;     // pattern area top
constexpr int kPatternRowH = 7;    // pitch of the normal pattern rows
constexpr int kRowsAbove = 7;      // rows shown above the centre bar
constexpr int kBarY = kPatternY + kRowsAbove * kPatternRowH;  // centre row bar
constexpr int kBarH = 14;           // the playing row: taller, bigger font
constexpr int kTrackX0 = 32, kTrackPitch = 72, kTrackW = 68;
constexpr int kRowNumX = 4, kRowNumW = 24;

enum Id : int {
    kNone = -1,
    kArrow = 0,        // + row * 2 + (0 up, 1 down), rows 0..8
    kPosIns = 20,      // the I / D boxes next to POS
    kPosDel = 21,
    kButton = 30,      // + 0..11, see buttonLabel()
    kSide = 50,        // + 0..4 ("1".."4", "A")
    kScope = 60,       // + 0..3
    kLoad = 70,
    kTempoUp = 71,
    kTempoDown = 72,
    kTrack = 80,       // + 0..3 (pattern area columns)
    kRowNumbers = 90,
    kExit = 91,        // EXIT PURE button in the line under the scopes
    kSongName = 92,    // click: edit the song name
    kSampleName = 93,  // click: edit the sample name
    kOpTitle = 100,    // EDIT OP. / POS ED. title (EDIT OP.: next screen)
    kOpExit = 101,
    kOpItem = 110,     // + item index of the EDIT OP. screen
    kPosLine = 160,    // + 0..kPosLines-1
    kPosUp = 170,
    kPosDown = 171,
    kSmpWave = 180,
    kSmpButton = 200,  // + sampler operation (the normal sampler's order)
};

// What covers the screen: EDIT OP. and POS ED. sit over the quadrascope, SAMPLER over the pattern area.
enum View { kViewMain, kViewEditOp, kViewPosEd, kViewSampler };
constexpr int kOpScreens = 3, kPosLines = 6, kSamplerButtons = 30;

// EDIT OP. items. kind: button, value display, up / down arrow of a value, embossed caption
enum OpKind { kOpButton, kOpValue, kOpUp, kOpDown, kOpCaption };
enum OpAction {
    kOpNone, kOpTrack, kOpPattern, kOpSong, kOpBlock, kOpTrM12, kOpTrM1, kOpTrP1, kOpTrP12, kOpOnlySample,
    kOpExchange, kOpReplace, kOpSwap, kOpSkip, kOpOctave, kOpQuant, kOpMetro, kOpPatLen, kOpNewPat, kOpClonePat,
    kOpSample = 100,  // + sampler operation
};
struct OpItem {
    Box box;
    const char* label;
    int action;
    int kind;
};

Box counterLabel(int row);
Box counterValue(int row);
Box counterArrow(int row, int dir);  // dir 0 up, 1 down
Box posButton(int i);                // 0 = I, 1 = D
Box gridButton(int i);               // 3 columns x 4 rows
Box sideButton(int i);               // narrow column right of the grid, 0..4
Box scopeTitle();
Box scope(int i);                    // black area of scope i
Box creditLine();                    // machine name + power / drive LEDs
Box exitButton();
Box songRow();
Box sampleRow();
Box loadButton();
Box infoArea();
Box tempoArrow(int dir);             // 0 up, 1 down
Box track(int i);                    // whole column of track i in the pattern area
Box rowNumbers();
const char* buttonLabel(int i);
// x of right aligned value text (8 px per character) keeping 1 px clear of the box bevel.
int valueX(const Box& b, int chars);
int hitTest(int x, int y, View view = kViewMain, int opScreen = 0);
Box opArea();
Box opTitle();
Box opExit();
const char* opScreenTitle(int screen);
int opCount(int screen);
OpItem opItem(int screen, int i);
Box posLine(int i);    // POS ED. list lines, the current position on line kPosLines / 2 - 1
Box posScroll(int dir);  // 0 up, 1 down
Box samplerWave();
Box samplerButton(int i);
const char* samplerLabel(int i);
// Pattern row under y, relative to the centre row (0 = centre); false outside the pattern area.
bool rowAt(int y, int& rel);
// Cell column under x inside a track (0 note, 1/2 instrument, 5 effect, 6/7 parameter), -1 outside.
int columnAt(int track, int x);
}  // namespace pure
