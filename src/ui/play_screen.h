// Main screen: ProTracker-style layout (counters, buttons, scopes, info bar, pattern) that
// stretches to fill the whole window, with a modern finish, plus the pattern editor.
#pragma once
#include <functional>

#include "audio/engine.h"
#include "core/edit.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/pattern_view.h"
#include "ui/text.h"

enum class UiAction { None, Redraw, OpenFile, SaveFile, Quit };

// Modal question with up to four answers (drawn over the pattern area).
struct Dialog {
    bool open = false;
    char text[96] = {};
    const char* buttons[4] = {};
    int count = 0;
    std::function<void(int)> onAnswer;  // index of the clicked button
};

class PlayScreen {
public:
    void draw(Gfx& g, Text& tx, Font& f, const Theme& t, Engine& e);
    void setStatus(const char* msg);
    void clearStatusIf(const char* prefix);  // resets to ALL RIGHT if the status starts with prefix
    UiAction onKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat, Engine& e);
    UiAction onKeyUp(unsigned vk, Engine& e);
    UiAction onMouse(float lx, float ly, bool right, Engine& e);
    bool onMouseMove(float lx, float ly);  // true when the hovered element changed
    bool animating() const { return animating_; }
    void songChanged();
    void setViewMode(int m);
    // editing state shared with the app shell
    Editor& editor() { return editor_; }
    bool dirty() const { return editor_.dirty(); }
    void ask(const char* text, std::initializer_list<const char*> buttons, std::function<void(int)> onAnswer);

private:
    struct Layout {
        float W, H;
        RectF counters, buttons, logo, scopes, scopeGrid, clock, info, pattern, side;
    };
    Layout layout(const Viewport& vp) const;
    RectF counterRow(int i) const;
    RectF buttonRect(int i) const;
    RectF viewButton(int i) const;
    RectF sideButton(int i) const;
    RectF sideArrow(int i) const;  // 0/1 pattern -/+, 2/3 bpm -/+
    RectF dialogButton(int i) const;
    int hitId(float x, float y) const;
    PatternCtx ctx(const Song* s, const Telemetry& tel) const;
    void toggleMute(int ch, Engine& e);
    void solo(int ch, Engine& e);
    void counterStep(int row, int dir, bool big, Engine& e);
    void drawDialog(Gfx& g, Text& tx, const Theme& t);
    // play_edit.cpp
    UiAction editKey(unsigned vk, bool shift, bool ctrl, bool alt, bool repeat, Engine& e);
    void sideAction(int i, Engine& e);
    void moveCursor(int rows, int cols, Engine& e);
    void ensureCursorVisible(const Song& s);
    int editRow() const;      // row the editor writes to (playing row while recording)
    int editPattern() const;
    Block currentBlock(bool wholeTrackIfNone) const;
    void advance(Engine& e);  // after an entry: move down by the edit skip

    Layout L_{};
    int viewMode_ = 8;
    int firstChannel_ = 0;
    int viewRow_ = 0;
    int viewPattern_ = 0;
    int viewOrder_ = 0;
    int sample_ = 0;
    int hover_ = -1;
    char status_[64] = "DROP A MODULE HERE OR CLICK DISK OP.";
    bool animating_ = false;
    Telemetry last_{};
    PatternView pattern_;
    Font* font_ = nullptr;
    float scopeBuf_[256];
    // editor
    Editor editor_;
    Clip clip_;
    bool editMode_ = false;
    bool recordMode_ = false;
    bool scopePattern_ = false;  // side panel operations act on the whole pattern (else track)
    int curCh_ = 0, curCol_ = 0;
    int octave_ = 4;
    int editSkip_ = 1;
    bool blockOn_ = false;
    int blockRow_ = 0, blockCh_ = 0;  // anchor of the selection
    unsigned jamKey_ = 0;
    Dialog dialog_;

    friend class EditOpScreen;
};
