#include "ui/gfx.h"

#include <d2d1effects.h>
#include <wincodec.h>

namespace {
template <class T>
void rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
D2D1_COLOR_F dcol(Color c) { return {c.r, c.g, c.b, c.a}; }
D2D1_RECT_F drect(RectF r) { return {r.x, r.y, r.x + r.w, r.y + r.h}; }
uint32_t pack(Color c) {
    auto b = [](float v) { return (uint32_t)(v < 0 ? 0 : (v > 1 ? 255 : v * 255.f + 0.5f)); };
    return (b(c.r) << 24) | (b(c.g) << 16) | (b(c.b) << 8) | b(c.a);
}
}  // namespace

Gfx::~Gfx() {
    releaseDevice();
    rel(factory_);
}

bool Gfx::init(HWND hwnd) {
    hwnd_ = hwnd;
    RECT rc;
    GetClientRect(hwnd, &rc);
    w_ = (uint32_t)(rc.right - rc.left);
    h_ = (uint32_t)(rc.bottom - rc.top);
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 (void**)&factory_)))
        return false;
    return createDevice();
}

bool Gfx::initOffscreen(uint32_t w, uint32_t h) {
    hwnd_ = nullptr;
    w_ = w;
    h_ = h;
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 (void**)&factory_)))
        return false;
    return createDevice();
}

bool Gfx::savePng(const wchar_t* path) {
    if (!dc_ || !target_) return false;
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
    bp.dpiX = bp.dpiY = 96.f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ID2D1Bitmap1* cpu = nullptr;
    if (FAILED(dc_->CreateBitmap({w_, h_}, nullptr, 0, &bp, &cpu))) return false;
    bool ok = SUCCEEDED(cpu->CopyFromBitmap(nullptr, target_, nullptr));
    D2D1_MAPPED_RECT mr = {};
    if (ok) ok = SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ, &mr));
    if (ok) {
        IWICImagingFactory* wic = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        __uuidof(IWICImagingFactory), (void**)&wic)) &&
             SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
             SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
             SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
             SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
             SUCCEEDED(frame->SetSize(w_, h_));
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        if (ok) ok = SUCCEEDED(frame->SetPixelFormat(&pf)) && IsEqualGUID(pf, GUID_WICPixelFormat32bppBGRA);
        if (ok) {
            // tightly packed copy with opaque alpha (target is ALPHA_MODE_IGNORE)
            const UINT stride = w_ * 4;
            BYTE* px = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)stride * h_);
            ok = px != nullptr;
            if (ok) {
                for (UINT y = 0; y < h_; ++y) {
                    const BYTE* src = mr.bits + (size_t)y * mr.pitch;
                    BYTE* dst = px + (size_t)y * stride;
                    for (UINT x = 0; x < w_; ++x) {
                        dst[x * 4 + 0] = src[x * 4 + 0];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = src[x * 4 + 2];
                        dst[x * 4 + 3] = 255;
                    }
                }
                ok = SUCCEEDED(frame->WritePixels(h_, stride, stride * h_, px)) && SUCCEEDED(frame->Commit()) &&
                     SUCCEEDED(enc->Commit());
                HeapFree(GetProcessHeap(), 0, px);
            }
        }
        rel(frame);
        rel(enc);
        rel(stream);
        rel(wic);
        cpu->Unmap();
    }
    cpu->Release();
    return ok;
}

bool Gfx::createDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                  D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 5, D3D11_SDK_VERSION,
                                   &d3d_, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 5, D3D11_SDK_VERSION, &d3d_,
                               nullptr, nullptr);
    if (FAILED(hr)) return false;
    IDXGIDevice* dxgiDev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* dxgiFactory = nullptr;
    d3d_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    if (dxgiDev) dxgiDev->GetAdapter(&adapter);
    if (adapter) adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);
    bool ok = dxgiDev && dxgiFactory;
    if (ok && hwnd_) {
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = w_ ? w_ : 1;
        sd.Height = h_ ? h_ : 1;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ok = SUCCEEDED(dxgiFactory->CreateSwapChainForHwnd(d3d_, hwnd_, &sd, nullptr, nullptr, &swap_));
        if (ok) dxgiFactory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    }
    if (ok) ok = SUCCEEDED(factory_->CreateDevice(dxgiDev, &d2dDevice_));
    if (ok) ok = SUCCEEDED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_));
    rel(dxgiFactory);
    rel(adapter);
    rel(dxgiDev);
    if (ok) ok = SUCCEEDED(dc_->CreateSolidColorBrush({1, 1, 1, 1}, &brush_));
    if (ok) ok = createTargets();
    if (!ok) releaseDevice();
    ++generation_;
    deviceLost_ = !ok;
    return ok;
}

bool Gfx::createTargets() {
    if (!dc_) return false;
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
    bp.dpiX = bp.dpiY = 96.f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    if (swap_) {
        IDXGISurface* surf = nullptr;
        if (FAILED(swap_->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf))) return false;
        HRESULT hr = dc_->CreateBitmapFromDxgiSurface(surf, &bp, &target_);
        rel(surf);
        if (FAILED(hr)) return false;
    } else {
        bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;  // offscreen: must be copyable for savePng
        if (FAILED(dc_->CreateBitmap({w_ ? w_ : 1, h_ ? h_ : 1}, nullptr, 0, &bp, &target_))) return false;
    }
    D2D1_BITMAP_PROPERTIES1 gp = {};
    gp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    gp.dpiX = gp.dpiY = 96.f;
    gp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    D2D1_SIZE_U sz = {w_ ? w_ : 1, h_ ? h_ : 1};
    if (FAILED(dc_->CreateBitmap(sz, nullptr, 0, &gp, &glowTarget_))) return false;
    dc_->SetTarget(target_);
    dc_->SetDpi(96.f, 96.f);
    vp_ = computeViewport(w_, h_);
    return true;
}

void Gfx::releaseTargets() {
    if (dc_) dc_->SetTarget(nullptr);
    rel(glowTarget_);
    rel(target_);
}

void Gfx::releaseDevice() {
    releaseTargets();
    for (auto& g : grad_) rel(g.stops);
    rel(pix_);
    rel(pixUp_);
    rel(brush_);
    rel(dc_);
    rel(d2dDevice_);
    rel(swap_);
    rel(d3d_);
}

void Gfx::resize(uint32_t w, uint32_t h) {
    w_ = w;
    h_ = h;
    vp_ = computeViewport(w, h);
    if (!swap_ || w == 0 || h == 0) return;
    releaseTargets();
    if (FAILED(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0)) || !createTargets()) deviceLost_ = true;
}

void Gfx::drawPixels(const uint32_t* argb, uint32_t w, uint32_t h, float devX, float devY, float devW, float devH) {
    if (!dc_ || !w || !h) return;
    if (pix_ && (pixW_ != w || pixH_ != h)) rel(pix_);
    if (!pix_) {
        const D2D1_BITMAP_PROPERTIES bp = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 96.f, 96.f};
        if (FAILED(dc_->CreateBitmap(D2D1::SizeU(w, h), argb, w * 4, bp, &pix_))) return;
        pixW_ = w;
        pixH_ = h;
    } else {
        pix_->CopyFromMemory(nullptr, argb, w * 4);
    }
    D2D1_MATRIX_3X2_F saved;
    dc_->GetTransform(&saved);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    const D2D1_RECT_F dst = D2D1::RectF(devX, devY, devX + devW, devY + devH);
    // whole factor that reaches the target size; the final fit then only shrinks a little
    const float need = devW / w > devH / h ? devW / w : devH / h;
    uint32_t k = (uint32_t)need + ((float)(uint32_t)need < need ? 1 : 0);
    if (k < 1) k = 1;
    if (k > 8) k = 8;
    const bool exact = devW == (float)(w * k) && devH == (float)(h * k);
    if (!exact && (!pixUp_ || upW_ != w * k || upH_ != h * k)) {
        rel(pixUp_);
        const D2D1_BITMAP_PROPERTIES1 bp1 = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(dc_->CreateBitmap(D2D1::SizeU(w * k, h * k), nullptr, 0, bp1, &pixUp_))) {
            upW_ = w * k;
            upH_ = h * k;
        }
    }
    if (exact || !pixUp_) {
        dc_->DrawBitmap(pix_, dst, 1.f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
    } else {
        ID2D1Image* old = nullptr;
        dc_->GetTarget(&old);
        dc_->SetTarget(pixUp_);
        dc_->DrawBitmap(pix_, D2D1::RectF(0, 0, (float)upW_, (float)upH_), 1.f,
                        D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
        dc_->SetTarget(old);
        if (old) old->Release();
        dc_->DrawBitmap(pixUp_, dst, 1.f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
    }
    dc_->SetTransform(saved);
}

void Gfx::applyTransform() {
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(vp_.scale, vp_.scale) *
                      D2D1::Matrix3x2F::Translation(vp_.offX, vp_.offY));
}

bool Gfx::beginFrame(const Color& clear) {
    if (w_ == 0 || h_ == 0 || vp_.scale <= 0) return false;
    if (deviceLost_) {
        releaseDevice();
        if (!createDevice()) return false;
    }
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear(dcol(clear));
    applyTransform();
    return true;
}

void Gfx::endFrame() {
    HRESULT hr = dc_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        deviceLost_ = true;
        return;
    }
    if (!swap_) return;
    hr = swap_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) deviceLost_ = true;
}

ID2D1SolidColorBrush* Gfx::brush(Color c) {
    brush_->SetColor(dcol(c));
    return brush_;
}

void Gfx::fillRect(RectF r, Color c) { dc_->FillRectangle(drect(r), brush(c)); }

void Gfx::fillRoundRect(RectF r, float radius, Color c) {
    dc_->FillRoundedRectangle({drect(r), radius, radius}, brush(c));
}

void Gfx::strokeRect(RectF r, Color c, float width) {
    float h = width * 0.5f;
    dc_->DrawRectangle({r.x + h, r.y + h, r.x + r.w - h, r.y + r.h - h}, brush(c), width);
}

void Gfx::fillGradientV(RectF r, Color top, Color bottom) { fillRoundGradientV(r, 0.f, top, bottom); }

void Gfx::fillRoundGradientV(RectF r, float radius, Color top, Color bottom) {
    fillGradient(r, radius, top, bottom, false);
}

void Gfx::fillGradientH(RectF r, Color left, Color right) { fillGradient(r, 0.f, left, right, true); }

void Gfx::fillGradient(RectF r, float radius, Color top, Color bottom, bool horizontal) {
    uint32_t k0 = pack(top), k1 = pack(bottom);
    ID2D1GradientStopCollection* stops = nullptr;
    for (auto& g : grad_)
        if (g.stops && g.key[0] == k0 && g.key[1] == k1) stops = g.stops;
    if (!stops) {
        D2D1_GRADIENT_STOP s[2] = {{0.f, dcol(top)}, {1.f, dcol(bottom)}};
        if (FAILED(dc_->CreateGradientStopCollection(s, 2, &stops))) return;
        GradEntry& e = grad_[gradNext_];
        gradNext_ = (gradNext_ + 1) % 32;
        rel(e.stops);
        e = {{k0, k1}, stops};
    }
    ID2D1LinearGradientBrush* lb = nullptr;
    const D2D1_POINT_2F end = horizontal ? D2D1_POINT_2F{r.x + r.w, r.y} : D2D1_POINT_2F{r.x, r.y + r.h};
    if (SUCCEEDED(dc_->CreateLinearGradientBrush({{r.x, r.y}, end}, stops, &lb))) {
        if (radius > 0)
            dc_->FillRoundedRectangle({drect(r), radius, radius}, lb);
        else
            dc_->FillRectangle(drect(r), lb);
        lb->Release();
    }
}

void Gfx::well(RectF r, const Theme& t, Color fill) {
    const float rad = t.corner;
    dc_->FillRoundedRectangle({drect(r), rad, rad}, brush(fill));
    // inner shadow along the top edge, light rim along the bottom
    fillRoundGradientV({r.x + 1, r.y + 1, r.w - 2, 6.f}, 0.f, withAlpha({0, 0, 0, 1}, 0.35f), {0, 0, 0, 0});
    dc_->DrawRoundedRectangle({{r.x + 0.5f, r.y + 0.5f, r.x + r.w - 0.5f, r.y + r.h - 0.5f}, rad, rad},
                              brush(withAlpha(t.panelDark, 0.9f)), 1.f);
    line(r.x + rad, r.y + r.h + 0.5f, r.x + r.w - rad, r.y + r.h + 0.5f, withAlpha(t.panelLight, 0.7f), 1.f);
}

void Gfx::line(float x0, float y0, float x1, float y1, Color c, float width) {
    dc_->DrawLine({x0, y0}, {x1, y1}, brush(c), width);
}

void Gfx::polyline(const float* xy, int points, Color c, float width) {
    if (points < 2) return;
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory_->CreatePathGeometry(&geo))) return;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink))) {
        sink->BeginFigure({xy[0], xy[1]}, D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i < points; ++i) sink->AddLine({xy[i * 2], xy[i * 2 + 1]});
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        dc_->DrawGeometry(geo, brush(c), width);
    }
    geo->Release();
}

void Gfx::bevel(RectF r, const Theme& t, bool pressed) { panel(r, t, pressed ? PanelState::Pressed : PanelState::Normal); }

void Gfx::panel(RectF r, const Theme& t, PanelState s) {
    const float rad = t.corner, g = t.bevelGradient;
    Color top, bot;
    switch (s) {
        case PanelState::Pressed:
            top = mixColor(t.panel, t.panelDark, 0.35f * g + 0.1f);
            bot = mixColor(t.panel, t.panelLight, 0.2f * g);
            break;
        case PanelState::Hover:
            top = mixColor(t.panel, t.panelLight, 0.85f * g + 0.1f);
            bot = mixColor(t.panel, t.panelLight, 0.15f);
            break;
        case PanelState::Active:
            top = mixColor(t.digits, t.panelLight, 0.55f);
            bot = mixColor(t.digits, t.panelLight, 0.2f);
            break;
        default:
            top = mixColor(t.panel, t.panelLight, 0.65f * g + 0.05f);
            bot = mixColor(t.panel, t.panelDark, 0.25f * g);
            break;
    }
    fillRoundGradientV(r, rad, top, bot);
    if (s != PanelState::Pressed)
        line(r.x + rad, r.y + 1.5f, r.x + r.w - rad, r.y + 1.5f, withAlpha(t.panelLight, 0.95f), 1.f);
    dc_->DrawRoundedRectangle({{r.x + 0.5f, r.y + 0.5f, r.x + r.w - 0.5f, r.y + r.h - 0.5f}, rad, rad},
                              brush(withAlpha(t.panelDark, 0.75f)), 1.f);
}

void Gfx::beginGlow() {
    if (!glowTarget_ || inGlow_) return;
    inGlow_ = true;
    dc_->SetTarget(glowTarget_);
    D2D1_MATRIX_3X2_F m;
    dc_->GetTransform(&m);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear({0, 0, 0, 0});
    dc_->SetTransform(m);
}

void Gfx::endGlow(float radiusLogical, float strength) {
    if (!inGlow_) return;
    inGlow_ = false;
    dc_->SetTarget(target_);
    D2D1_MATRIX_3X2_F m;
    dc_->GetTransform(&m);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    ID2D1Effect* blur = nullptr;
    ID2D1Effect* cm = nullptr;
    if (strength > 0 && SUCCEEDED(dc_->CreateEffect(CLSID_D2D1GaussianBlur, &blur)) &&
        SUCCEEDED(dc_->CreateEffect(CLSID_D2D1ColorMatrix, &cm))) {
        blur->SetInput(0, glowTarget_);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, radiusLogical * vp_.scale);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
        cm->SetInputEffect(0, blur);
        D2D1_MATRIX_5X4_F mat = D2D1::Matrix5x4F(strength, 0, 0, 0, 0, strength, 0, 0, 0, 0, strength, 0, 0, 0, 0,
                                                 strength, 0, 0, 0, 0);
        cm->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, mat);
        dc_->DrawImage(cm, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
    }
    rel(cm);
    rel(blur);
    dc_->DrawImage(glowTarget_, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    dc_->SetTransform(m);
}

void Gfx::pushClip(RectF r) { dc_->PushAxisAlignedClip(drect(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); }

void Gfx::popClip() { dc_->PopAxisAlignedClip(); }

void Gfx::pushRoundedClip(RectF r, float radius) {
    ID2D1RoundedRectangleGeometry* geo = nullptr;
    factory_->CreateRoundedRectangleGeometry({drect(r), radius, radius}, &geo);
    dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), geo), nullptr);
    if (geo) geo->Release();  // the layer keeps its own reference while pushed
}

void Gfx::popLayer() { dc_->PopLayer(); }
