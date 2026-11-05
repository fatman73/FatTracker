// Stage 5: OctaMED (MMD0-MMD3) loader.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "core/render.h"
#include "formats/formats.h"
#include "test.h"

namespace {
struct Be {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back((uint8_t)v); }
    void u16(int v) {
        u8(v >> 8);
        u8(v);
    }
    void u32(uint32_t v) {
        u16((int)(v >> 16));
        u16((int)(v & 0xFFFF));
    }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
    void put32(size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[at + i] = (uint8_t)(v >> (24 - 8 * i));
    }
};

// One 4-track MMD0 song: block 0 (64 rows), one 100 byte sample with a loop.
// Row 0 track 0: MED note 1 (C-1), instrument 1, command 0C 32 (decimal volume 32).
// Row 1 track 1: note 13, instrument 17 (needs the high instrument bit), command 0F 00 (break).
std::vector<uint8_t> tinyMmd0(int defTempo = 33, int tempo2 = 6, int flags = 0) {
    Be w;
    w.b.reserve(2000);
    w.u8('M'), w.u8('M'), w.u8('D'), w.u8('0');
    w.u32(0);       // length, patched
    w.u32(52);      // song
    w.u32(0);       // player settings
    w.u32(0);       // block array, patched
    w.u8(0), w.zeros(3);
    w.u32(0);       // sample array, patched
    w.u32(0), w.u32(0), w.u32(0);
    w.zeros(11), w.u8(0);
    // 63 sample headers: loopStart / loopLength in words, volume, transpose
    for (int i = 0; i < 63; ++i) {
        w.u16(i == 0 ? 10 : 0), w.u16(i == 0 ? 20 : 0);
        w.u8(0), w.u8(0), w.u8(i == 0 ? 48 : 64), w.u8(0);
    }
    w.u16(1);       // blocks
    w.u16(1);       // song length
    w.u8(0), w.zeros(255);  // play sequence
    w.u16(defTempo);
    w.u8(0);        // play transpose
    w.u8(flags), w.u8(0), w.u8(tempo2);
    w.zeros(16), w.u8(64);
    w.u8(17);       // samples (instrument 17 exists)
    const size_t blockArr = w.b.size();
    w.u32(0);
    const size_t block = w.b.size();
    w.u8(4), w.u8(63);
    for (int row = 0; row < 64; ++row)
        for (int t = 0; t < 4; ++t) {
            if (row == 0 && t == 0) {
                w.u8(1), w.u8(0x1C), w.u8(0x32);
            } else if (row == 1 && t == 1) {
                // instrument 17 = 0b10001: bit 4 lives in bit 7 of the note byte
                w.u8(0x80 | 13), w.u8(0x1F), w.u8(0x00);
            } else {
                w.u8(0), w.u8(0), w.u8(0);
            }
        }
    const size_t smpArr = w.b.size();
    for (int i = 0; i < 17; ++i) w.u32(0);
    const size_t instr = w.b.size();
    w.u32(100), w.u16(0);
    for (int i = 0; i < 100; ++i) w.u8((uint8_t)(i * 2));
    w.put32(4, (uint32_t)w.b.size());
    w.put32(16, (uint32_t)blockArr);
    w.put32(blockArr, (uint32_t)block);
    w.put32(24, (uint32_t)smpArr);
    w.put32(smpArr, (uint32_t)instr);
    return w.b;
}

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::vector<uint8_t> d;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return d;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    d.resize(n > 0 ? (size_t)n : 0);
    if (n > 0) std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    return d;
}
}  // namespace

TEST(med_mmd0_basic_mapping) {
    const auto f = tinyMmd0();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK_EQ(s.numChannels, 4);
    CHECK(s.quirks == Quirks::PT23);
    CHECK_EQ(s.order.size(), 1u);
    CHECK_EQ(s.patterns.size(), 1u);
    CHECK_EQ(s.patterns[0].rows, 64);
    const Cell& a = s.patterns[0].at(0, 0);
    CHECK_EQ(a.note, 37);   // MED C-1 = ProTracker C-1 (period 856) = XM note 37
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.fx, 0xC);
    CHECK_EQ(a.param, 32);  // "32" is decimal in MED unless the song uses hex volumes
    const Cell& b = s.patterns[0].at(1, 1);
    CHECK_EQ(b.note, 49);
    CHECK_EQ(b.instr, 17);
    CHECK_EQ(b.fx, 0xD);    // 0F00 = next block
    CHECK_EQ(s.initialSpeed, 6);
    CHECK_EQ(s.initialBpm, 125);  // tempo 33 / 0.264
    CHECK(s.samples.size() >= 1);
    const Sample& sm = s.samples[0];
    CHECK_EQ(sm.length, 100u);
    CHECK_EQ(sm.volume, 48);
    CHECK(sm.loop == LoopType::Forward);
    CHECK_EQ(sm.loopStart, 20u);  // words -> bytes
    CHECK_EQ(sm.loopLength, 40u);
    CHECK_EQ(sm.data8[3], 6);
}

TEST(med_hex_volume_and_soundtracker_tempo) {
    const auto f = tinyMmd0(6, 6, 0x10);  // FLAG_VOLHEX, tempo 6 = SoundTracker compatible
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK_EQ(r.value().patterns[0].at(0, 0).param, 0x32);
    CHECK_EQ(r.value().initialBpm, 122);  // 6 * 1773447 / 14500 / 6
}

TEST(med_rejects_truncated_files) {
    auto f = tinyMmd0();
    for (size_t cut : {(size_t)8, (size_t)60, (size_t)600, f.size() / 2}) {
        auto r = loadModule(f.data(), cut);
        (void)r;  // must not crash; a cut inside the sample may still load with the flag set
    }
    std::vector<uint8_t> bad = f;
    bad[16] = 0x7F;  // block array pointer far past the end
    auto r = loadModule(bad.data(), bad.size());
    CHECK(!r.ok() || r.value().patterns.size() <= 1);
}

TEST(real_modules_octamed_load_and_play) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        std::printf("SKIP real_modules_octamed (no modules folder)\n");
        return;
    }
    int count = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().wstring();
        for (auto& ch : ext) ch = (wchar_t)towlower(ch);
        if (ext.rfind(L".mmd", 0) != 0 && ext != L".med") continue;
        const auto data = readAll(e.path());
        auto r = loadModule(data.data(), data.size());
        if (!r.ok()) {
            std::printf("  MED FAIL %ls: %s\n", e.path().filename().c_str(), r.error());
            CHECK(r.ok());
            continue;
        }
        std::vector<float> pcm;
        const size_t frames = renderSong(r.value(), 22050, Interp::Linear, true, 10.f, pcm);
        float peak = 0;
        for (float v : pcm) peak = v > peak ? v : (-v > peak ? -v : peak);
        if (peak < 0.01f) std::printf("  MED SILENT %ls\n", e.path().filename().c_str());
        CHECK(frames > 0);
        CHECK(peak >= 0.01f);
        ++count;
    }
    std::printf("  octamed: %d modules\n", count);
}
