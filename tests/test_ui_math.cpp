#include "test.h"
#include "ui/viewport.h"

// The logical canvas is at least 1920x1080 and grows in one dimension to fill the window
// completely (no letterbox bars).
TEST(viewport_exact) {
    auto v = computeViewport(1920, 1080);
    CHECK_NEAR(v.scale, 1, 1e-6);
    CHECK_NEAR(v.w, 1920, 1e-3);
    CHECK_NEAR(v.h, 1080, 1e-3);
}

TEST(viewport_4k) {
    auto v = computeViewport(3840, 2160);
    CHECK_NEAR(v.scale, 2, 1e-6);
    CHECK_NEAR(v.w, 1920, 1e-3);
}

TEST(viewport_maximized_fills_width) {  // 1920x1009: maximized window above the taskbar
    auto v = computeViewport(1920, 1009);
    CHECK_NEAR(v.h, 1080, 1e-3);
    CHECK_NEAR(v.w * v.scale, 1920, 1e-2);
    CHECK_NEAR(v.h * v.scale, 1009, 1e-2);
    CHECK(v.w > 1920);
}

TEST(viewport_tall_fills_height) {
    auto v = computeViewport(1280, 1080);
    CHECK_NEAR(v.w, 1920, 1e-3);
    CHECK_NEAR(v.h * v.scale, 1080, 1e-2);
    CHECK(v.h > 1080);
}

TEST(viewport_zero) {
    auto v = computeViewport(0, 0);
    CHECK_EQ(v.scale, 0.f);
}

TEST(viewport_logical_roundtrip) {
    auto v = computeViewport(1280, 1080);
    float lx, ly;
    deviceToLogical(v, 640.f, 540.f, lx, ly);
    CHECK_NEAR(lx, 640 / v.scale, 1e-3);
    CHECK_NEAR(ly, 540 / v.scale, 1e-3);
}
