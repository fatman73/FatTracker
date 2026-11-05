// Writing songs: XM (FatTracker's native format) and ProTracker MOD export.
#pragma once
#include <cstdint>
#include <vector>

#include "core/song.h"

std::vector<uint8_t> saveXm(const Song& s);

struct ModExport {
    std::vector<uint8_t> data;
    std::vector<const char*> warnings;  // static strings, each listed once
};
ModExport exportMod(const Song& s);
