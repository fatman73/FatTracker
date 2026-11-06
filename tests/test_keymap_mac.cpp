// macOS key codes -> VK (src/mac/keymap_mac.cpp): every piano key gives the note it gives on Windows.
#ifdef __APPLE__
#include "core/keymap.h"
#include "mac/keymap_mac.h"
#include "platform/vk.h"
#include "test.h"

namespace {
struct MacKey {
    uint16_t kc;  // kVK_ANSI_*
    unsigned vk;  // the Windows VK of the key at the same place on a US keyboard
};
// the two piano rows (ProTracker / FastTracker 2) and the keys around them
constexpr MacKey kPiano[] = {
    {0x06, 'Z'}, {0x01, 'S'}, {0x07, 'X'}, {0x02, 'D'}, {0x08, 'C'}, {0x09, 'V'}, {0x05, 'G'}, {0x0B, 'B'},
    {0x04, 'H'}, {0x2D, 'N'}, {0x26, 'J'}, {0x2E, 'M'}, {0x2B, kVkComma}, {0x25, 'L'}, {0x2F, kVkPeriod},
    {0x29, kVkSemicolon}, {0x2C, kVkSlash}, {0x0C, 'Q'}, {0x13, '2'}, {0x0D, 'W'}, {0x14, '3'}, {0x0E, 'E'},
    {0x0F, 'R'}, {0x17, '5'}, {0x11, 'T'}, {0x16, '6'}, {0x10, 'Y'}, {0x1A, '7'}, {0x20, 'U'}, {0x22, 'I'},
    {0x19, '9'}, {0x1F, 'O'}, {0x1D, '0'}, {0x23, 'P'}, {0x21, kVkBracketL}, {0x18, kVkEquals},
    {0x1E, kVkBracketR}};
}  // namespace

TEST(mac_keymap_piano_same_notes_as_windows) {
    for (const MacKey& k : kPiano) {
        CHECK_EQ(vkFromMac(k.kc), k.vk);
        for (int oct = 0; oct <= 6; ++oct) CHECK_EQ(noteForKey(vkFromMac(k.kc), oct), noteForKey(k.vk, oct));
        CHECK(noteForKey(vkFromMac(k.kc), 3) > 0);
    }
}

TEST(mac_keymap_letters_digits_and_hex) {
    const uint16_t letters[26] = {0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
                                  0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06};
    for (int i = 0; i < 26; ++i) CHECK_EQ(vkFromMac(letters[i]), (unsigned)('A' + i));
    const uint16_t digits[10] = {0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19};
    for (int i = 0; i < 10; ++i) {
        CHECK_EQ(vkFromMac(digits[i]), (unsigned)('0' + i));
        CHECK_EQ(hexDigit(vkFromMac(digits[i])), i);
    }
    const uint16_t keypad[10] = {0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5B, 0x5C};
    for (int i = 0; i < 10; ++i) CHECK_EQ(vkFromMac(keypad[i]), VK_NUMPAD0 + i);
}

TEST(mac_keymap_editing_and_function_keys) {
    CHECK_EQ(vkFromMac(0x24), VK_RETURN);
    CHECK_EQ(vkFromMac(0x4C), VK_RETURN);  // keypad Enter
    CHECK_EQ(vkFromMac(0x30), VK_TAB);
    CHECK_EQ(vkFromMac(0x31), VK_SPACE);
    CHECK_EQ(vkFromMac(0x33), VK_BACK);
    CHECK_EQ(vkFromMac(0x75), VK_DELETE);
    CHECK_EQ(vkFromMac(0x72), VK_INSERT);
    CHECK_EQ(vkFromMac(0x35), VK_ESCAPE);
    CHECK_EQ(vkFromMac(0x73), VK_HOME);
    CHECK_EQ(vkFromMac(0x77), VK_END);
    CHECK_EQ(vkFromMac(0x74), VK_PRIOR);
    CHECK_EQ(vkFromMac(0x79), VK_NEXT);
    CHECK_EQ(vkFromMac(0x7B), VK_LEFT);
    CHECK_EQ(vkFromMac(0x7C), VK_RIGHT);
    CHECK_EQ(vkFromMac(0x7D), VK_DOWN);
    CHECK_EQ(vkFromMac(0x7E), VK_UP);
    const uint16_t fkeys[12] = {0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F};
    for (int i = 0; i < 12; ++i) CHECK_EQ(vkFromMac(fkeys[i]), VK_F1 + i);
    CHECK_EQ(vkFromMac(0x3A), VK_MENU);
    CHECK_EQ(vkFromMac(0x3D), VK_MENU);
    CHECK_EQ(vkFromMac(0x3B), VK_CONTROL);
    CHECK_EQ(vkFromMac(0x3E), VK_CONTROL);
    CHECK_EQ(vkFromMac(0x38), VK_SHIFT);
    CHECK_EQ(vkFromMac(0x37), 0u);  // Command: no VK (acts as Ctrl through the modifier flags)
    CHECK_EQ(vkFromMac(0x3F), 0u);  // fn
    CHECK_EQ(vkFromMac(200), 0u);
}
#endif
