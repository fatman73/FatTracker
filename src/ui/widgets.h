// Reusable ProTracker-style widgets (logical coordinates).
#pragma once
#include "ui/font.h"
#include "ui/gfx.h"

enum class Align { Left, Center, Right };

void drawPanel(Gfx& g, const Theme& t, RectF r);
void drawText(Gfx& g, Font& f, RectF r, const char* s, float size, Color c, Align a);
void drawButton(Gfx& g, Font& f, const Theme& t, RectF r, const char* label, bool pressed, float size = 20.f);
// label | value | up | down  (arrow buttons are drawn, inactive in stage 1)
void drawCounter(Gfx& g, Font& f, const Theme& t, RectF r, const char* label, const char* value);
void drawVu(Gfx& g, const Theme& t, RectF r, float level01, float alpha);
void drawScopeBg(Gfx& g, const Theme& t, RectF r);
void drawScopeLine(Gfx& g, const Theme& t, RectF r, const float* samples, int n);
