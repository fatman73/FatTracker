#include "ui/cursor.h"

#include <wincodec.h>

#include <vector>

#include "ui/cursor_art.h"

namespace {
HCURSOR makeCursor(int size, bool busy) {
    std::vector<uint32_t> px((size_t)size * size);
    renderAmigaCursor(px.data(), size, busy);
    BITMAPV5HEADER bh = {};
    bh.bV5Size = sizeof bh;
    bh.bV5Width = size;
    bh.bV5Height = -size;  // top-down
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bh, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    memcpy(bits, px.data(), px.size() * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = FALSE;
    int hx, hy;
    amigaCursorHotspot(size, hx, hy);
    ii.xHotspot = (DWORD)hx;
    ii.yHotspot = (DWORD)hy;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return c;
}
}  // namespace

void AmigaCursors::release() {
    if (arrow_) DestroyCursor(arrow_);
    if (busy_) DestroyCursor(busy_);
    arrow_ = busy_ = nullptr;
}

void AmigaCursors::build(UINT dpi) {
    release();
    int size = MulDiv(32, (int)dpi, 96);
    if (size < 24) size = 24;
    arrow_ = makeCursor(size, false);
    busy_ = makeCursor(size, true);
}

bool AmigaCursors::savePng(const wchar_t* path, int size) {
    std::vector<uint32_t> px((size_t)size * size * 2);
    renderAmigaCursor(px.data(), size, false);
    renderAmigaCursor(px.data() + (size_t)size * size, size, true);
    // un-premultiply for a straight-alpha PNG, two cursors side by side
    std::vector<uint32_t> out((size_t)size * 2 * size);
    for (int y = 0; y < size; ++y)
        for (int k = 0; k < 2; ++k)
            for (int x = 0; x < size; ++x) {
                uint32_t p = px[(size_t)k * size * size + (size_t)y * size + x];
                const uint32_t a = p >> 24;
                auto un = [&](uint32_t c) { return a ? (c * 255 + a / 2) / a : 0; };
                out[(size_t)y * size * 2 + k * size + x] =
                    (a << 24) | (un((p >> 16) & 255) << 16) | (un((p >> 8) & 255) << 8) | un(p & 255);
            }
    IWICImagingFactory* wic = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         __uuidof(IWICImagingFactory), (void**)&wic)) &&
              SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
              SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(size * 2, size)) && SUCCEEDED(frame->SetPixelFormat(&pf)) &&
              SUCCEEDED(frame->WritePixels(size, size * 8, size * 8 * size, (BYTE*)out.data())) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (wic) wic->Release();
    return ok;
}
