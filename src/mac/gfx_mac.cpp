// Gfx on Core Graphics (macOS): every frame is drawn into a bitmap (device pixels, top-left origin, the
// viewport scale on top), like the Direct2D target on Windows. The window shows the finished frame
// (copyImage), --shot saves it as PNG.
#include "ui/gfx.h"

#include <Accelerate/Accelerate.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
CGColorSpaceRef srgb() {
    static CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    return cs;
}

uint32_t pack(Color c) {
    auto b = [](float v) { return (uint32_t)(v < 0 ? 0 : (v > 1 ? 255 : v * 255.f + 0.5f)); };
    return (b(c.r) << 24) | (b(c.g) << 16) | (b(c.b) << 8) | b(c.a);
}

void setFill(CGContextRef ctx, Color c) {
    const CGFloat comp[4] = {c.r, c.g, c.b, c.a};
    CGContextSetFillColorSpace(ctx, srgb());
    CGContextSetFillColor(ctx, comp);
}

void setStroke(CGContextRef ctx, Color c, float width) {
    const CGFloat comp[4] = {c.r, c.g, c.b, c.a};
    CGContextSetStrokeColorSpace(ctx, srgb());
    CGContextSetStrokeColor(ctx, comp);
    CGContextSetLineWidth(ctx, width);
}

CGRect cgr(RectF r) { return CGRectMake(r.x, r.y, r.w, r.h); }

// CGPathCreateWithRoundedRect asserts radius <= half the size
CGPathRef roundedPath(CGRect r, float radius) {
    const CGFloat k = std::max<CGFloat>(0, std::min<CGFloat>(radius, std::min(r.size.width, r.size.height) * 0.5));
    return CGPathCreateWithRoundedRect(r, k, k, nullptr);
}

// The two contexts the frame needs: 32-bit, little-endian (BGRA in memory like DXGI_FORMAT_B8G8R8A8).
CGContextRef makeContext(uint32_t w, uint32_t h, bool alpha, CGColorSpaceRef space) {
    const uint32_t info = (uint32_t)kCGBitmapByteOrder32Little |
                          (uint32_t)(alpha ? kCGImageAlphaPremultipliedFirst : kCGImageAlphaNoneSkipFirst);
    CGContextRef c = CGBitmapContextCreate(nullptr, w, h, 8, 0, space ? space : srgb(), info);
    if (!c) return nullptr;
    CGContextTranslateCTM(c, 0, h);  // top-left origin, y down, like Direct2D
    CGContextScaleCTM(c, 1, -1);
    CGContextSetShouldAntialias(c, true);
    CGContextSetInterpolationQuality(c, kCGInterpolationLow);
    return c;
}

// Device space with the top-left origin (undoes the viewport scale, keeps the flip).
void toDevice(CGContextRef c, uint32_t h) {
    CGContextConcatCTM(c, CGAffineTransformInvert(CGContextGetCTM(c)));
    CGContextTranslateCTM(c, 0, h);
    CGContextScaleCTM(c, 1, -1);
}

struct GradCache {
    uint32_t key[2];
    CGGradientRef g;
};
GradCache g_grad[32] = {};
int g_gradNext = 0;

CGGradientRef gradient(Color c0, Color c1) {
    const uint32_t k0 = pack(c0), k1 = pack(c1);
    for (auto& e : g_grad)
        if (e.g && e.key[0] == k0 && e.key[1] == k1) return e.g;
    const CGFloat comp[8] = {c0.r, c0.g, c0.b, c0.a, c1.r, c1.g, c1.b, c1.a};
    const CGFloat loc[2] = {0, 1};
    CGGradientRef g = CGGradientCreateWithColorComponents(srgb(), comp, loc, 2);
    GradCache& slot = g_grad[g_gradNext];
    g_gradNext = (g_gradNext + 1) % 32;
    if (slot.g) CGGradientRelease(slot.g);
    slot = {{k0, k1}, g};
    return g;
}

// Pure mode pixels: scratch for the whole-factor enlargement
CGContextRef g_up = nullptr;
uint32_t g_upW = 0, g_upH = 0;
}  // namespace

Gfx::~Gfx() {
    release();
    if (space_) CGColorSpaceRelease(space_);
}

void Gfx::setColorSpace(CGColorSpaceRef space) {
    if (space == space_ || (space && space_ && CFEqual(space, space_))) return;
    if (space_) CGColorSpaceRelease(space_);
    space_ = space ? CGColorSpaceRetain(space) : nullptr;
    if (main_) initOffscreen(w_, h_);  // the bitmaps again, in the new space (the next frame redraws all)
}

void Gfx::release() {
    if (ctx_ && main_ && ctx_ != main_) ctx_ = main_;
    if (main_) CGContextRelease(main_);
    if (glowCtx_) CGContextRelease(glowCtx_);
    std::free(glowTmp_);
    main_ = ctx_ = glowCtx_ = nullptr;
    glowTmp_ = nullptr;
    glowTmpBytes_ = 0;
    clearX0_ = clearY0_ = clearX1_ = clearY1_ = 0;
}

bool Gfx::initOffscreen(uint32_t w, uint32_t h) {
    release();
    w_ = w;
    h_ = h;
    vp_ = computeViewport(w, h);
    ++generation_;
    if (!w || !h) return false;
    main_ = makeContext(w, h, false, space_);
    glowCtx_ = makeContext(w, h, true, space_);
    clearX0_ = clearY0_ = 0, clearX1_ = (int)w, clearY1_ = (int)h;  // the first glow starts from a cleared layer
    ctx_ = main_;
    return main_ && glowCtx_;
}

void Gfx::resize(uint32_t w, uint32_t h) {
    if (w == w_ && h == h_ && main_) return;
    initOffscreen(w, h);
}

void Gfx::applyTransform() {
    CGContextTranslateCTM(ctx_, vp_.offX, vp_.offY);
    CGContextScaleCTM(ctx_, vp_.scale, vp_.scale);
}

bool Gfx::beginFrame(const Color& clear) {
    if (!main_ || w_ == 0 || h_ == 0 || vp_.scale <= 0) return false;
    partial_ = false;
    ctx_ = main_;
    layers_ = 0;
    CGContextSaveGState(ctx_);  // the frame's base state; endFrame restores it
    setFill(ctx_, {clear.r, clear.g, clear.b, 1.f});
    CGContextFillRect(ctx_, CGRectMake(0, 0, w_, h_));
    applyTransform();
    return true;
}

bool Gfx::beginFrameIn(const Color& clear, RectF r) {
    if (!main_ || w_ == 0 || h_ == 0 || vp_.scale <= 0) return false;
    ctx_ = main_;
    layers_ = 0;
    CGContextSaveGState(ctx_);
    // the part to redraw in bitmap pixels, whole pixels so its edge pixels come out as in a full frame
    const float x0 = std::floor(vp_.offX + r.x * vp_.scale), y0 = std::floor(vp_.offY + r.y * vp_.scale);
    const float x1 = std::ceil(vp_.offX + (r.x + r.w) * vp_.scale), y1 = std::ceil(vp_.offY + (r.y + r.h) * vp_.scale);
    const float cx0 = std::max(0.f, x0), cy0 = std::max(0.f, y0);
    const float cx1 = std::min((float)w_, x1), cy1 = std::min((float)h_, y1);
    const CGRect part = CGRectMake(cx0, cy0, std::max(0.f, cx1 - cx0), std::max(0.f, cy1 - cy0));  // top-left origin
    partial_ = true;
    partX_ = (int)cx0, partY_ = (int)cy0, partW_ = (int)part.size.width, partH_ = (int)part.size.height;
    CGContextClipToRect(ctx_, part);
    setFill(ctx_, {clear.r, clear.g, clear.b, 1.f});
    CGContextFillRect(ctx_, part);
    applyTransform();
    return true;
}

void Gfx::endFrame() {
    if (!main_) return;
    ctx_ = main_;
    while (layers_ > 0) {  // unbalanced clips: restore them with the frame
        CGContextRestoreGState(ctx_);
        --layers_;
    }
    CGContextRestoreGState(ctx_);
    CGContextFlush(ctx_);
}

CGImageRef Gfx::copyImage() { return main_ ? CGBitmapContextCreateImage(main_) : nullptr; }

bool Gfx::partialRect(int& x, int& y, int& w, int& h) const {
    if (!partial_ || partW_ <= 0 || partH_ <= 0) return false;
    x = partX_, y = partY_, w = partW_, h = partH_;
    return true;
}

CGImageRef Gfx::copyPartialImage() {
    if (!main_ || !partial_ || partW_ <= 0 || partH_ <= 0) return nullptr;
    const size_t row = CGBitmapContextGetBytesPerRow(main_);
    uint8_t* at = (uint8_t*)CGBitmapContextGetData(main_) + (size_t)partY_ * row + (size_t)partX_ * 4;
    CGContextRef c = CGBitmapContextCreate(at, partW_, partH_, 8, row, space_ ? space_ : srgb(),
                                           (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaNoneSkipFirst);
    if (!c) return nullptr;
    CGImageRef img = CGBitmapContextCreateImage(c);  // a copy of just these pixels
    CGContextRelease(c);
    return img;
}

bool Gfx::savePng(const wchar_t* path) {
    CGImageRef img = copyImage();
    if (!img) return false;
    const std::string p = std::filesystem::path(path).string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)p.c_str(), (CFIndex)p.size(), false);
    bool ok = false;
    if (url) {
        CGImageDestinationRef d = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
        if (d) {
            CGImageDestinationAddImage(d, img, nullptr);
            ok = CGImageDestinationFinalize(d);
            CFRelease(d);
        }
        CFRelease(url);
    }
    CGImageRelease(img);
    return ok;
}

void Gfx::noteGlow(float x, float y, float w, float h, float pad) {
    if (!glowCtx_ || ctx_ != glowCtx_) return;
    // user space -> device space (y up from the bottom) -> bitmap rows (top-left), plus antialiasing
    const CGRect d = CGRectApplyAffineTransform(CGRectMake(x - pad, y - pad, w + 2 * pad, h + 2 * pad), CGContextGetCTM(ctx_));
    const int x0 = (int)std::floor(CGRectGetMinX(d)) - 2, x1 = (int)std::ceil(CGRectGetMaxX(d)) + 2;
    const int y0 = (int)h_ - (int)std::ceil(CGRectGetMaxY(d)) - 2, y1 = (int)h_ - (int)std::floor(CGRectGetMinY(d)) + 2;
    if (glowX1_ <= glowX0_) {
        glowX0_ = x0, glowY0_ = y0, glowX1_ = x1, glowY1_ = y1;
    } else {
        glowX0_ = std::min(glowX0_, x0), glowY0_ = std::min(glowY0_, y0);
        glowX1_ = std::max(glowX1_, x1), glowY1_ = std::max(glowY1_, y1);
    }
}

void Gfx::fillRect(RectF r, Color c) {
    noteGlow(r.x, r.y, r.w, r.h, 0);
    setFill(ctx_, c);
    CGContextFillRect(ctx_, cgr(r));
}

void Gfx::fillPath(void* path, Color c) {
    const CGRect b = CGPathGetBoundingBox((CGPathRef)path);
    noteGlow(b.origin.x, b.origin.y, b.size.width, b.size.height, 0);
    setFill(ctx_, c);
    CGContextAddPath(ctx_, (CGPathRef)path);
    CGContextFillPath(ctx_);
}

void Gfx::fillRoundRect(RectF r, float radius, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    CGPathRef p = roundedPath(cgr(r), radius);
    fillPath((void*)p, c);
    CGPathRelease(p);
}

void Gfx::strokeRect(RectF r, Color c, float width) {
    noteGlow(r.x, r.y, r.w, r.h, width);
    const float h = width * 0.5f;
    setStroke(ctx_, c, width);
    CGContextStrokeRect(ctx_, CGRectMake(r.x + h, r.y + h, r.w - width, r.h - width));
}

void Gfx::fillGradientV(RectF r, Color top, Color bottom) { fillRoundGradientV(r, 0.f, top, bottom); }

void Gfx::fillRoundGradientV(RectF r, float radius, Color top, Color bottom) {
    fillGradient(r, radius, top, bottom, false);
}

void Gfx::fillGradientH(RectF r, Color left, Color right) { fillGradient(r, 0.f, left, right, true); }

void Gfx::fillGradient(RectF r, float radius, Color c0, Color c1, bool horizontal) {
    if (r.w <= 0 || r.h <= 0) return;
    noteGlow(r.x, r.y, r.w, r.h, 0);
    CGGradientRef g = gradient(c0, c1);
    if (!g) return;
    CGContextSaveGState(ctx_);
    if (radius > 0) {
        CGPathRef p = roundedPath(cgr(r), radius);
        CGContextAddPath(ctx_, p);
        CGContextClip(ctx_);
        CGPathRelease(p);
    } else {
        CGContextClipToRect(ctx_, cgr(r));
    }
    const CGPoint end = horizontal ? CGPointMake(r.x + r.w, r.y) : CGPointMake(r.x, r.y + r.h);
    CGContextDrawLinearGradient(ctx_, g, CGPointMake(r.x, r.y), end,
                                kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGContextRestoreGState(ctx_);
}

void Gfx::line(float x0, float y0, float x1, float y1, Color c, float width) {
    noteGlow(std::min(x0, x1), std::min(y0, y1), std::fabs(x1 - x0), std::fabs(y1 - y0), width);
    setStroke(ctx_, c, width);
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, x0, y0);
    CGContextAddLineToPoint(ctx_, x1, y1);
    CGContextStrokePath(ctx_);
}

void Gfx::polyline(const float* xy, int points, Color c, float width) {
    if (points < 2) return;
    if (ctx_ == glowCtx_) {
        float x0 = xy[0], y0 = xy[1], x1 = x0, y1 = y0;
        for (int i = 1; i < points; ++i) {
            x0 = std::min(x0, xy[i * 2]), x1 = std::max(x1, xy[i * 2]);
            y0 = std::min(y0, xy[i * 2 + 1]), y1 = std::max(y1, xy[i * 2 + 1]);
        }
        noteGlow(x0, y0, x1 - x0, y1 - y0, width);
    }
    setStroke(ctx_, c, width);
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, xy[0], xy[1]);
    for (int i = 1; i < points; ++i) CGContextAddLineToPoint(ctx_, xy[i * 2], xy[i * 2 + 1]);
    CGContextStrokePath(ctx_);
}

void Gfx::bevel(RectF r, const Theme& t, bool pressed) { panel(r, t, pressed ? PanelState::Pressed : PanelState::Normal); }

namespace {
void strokeRounded(CGContextRef ctx, RectF r, float rad, Color c) {
    if (r.w <= 1 || r.h <= 1) return;
    setStroke(ctx, c, 1.f);
    CGPathRef p = roundedPath(CGRectMake(r.x + 0.5f, r.y + 0.5f, r.w - 1.f, r.h - 1.f), rad);
    CGContextAddPath(ctx, p);
    CGContextStrokePath(ctx);
    CGPathRelease(p);
}
}  // namespace

void Gfx::well(RectF r, const Theme& t, Color fill) {
    const float rad = t.corner;
    fillRoundRect(r, rad, fill);
    // inner shadow along the top edge, light rim along the bottom
    fillRoundGradientV({r.x + 1, r.y + 1, r.w - 2, 6.f}, 0.f, withAlpha({0, 0, 0, 1}, 0.35f), {0, 0, 0, 0});
    strokeRounded(ctx_, r, rad, withAlpha(t.panelDark, 0.9f));
    line(r.x + rad, r.y + r.h + 0.5f, r.x + r.w - rad, r.y + r.h + 0.5f, withAlpha(t.panelLight, 0.7f), 1.f);
}

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
    strokeRounded(ctx_, r, rad, withAlpha(t.panelDark, 0.75f));
}

void Gfx::beginGlow() {
    if (!glowCtx_ || ctx_ != main_) return;
    // the glow layer gets the same transform and clip-free state; it starts transparent (only the part the last
    // glow drew is cleared: the rest is still transparent)
    CGContextSaveGState(glowCtx_);
    CGContextConcatCTM(glowCtx_, CGAffineTransformInvert(CGContextGetCTM(glowCtx_)));  // device space, y up
    if (clearX1_ > clearX0_ && clearY1_ > clearY0_)
        CGContextClearRect(glowCtx_, CGRectMake(clearX0_, (CGFloat)h_ - clearY1_, clearX1_ - clearX0_, clearY1_ - clearY0_));
    clearX0_ = clearY0_ = clearX1_ = clearY1_ = 0;
    CGContextConcatCTM(glowCtx_, CGContextGetCTM(main_));
    glowX0_ = glowY0_ = glowX1_ = glowY1_ = 0;
    glowAll_ = false;
    ctx_ = glowCtx_;
}

// The glow: the layer blurred (a Gaussian from three box blurs) added onto the frame, then the sharp layer on
// top. Only the part of the layer that was drawn on is worked on - that part plus twice the blur's reach, so the
// blurred pixels are exactly those of blurring the whole layer (the layer is transparent everywhere else).
void Gfx::endGlow(float radiusLogical, float strength) {
    if (ctx_ != glowCtx_) return;
    CGContextRestoreGState(glowCtx_);
    ctx_ = main_;
    CGContextFlush(glowCtx_);
    const float sigma = radiusLogical * vp_.scale;
    const bool blur = strength > 0 && sigma > 0.5f;
    uint32_t k = 1;
    if (blur) {
        k = (uint32_t)std::lround(std::sqrt(12.f * sigma * sigma / 3.f + 1.f));
        if (!(k & 1)) ++k;
    }
    const int reach = 3 * (int)(k / 2);  // how far three box blurs spread a pixel
    int x0, y0, x1, y1;
    if (glowAll_) {
        x0 = 0, y0 = 0, x1 = (int)w_, y1 = (int)h_;
    } else {
        if (glowX1_ <= glowX0_ || glowY1_ <= glowY0_) return;  // nothing glows
        const int m = 2 * reach + 2;
        x0 = std::max(0, glowX0_ - m), y0 = std::max(0, glowY0_ - m);
        x1 = std::min((int)w_, glowX1_ + m), y1 = std::min((int)h_, glowY1_ + m);
    }
    clearX0_ = x0, clearY0_ = y0, clearX1_ = x1, clearY1_ = y1;
    if (x1 <= x0 || y1 <= y0) return;
    {  // a partial frame that does not reach the glow: nothing to blur or composite
        CGContextSaveGState(main_);
        CGContextConcatCTM(main_, CGAffineTransformInvert(CGContextGetCTM(main_)));
        const CGRect clip = CGContextGetClipBoundingBox(main_);
        CGContextRestoreGState(main_);
        if (!CGRectIntersectsRect(clip, CGRectMake(x0, (CGFloat)h_ - y1, x1 - x0, y1 - y0))) return;
    }
    const size_t rw = (size_t)(x1 - x0), rh = (size_t)(y1 - y0), rowBytes = rw * 4;
    const size_t layerRow = CGBitmapContextGetBytesPerRow(glowCtx_);
    uint8_t* const region = (uint8_t*)CGBitmapContextGetData(glowCtx_) + (size_t)y0 * layerRow + (size_t)x0 * 4;
    const uint32_t info = (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst;
    auto imageOf = [&](void* data, size_t row) -> CGImageRef {
        CGContextRef c = CGBitmapContextCreate(data, rw, rh, 8, row, space_ ? space_ : srgb(), info);
        if (!c) return nullptr;
        CGImageRef img = CGBitmapContextCreateImage(c);  // a copy of the pixels
        CGContextRelease(c);
        return img;
    };
    CGImageRef blurred = nullptr;
    if (blur) {
        const size_t bytes = rowBytes * rh;
        if (bytes > glowTmpBytes_) {
            std::free(glowTmp_);
            glowTmp_ = std::malloc(bytes * 2);
            glowTmpBytes_ = glowTmp_ ? bytes : 0;
        }
        if (glowTmp_) {
            uint8_t* a = (uint8_t*)glowTmp_;
            uint8_t* b = a + glowTmpBytes_;
            for (size_t y = 0; y < rh; ++y) std::memcpy(a + y * rowBytes, region + y * layerRow, rowBytes);
            vImage_Buffer A = {a, rh, rw, rowBytes}, B = {b, rh, rw, rowBytes};
            vImageBoxConvolve_ARGB8888(&A, &B, nullptr, 0, 0, k, k, nullptr, kvImageEdgeExtend);
            vImageBoxConvolve_ARGB8888(&B, &A, nullptr, 0, 0, k, k, nullptr, kvImageEdgeExtend);
            vImageBoxConvolve_ARGB8888(&A, &B, nullptr, 0, 0, k, k, nullptr, kvImageEdgeExtend);
            blurred = imageOf(b, rowBytes);
        }
    }
    CGImageRef sharp = imageOf(region, layerRow);
    CGContextSaveGState(main_);
    CGContextConcatCTM(main_, CGAffineTransformInvert(CGContextGetCTM(main_)));  // device space, bottom-left
    const CGRect where = CGRectMake(x0, (CGFloat)h_ - y1, rw, rh);
    if (blurred) {
        CGContextSaveGState(main_);
        CGContextSetBlendMode(main_, kCGBlendModePlusLighter);
        CGContextSetAlpha(main_, std::min(strength, 1.f));
        CGContextDrawImage(main_, where, blurred);
        if (strength > 1.f) {  // the Windows color matrix scales past 1: add the rest
            CGContextSetAlpha(main_, std::min(strength - 1.f, 1.f));
            CGContextDrawImage(main_, where, blurred);
        }
        CGContextRestoreGState(main_);
        CGImageRelease(blurred);
    }
    if (sharp) {
        CGContextDrawImage(main_, where, sharp);
        CGImageRelease(sharp);
    }
    CGContextRestoreGState(main_);
}

void Gfx::pushClip(RectF r) {
    CGContextSaveGState(ctx_);
    CGContextClipToRect(ctx_, cgr(r));
}

void Gfx::popClip() { CGContextRestoreGState(ctx_); }

void Gfx::pushRoundedClip(RectF r, float radius) {
    CGContextSaveGState(ctx_);
    ++layers_;
    if (r.w <= 0 || r.h <= 0) {
        CGContextClipToRect(ctx_, CGRectZero);
        return;
    }
    CGPathRef p = roundedPath(cgr(r), radius);
    CGContextAddPath(ctx_, p);
    CGContextClip(ctx_);
    CGPathRelease(p);
}

void Gfx::popLayer() {
    if (layers_ > 0) --layers_;
    CGContextRestoreGState(ctx_);
}

void Gfx::drawPixels(const uint32_t* argb, uint32_t w, uint32_t h, float devX, float devY, float devW, float devH) {
    if (!ctx_ || !w || !h) return;
    if (ctx_ == glowCtx_) glowAll_ = true;  // device coordinates: not tracked
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, argb, (size_t)w * h * 4, nullptr);
    CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, srgb(),
                                   (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaNoneSkipFirst, dp, nullptr, false,
                                   kCGRenderingIntentDefault);
    CGDataProviderRelease(dp);
    if (!img) return;
    // whole factor that reaches the target size; the final fit then only shrinks a little
    const float need = devW / w > devH / h ? devW / w : devH / h;
    uint32_t k = (uint32_t)need + ((float)(uint32_t)need < need ? 1 : 0);
    k = std::clamp<uint32_t>(k, 1, 8);
    const bool exact = devW == (float)(w * k) && devH == (float)(h * k);
    CGImageRef draw = img;
    CGImageRef up = nullptr;
    if (!exact) {
        CGColorSpaceRef upSpace = space_ ? space_ : srgb();  // the frame's space: the small source gets converted
        if (!g_up || g_upW != w * k || g_upH != h * k || !CFEqual(CGBitmapContextGetColorSpace(g_up), upSpace)) {
            if (g_up) CGContextRelease(g_up);
            g_up = CGBitmapContextCreate(nullptr, w * k, h * k, 8, 0, upSpace,
                                         (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaNoneSkipFirst);
            g_upW = w * k;
            g_upH = h * k;
        }
        if (g_up) {
            CGContextSetInterpolationQuality(g_up, kCGInterpolationNone);
            CGContextDrawImage(g_up, CGRectMake(0, 0, g_upW, g_upH), img);
            up = CGBitmapContextCreateImage(g_up);
            if (up) draw = up;
        }
    }
    CGContextSaveGState(ctx_);
    toDevice(ctx_, h_);
    // CGContextDrawImage draws upright in a y-up space: flip locally around the target rectangle
    CGContextTranslateCTM(ctx_, devX, devY + devH);
    CGContextScaleCTM(ctx_, 1, -1);
    CGContextSetInterpolationQuality(ctx_, (exact || !up) ? kCGInterpolationNone : kCGInterpolationLow);
    CGContextDrawImage(ctx_, CGRectMake(0, 0, devW, devH), draw);
    CGContextRestoreGState(ctx_);
    if (up) CGImageRelease(up);
    CGImageRelease(img);
}
