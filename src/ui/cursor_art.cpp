#include "ui/cursor_art.h"

#include <cmath>
#include <cstddef>

namespace {
// Pointer outline in a 16x16 unit grid (Workbench-style arrow).
const float kPoly[][2] = {{1, 1}, {1, 14}, {4.2f, 11.2f}, {6.6f, 15.6f}, {8.6f, 14.6f}, {6.3f, 10.3f}, {10.6f, 10.3f}};
constexpr int kN = sizeof kPoly / sizeof kPoly[0];

bool inside(float x, float y) {
    bool in = false;
    for (int i = 0, j = kN - 1; i < kN; j = i++) {
        const float xi = kPoly[i][0], yi = kPoly[i][1], xj = kPoly[j][0], yj = kPoly[j][1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

float edgeDistance(float x, float y) {
    float best = 1e9f;
    for (int i = 0, j = kN - 1; i < kN; j = i++) {
        const float ax = kPoly[j][0], ay = kPoly[j][1], bx = kPoly[i][0], by = kPoly[i][1];
        const float dx = bx - ax, dy = by - ay;
        float t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float px = ax + t * dx - x, py = ay + t * dy - y;
        best = std::fmin(best, std::sqrt(px * px + py * py));
    }
    return best;
}

float segDist(float x, float y, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    float t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const float px = ax + t * dx - x, py = ay + t * dy - y;
    return std::sqrt(px * px + py * py);
}

struct Rgba {
    float r, g, b, a;
};

// Colour of one sample point (unit coordinates), straight alpha.
Rgba shade(float x, float y, bool busy) {
    Rgba c{0, 0, 0, 0};
    const bool in = inside(x, y);
    const float d = edgeDistance(x, y);
    if (in || d < 0.55f) {
        if (!in || d < 0.75f) {
            c = {0.05f, 0.03f, 0.03f, 1};  // black outline
        } else if (x < 2.9f && y < 12.f) {
            c = {1.f, 0.88f, 0.82f, 1};  // light highlight along the left edge
        } else {
            const float t = (y - 1) / 14.f;  // red, a little darker towards the bottom
            c = {1.f - 0.25f * t, 0.22f - 0.12f * t, 0.18f - 0.1f * t, 1};
        }
    }
    if (busy) {  // small Workbench-style clock
        const float cx = 11.8f, cy = 11.8f, dx = x - cx, dy = y - cy, r = std::sqrt(dx * dx + dy * dy);
        if (r < 3.6f) {
            c = r > 2.85f ? Rgba{0.05f, 0.03f, 0.03f, 1} : Rgba{1, 1, 1, 1};
            if (segDist(x, y, cx, cy, cx, cy - 2.0f) < 0.32f || segDist(x, y, cx, cy, cx + 1.5f, cy) < 0.32f)
                c = {0.05f, 0.03f, 0.03f, 1};
        }
    }
    return c;
}
}  // namespace

void amigaCursorHotspot(int size, int& x, int& y) {
    const float scale = size / 16.f;
    x = (int)(1 * scale);
    y = (int)(1 * scale);
}

void renderAmigaCursor(uint32_t* argb, int size, bool busy) {
    const float scale = size / 16.f;
    constexpr int ss = 4;  // 4x4 supersampling: smooth edges at any size
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    const Rgba c = shade((px + (sx + 0.5f) / ss) / scale, (py + (sy + 0.5f) / ss) / scale, busy);
                    r += c.r * c.a;
                    g += c.g * c.a;
                    b += c.b * c.a;
                    a += c.a;
                }
            const float n = ss * ss;
            auto q = [](float v) { return (uint32_t)(v < 0 ? 0 : (v > 1 ? 255 : v * 255.f + 0.5f)); };
            argb[(size_t)py * size + px] = (q(a / n) << 24) | (q(r / n) << 16) | (q(g / n) << 8) | q(b / n);
        }
}
