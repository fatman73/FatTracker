// macOS key codes (Carbon kVK_*, the physical key position) to the Windows virtual-key codes the UI uses
// (platform/vk.h). Positional, so the piano rows sit on the same keys whatever the keyboard layout.
#pragma once
#include <cstdint>

unsigned vkFromMac(uint16_t keyCode);  // 0 = no VK (Command, fn, media keys ...)
