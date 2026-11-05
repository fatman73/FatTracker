#include "ui/pattern_view.h"

#include <cmath>
#include <cstdio>

#include "ui/widgets.h"

PatternView::Geo PatternView::geometry(RectF area, const PatternCtx& c) const {
    Geo o{};
    const int nch = c.song ? c.song->numChannels : 4;
    o.l = computePatternLayout(c.viewMode, nch, c.firstChannel, area.w);
    o.contentW = o.l.rowNumW + o.l.visible * o.l.chanW;
    o.x0 = area.x + (area.w - o.contentW) * 0.5f;
    o.headerH = o.l.glyph * 1.7f;
    if (o.headerH < 26.f) o.headerH = 26.f;
    o.rowH = std::floor(o.l.glyph * 1.3f);
    o.bodyTop = area.y + o.headerH;
    const float bodyH = area.h - o.headerH;
    o.centerY = o.bodyTop + std::floor((bodyH - o.rowH) * 0.5f);
    o.rowsAbove = (int)std::ceil((o.centerY - o.bodyTop) / o.rowH);
    o.rowsBelow = (int)std::ceil((area.y + area.h - (o.centerY + o.rowH)) / o.rowH);
    return o;
}

void PatternView::drawBack(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c) {
    const Geo o = geometry(area, c);
    g.fillRect(area, t.patternBg);
    // header strip
    g.bevel({area.x, area.y, area.w, o.headerH}, t, false);
    const float hs = o.l.glyph < 14.f ? 12.f : 16.f;
    ::drawText(g, f, {o.x0, area.y, o.l.rowNumW, o.headerH}, "POS", hs, t.label, Align::Center);
    for (int k = 0; k < o.l.visible; ++k) {
        const int ch = o.l.firstChannel + k;
        RectF hr = {o.x0 + o.l.rowNumW + k * o.l.chanW, area.y, o.l.chanW, o.headerH};
        const bool muted = c.tel && c.tel->ch[ch].muted;
        g.bevel({hr.x + 2, hr.y + 2, hr.w - 4, hr.h - 4}, t, muted);
        char buf[16];
        if (c.viewMode >= 16)
            std::snprintf(buf, sizeof buf, muted ? "-%02d-" : "#%02d", ch + 1);
        else
            std::snprintf(buf, sizeof buf, muted ? "MUTED %02d" : "TRACK #%02d", ch + 1);
        ::drawText(g, f, hr, buf, hs, muted ? t.panelDark : t.label, Align::Center);
    }
    // current row bar
    g.fillRect({area.x, o.centerY, area.w, o.rowH}, t.rowBar);
    // channel separators
    for (int k = 0; k <= o.l.visible; ++k) {
        float x = o.x0 + o.l.rowNumW + k * o.l.chanW;
        g.fillRect({x - 1.5f, o.bodyTop, 3.f, area.y + area.h - o.bodyTop}, withAlpha(t.panelDark, 0.9f));
    }
}

void PatternView::drawMeters(Gfx& g, const Theme& t, RectF area, const PatternCtx& c) {
    if (!c.tel) return;
    const Geo o = geometry(area, c);
    const float h = o.centerY - o.bodyTop - 4.f;
    for (int k = 0; k < o.l.visible; ++k) {
        const int ch = o.l.firstChannel + k;
        const ChannelTelemetry& ct = c.tel->ch[ch];
        if (ct.muted) continue;
        const float w = o.l.chanW * 0.22f;
        const float x = o.x0 + o.l.rowNumW + k * o.l.chanW + (o.l.chanW - w) * 0.5f;
        drawVu(g, t, {x, o.bodyTop + 4.f, w, h}, ct.vu, 0.8f);
    }
}

void PatternView::drawText(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c) {
    if (!c.song || c.song->patterns.empty()) return;
    const Geo o = geometry(area, c);
    const int pi = c.pattern < (int)c.song->patterns.size() ? c.pattern : 0;
    const Pattern& p = c.song->patterns[pi];
    const bool pt = c.song->quirks == Quirks::PT23;
    const float gl = o.l.glyph;
    glyphs_.clear();
    auto put = [&](float x, float y, const char* s, int n, Color col) {
        for (int i = 0; i < n; ++i) glyphs_.push_back({x + i * gl, y, s[i], col});
    };
    const float textDy = std::floor((o.rowH - gl) * 0.5f);
    for (int dr = -o.rowsAbove; dr <= o.rowsBelow; ++dr) {
        const int row = c.row + dr;
        if (row < 0 || row >= p.rows) continue;
        const float y = o.centerY + dr * o.rowH + textDy;
        const bool cur = dr == 0;
        char num[8];
        std::snprintf(num, sizeof num, p.rows > 100 ? "%03d" : "%02d", row);
        float nx = o.x0 + (o.l.rowNumW - gl * (float)lstrlenA(num)) * 0.5f;
        put(nx, y, num, lstrlenA(num), cur ? t.rowBarText : t.rowNumber);
        for (int k = 0; k < o.l.visible; ++k) {
            const int ch = o.l.firstChannel + k;
            if (ch >= p.channels) break;
            const Cell& cell = p.at(row, ch);
            char txt[11];
            formatCell(cell, pt, txt);
            float x = o.x0 + o.l.rowNumW + k * o.l.chanW + 0.5f * gl;
            const bool muted = c.tel && c.tel->ch[ch].muted;
            auto color = [&](Color normal, bool empty) {
                Color col = cur ? t.rowBarText : (empty ? withAlpha(t.patternText, 0.55f) : normal);
                if (muted) col = withAlpha(col, 0.4f);
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
    g.pushClip({area.x, o.bodyTop, area.w, area.y + area.h - o.bodyTop});
    f.drawGlyphs(g, glyphs_.data(), (int)glyphs_.size(), gl);
    g.popClip();
}

int PatternView::headerHit(RectF area, const PatternCtx& c, float x, float y) const {
    const Geo o = geometry(area, c);
    if (y < area.y || y >= area.y + o.headerH) return -1;
    float rel = x - (o.x0 + o.l.rowNumW);
    if (rel < 0) return -1;
    int k = (int)(rel / o.l.chanW);
    return k < o.l.visible ? o.l.firstChannel + k : -1;
}
