// Module format detection and loading.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/result.h"
#include "core/song.h"

Result<Song> loadModule(const uint8_t* data, size_t size);
