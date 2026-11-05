#include "formats/formats.h"

#include <cstring>

#include "formats/formats_internal.h"

Result<Song> loadModule(const uint8_t* data, size_t size) {
    if (!data || size == 0) return Result<Song>::fail("NOT A MODULE");
    if (xmDetect(data, size)) return xmLoad(data, size);
    if (s3mDetect(data, size)) return s3mLoad(data, size);
    if (itDetect(data, size)) return itLoad(data, size);
    if (modDetect(data, size)) return modLoad(data, size);
    if (mtmDetect(data, size)) return mtmLoad(data, size);
    if (c669Detect(data, size)) return c669Load(data, size);
    if (stDetect(data, size)) return stLoad(data, size);
    if (medDetect(data, size)) return medLoad(data, size);
    if (dbmDetect(data, size)) return dbmLoad(data, size);
    if (digiDetect(data, size)) return digiLoad(data, size);
    if (symDetect(data, size)) return symLoad(data, size);
    // recognised but not playable yet: say what it is
    auto at = [&](size_t off, const char* magic) {
        const size_t n = std::strlen(magic);
        return size >= off + n && std::memcmp(data + off, magic, n) == 0;
    };
    if (at(0, "TFMX-SONG") || at(0, "TFMX_SONG") || at(0, "tfmxsong") || at(0, "TFMX "))
        return Result<Song>::fail("UNSUPPORTED FORMAT: TFMX (COMING)");
    if (at(0, "MMDC")) return Result<Song>::fail("UNSUPPORTED FORMAT: PACKED OCTAMED (MMDC)");
    return Result<Song>::fail("NOT A MODULE");
}
