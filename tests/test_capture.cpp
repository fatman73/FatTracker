// Recording from the default audio input (smoke test: skipped without an input device).
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio/capture.h"
#include "test.h"

TEST(capture_records_or_declines) {
    AudioCapture c;
    if (!c.start()) {
        std::printf("SKIP capture_records (no audio input)\n");
        CHECK(!c.running());
        return;
    }
    CHECK(c.running());
    Sleep(400);
    c.stop();
    CHECK(!c.running());
    const auto rec = c.take();
    std::printf("  capture: %u frames at %u Hz\n", (unsigned)rec.size(), c.rate());
    CHECK(rec.size() > c.rate() / 10);  // at least 0.1 s arrived
    CHECK(c.take().empty());           // taken only once
}
