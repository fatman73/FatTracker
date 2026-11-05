#include <cstring>

#include "test.h"
#include "ui/font_data.h"

namespace {
int bitsOf(int c) {
    int b = 0;
    for (int r = 0; r < 8; ++r) b |= kGlyphs[c][r];
    return b;
}
}  // namespace

TEST(font_printable_glyphs_nonempty) {
    for (int c = 33; c <= 126; ++c) CHECK(bitsOf(c) != 0);
}

TEST(font_space_empty) {
    for (int r = 0; r < 8; ++r) CHECK_EQ(kGlyphs[' '][r], 0);
}

TEST(font_arrows_present) {
    for (int c = 1; c <= 6; ++c) CHECK(bitsOf(c) != 0);
}

TEST(font_zero_differs_from_O) { CHECK(std::memcmp(kGlyphs['0'], kGlyphs['O'], 8) != 0); }

TEST(font_no_duplicate_glyphs) {
    for (int a = 33; a <= 126; ++a)
        for (int b = a + 1; b <= 126; ++b)
            if (std::memcmp(kGlyphs[a], kGlyphs[b], 8) == 0) {
                std::printf("  duplicate glyphs '%c' and '%c'\n", a, b);
                CHECK(false);
            }
}
