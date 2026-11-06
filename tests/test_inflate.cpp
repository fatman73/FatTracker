// deflate / gzip decoding (.vgz tunes).
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/inflate.h"
#include "test.h"

namespace {
// gzip member with one stored deflate block holding `text`
std::vector<uint8_t> gzStored(const char* text) {
    const size_t n = std::strlen(text);
    std::vector<uint8_t> g = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3};
    g.push_back(1);  // final block, stored
    g.push_back((uint8_t)n), g.push_back((uint8_t)(n >> 8));
    g.push_back((uint8_t)~n), g.push_back((uint8_t)(~n >> 8));
    g.insert(g.end(), text, text + n);
    for (int i = 0; i < 8; ++i) g.push_back(0);  // crc / size (not checked)
    return g;
}
}  // namespace

TEST(gunzip_stored_block) {
    const auto g = gzStored("hello chip music");
    std::vector<uint8_t> out;
    CHECK(gunzip(g.data(), g.size(), out, 1024));
    CHECK_EQ(out.size(), 16u);
    CHECK(std::memcmp(out.data(), "hello chip music", 16) == 0);
    CHECK(!gunzip(g.data(), g.size(), out, 8));  // the cap holds
}

TEST(gunzip_rejects_broken_data) {
    auto g = gzStored("abcdef");
    g[12] ^= 0x55;  // LEN no longer matches NLEN
    std::vector<uint8_t> out;
    CHECK(!gunzip(g.data(), g.size(), out, 1024));
    std::vector<uint8_t> junk(200, 0xFF);
    junk[0] = 0x1F, junk[1] = 0x8B, junk[2] = 8, junk[3] = 0;
    CHECK(!gunzip(junk.data(), junk.size(), out, 1 << 20));  // block type 3 is invalid
    for (size_t cut = 0; cut < g.size(); ++cut) gunzip(g.data(), cut, out, 1024);
}

TEST(gunzip_real_vgz) {
    const auto path = std::filesystem::path(FT_SOURCE_DIR) / "modules" / "MegaDrive" / "test.vgz";
    if (!std::filesystem::exists(path)) return;
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
    std::vector<uint8_t> out;
    CHECK(gunzip(d.data(), d.size(), out, 64u << 20));
    CHECK(out.size() > d.size());
    CHECK(out.size() >= 4 && std::memcmp(out.data(), "Vgm ", 4) == 0);
    for (size_t cut = 20; cut < d.size(); cut += d.size() / 9 + 1) gunzip(d.data(), cut, out, 64u << 20);  // no crash
}
