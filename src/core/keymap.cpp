#include "core/keymap.h"

namespace {
struct Key {
    unsigned vk;
    int semitone;  // relative to C of the base octave
};
constexpr Key kKeys[] = {
    // lower row: base octave
    {'Z', 0}, {'S', 1}, {'X', 2}, {'D', 3}, {'C', 4}, {'V', 5}, {'G', 6}, {'B', 7}, {'H', 8}, {'N', 9},
    {'J', 10}, {'M', 11}, {kVkComma, 12}, {'L', 13}, {kVkPeriod, 14}, {kVkSemicolon, 15}, {kVkSlash, 16},
    // upper row: one octave up
    {'Q', 12}, {'2', 13}, {'W', 14}, {'3', 15}, {'E', 16}, {'R', 17}, {'5', 18}, {'T', 19}, {'6', 20},
    {'Y', 21}, {'7', 22}, {'U', 23}, {'I', 24}, {'9', 25}, {'O', 26}, {'0', 27}, {'P', 28},
    {kVkBracketL, 29}, {kVkEquals, 30}, {kVkBracketR, 31}};
}  // namespace

int noteForKey(unsigned vk, int baseOctave) {
    for (const Key& k : kKeys)
        if (k.vk == vk) {
            const int note = baseOctave * 12 + k.semitone + 1;
            return (note >= 1 && note <= 96) ? note : -1;
        }
    return -1;
}

int hexDigit(unsigned vk) {
    if (vk >= '0' && vk <= '9') return (int)(vk - '0');
    if (vk >= 'A' && vk <= 'F') return (int)(vk - 'A') + 10;
    if (vk >= 0x60 && vk <= 0x69) return (int)(vk - 0x60);  // numpad
    return -1;
}

int fxDigit(unsigned vk) {
    if (vk >= '0' && vk <= '9') return (int)(vk - '0');
    if (vk >= 'A' && vk <= 'Z') return (int)(vk - 'A') + 10;
    if (vk >= 0x60 && vk <= 0x69) return (int)(vk - 0x60);
    return -1;
}
