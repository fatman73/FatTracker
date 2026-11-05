#include "formats/formats.h"

#include "formats/formats_internal.h"

Result<Song> loadModule(const uint8_t* data, size_t size) {
    if (!data || size == 0) return Result<Song>::fail("NOT A MODULE");
    if (xmDetect(data, size)) return xmLoad(data, size);
    if (modDetect(data, size)) return modLoad(data, size);
    if (stDetect(data, size)) return stLoad(data, size);
    return Result<Song>::fail("NOT A MODULE");
}
