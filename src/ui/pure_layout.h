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
int hitTest(int x, int y);
// Pattern row under y, relative to the centre row (0 = centre); false outside the pattern area.
bool rowAt(int y, int& rel);
// Cell column under x inside a track (0 note, 1/2 instrument, 5 effect, 6/7 parameter), -1 outside.
int columnAt(int track, int x);
}  // namespace pure
