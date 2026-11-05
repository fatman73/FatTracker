#include "ui/widgets.h"

namespace {
constexpr float kArrowW = 38.f;
constexpr float kValueW = 130.f;
}  // namespace

void drawButton(Ui& u, RectF r, const char* label, Gfx::PanelState s, float size) {
    u.g.panel(r, u.t, s);
    Color c = s == Gfx::PanelState::Active ? u.t.panelLight : u.t.label;
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
    // fixed gradients per segment (cached by the renderer), clipped to the level
    g.pushClip({r.x, bottom - level * r.h, r.w, level * r.h});
    for (const Seg& s : segs) {
        float y0 = bottom - s.to * r.h, y1 = bottom - s.from * r.h;
        g.fillRoundGradientV({r.x, y0, r.w, y1 - y0}, 0.f, withAlpha(s.hi, alpha), withAlpha(s.lo, alpha));
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
