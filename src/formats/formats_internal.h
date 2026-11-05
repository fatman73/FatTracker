// Per-format entry points used by the registry in formats.cpp.
#pragma once
#include "formats/formats.h"

bool modDetect(const uint8_t* data, size_t size);
Result<Song> modLoad(const uint8_t* data, size_t size);
bool stDetect(const uint8_t* data, size_t size);
Result<Song> stLoad(const uint8_t* data, size_t size);
bool xmDetect(const uint8_t* data, size_t size);
Result<Song> xmLoad(const uint8_t* data, size_t size);
