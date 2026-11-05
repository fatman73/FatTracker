// ProTracker / FastTracker 2 keyboard layout (virtual-key codes, no Windows headers needed).
#pragma once

constexpr unsigned kVkComma = 0xBC, kVkPeriod = 0xBE, kVkSemicolon = 0xBA, kVkSlash = 0xBF;
constexpr unsigned kVkBracketL = 0xDB, kVkBracketR = 0xDD, kVkEquals = 0xBB;

// XM note (1..96) for a piano key, or -1. baseOctave 0..7: Z = C of baseOctave,
// Q = C of baseOctave+1 (two piano rows like ProTracker / FastTracker 2).
int noteForKey(unsigned vk, int baseOctave);
int hexDigit(unsigned vk);  // 0..15 or -1 (0-9, A-F, numpad)
int fxDigit(unsigned vk);   // 0..35 or -1 (0-9, A-Z) for the effect column
