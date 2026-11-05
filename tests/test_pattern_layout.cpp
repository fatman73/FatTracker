#include "test.h"
#include "ui/pattern_layout.h"

TEST(pattern_layout_4ch) {
    auto l = computePatternLayout(4, 4, 0, 1660);
    CHECK_EQ(l.visible, 4);
    CHECK_NEAR(l.glyph, 13, 1e-3);  // glyph = character advance of the monospace font, capped
    // columns stretch to use the whole width
    CHECK_NEAR(l.rowNumW + 4 * l.chanW, 1660, 1e-2);
}

TEST(pattern_layout_8ch) {
    auto l = computePatternLayout(8, 32, 0, 1660);
    CHECK_EQ(l.visible, 8);
    CHECK_NEAR(l.glyph, 13, 1e-3);
    CHECK_NEAR(l.rowNumW + 8 * l.chanW, 1660, 1e-2);
}

TEST(pattern_layout_16ch) {
    auto l = computePatternLayout(16, 32, 0, 1920);
    CHECK_EQ(l.visible, 16);
    CHECK(l.glyph >= 9.5f && l.glyph <= 10.5f);
}

TEST(pattern_layout_32ch_full_cells) {
    // all 32 channels with complete cells (note, sample, volume, effect), smaller font
    auto l = computePatternLayout(32, 32, 0, 1900);
    CHECK_EQ(l.visible, 32);
    CHECK(l.glyph >= 4.9f && l.glyph < 6.f);
    CHECK(l.chanW >= 10.75f * l.glyph);  // a full cell fits in its column
    CHECK_NEAR(l.rowNumW + 32 * l.chanW, 1900, 1e-2);
}

TEST(pattern_layout_clamp_first) {
    auto l = computePatternLayout(8, 12, 10, 1660);
    CHECK_EQ(l.firstChannel, 4);
}

TEST(pattern_layout_view_shows_n_columns) {
    // the chosen view always shows that many columns, even if the song has fewer channels
    auto l = computePatternLayout(16, 6, 0, 1920);
    CHECK_EQ(l.visible, 16);
    CHECK_EQ(l.firstChannel, 0);
    auto l32 = computePatternLayout(32, 8, 5, 1920);
    CHECK_EQ(l32.visible, 32);
    CHECK_EQ(l32.firstChannel, 0);
}

TEST(pattern_cell_text) {
    char buf[16];
    formatCell({49, 0x1F, 0x10 + 0x40, 0xC, 0x20}, false, buf);
    CHECK(std::string_view(buf) == "C-41F40C20");
    formatCell({49, 1, 0, 0, 0}, true, buf);  // PT octave display: XM C-4 shows as C-2
    CHECK(std::string_view(buf) == "C-201.....");
    formatCell({NOTE_OFF, 0, 0x64, 0, 0}, false, buf);
    CHECK(std::string_view(buf) == "===..-4...");
    formatCell({0, 0, 0, 0xF, 0x06}, false, buf);
    CHECK(std::string_view(buf) == "---....F06");
}
