// Restoring the saved window position: the window must come back whole on a monitor that exists.
#include "app/window_rect.h"
#include "test.h"

namespace {
const WinRect kWork = {0, 0, 1920, 1040};  // 1920x1080 screen, taskbar 40 px
}

TEST(window_rect_saved_inside_kept) {
    const WinRect r = fitWindowRect({100, 50, 1300, 850}, kWork, 640, 480);
    CHECK_EQ(r.left, 100);
    CHECK_EQ(r.top, 50);
    CHECK_EQ(r.right, 1300);
    CHECK_EQ(r.bottom, 850);
}

TEST(window_rect_moved_back_from_a_missing_monitor) {
    // saved on a second monitor to the right that is gone; the nearest work area is the main one
    const WinRect r = fitWindowRect({2500, 100, 3500, 900}, kWork, 640, 480);
    CHECK_EQ(r.right - r.left, 1000);  // size kept
    CHECK_EQ(r.bottom - r.top, 800);
    CHECK(r.left >= kWork.left && r.right <= kWork.right);
    CHECK(r.top >= kWork.top && r.bottom <= kWork.bottom);
}

TEST(window_rect_too_big_shrunk_to_the_work_area) {
    const WinRect r = fitWindowRect({-50, -20, 2600, 1500}, kWork, 640, 480);
    CHECK_EQ(r.left, 0);
    CHECK_EQ(r.top, 0);
    CHECK_EQ(r.right, 1920);
    CHECK_EQ(r.bottom, 1040);
}

TEST(window_rect_too_small_grown_to_the_minimum) {
    const WinRect r = fitWindowRect({100, 100, 150, 120}, kWork, 640, 480);
    CHECK_EQ(r.right - r.left, 640);
    CHECK_EQ(r.bottom - r.top, 480);
    CHECK(r.right <= kWork.right && r.bottom <= kWork.bottom);
}

TEST(window_rect_work_area_off_origin) {
    // a monitor left of the main one, with negative coordinates
    const WinRect work = {-1280, 0, 0, 984};
    const WinRect r = fitWindowRect({-1500, 600, -500, 1400}, work, 640, 480);
    CHECK(r.left >= work.left && r.right <= work.right);
    CHECK(r.top >= work.top && r.bottom <= work.bottom);
    CHECK_EQ(r.right - r.left, 1000);
    CHECK_EQ(r.bottom - r.top, 800);
}
