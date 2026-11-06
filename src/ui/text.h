// Smooth text with fonts that ship with the system (no size cost):
//   Windows (DirectWrite, text.cpp): Ui - Bahnschrift SemiBold (fallback Segoe UI Semibold),
//                                    Mono - Cascadia Mono SemiBold (fallback Consolas Bold)
//   macOS (Core Text, src/mac/text_mac.cpp): Ui - DIN Alternate Bold (fallback system semibold),
//                                    Mono - SF Mono semibold (fallback Menlo Bold)
// Codes 1..6 in strings are UI arrows (up, down, left, right, filled up, filled down).
#pragma once
#ifdef _WIN32
#include <dwrite.h>
#endif

#include "ui/gfx.h"

enum class Align { Left, Center, Right };
enum class FontRole { Ui, Mono };

class Text {
public:
    Text() = default;
    ~Text();
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;
    bool init();
#ifdef _WIN32
    IDWriteFactory* factory() { return factory_; }
    const wchar_t* family(FontRole r) const { return r == FontRole::Mono ? mono_ : ui_; }
    DWRITE_FONT_WEIGHT weight(FontRole r) const;
#else
    const void* font(FontRole role, float size);  // CTFontRef at that size (cached, owned by Text)
#endif
    // Draws `s` vertically centered in `r` (logical units). `size` = em size.
    void draw(Gfx& g, RectF r, const char* s, float size, Color c, Align a, FontRole role = FontRole::Ui);
    float measure(const char* s, float size, FontRole role);
    float monoAdvance() const { return monoAdvance_; }  // advance / em for the mono font
    static int widen(const char* s, wchar_t* out, int cap);

private:
#ifdef _WIN32
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
#else
    struct Entry {
        FontRole role;
        int size10;
        const void* font;  // CTFontRef
    };
    const void* base_[2] = {};  // CTFontRef at size 100: Ui, Mono
    float monoAdvance_ = 0.6f;
    Entry cache_[48] = {};
    int cacheNext_ = 0;
#endif
};
