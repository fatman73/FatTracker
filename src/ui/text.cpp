#include "ui/text.h"

#include <cmath>

namespace {
bool hasFamily(IDWriteFactory* f, const wchar_t* name) {
    IDWriteFontCollection* coll = nullptr;
    if (FAILED(f->GetSystemFontCollection(&coll))) return false;
    UINT32 idx = 0;
    BOOL exists = FALSE;
    coll->FindFamilyName(name, &idx, &exists);
    coll->Release();
    return exists != FALSE;
}
}  // namespace

Text::~Text() {
    for (Entry& e : cache_)
        if (e.fmt) e.fmt->Release();
    if (factory_) factory_->Release();
}

bool Text::init() {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory_)))
        return false;
    if (hasFamily(factory_, L"Bahnschrift")) {
        ui_ = L"Bahnschrift";
        uiIsBahnschrift_ = true;
    }
    if (hasFamily(factory_, L"Cascadia Mono")) mono_ = L"Cascadia Mono";
    // advance of a monospace digit relative to the em size
    IDWriteTextFormat* f = nullptr;
    if (SUCCEEDED(factory_->CreateTextFormat(mono_, nullptr, weight(FontRole::Mono), DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, 100.f, L"en-us", &f))) {
        IDWriteTextLayout* l = nullptr;
        if (SUCCEEDED(factory_->CreateTextLayout(L"0000000000", 10, f, 10000.f, 1000.f, &l))) {
            DWRITE_TEXT_METRICS m;
            if (SUCCEEDED(l->GetMetrics(&m)) && m.widthIncludingTrailingWhitespace > 0)
                monoAdvance_ = m.widthIncludingTrailingWhitespace / 1000.f;
            l->Release();
        }
        f->Release();
    }
    return true;
}

DWRITE_FONT_WEIGHT Text::weight(FontRole r) const {
    if (r == FontRole::Mono) return DWRITE_FONT_WEIGHT_SEMI_BOLD;
    return uiIsBahnschrift_ ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD;
}

int Text::widen(const char* s, wchar_t* out, int cap) {
    static const wchar_t arrows[7] = {0, 0x25B2, 0x25BC, 0x25C0, 0x25B6, 0x25B2, 0x25BC};
    int n = 0;
    for (; s[n] && n < cap - 1; ++n) {
        unsigned char c = (unsigned char)s[n];
        out[n] = (c >= 1 && c <= 6) ? arrows[c] : (wchar_t)c;
    }
    out[n] = 0;
    return n;
}

IDWriteTextFormat* Text::format(FontRole role, float size, Align a) {
    const int s10 = (int)std::lround(size * 10.f);
    for (Entry& e : cache_)
        if (e.fmt && e.role == role && e.size10 == s10 && e.align == a) return e.fmt;
    IDWriteTextFormat* f = nullptr;
    if (FAILED(factory_->CreateTextFormat(family(role), nullptr, weight(role), DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, s10 / 10.f, L"en-us", &f)))
        return nullptr;
    f->SetTextAlignment(a == Align::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                        : a == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                             : DWRITE_TEXT_ALIGNMENT_TRAILING);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    Entry& slot = cache_[cacheNext_];
    cacheNext_ = (cacheNext_ + 1) % 48;
    if (slot.fmt) slot.fmt->Release();
    slot = {role, s10, a, f};
    return f;
}

void Text::draw(Gfx& g, RectF r, const char* s, float size, Color c, Align a, FontRole role) {
    if (!factory_ || !*s) return;
    IDWriteTextFormat* f = format(role, size, a);
    if (!f) return;
    wchar_t w[256];
    const int n = widen(s, w, 256);
    const float pad = a == Align::Center ? 0.f : size * 0.45f;
    D2D1_RECT_F rc = {r.x + pad, r.y, r.x + r.w - pad, r.y + r.h};
    g.dc()->DrawTextW(w, (UINT32)n, f, rc, g.brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Text::measure(const char* s, float size, FontRole role) {
    IDWriteTextFormat* f = format(role, size, Align::Left);
    if (!f) return 0;
    wchar_t w[256];
    const int n = widen(s, w, 256);
    IDWriteTextLayout* l = nullptr;
    float width = 0;
    if (SUCCEEDED(factory_->CreateTextLayout(w, (UINT32)n, f, 100000.f, 1000.f, &l))) {
        DWRITE_TEXT_METRICS m;
        if (SUCCEEDED(l->GetMetrics(&m))) width = m.widthIncludingTrailingWhitespace;
        l->Release();
    }
    return width;
}
