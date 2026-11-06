#include "mac/keymap_mac.h"

unsigned vkFromMac(uint16_t kc) {
    // index = kVK_* code (HIToolbox/Events.h); value = VK_* code
    static const uint8_t kTable[128] = {
        /*00*/ 'A', 'S', 'D', 'F', 'H', 'G', 'Z', 'X', 'C', 'V', 0xC0 /*ISO section (key left of 1)*/, 'B', 'Q', 'W',
        'E', 'R',
        /*10*/ 'Y', 'T', '1', '2', '3', '4', '6', '5', 0xBB /*=*/, '9', '7', 0xBD /*-*/, '8', '0', 0xDD /*]*/, 'O',
        /*20*/ 'U', 0xDB /*[*/, 'I', 'P', 0x0D /*Return*/, 'L', 'J', 0xDE /*'*/, 'K', 0xBA /*;*/, 0xDC /*\*/,
        0xBC /*,*/, 0xBF /*/*/, 'N', 'M', 0xBE /*.*/,
        /*30*/ 0x09 /*Tab*/, 0x20 /*Space*/, 0xC0 /*`*/, 0x08 /*Delete = Backspace*/, 0, 0x1B /*Esc*/,
        0 /*right Command*/, 0 /*Command*/, 0x10 /*Shift*/, 0x14 /*Caps Lock*/, 0x12 /*Option*/, 0x11 /*Control*/,
        0x10 /*right Shift*/, 0x12 /*right Option*/, 0x11 /*right Control*/, 0 /*fn*/,
        /*40*/ 0, 0x6E /*num .*/, 0, 0x6A /*num **/, 0, 0x6B /*num +*/, 0, 0, 0, 0, 0, 0x6F /*num /*/,
        0x0D /*num Enter*/, 0, 0x6D /*num -*/, 0,
        /*50*/ 0, 0, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0, 0x68, 0x69, 0, 0, 0,
        /*60*/ 0x74 /*F5*/, 0x75 /*F6*/, 0x76 /*F7*/, 0x72 /*F3*/, 0x77 /*F8*/, 0x78 /*F9*/, 0, 0x7A /*F11*/, 0, 0, 0,
        0, 0, 0x79 /*F10*/, 0, 0x7B /*F12*/,
        /*70*/ 0, 0, 0x2D /*Help = Insert*/, 0x24 /*Home*/, 0x21 /*PgUp*/, 0x2E /*Forward Delete*/, 0x73 /*F4*/,
        0x23 /*End*/, 0x71 /*F2*/, 0x22 /*PgDn*/, 0x70 /*F1*/, 0x25 /*Left*/, 0x27 /*Right*/, 0x28 /*Down*/,
        0x26 /*Up*/, 0};
    return kc < 128 ? kTable[kc] : 0;
}
