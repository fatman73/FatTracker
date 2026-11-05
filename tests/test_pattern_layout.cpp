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

TEST(pattern_layout_clamp_first) {
    auto l = computePatternLayout(8, 12, 10, 1660);
    CHECK_EQ(l.firstChannel, 4);
}

TEST(pattern_layout_fewer_channels) {
    auto l = computePatternLayout(16, 6, 0, 1920);
    CHECK_EQ(l.visible, 6);
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
