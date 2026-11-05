// Reusable ProTracker-inspired widgets with a modern finish (logical coordinates).
#pragma once
#include "ui/gfx.h"
#include "ui/text.h"

struct Ui {
    Gfx& g;
    Text& text;
    const Theme& t;
};

void drawButton(Ui& u, RectF r, const char* label, Gfx::PanelState s, float size = 19.f);
// label | value field | up | down. `editable` = arrows active (dimmed otherwise).
void drawCounter(Ui& u, RectF r, const char* label, const char* value, bool editable, int hoverArrow);
RectF counterArrow(RectF r, int which);  // 0 = up, 1 = down
void drawVu(Gfx& g, const Theme& t, RectF r, float level01, float alpha);
void drawScopeBg(Gfx& g, const Theme& t, RectF r);
void drawScopeLine(Gfx& g, const Theme& t, RectF r, const float* samples, int n);
