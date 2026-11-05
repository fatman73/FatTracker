#include "formats/formats.h"

#include <cstring>

#include "formats/formats_internal.h"

Result<Song> loadModule(const uint8_t* data, size_t size) {
    if (!data || size == 0) return Result<Song>::fail("NOT A MODULE");
    if (xmDetect(data, size)) return xmLoad(data, size);
    if (modDetect(data, size)) return modLoad(data, size);
    if (stDetect(data, size)) return stLoad(data, size);
    // recognised but not playable yet: say what it is
    auto at = [&](size_t off, const char* magic) {
        const size_t n = std::strlen(magic);
        return size >= off + n && std::memcmp(data + off, magic, n) == 0;
    };
    if (at(0, "TFMX-SONG") || at(0, "TFMX_SONG") || at(0, "tfmxsong") || at(0, "TFMX "))
        return Result<Song>::fail("UNSUPPORTED FORMAT: TFMX (COMING)");
    if (at(0, "MMD0") || at(0, "MMD1") || at(0, "MMD2") || at(0, "MMD3") || at(0, "MMDC"))
        return Result<Song>::fail("UNSUPPORTED FORMAT: OCTAMED (COMING)");
    if (at(0, "SymM")) return Result<Song>::fail("UNSUPPORTED FORMAT: SYMPHONIE (COMING)");
    if (at(44, "SCRM")) return Result<Song>::fail("UNSUPPORTED FORMAT: SCREAM TRACKER 3 (COMING)");
    if (at(0, "IMPM")) return Result<Song>::fail("UNSUPPORTED FORMAT: IMPULSE TRACKER (COMING)");
    if (at(0, "DIGI Booster")) return Result<Song>::fail("UNSUPPORTED FORMAT: DIGIBOOSTER (COMING)");
    return Result<Song>::fail("NOT A MODULE");
}
