// Text rendering with the built-in glyph set. Glyphs are rasterized as vector geometry into
// an atlas at the exact device pixel size (re-rasterized when the scale changes), then drawn
// with a sprite batch at pixel-snapped positions: sharp at every window size.
#pragma once
#include <d2d1_3.h>

#include "ui/gfx.h"

struct Glyph {
    float x, y;  // logical
    char ch;
    Color c;
};

class Font {
public:
    ~Font();
    // Many glyphs of one size in a single sprite batch (used by the pattern view).
    void drawGlyphs(Gfx& g, const Glyph* glyphs, int n, float size);
    // x, y, size in logical units; size = height of the 8x8 cell. Returns the end x.
    float drawText(Gfx& g, float x, float y, float size, const char* s, Color c);
    float advance(float size) const { return size; }
    void invalidate();

private:
    struct Atlas {
        int cellPx = 0;
        ID2D1Bitmap1* bmp = nullptr;
        uint32_t lastUse = 0;
    };
    Atlas* atlasFor(Gfx& g, int cellPx);
    bool rasterize(Gfx& g, Atlas& a);

    Atlas atlases_[4];
    ID2D1SpriteBatch* batch_ = nullptr;
    uint32_t generation_ = 0;
    uint32_t useCounter_ = 0;
};
