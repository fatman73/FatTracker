#include "ui/viewport.h"

Viewport computeViewport(uint32_t winW, uint32_t winH) {
    if (winW == 0 || winH == 0) return {0.f, kCanvasW, kCanvasH};
    const float sx = winW / kCanvasW, sy = winH / kCanvasH;
    const float s = sx < sy ? sx : sy;
    return {s, winW / s, winH / s};
}

void deviceToLogical(const Viewport& v, float dx, float dy, float& lx, float& ly) {
    if (v.scale <= 0.f) {
        lx = ly = -1.f;
        return;
    }
    lx = (dx - v.offX) / v.scale;
    ly = (dy - v.offY) / v.scale;
}
