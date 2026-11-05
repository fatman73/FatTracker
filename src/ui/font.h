// Fast monospace text for the pattern view: glyphs of the system monospace font are
// rendered smoothly with DirectWrite into an atlas at the exact device pixel size
// (re-rendered when the scale changes) and drawn with one sprite batch per call.
#pragma once
#include <d2d1_3.h>

#include "ui/gfx.h"
#include "ui/text.h"

struct Glyph {
    float x, y;  // logical, top-left of the character cell
    char ch;
    Color c;
};

class Font {
public:
    ~Font();
    void setText(Text* t) { text_ = t; }
    void drawGlyphs(Gfx& g, const Glyph* glyphs, int n, float em);
    float advance(float em) const { return em * (text_ ? text_->monoAdvance() : 0.55f); }
    static float lineHeight(float em) { return em * 1.3f; }
    void invalidate();

private:
    struct Atlas {
        int emPx = 0, cellW = 0, cellH = 0;
        ID2D1Bitmap1* bmp = nullptr;
        uint32_t lastUse = 0;
    };
    Atlas* atlasFor(Gfx& g, int emPx);
    bool rasterize(Gfx& g, Atlas& a);

    Text* text_ = nullptr;
    Atlas atlases_[4];
    ID2D1SpriteBatch* batch_ = nullptr;
    uint32_t generation_ = 0;
    uint32_t useCounter_ = 0;
};
