#include "ui/pattern_view.h"

#include <cmath>
#include <cstdio>

#include "ui/widgets.h"

namespace {
constexpr float kInset = 8.f;  // inner padding of the pattern well
}

PatternView::Geo PatternView::geometry(Font& f, RectF area, const PatternCtx& c) const {
    Geo o{};
    const int nch = c.song ? c.song->numChannels : 4;
    o.l = computePatternLayout(c.viewMode, nch, c.firstChannel, area.w - 2 * kInset);
    o.em = o.l.glyph / (f.advance(1.f) > 0 ? f.advance(1.f) : 0.55f);
    o.contentW = o.l.rowNumW + o.l.visible * o.l.chanW;
    o.x0 = area.x + (area.w - o.contentW) * 0.5f;
    o.headerH = std::floor(o.em * 1.7f);
    if (o.headerH < 30.f) o.headerH = 30.f;
    o.rowH = std::floor(o.em * 1.32f);
    o.bodyTop = area.y + kInset + o.headerH + 4.f;
    o.bodyBottom = area.y + area.h - kInset;
    o.centerY = o.bodyTop + std::floor((o.bodyBottom - o.bodyTop - o.rowH) * 0.5f);
    o.rowsAbove = (int)std::ceil((o.centerY - o.bodyTop) / o.rowH);
    o.rowsBelow = (int)std::ceil((o.bodyBottom - (o.centerY + o.rowH)) / o.rowH);
    return o;
}

void PatternView::drawBack(Gfx& g, Text& tx, Font& f, const Theme& t, RectF area, const PatternCtx& c) {
    const Geo o = geometry(f, area, c);
    g.well(area, t, t.patternBg);
    const float hs = o.l.visible > 8 ? 13.f : 15.f;
    const float hy = area.y + kInset;
    tx.draw(g, {o.x0, hy, o.l.rowNumW, o.headerH}, "ROW", hs, t.rowNumber, Align::Center);
    for (int k = 0; k < o.l.visible; ++k) {
        const int ch = o.l.firstChannel + k;
        RectF hr = {o.x0 + o.l.rowNumW + k * o.l.chanW + 3, hy, o.l.chanW - 6, o.headerH};
        const bool muted = c.tel && c.tel->ch[ch].muted;
        g.panel(hr, t, muted ? Gfx::PanelState::Pressed : Gfx::PanelState::Normal);
        char buf[24];
        if (o.l.visible > 8)
            std::snprintf(buf, sizeof buf, muted ? "%02d off" : "%02d", ch + 1);
        else
            std::snprintf(buf, sizeof buf, muted ? "TRACK %02d  MUTED" : "TRACK %02d", ch + 1);
        tx.draw(g, hr, buf, hs, muted ? withAlpha(t.label, 0.45f) : t.label, Align::Center);
    }
    // row stripes (every 4th row, stronger every 16th) and the current-row bar
    g.pushClip({area.x + 2, o.bodyTop, area.w - 4, o.bodyBottom - o.bodyTop});
    const int rows = (c.song && !c.song->patterns.empty())
                         ? c.song->patterns[c.pattern < (int)c.song->patterns.size() ? c.pattern : 0].rows
                         : 64;
    for (int dr = -o.rowsAbove; dr <= o.rowsBelow; ++dr) {
        const int row = c.row + dr;
        if (row < 0 || row >= rows || dr == 0 || row % 4) continue;
        g.fillRect({o.x0, o.centerY + dr * o.rowH, o.contentW, o.rowH},
                   withAlpha(t.patternText, row % 16 ? 0.06f : 0.12f));
    }
    g.fillRoundGradientV({area.x + 4, o.centerY, area.w - 8, o.rowH}, 3.f, t.rowBar,
                         mixColor(t.rowBar, t.patternText, 0.25f));
    g.popClip();
    // channel separators
    for (int k = 0; k < o.l.visible; ++k) {
        float x = o.x0 + o.l.rowNumW + k * o.l.chanW;
        g.fillRect({x - 1.f, o.bodyTop, 2.f, o.bodyBottom - o.bodyTop}, withAlpha(t.panelDark, 0.45f));
    }
}

void PatternView::drawMeters(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c) {
    if (!c.tel) return;
    const Geo o = geometry(f, area, c);
    const float h = o.centerY - o.bodyTop - 6.f;
    for (int k = 0; k < o.l.visible; ++k) {
        const int ch = o.l.firstChannel + k;
        const ChannelTelemetry& ct = c.tel->ch[ch];
        if (ct.muted) continue;
        const float w = o.l.chanW * 0.2f;
        const float x = o.x0 + o.l.rowNumW + k * o.l.chanW + (o.l.chanW - w) * 0.5f;
        drawVu(g, t, {x, o.bodyTop + 6.f, w, h}, ct.vu, 0.55f);
    }
}

void PatternView::drawText(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c) {
    if (!c.song || c.song->patterns.empty()) return;
    const Geo o = geometry(f, area, c);
    const int pi = c.pattern < (int)c.song->patterns.size() ? c.pattern : 0;
    const Pattern& p = c.song->patterns[pi];
    const bool pt = c.song->quirks == Quirks::PT23;
    const float gl = o.l.glyph;
    glyphs_.clear();
    auto put = [&](float x, float y, const char* s, int n, Color col) {
        for (int i = 0; i < n; ++i) glyphs_.push_back({x + i * gl, y, s[i], col});
    };
    const float textDy = std::floor((o.rowH - Font::lineHeight(o.em)) * 0.5f);
    for (int dr = -o.rowsAbove; dr <= o.rowsBelow; ++dr) {
        const int row = c.row + dr;
        if (row < 0 || row >= p.rows) continue;
        const float y = o.centerY + dr * o.rowH + textDy;
        const bool cur = dr == 0;
        char num[8];
        std::snprintf(num, sizeof num, p.rows > 100 ? "%03d" : "%02d", row);
        const float nx = o.x0 + (o.l.rowNumW - gl * (float)lstrlenA(num)) * 0.5f;
        put(nx, y, num, lstrlenA(num), cur ? t.rowBarText : (row % 4 ? t.rowNumber : t.patternText));
        for (int k = 0; k < o.l.visible; ++k) {
            const int ch = o.l.firstChannel + k;
            if (ch >= p.channels) break;
            const Cell& cell = p.at(row, ch);
            char txt[11];
            formatCell(cell, pt, txt);
            float x = o.x0 + o.l.rowNumW + k * o.l.chanW + (o.l.chanW - 10.75f * gl) * 0.5f;
            const bool muted = c.tel && c.tel->ch[ch].muted;
            auto color = [&](Color normal, bool empty) {
                Color col = cur ? (empty ? withAlpha(t.rowBarText, 0.45f) : t.rowBarText)
                                : (empty ? withAlpha(t.patternText, 0.35f) : normal);
                if (muted) col = withAlpha(col, col.a * 0.4f);
                return col;
            };
            put(x, y, txt, 3, color(t.patternNote, cell.note == 0));
            x += 3 * gl + o.l.gap;
            put(x, y, txt + 3, 2, color(t.patternSample, cell.instr == 0));
            x += 2 * gl + o.l.gap;
            put(x, y, txt + 5, 2, color(t.patternVol, cell.vol == 0));
            x += 2 * gl + o.l.gap;
            put(x, y, txt + 7, 3, color(t.patternFx, cell.fx == 0 && cell.param == 0));
        }
    }
    g.pushClip({area.x + 2, o.bodyTop, area.w - 4, o.bodyBottom - o.bodyTop});
    f.drawGlyphs(g, glyphs_.data(), (int)glyphs_.size(), o.em);
    g.popClip();
}

int PatternView::headerHit(Font& f, RectF area, const PatternCtx& c, float x, float y) const {
    const Geo o = geometry(f, area, c);
    const float hy = area.y + kInset;
    if (y < hy || y >= hy + o.headerH) return -1;
    float rel = x - (o.x0 + o.l.rowNumW);
    if (rel < 0) return -1;
    int k = (int)(rel / o.l.chanW);
    return k < o.l.visible ? o.l.firstChannel + k : -1;
}
