// Direct2D 1.1 renderer on a DXGI flip-model swap chain. All drawing calls take
// logical (1920x1080 canvas) coordinates; the viewport transform maps them to pixels.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include "ui/theme.h"
#include "ui/viewport.h"

struct RectF {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Gfx {
public:
    Gfx() = default;
    ~Gfx();
    bool init(HWND hwnd);
    bool initOffscreen(uint32_t w, uint32_t h);  // dev screenshots (--shot)
    bool savePng(const wchar_t* path);
    void resize(uint32_t w, uint32_t h);
    bool beginFrame(const Color& clear);  // false when minimized / device lost
    void endFrame();
    const Viewport& viewport() const { return vp_; }
    float deviceScale() const { return vp_.scale; }
    ID2D1DeviceContext* dc() { return dc_; }
    ID2D1Factory1* factory() { return factory_; }
    uint32_t generation() const { return generation_; }  // bumps when the device is recreated

    void fillRect(RectF r, Color c);
    void fillRoundRect(RectF r, float radius, Color c);
    void fillGradientV(RectF r, Color top, Color bottom);
    void strokeRect(RectF r, Color c, float width);
    void line(float x0, float y0, float x1, float y1, Color c, float width);
    void polyline(const float* xy, int points, Color c, float width);
    void bevel(RectF r, const Theme& t, bool pressed);
    void beginGlow();
    void endGlow(float radiusLogical, float strength);
    void pushClip(RectF r);
    void popClip();
    ID2D1SolidColorBrush* brush(Color c);

private:
    bool createDevice();
    void releaseDevice();
    bool createTargets();
    void releaseTargets();
    void applyTransform();

    HWND hwnd_ = nullptr;
    uint32_t w_ = 0, h_ = 0;
    Viewport vp_{0, 0, 0};
    uint32_t generation_ = 0;
    ID3D11Device* d3d_ = nullptr;
    IDXGISwapChain1* swap_ = nullptr;
    ID2D1Factory1* factory_ = nullptr;
    ID2D1Device* d2dDevice_ = nullptr;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1Bitmap1* target_ = nullptr;
    ID2D1Bitmap1* glowTarget_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    bool inGlow_ = false;
    bool deviceLost_ = false;
    struct GradEntry {
        uint32_t key[2];
        ID2D1GradientStopCollection* stops;
    };
    GradEntry grad_[32] = {};
    int gradNext_ = 0;
};
