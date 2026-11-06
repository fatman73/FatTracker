// 320x256 pixel canvas for the Pure mode screen (an Amiga lowres screen), with a chunky
// 8x8 font in the Amiga style. Pure C++ (no Windows), so it is unit tested.
#pragma once
#include <cstdint>

constexpr uint32_t argb(uint32_t rgb) { return 0xFF000000u | rgb; }

// Glyph heights: 5 px (captions, pattern rows), 6 px (values), 7 px, 9 px (the playing row bar).
enum FontSize : int { kSmall = 0, kNormal = 1, kTall = 2, kMedium = 3 };

class PixelCanvas {
public:
    static constexpr int W = 320, H = 256;
    void clear(uint32_t c) { fill(0, 0, W, H, c); }
    void fill(int x, int y, int w, int h, uint32_t c);
    void frame(int x, int y, int w, int h, uint32_t c);  // 1 px outline
    // Amiga style raised box: light top/left edge, dark bottom/right edge (swapped when sunken).
    void bevel(int x, int y, int w, int h, uint32_t face, uint32_t light, uint32_t dark, bool sunken = false);
    void set(int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < W && y < H) px_[y * W + x] = c;
    }
    uint32_t at(int x, int y) const { return (x >= 0 && y >= 0 && x < W && y < H) ? px_[y * W + x] : 0; }
    // Text, 8 px per character.
    void text(int x, int y, const char* s, uint32_t c, FontSize size = kNormal);
    void textShadow(int x, int y, const char* s, uint32_t c, uint32_t shadow, FontSize size = kNormal);
    void line(int x0, int y0, int x1, int y1, uint32_t c);
    const uint32_t* pixels() const { return px_; }

private:
    uint32_t px_[W * H] = {};
};

// Glyph rows (7 rows, bit 6 = leftmost of 7 columns); unknown characters are blank.
// '\x01' / '\x02' are the up / down arrows of the counters.
const uint8_t* amigaGlyph(char c);
int glyphRows(FontSize size);  // 5, 6, 7 or 9
// Columns a glyph uses in any row (bit 6 = leftmost column), e.g. to measure the ink of a caption.
uint8_t glyphColumns(char c, FontSize size);
