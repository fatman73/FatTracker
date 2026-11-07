// Renderer: Direct2D 1.1 on a DXGI flip-model swap chain (Windows, gfx.cpp) or Core Graphics into a
// bitmap (macOS, src/mac/gfx_mac.cpp). All drawing calls take logical (1920x1080 canvas) coordinates;
// the viewport transform maps them to pixels.
#pragma once
#include <cstddef>
#include <cstdint>

#include "platform/vk.h"  // VK_* key codes for the UI (on Windows also <windows.h>)
#ifdef _WIN32
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#else
typedef struct CGContext* CGContextRef;
typedef struct CGImage* CGImageRef;
typedef struct CGColorSpace* CGColorSpaceRef;
#endif

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
    Gfx(const Gfx&) = delete;
    Gfx& operator=(const Gfx&) = delete;
#ifdef _WIN32
    bool init(HWND hwnd);
#endif
    bool initOffscreen(uint32_t w, uint32_t h);  // dev screenshots (--shot); on macOS also the window's bitmap
    bool savePng(const wchar_t* path);
    void resize(uint32_t w, uint32_t h);
    bool beginFrame(const Color& clear);  // false when minimized / device lost
    void endFrame();
    const Viewport& viewport() const { return vp_; }
    float deviceScale() const { return vp_.scale; }
#ifdef _WIN32
    ID2D1DeviceContext* dc() { return dc_; }
    ID2D1Factory1* factory() { return factory_; }
#else
    // for Text and Font; drawing straight into the glow layer is not tracked, so that glow is blurred whole
    CGContextRef cg() {
        if (ctx_ && ctx_ == glowCtx_) glowAll_ = true;
        return ctx_;
    }
    CGImageRef copyImage();             // the finished frame (caller releases it), for the window's layer
    // A frame that redraws only `logical` (grown to whole pixels); the rest of the bitmap stays as it was.
    bool beginFrameIn(const Color& clear, RectF logical);
    // The color space the bitmap is in (default sRGB). The window's own space: Core Animation then shows the
    // frame as it is, instead of converting every pixel of every frame on the CPU. Colors stay sRGB values.
    void setColorSpace(CGColorSpaceRef space);
    // After beginFrameIn: the redrawn part in bitmap pixels (top-left origin), and a copy of just those pixels
    // (the window shows it as a small layer over the last full frame). False after a full frame.
    bool partialRect(int& x, int& y, int& w, int& h) const;
    CGImageRef copyPartialImage();
#endif
    uint32_t generation() const { return generation_; }  // bumps when the device is recreated

    void fillRect(RectF r, Color c);
    void fillRoundRect(RectF r, float radius, Color c);
    void fillGradientV(RectF r, Color top, Color bottom);
    void strokeRect(RectF r, Color c, float width);
    void line(float x0, float y0, float x1, float y1, Color c, float width);
    void polyline(const float* xy, int points, Color c, float width);
    void bevel(RectF r, const Theme& t, bool pressed);
    enum class PanelState { Normal, Hover, Pressed, Active };
    void panel(RectF r, const Theme& t, PanelState s);       // raised, rounded, soft gradient
    void well(RectF r, const Theme& t, Color fill);          // sunken field (values, scopes, pattern)
    void fillRoundGradientV(RectF r, float radius, Color top, Color bottom);
    void fillGradientH(RectF r, Color left, Color right);
    void fillGradient(RectF r, float radius, Color c0, Color c1, bool horizontal);
    void beginGlow();
    void endGlow(float radiusLogical, float strength);
    void pushClip(RectF r);
    void popClip();
    void pushRoundedClip(RectF r, float radius);  // anti-aliased rounded mask (layer)
    void popLayer();
#ifdef _WIN32
    ID2D1SolidColorBrush* brush(Color c);
#endif
    // Pure mode: a 32-bit pixel image stretched over a device-pixel rectangle. Pixels stay sharp:
    // the image is first enlarged by a whole factor without smoothing, then fitted with a light filter.
    void drawPixels(const uint32_t* argb, uint32_t w, uint32_t h, float devX, float devY, float devW, float devH);

private:
#ifdef _WIN32
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
    ID2D1Bitmap* pix_ = nullptr;
    ID2D1Bitmap1* pixUp_ = nullptr;  // pix_ enlarged by an integer factor
    uint32_t pixW_ = 0, pixH_ = 0, upW_ = 0, upH_ = 0;
    bool inGlow_ = false;
    bool deviceLost_ = false;
    struct GradEntry {
        uint32_t key[2];
        ID2D1GradientStopCollection* stops;
    };
    GradEntry grad_[32] = {};
    int gradNext_ = 0;
#else
    void release();
    void applyTransform();
    void fillPath(void* path, Color c);  // CGPathRef
    uint32_t w_ = 0, h_ = 0;
    Viewport vp_{0, 0, 0};
    uint32_t generation_ = 0;
    CGContextRef ctx_ = nullptr;      // the frame (device pixels, top-left origin)
    CGContextRef main_ = nullptr;     // the frame's own context while ctx_ points at the glow layer
    CGContextRef glowCtx_ = nullptr;  // beginGlow .. endGlow draw here
    CGColorSpaceRef space_ = nullptr; // the bitmap's color space (retained; nullptr = sRGB)
    bool partial_ = false;            // the last frame was a beginFrameIn frame
    int partX_ = 0, partY_ = 0, partW_ = 0, partH_ = 0;
    void* glowTmp_ = nullptr;         // blur scratch: two buffers of glowTmpBytes_
    size_t glowTmpBytes_ = 0;
    int layers_ = 0;                  // pushRoundedClip depth (each is a saved graphics state)
    // what the glow layer holds, in bitmap pixels (top-left origin): only that part is blurred and composited
    void noteGlow(float x, float y, float w, float h, float pad);  // a user-space rectangle drawn into the glow
    int glowX0_ = 0, glowY0_ = 0, glowX1_ = 0, glowY1_ = 0;       // empty when x1 <= x0
    bool glowAll_ = false;                                         // something untracked: the whole layer
    int clearX0_ = 0, clearY0_ = 0, clearX1_ = 0, clearY1_ = 0;   // what the next beginGlow must clear
#endif
};
