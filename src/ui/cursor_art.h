// Amiga Workbench style mouse pointer, rendered smoothly (supersampled) at any size.
#pragma once
#include <cstdint>

// Fills size*size premultiplied ARGB pixels (0xAARRGGBB). busy adds a small clock.
void renderAmigaCursor(uint32_t* argb, int size, bool busy);
void amigaCursorHotspot(int size, int& x, int& y);
