#include "test.h"
#include "ui/viewport.h"

TEST(viewport_exact) {
    auto v = computeViewport(1920, 1080);
    CHECK_NEAR(v.scale, 1, 1e-6);
    CHECK_NEAR(v.offX, 0, 1e-6);
    CHECK_NEAR(v.offY, 0, 1e-6);
}

TEST(viewport_4k) {
    auto v = computeViewport(3840, 2160);
    CHECK_NEAR(v.scale, 2, 1e-6);
}

TEST(viewport_tall) {
    auto v = computeViewport(1920, 1200);
    CHECK_NEAR(v.scale, 1, 1e-6);
    CHECK_NEAR(v.offY, 60, 1e-6);
}

TEST(viewport_narrow) {
    auto v = computeViewport(1280, 1080);
    CHECK_NEAR(v.scale, 2.0 / 3, 1e-5);
    CHECK_NEAR(v.offY, 180, 1e-3);
}

TEST(viewport_zero) {
    auto v = computeViewport(0, 0);
    CHECK_EQ(v.scale, 0.f);
}

TEST(viewport_logical_roundtrip) {
    auto v = computeViewport(1280, 1080);
    float lx, ly;
    deviceToLogical(v, 640.f, 540.f, lx, ly);
    CHECK_NEAR(lx, 960, 1e-3);
    CHECK_NEAR(ly, 540, 1e-3);
}
