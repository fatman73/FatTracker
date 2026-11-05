// Main screen: ProTracker-style layout (counters, buttons, scopes, info bar, pattern) that
// stretches to fill the whole window, with a modern finish.
#pragma once
#include "audio/engine.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/pattern_view.h"
#include "ui/text.h"

enum class UiAction { None, Redraw, OpenFile };

class PlayScreen {
public:
    void draw(Gfx& g, Text& tx, Font& f, const Theme& t, Engine& e);
    void setStatus(const char* msg);
    void clearStatusIf(const char* prefix);  // resets to ALL RIGHT if the status starts with prefix
    UiAction onKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat, Engine& e);
    UiAction onMouse(float lx, float ly, bool right, Engine& e);
    bool onMouseMove(float lx, float ly);  // true when the hovered element changed
    bool animating() const { return animating_; }
    void songChanged();
    void setViewMode(int m);

private:
    struct Layout {
        float W, H;
        RectF counters, buttons, logo, scopes, scopeGrid, clock, info, pattern, side;
    };
    Layout layout(const Viewport& vp) const;
    RectF counterRow(int i) const;
    RectF buttonRect(int i) const;
    RectF viewButton(int i) const;
    int hitId(float x, float y) const;
    PatternCtx ctx(const Song* s, const Telemetry& tel) const;
    void toggleMute(int ch, Engine& e);
    void solo(int ch, Engine& e);
    void counterStep(int row, int dir, bool big, Engine& e);

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
};
