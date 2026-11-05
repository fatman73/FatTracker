// Smooth text via DirectWrite with fonts that ship with Windows (no size cost):
//   Ui   - labels and buttons: Bahnschrift SemiBold (fallback Segoe UI Semibold)
//   Mono - numbers and pattern data: Cascadia Mono SemiBold (fallback Consolas Bold)
// Codes 1..6 in strings are UI arrows (up, down, left, right, filled up, filled down).
#pragma once
#include <dwrite.h>

#include "ui/gfx.h"

enum class Align { Left, Center, Right };
enum class FontRole { Ui, Mono };

class Text {
public:
    ~Text();
    bool init();
    IDWriteFactory* factory() { return factory_; }
    const wchar_t* family(FontRole r) const { return r == FontRole::Mono ? mono_ : ui_; }
    DWRITE_FONT_WEIGHT weight(FontRole r) const;
    // Draws `s` vertically centered in `r` (logical units). `size` = em size.
    void draw(Gfx& g, RectF r, const char* s, float size, Color c, Align a, FontRole role = FontRole::Ui);
    float measure(const char* s, float size, FontRole role);
    float monoAdvance() const { return monoAdvance_; }  // advance / em for the mono font
    static int widen(const char* s, wchar_t* out, int cap);

private:
    IDWriteTextFormat* format(FontRole role, float size, Align a);
    struct Entry {
        FontRole role;
        int size10;
        Align align;
        IDWriteTextFormat* fmt;
    };
    IDWriteFactory* factory_ = nullptr;
    const wchar_t* ui_ = L"Segoe UI";
    const wchar_t* mono_ = L"Consolas";
    bool uiIsBahnschrift_ = false;
    float monoAdvance_ = 0.55f;
    Entry cache_[48] = {};
    int cacheNext_ = 0;
};
