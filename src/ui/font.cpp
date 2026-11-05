#include "ui/font.h"

#include <cmath>

#include "ui/font_data.h"

namespace {
constexpr int kCols = 16, kRows = 8, kPad = 1;
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
    ID2D1DeviceContext* dc = g.dc();
    const int pitch = a.cellPx + 2 * kPad;
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    bp.dpiX = bp.dpiY = 96.f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    if (FAILED(dc->CreateBitmap({(UINT32)(pitch * kCols), (UINT32)(pitch * kRows)}, nullptr, 0, &bp, &a.bmp)))
        return false;

    ID2D1Image* oldTarget = nullptr;
    dc->GetTarget(&oldTarget);
    D2D1_MATRIX_3X2_F oldXf;
    dc->GetTransform(&oldXf);
    dc->SetTarget(a.bmp);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->Clear({0, 0, 0, 0});
    ID2D1SolidColorBrush* white = g.brush({1, 1, 1, 1});
    const float px = a.cellPx / 8.f;
    for (int code = 0; code < 128; ++code) {
        const float ox = (float)((code % kCols) * pitch + kPad);
        const float oy = (float)((code / kCols) * pitch + kPad);
        // one rectangle per horizontal run of lit pixels, unioned via a winding-filled group
        ID2D1Geometry* rects[64];
        int n = 0;
        for (int r = 0; r < 8; ++r) {
            uint8_t bits = kGlyphs[code][r];
            int c = 0;
            while (c < 8) {
                if (!(bits & (0x80 >> c))) {
                    ++c;
                    continue;
                }
                int start = c;
                while (c < 8 && (bits & (0x80 >> c))) ++c;
                ID2D1RectangleGeometry* rg = nullptr;
                D2D1_RECT_F rc = {ox + start * px, oy + r * px, ox + c * px, oy + (r + 1) * px};
                if (SUCCEEDED(g.factory()->CreateRectangleGeometry(rc, &rg))) rects[n++] = rg;
            }
        }
        if (n) {
            ID2D1GeometryGroup* group = nullptr;
            if (SUCCEEDED(g.factory()->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, rects, n, &group))) {
                dc->FillGeometry(group, white);
                group->Release();
            }
            for (int i = 0; i < n; ++i) rects[i]->Release();
        }
    }
    dc->SetTarget(oldTarget);
    dc->SetTransform(oldXf);
    if (oldTarget) oldTarget->Release();
    return true;
}

Font::Atlas* Font::atlasFor(Gfx& g, int cellPx) {
    if (g.generation() != generation_) {
        invalidate();
        generation_ = g.generation();
    }
    ++useCounter_;
    Atlas* victim = &atlases_[0];
    for (Atlas& a : atlases_) {
        if (a.bmp && a.cellPx == cellPx) {
            a.lastUse = useCounter_;
            return &a;
        }
        if (!a.bmp || a.lastUse < victim->lastUse) victim = &a;
    }
    if (victim->bmp) victim->bmp->Release();
    *victim = Atlas{};
    victim->cellPx = cellPx;
    victim->lastUse = useCounter_;
    if (!rasterize(g, *victim)) {
        if (victim->bmp) victim->bmp->Release();
        *victim = Atlas{};
        return nullptr;
    }
    return victim;
}

float Font::drawText(Gfx& g, float x, float y, float size, const char* s, Color c) {
    const Viewport& vp = g.viewport();
    const float endX = x + advance(size) * (float)lstrlenA(s);
    if (vp.scale <= 0 || !*s) return endX;
    int cellPx = (int)std::lround(size * vp.scale);
    if (cellPx < 4) cellPx = 4;
    Atlas* a = atlasFor(g, cellPx);
    if (!a) return endX;
    ID2D1DeviceContext* dc = g.dc();
    ID2D1DeviceContext3* dc3 = nullptr;
    if (FAILED(dc->QueryInterface(__uuidof(ID2D1DeviceContext3), (void**)&dc3))) return endX;
    if (!batch_) dc3->CreateSpriteBatch(&batch_);
    if (!batch_) {
        dc3->Release();
        return endX;
    }
    batch_->Clear();
    const int pitch = cellPx + 2 * kPad;
    D2D1_RECT_F dst[256];
    D2D1_RECT_U src[256];
    int n = 0;
    const float step = advance(size) * vp.scale;
    const float dy = std::floor(vp.offY + y * vp.scale + 0.5f);
    for (int i = 0; s[i] && n < 256; ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == ' ' || ch >= 128) continue;
        const float dx = std::floor(vp.offX + x * vp.scale + i * step + 0.5f);
        const UINT32 sx = (UINT32)((ch % kCols) * pitch), sy = (UINT32)((ch / kCols) * pitch);
        src[n] = {sx, sy, sx + (UINT32)pitch, sy + (UINT32)pitch};
        dst[n] = {dx - kPad, dy - kPad, dx - kPad + pitch, dy - kPad + pitch};
        ++n;
    }
    if (n) {
        D2D1_COLOR_F col = {c.r, c.g, c.b, c.a};
        batch_->AddSprites((UINT32)n, dst, src, &col, nullptr, sizeof(D2D1_RECT_F), sizeof(D2D1_RECT_U), 0, 0);
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
    return endX;
}
