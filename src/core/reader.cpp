#include "core/reader.h"

#include <cstring>

ByteReader::ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

uint8_t ByteReader::u8() {
    if (pos_ >= size_) {
        overrun_ = true;
        return 0;
    }
    return data_[pos_++];
}

uint16_t ByteReader::u16le() {
    uint16_t a = u8();
    return (uint16_t)(a | (u8() << 8));
}

uint16_t ByteReader::u16be() {
    uint16_t a = u8();
    return (uint16_t)((a << 8) | u8());
}

uint32_t ByteReader::u32le() {
    uint32_t a = u16le();
    return a | ((uint32_t)u16le() << 16);
}

uint32_t ByteReader::u32be() {
    uint32_t a = u16be();
    return (a << 16) | u16be();
}

void ByteReader::bytes(void* dst, size_t n) {
    size_t avail = remaining();
    size_t take = n < avail ? n : avail;
    if (take) std::memcpy(dst, data_ + pos_, take);
    if (take < n) {
        std::memset((uint8_t*)dst + take, 0, n - take);
        overrun_ = true;
    }
    pos_ = pos_ < size_ ? pos_ + take : pos_;
}

void ByteReader::skip(size_t n) {
    if (n > remaining()) {
        overrun_ = true;
        pos_ = size_;
    } else {
        pos_ += n;
    }
}

void ByteReader::seek(size_t pos) {
    if (pos > size_) {
        overrun_ = true;
        pos_ = size_;
    } else {
        pos_ = pos;
    }
}
