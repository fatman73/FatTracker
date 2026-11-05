// Bounds-checked little/big-endian byte reader. Reads past the end return 0
// (or zero-fill) and set overrun().
#pragma once
#include <cstddef>
#include <cstdint>

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size);
    uint8_t u8();
    uint16_t u16le();
    uint16_t u16be();
    uint32_t u32le();
    uint32_t u32be();
    void bytes(void* dst, size_t n);
    void skip(size_t n);
    void seek(size_t pos);
    size_t pos() const { return pos_; }
    size_t size() const { return size_; }
    size_t remaining() const { return pos_ < size_ ? size_ - pos_ : 0; }
    bool overrun() const { return overrun_; }
    const uint8_t* data() const { return data_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool overrun_ = false;
};
