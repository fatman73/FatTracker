// Stage 3: Pure mode screen - pixel canvas, Amiga style font, ProTracker style layout.
#include <cstring>
#include <initializer_list>

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

TEST(pixel_font_small_is_five_rows) {
    static PixelCanvas c;
    c.clear(0);
    c.text(0, 0, "08BEH", kInk, kSmall);
    bool row5 = false;
    for (int x = 0; x < 40; ++x) row5 |= c.at(x, 5) != 0;
    CHECK(!row5);
    CHECK(c.at(1, 4) == kInk);  // '0' still closes at its bottom
    CHECK_EQ(glyphRows(kSmall), 5);
}

TEST(pixel_font_tall_is_nine_rows) {
    static PixelCanvas c;
    c.clear(0);
    c.text(0, 0, "8", kInk, kTall);
    CHECK(c.at(1, 8) == kInk);  // bottom of the 8
    bool row9 = false;
    for (int x = 0; x < 8; ++x) row9 |= c.at(x, 9) != 0;
    CHECK(!row9);
    CHECK_EQ(glyphRows(kTall), 9);
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
    CHECK_EQ(hitTest(40, 200), kTrack + 0);
    CHECK_EQ(hitTest(30, 200), kNone);  // grey frame between the row numbers and track 1
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

TEST(pure_layout_exit_button_inside_screen) {
    using namespace pure;
    const Box b = exitButton();
    CHECK(b.x >= 0 && b.x + b.w <= 320 && b.y >= 0 && b.y + b.h <= 256);
    CHECK_EQ(hitTest(b.x + 2, b.y + 2), kExit);
    CHECK_EQ(hitTest(b.x + b.w - 2, b.y + b.h - 2), kExit);
}

TEST(pure_layout_matches_protracker) {
    using namespace pure;
    CHECK_EQ(kBarH, 14);  // the playing row is taller than the others
    CHECK(kBarY + kBarH + 7 * kPatternRowH <= 256);
    CHECK_EQ(counterValue(6).w, 43);  // LENGTH / REPEAT / REPLEN: 5 hex digits + bevel + 1 px margin each side
    CHECK_EQ(counterValue(1).w, 35);
    CHECK_EQ(counterLabel(6).w + counterValue(6).w, counterLabel(1).w + counterValue(1).w);
    CHECK_EQ(track(0).x, 32);
    CHECK_EQ(track(3).x + track(3).w, 316);
}

TEST(pixel_font_medium_is_six_rows) {
    // counter values and names: 6 px tall, so they keep a margin inside the 11 px boxes like ProTracker
    static PixelCanvas c;
    c.clear(0);
    c.text(0, 0, "0825", kInk, kMedium);
    bool row6 = false;
    for (int x = 0; x < 32; ++x) row6 |= c.at(x, 6) != 0;
    CHECK(!row6);
    CHECK(c.at(1, 5) == kInk);  // '0' closes at its bottom
    CHECK_EQ(glyphRows(kMedium), 6);
}

TEST(pixel_font_medium_three_keeps_its_middle) {
    static PixelCanvas c;
    c.clear(0);
    c.text(0, 0, "3", kInk, kMedium);
    CHECK(c.at(4, 2) == kInk);  // the middle stroke of the 3 (without it the 3 reads as a J)
}

TEST(pure_values_keep_off_the_box_edges) {
    // black counter digits must not touch the bevel of their box (ProTracker leaves a pixel)
    using namespace pure;
    for (int r : {1, 6}) {
        const Box b = counterValue(r);
        const int digits = r >= 6 ? 5 : 4;
        const int x = valueX(b, digits);
        CHECK(x >= b.x + 2);                      // bevel + 1 px gap on the left
        CHECK(x + digits * 8 - 2 <= b.x + b.w - 2);  // glyphs end before the right gap + bevel
    }
}

namespace {
// widest row of a glyph in pixels, and whether the glyph differs from another one
int glyphWidth(char ch, FontSize size) {
    static PixelCanvas c;
    c.clear(0);
    const char s[2] = {ch, 0};
    c.text(0, 0, s, kInk, size);
    int widest = 0;
    for (int y = 0; y < glyphRows(size); ++y) {
        int l = 99, r = -1;
        for (int x = 0; x < 8; ++x)
            if (c.at(x, y)) {
                l = x < l ? x : l;
                r = x;
            }
        if (r >= 0 && r - l + 1 > widest) widest = r - l + 1;
    }
    return widest;
}
bool sameGlyph(char a, char b, FontSize size) {
    static PixelCanvas c;
    c.clear(0);
    const char s[3] = {a, b, 0};
    c.text(0, 0, s, kInk, size);
    for (int y = 0; y < glyphRows(size); ++y)
        for (int x = 0; x < 8; ++x)
            if (c.at(x, y) != c.at(x + 8, y)) return false;
    return true;
}
}  // namespace

TEST(pixel_font_zero_is_slashed_not_an_o) {
    for (FontSize f : {kSmall, kMedium, kNormal, kTall}) CHECK(!sameGlyph('0', 'O', f));
}

TEST(pixel_font_counter_arrows_are_five_wide) {
    for (FontSize f : {kSmall, kNormal}) {
        CHECK_EQ(glyphWidth('\x01', f), 5);
        CHECK_EQ(glyphWidth('\x02', f), 5);
    }
}

TEST(pure_layout_name_rows_are_clickable) {
    using namespace pure;
    CHECK_EQ(hitTest(150, songRow().y + 4), kSongName);
    CHECK_EQ(hitTest(150, sampleRow().y + 4), kSampleName);
    CHECK_EQ(hitTest(loadButton().x + 4, loadButton().y + 4), kLoad);  // LOAD keeps working
}

// ---- Pure panels: EDIT OP. and POS ED. over the quadrascope, SAMPLER over the pattern area
TEST(pure_edit_op_covers_the_scopes) {
    using namespace pure;
    const Box a = opArea();
    CHECK_EQ(a.x, 120);
    CHECK_EQ(a.y, 44);
    CHECK_EQ(hitTest(150, 70), kScope + 0);  // main view: the scope
    for (int sc = 0; sc < kOpScreens; ++sc) {
        CHECK(opCount(sc) > 0);
        for (int i = 0; i < opCount(sc); ++i) {
            const OpItem it = opItem(sc, i);
            CHECK(it.box.x >= a.x && it.box.y >= a.y && it.box.x + it.box.w <= a.x + a.w && it.box.y + it.box.h <= a.y + a.h);
            CHECK((int)std::strlen(it.label) * 8 - 1 <= it.box.w - 6);  // 3 px of face on each side of the label
            const int h = hitTest(it.box.x + it.box.w / 2, it.box.y + it.box.h / 2, kViewEditOp, sc);
            CHECK_EQ(h, kOpItem + i);
            for (int j = 0; j < i; ++j) {  // no overlaps
                const Box b = opItem(sc, j).box;
                CHECK(it.box.x >= b.x + b.w || b.x >= it.box.x + it.box.w || it.box.y >= b.y + b.h || b.y >= it.box.y + it.box.h);
            }
        }
    }
    CHECK_EQ(hitTest(opTitle().x + 5, opTitle().y + 5, kViewEditOp, 0), kOpTitle);
    CHECK_EQ(hitTest(opExit().x + 5, opExit().y + 5, kViewEditOp, 0), kOpExit);
    CHECK_EQ(hitTest(130, 5, kViewEditOp, 0), kButton + 0);  // the button grid stays usable
    CHECK_EQ(hitTest(310, 48, kViewEditOp, 0), kSide + 4);   // so does "A" (unmute all) next to the title
    CHECK_EQ(hitTest(310, 48, kViewPosEd, 0), kSide + 4);
    CHECK(opExit().x + opExit().w <= sideButton(4).x);
}

TEST(pure_position_editor_lines) {
    using namespace pure;
    for (int i = 0; i < kPosLines; ++i) {
        const Box l = posLine(i);
        CHECK(l.y >= opArea().y + kRowH && l.y + l.h <= opArea().y + opArea().h);
        CHECK_EQ(hitTest(l.x + 4, l.y + l.h / 2, kViewPosEd), kPosLine + i);
    }
    CHECK_EQ(hitTest(posScroll(0).x + 3, posScroll(0).y + 3, kViewPosEd), kPosUp);
    CHECK_EQ(hitTest(posScroll(1).x + 3, posScroll(1).y + 3, kViewPosEd), kPosDown);
    CHECK_EQ(hitTest(opExit().x + 5, opExit().y + 5, kViewPosEd), kOpExit);
}

TEST(pure_sampler_replaces_the_pattern_area) {
    using namespace pure;
    const Box w = samplerWave();
    CHECK(w.y >= kPatternY && w.y <= kPatternY + 2);
    CHECK_EQ(hitTest(160, w.y + w.h / 2, kViewSampler), kSmpWave);
    CHECK_EQ(hitTest(40, 200), kTrack + 0);
    for (int i = 0; i < kSamplerButtons; ++i) {
        const Box b = samplerButton(i);
        CHECK(b.x >= 0 && b.x + b.w <= 320 && b.y >= w.y + w.h && b.y + b.h <= 256);
        CHECK((int)std::strlen(samplerLabel(i)) * 8 - 1 <= b.w - 6);
        CHECK_EQ(hitTest(b.x + b.w / 2, b.y + b.h / 2, kViewSampler), kSmpButton + i);
    }
    CHECK_EQ(hitTest(130, 5, kViewSampler), kButton + 0);
}
