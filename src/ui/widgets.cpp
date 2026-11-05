#include "ui/widgets.h"

void drawPanel(Gfx& g, const Theme& t, RectF r) { g.bevel(r, t, false); }

void drawText(Gfx& g, Font& f, RectF r, const char* s, float size, Color c, Align a) {
    float w = f.advance(size) * (float)lstrlenA(s);
    float x = r.x + 8.f;
    if (a == Align::Center) x = r.x + (r.w - w) * 0.5f;
    if (a == Align::Right) x = r.x + r.w - w - 8.f;
    f.drawText(g, x, r.y + (r.h - size) * 0.5f, size, s, c);
}

void drawButton(Gfx& g, Font& f, const Theme& t, RectF r, const char* label, bool pressed, float size) {
    g.bevel(r, t, pressed);
    drawText(g, f, r, label, size, t.label, Align::Center);
}

void drawCounter(Gfx& g, Font& f, const Theme& t, RectF r, const char* label, const char* value) {
    const float arrowW = r.h * 1.1f;
    const float valueW = 150.f;
    const float labelW = r.w - valueW - 2 * arrowW;
    RectF lr = {r.x, r.y, labelW, r.h};
    g.bevel(lr, t, false);
    drawText(g, f, lr, label, 18.f, t.label, Align::Left);
    RectF vr = {r.x + labelW, r.y, valueW, r.h};
    g.bevel(vr, t, true);
    drawText(g, f, vr, value, 20.f, t.digits, Align::Right);
    drawButton(g, f, t, {vr.x + valueW, r.y, arrowW, r.h}, "\x01", false, 18.f);
    drawButton(g, f, t, {vr.x + valueW + arrowW, r.y, arrowW, r.h}, "\x02", false, 18.f);
}

void drawVu(Gfx& g, const Theme& t, RectF r, float level, float alpha) {
    if (level <= 0.001f) return;
    if (level > 1.f) level = 1.f;
    const float bottom = r.y + r.h;
    struct Seg {
        float from, to;
        Color lo, hi;
    } segs[3] = {{0.f, 0.6f, t.vuLow, mixColor(t.vuLow, t.vuMid, 0.5f)},
                 {0.6f, 0.85f, mixColor(t.vuLow, t.vuMid, 0.5f), t.vuMid},
                 {0.85f, 1.f, t.vuMid, t.vuHigh}};
    for (const Seg& s : segs) {
        if (level <= s.from) break;
        float to = level < s.to ? level : s.to;
        float y0 = bottom - to * r.h, y1 = bottom - s.from * r.h;
        float k = (to - s.from) / (s.to - s.from);
        g.fillGradientV({r.x, y0, r.w, y1 - y0}, withAlpha(mixColor(s.lo, s.hi, k), alpha), withAlpha(s.lo, alpha));
    }
}

void drawScopeBg(Gfx& g, const Theme& t, RectF r) {
    g.bevel({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, t, true);
    g.fillRect(r, t.scopeBg);
    g.line(r.x, r.y + r.h * 0.5f, r.x + r.w, r.y + r.h * 0.5f, withAlpha(t.scopeLine, 0.18f), 1.f);
}

void drawScopeLine(Gfx& g, const Theme& t, RectF r, const float* s, int n) {
    if (n < 2) return;
    float pts[2 * 512];
    if (n > 512) n = 512;
    const float cy = r.y + r.h * 0.5f, amp = r.h * 0.46f;
    for (int i = 0; i < n; ++i) {
        float v = s[i] * 1.6f;
        v = v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
        pts[i * 2] = r.x + r.w * (float)i / (float)(n - 1);
        pts[i * 2 + 1] = cy - v * amp;
    }
    g.polyline(pts, n, t.scopeLine, 2.f);
}
