#include "core/reader.h"
#include "test.h"

TEST(reader_reads_le_be) {
    const uint8_t d[] = {0x34, 0x12, 0x12, 0x34, 0x78, 0x56, 0x34, 0x12};
    ByteReader r(d, sizeof d);
    CHECK_EQ(r.u16le(), 0x1234);
    CHECK_EQ(r.u16be(), 0x1234);
    CHECK_EQ(r.u32le(), 0x12345678u);
    CHECK(!r.overrun());
    CHECK_EQ(r.remaining(), 0u);
}

TEST(reader_overrun_returns_zero) {
    const uint8_t d[] = {0xFF};
    ByteReader r(d, 1);
    r.u8();
    CHECK_EQ(r.u32be(), 0u);
    CHECK(r.overrun());
}

TEST(reader_bytes_zero_fills) {
    const uint8_t d[] = {1, 2};
    ByteReader r(d, 2);
    uint8_t out[4] = {9, 9, 9, 9};
    r.bytes(out, 4);
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 2);
    CHECK_EQ(out[2], 0);
    CHECK_EQ(out[3], 0);
    CHECK(r.overrun());
}

TEST(reader_seek_past_end) {
    const uint8_t d[] = {1};
    ByteReader r(d, 1);
    r.seek(100);
    CHECK_EQ(r.u8(), 0);
    CHECK(r.overrun());
    CHECK_EQ(r.remaining(), 0u);
}
