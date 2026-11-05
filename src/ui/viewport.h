// Logical canvas: at least 1920x1080 units, scaled uniformly and extended in one
// dimension so that it always fills the whole window (no letterbox bars).
#pragma once
#include <cstdint>

constexpr float kCanvasW = 1920.f;
constexpr float kCanvasH = 1080.f;

struct Viewport {
    float scale;     // device pixels per logical unit
    float w, h;      // logical canvas size (>= 1920 x >= 1080, one of them exact)
    float offX = 0, offY = 0;  // always 0 (kept for the renderer's transform)
};

Viewport computeViewport(uint32_t winW, uint32_t winH);
void deviceToLogical(const Viewport& v, float dx, float dy, float& lx, float& ly);
