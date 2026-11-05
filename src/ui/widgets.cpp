#include "ui/widgets.h"

namespace {
constexpr float kArrowW = 38.f;
constexpr float kValueW = 130.f;
}  // namespace

void drawButton(Ui& u, RectF r, const char* label, Gfx::PanelState s, float size) {
    u.g.panel(r, u.t, s);
    Color c = u.t.label;
    if (s == Gfx::PanelState::Active) {  // readable on the active fill whatever the theme
        const Color fill = mixColor(u.t.digits, u.t.panelLight, 0.4f);
        const float lum = 0.3f * fill.r + 0.59f * fill.g + 0.11f * fill.b;
        c = lum > 0.6f ? Color{0.08f, 0.1f, 0.14f, 1.f} : Color{1.f, 1.f, 1.f, 1.f};
    }
    u.text.draw(u.g, r, label, size, c, Align::Center);
}

RectF counterArrow(RectF r, int which) {
    return {r.x + r.w - (2 - which) * kArrowW, r.y + 2, kArrowW - 3, r.h - 4};
}

void drawCounter(Ui& u, RectF r, const char* label, const char* value, bool editable, int hoverArrow) {
    const float labelW = r.w - kValueW - 2 * kArrowW - 6;
    u.text.draw(u.g, {r.x, r.y, labelW, r.h}, label, 16.f, u.t.label, Align::Left);
    RectF vr = {r.x + labelW, r.y + 3, kValueW, r.h - 6};
    u.g.well(vr, u.t, mixColor(u.t.panelLight, u.t.panel, 0.25f));
    u.text.draw(u.g, vr, value, 18.f, u.t.digits, Align::Right, FontRole::Mono);
    for (int i = 0; i < 2; ++i) {
        RectF ar = counterArrow(r, i);
        Gfx::PanelState st = hoverArrow == i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal;
        u.g.panel(ar, u.t, st);
        u.text.draw(u.g, ar, i == 0 ? "\x05" : "\x06", 12.f, editable ? u.t.label : withAlpha(u.t.label, 0.3f),
                    Align::Center);
    }
}

// ProTracker style VU bar: flat, its gradient spans the bar itself - red at its top,
// then orange and yellow, green at the bottom.
void drawVu(Gfx& g, const Theme& t, RectF r, float level, float alpha) {
    if (level <= 0.001f) return;
    if (level > 1.f) level = 1.f;
    const float bottom = r.y + r.h;
    const float h = level * r.h;
    const RectF lit = {r.x, bottom - h, r.w, h};
    const Color red = t.vuHigh, orange = mixColor(t.vuHigh, t.vuMid, 0.55f), yellow = t.vuMid,
                green = t.vuLow, deep = mixColor(t.vuLow, {0, 0, 0, 1}, 0.35f);
    struct Seg {
        float from, to;  // fraction of the bar, measured from its top
        Color a, b;
    } segs[4] = {{0.f, 0.12f, red, orange}, {0.12f, 0.38f, orange, yellow}, {0.38f, 0.62f, yellow, green},
                 {0.62f, 1.f, green, deep}};
    // flat bar: no rounding, no shading
    g.pushClip(lit);
    for (const Seg& s : segs) {
        const float y0 = lit.y + s.from * h, y1 = lit.y + s.to * h;
        g.fillRoundGradientV({r.x, y0, r.w, y1 - y0 + 0.5f}, 0.f, withAlpha(s.a, alpha), withAlpha(s.b, alpha));
    }
    g.popClip();
}

void drawScopeBg(Gfx& g, const Theme& t, RectF r) {
    g.well(r, t, t.scopeBg);
    g.line(r.x + 6, r.y + r.h * 0.5f, r.x + r.w - 6, r.y + r.h * 0.5f, withAlpha(t.scopeLine, 0.15f), 1.f);
}

void drawScopeLine(Gfx& g, const Theme& t, RectF r, const float* s, int n) {
    if (n < 2) return;
    float pts[2 * 512];
    if (n > 512) n = 512;
    const float cy = r.y + r.h * 0.5f, amp = r.h * 0.42f;
    const float x0 = r.x + 6, w = r.w - 12;
    for (int i = 0; i < n; ++i) {
        float v = s[i] * 1.6f;
        v = v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
        pts[i * 2] = x0 + w * (float)i / (float)(n - 1);
        pts[i * 2 + 1] = cy - v * amp;
    }
    g.polyline(pts, n, t.scopeLine, 2.f);
}
