// deflate / gzip decoding for packed tunes (.vgz). Output is capped at maxOut bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

bool inflateRaw(const uint8_t* in, size_t size, std::vector<uint8_t>& out, size_t maxOut);
bool isGzip(const uint8_t* data, size_t size);
bool gunzip(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut);
