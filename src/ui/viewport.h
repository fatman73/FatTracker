// Logical 1920x1080 canvas, scaled uniformly (letterboxed) into the window.
#pragma once
#include <cstdint>

constexpr float kCanvasW = 1920.f;
constexpr float kCanvasH = 1080.f;

struct Viewport {
    float scale, offX, offY;
};

Viewport computeViewport(uint32_t winW, uint32_t winH);
void deviceToLogical(const Viewport& v, float dx, float dy, float& lx, float& ly);
