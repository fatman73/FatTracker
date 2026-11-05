// Stage 3: Pure mode screen - pixel canvas, Amiga style font, ProTracker style layout.
#include "test.h"
#include "ui/pixel_canvas.h"
#include "ui/pure_layout.h"

namespace {
const uint32_t kInk = argb(0xFFFFFF);
}

TEST(pixel_font_draws_glyphs) {
    static PixelCanvas c;
    c.clear(0);
    c.text(0, 0, "A", kInk);
    CHECK(c.at(2, 0) == kInk && c.at(3, 0) == kInk);  // top of the A
    CHECK(c.at(0, 0) == 0);
    CHECK(c.at(0, 6) == kInk);                          // left leg reaches the bottom
    c.clear(0);
    c.text(0, 0, "a", kInk);  // lower case shows as upper case
    CHECK(c.at(2, 0) == kInk);
}

TEST(pixel_font_compact_is_six_rows) {
    static PixelCanvas c;
    c.clear(0);
    c.text(0, 0, "08BEH", kInk, true);
    bool row6 = false;
    for (int x = 0; x < 40; ++x) row6 |= c.at(x, 6) != 0;
    CHECK(!row6);
    CHECK(c.at(1, 5) == kInk);  // '0' still closes at its bottom
}

TEST(pixel_bevel_edges) {
    static PixelCanvas c;
    c.bevel(10, 10, 20, 11, argb(0x888888), argb(0xFFFFFF), argb(0x444444));
    CHECK(c.at(10, 10) == argb(0xFFFFFF));
    CHECK(c.at(29, 20) == argb(0x444444));
    CHECK(c.at(15, 15) == argb(0x888888));
}

TEST(pure_layout_hits_controls) {
    using namespace pure;
    CHECK_EQ(hitTest(130, 5), kButton + 0);   // PLAY
    CHECK_EQ(hitTest(250, 40), kButton + 11);  // SAMPLER
    CHECK_EQ(hitTest(100, 3), kArrow + 0);    // POS up
    CHECK_EQ(hitTest(112, 3), kArrow + 1);    // POS down
    CHECK_EQ(hitTest(45, 3), kPosIns);
    CHECK_EQ(hitTest(300, 115), kLoad);
    CHECK_EQ(hitTest(30, 200), kTrack + 0);
    CHECK_EQ(hitTest(310, 200), kTrack + 3);
}

TEST(pure_layout_rows_and_columns) {
    using namespace pure;
    int rel = 99;
    CHECK(rowAt(kBarY + 3, rel) && rel == 0);
    CHECK(rowAt(kBarY - 1, rel) && rel == -1);
    CHECK(rowAt(kBarY - kPatternRowH - 1, rel) && rel == -2);
    CHECK(rowAt(kBarY + kBarH, rel) && rel == 1);
    CHECK(!rowAt(kPatternY - 1, rel));
    CHECK_EQ(columnAt(0, kTrackX0 + 3), 0);
    CHECK_EQ(columnAt(0, kTrackX0 + 3 + 8 * 3), 1);
    CHECK_EQ(columnAt(1, kTrackX0 + kTrackPitch + 3 + 8 * 5), 5);
    CHECK_EQ(columnAt(0, kTrackX0 - 2), -1);
}
