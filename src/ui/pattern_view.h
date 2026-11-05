// Read-only pattern display: row numbers, track headers, colored cells, VU meters inside the
// track columns and the current row fixed in the vertical center.
#pragma once
#include <vector>

#include "audio/engine.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/pattern_layout.h"

struct PatternCtx {
    const Song* song;
    int pattern;
    int row;
    int viewMode;
    int firstChannel;
    const Telemetry* tel;
};

class PatternView {
public:
    void drawBack(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c);
    void drawMeters(Gfx& g, const Theme& t, RectF area, const PatternCtx& c);
    void drawText(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c);
    int headerHit(RectF area, const PatternCtx& c, float x, float y) const;  // channel or -1

private:
    struct Geo {
        PatternLayout l;
        float x0, contentW, headerH, rowH, bodyTop, centerY;
        int rowsAbove, rowsBelow;
    };
    Geo geometry(RectF area, const PatternCtx& c) const;
    std::vector<Glyph> glyphs_;
};
