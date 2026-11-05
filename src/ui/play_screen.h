// Stage-1 main screen: PT 2.3F-style top panel, scopes, info bar and the pattern view.
#pragma once
#include <vector>

#include "audio/engine.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/pattern_view.h"

class PlayScreen {
public:
    void draw(Gfx& g, Font& f, const Theme& t, Engine& e);
    void setStatus(const char* msg);
    // Return true when the screen needs a redraw.
    bool onKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, Engine& e);
    bool onMouse(float lx, float ly, bool right, Engine& e);
    bool animating() const { return animating_; }
    void songChanged();
    void setViewMode(int m);

private:
    PatternCtx ctx(const Song* s, const Telemetry& tel) const;
    void toggleMute(int ch, Engine& e);
    void solo(int ch, Engine& e);

    int viewMode_ = 8;
    int firstChannel_ = 0;
    int viewRow_ = 0;
    int viewPattern_ = 0;
    char status_[64] = "ALL RIGHT";
    bool animating_ = false;
    Telemetry last_{};
    PatternView pattern_;
    float scopeBuf_[256];
};
