// Read-only pattern display: row numbers, track headers, colored cells, VU meters inside the
// track columns and the current row fixed in the vertical center.
#pragma once
#include <vector>

#include "audio/engine.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/pattern_layout.h"
#include "ui/text.h"

struct PatternCtx {
    const Song* song;
    int pattern;
    int row;
    int viewMode;
    int firstChannel;
    const Telemetry* tel;
    // editing
    int cursorCh = 0, cursorCol = 0;
    bool edit = false;  // edit mode (cursor drawn in the cursor colour)
    bool blockOn = false;
    int bRow0 = 0, bRow1 = 0, bCh0 = 0, bCh1 = 0;
};

class PatternView {
public:
    void drawBack(Gfx& g, Text& tx, Font& f, const Theme& t, RectF area, const PatternCtx& c);
    void drawMeters(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c);
    void drawText(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c);
    void drawCursor(Gfx& g, Font& f, const Theme& t, RectF area, const PatternCtx& c);
    int headerHit(Font& f, RectF area, const PatternCtx& c, float x, float y) const;  // channel or -1
    // cell under (x,y): row, channel and cursor column; false when outside the pattern body
    bool cellHit(Font& f, RectF area, const PatternCtx& c, float x, float y, int& row, int& ch, int& col) const;
    int visibleChannels(Font& f, RectF area, const PatternCtx& c) const { return geometry(f, area, c).l.visible; }

private:
    struct Geo {
        PatternLayout l;
        float em, x0, contentW, headerH, rowH, bodyTop, bodyBottom, centerY;
        int rowsAbove, rowsBelow;
    };
    Geo geometry(Font& f, RectF area, const PatternCtx& c) const;
    std::vector<Glyph> glyphs_;
};
