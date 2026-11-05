#include <vector>

#include "test.h"
#include "ui/cursor_art.h"

TEST(cursor_art_shape) {
    const int n = 32;
    std::vector<uint32_t> px((size_t)n * n);
    renderAmigaCursor(px.data(), n, false);
    auto alpha = [&](int x, int y) { return (px[(size_t)y * n + x] >> 24) & 255; };
    CHECK(alpha(2, 3) > 128);       // near the tip: opaque
    CHECK_EQ(alpha(n - 1, 0), 0u);  // top right: transparent
    CHECK_EQ(alpha(0, n - 1), 0u);  // bottom left corner: transparent
    int red = 0;
    for (uint32_t p : px)
        if (((p >> 24) & 255) == 255 && ((p >> 16) & 255) > 150 && ((p >> 8) & 255) < 100) ++red;
    CHECK(red > 20);  // an Amiga-red body
    int hx, hy;
    amigaCursorHotspot(n, hx, hy);
    CHECK(hx <= 3 && hy <= 3);
}

TEST(cursor_art_busy_differs) {
    const int n = 32;
    std::vector<uint32_t> a((size_t)n * n), b((size_t)n * n);
    renderAmigaCursor(a.data(), n, false);
    renderAmigaCursor(b.data(), n, true);
    CHECK(a != b);
}
