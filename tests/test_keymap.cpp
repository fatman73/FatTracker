#include "core/keymap.h"
#include "test.h"

TEST(keymap_lower_row_base_octave) {
    CHECK_EQ(noteForKey('Z', 4), 49);  // C-4 (XM numbering)
    CHECK_EQ(noteForKey('S', 4), 50);  // C#4
    CHECK_EQ(noteForKey('M', 4), 60);  // B-4
    CHECK_EQ(noteForKey(kVkComma, 4), 61);  // C-5
}

TEST(keymap_upper_row_octave_up) {
    CHECK_EQ(noteForKey('Q', 4), 61);  // C-5
    CHECK_EQ(noteForKey('2', 4), 62);  // C#5
    CHECK_EQ(noteForKey('I', 4), 73);  // C-6
    CHECK_EQ(noteForKey(kVkBracketR, 4), 80);  // G-6
}

TEST(keymap_out_of_range_and_unmapped) {
    CHECK_EQ(noteForKey('A', 4), -1);
    CHECK_EQ(noteForKey('I', 7), -1);  // above B-7
    CHECK_EQ(noteForKey('Z', 0), 1);   // C-0
}

TEST(keymap_hex_and_fx_digits) {
    CHECK_EQ(hexDigit('0'), 0);
    CHECK_EQ(hexDigit('9'), 9);
    CHECK_EQ(hexDigit('A'), 10);
    CHECK_EQ(hexDigit('F'), 15);
    CHECK_EQ(hexDigit('G'), -1);
    CHECK_EQ(hexDigit(0x60), 0);  // numpad 0
    CHECK_EQ(fxDigit('K'), 20);
    CHECK_EQ(fxDigit('Z'), 35);
    CHECK_EQ(fxDigit('7'), 7);
}
