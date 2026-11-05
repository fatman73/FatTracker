#include "ui/font.h"

#include <cmath>

namespace {
constexpr int kCols = 16, kRows = 8, kPad = 2;
}

Font::~Font() { invalidate(); }

void Font::invalidate() {
    for (Atlas& a : atlases_) {
        if (a.bmp) a.bmp->Release();
        a = Atlas{};
    }
    if (batch_) {
        batch_->Release();
        batch_ = nullptr;
    }
}

bool Font::rasterize(Gfx& g, Atlas& a) {
    if (!text_ || !text_->factory()) return false;
    IDWriteTextFormat* fmt = nullptr;
    if (FAILED(text_->factory()->CreateTextFormat(text_->family(FontRole::Mono), nullptr,
                                                  text_->weight(FontRole::Mono), DWRITE_FONT_STYLE_NORMAL,
                                                  DWRITE_FONT_STRETCH_NORMAL, (float)a.emPx, L"en-us", &fmt)))
        return false;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ID2D1DeviceContext* dc = g.dc();
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    bp.dpiX = bp.dpiY = 96.f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    HRESULT hr = dc->CreateBitmap({(UINT32)(a.cellW * kCols), (UINT32)(a.cellH * kRows)}, nullptr, 0, &bp, &a.bmp);
    if (FAILED(hr)) {
        fmt->Release();
        return false;
    }
    ID2D1Image* oldTarget = nullptr;
    dc->GetTarget(&oldTarget);
    D2D1_MATRIX_3X2_F oldXf;
    dc->GetTransform(&oldXf);
    const D2D1_TEXT_ANTIALIAS_MODE oldAa = dc->GetTextAntialiasMode();
    dc->SetTarget(a.bmp);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    dc->Clear({0, 0, 0, 0});
    ID2D1SolidColorBrush* white = g.brush({1, 1, 1, 1});
    for (int code = 1; code < 128; ++code) {
        char s[2] = {(char)code, 0};
        if (code == ' ' || (code > 6 && code < 32)) continue;
        wchar_t w[2];
        Text::widen(s, w, 2);
        const float ox = (float)((code % kCols) * a.cellW + kPad);
        const float oy = (float)((code / kCols) * a.cellH + kPad);
        D2D1_RECT_F rc = {ox, oy, ox + a.cellW - kPad, oy + a.cellH - kPad};
        dc->DrawTextW(w, 1, fmt, rc, white, D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    dc->SetTextAntialiasMode(oldAa);
    dc->SetTarget(oldTarget);
    dc->SetTransform(oldXf);
    if (oldTarget) oldTarget->Release();
    fmt->Release();
    return true;
}

Font::Atlas* Font::atlasFor(Gfx& g, int emPx) {
    if (g.generation() != generation_) {
        invalidate();
        generation_ = g.generation();
    }
    ++useCounter_;
    Atlas* victim = &atlases_[0];
    for (Atlas& a : atlases_) {
        if (a.bmp && a.emPx == emPx) {
            a.lastUse = useCounter_;
            return &a;
        }
        if (!a.bmp || a.lastUse < victim->lastUse) victim = &a;
    }
    if (victim->bmp) victim->bmp->Release();
    *victim = Atlas{};
    victim->emPx = emPx;
    victim->cellW = (int)std::ceil(emPx * (text_ ? text_->monoAdvance() : 0.55f)) + 2 * kPad + 2;
    victim->cellH = (int)std::ceil(lineHeight((float)emPx)) + 2 * kPad;
    victim->lastUse = useCounter_;
    if (!rasterize(g, *victim)) {
        if (victim->bmp) victim->bmp->Release();
        *victim = Atlas{};
        return nullptr;
    }
    return victim;
}

void Font::drawGlyphs(Gfx& g, const Glyph* glyphs, int count, float em) {
    const Viewport& vp = g.viewport();
    if (vp.scale <= 0 || count <= 0) return;
    int emPx = (int)std::lround(em * vp.scale);
    if (emPx < 6) emPx = 6;
    Atlas* a = atlasFor(g, emPx);
    if (!a) return;
    ID2D1DeviceContext* dc = g.dc();
    ID2D1DeviceContext3* dc3 = nullptr;
    if (FAILED(dc->QueryInterface(__uuidof(ID2D1DeviceContext3), (void**)&dc3))) return;
    if (!batch_) dc3->CreateSpriteBatch(&batch_);
    if (!batch_) {
        dc3->Release();
        return;
    }
    batch_->Clear();
    constexpr int kChunk = 512;
    D2D1_RECT_F dst[kChunk];
    D2D1_RECT_U src[kChunk];
    D2D1_COLOR_F col[kChunk];
    int n = 0;
    auto flush = [&]() {
        if (n) batch_->AddSprites((UINT32)n, dst, src, col, nullptr, sizeof(D2D1_RECT_F), sizeof(D2D1_RECT_U),
                                  sizeof(D2D1_COLOR_F), 0);
        n = 0;
    };
    for (int i = 0; i < count; ++i) {
        const Glyph& gl = glyphs[i];
        unsigned char ch = (unsigned char)gl.ch;
        if (ch == ' ' || ch >= 128 || ch == 0) continue;
        const float dx = std::floor(vp.offX + gl.x * vp.scale + 0.5f) - kPad;
        const float dy = std::floor(vp.offY + gl.y * vp.scale + 0.5f) - kPad;
        const UINT32 sx = (UINT32)((ch % kCols) * a->cellW), sy = (UINT32)((ch / kCols) * a->cellH);
        src[n] = {sx, sy, sx + (UINT32)a->cellW, sy + (UINT32)a->cellH};
        dst[n] = {dx, dy, dx + a->cellW, dy + a->cellH};
        col[n] = {gl.c.r, gl.c.g, gl.c.b, gl.c.a};
        if (++n == kChunk) flush();
    }
    flush();
    if (batch_->GetSpriteCount()) {
        D2D1_MATRIX_3X2_F xf;
        dc->GetTransform(&xf);
        D2D1_ANTIALIAS_MODE aa = dc->GetAntialiasMode();
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        dc3->DrawSpriteBatch(batch_, a->bmp, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                             D2D1_SPRITE_OPTIONS_NONE);
        dc->SetAntialiasMode(aa);
        dc->SetTransform(xf);
    }
    dc3->Release();
}
