// Stage 5: DigiBooster 1.x (DIGI) and DigiBooster Pro (DBM0) loaders.
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
    void str(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) u8(i < std::strlen(s) ? s[i] : 0);
    }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
    size_t chunk(const char* id) {
        str(id, 4);
        u32(0);
        return b.size();
    }
    void endChunk(size_t start) {
        const uint32_t n = (uint32_t)(b.size() - start);
        for (int i = 0; i < 4; ++i) b[start - 4 + i] = (uint8_t)(n >> (24 - 8 * i));
    }
};

// DBM0: 2 channels, 1 instrument (8363 Hz, volume 40, forward loop 10+20), 1 pattern of 4 rows,
// a 16-bit sample of 64 frames. Row 0 ch 1: C-4, instrument 1, effects C 20 (volume) + F 06 (speed).
// Row 1 ch 2: key off. Row 2 ch 1: E-5 with effect A 12 (both nibbles: up wins) + 8 80 (panning).
std::vector<uint8_t> tinyDbm() {
    Be w;
    w.str("DBM0", 4), w.u8(2), w.u8(0x22), w.u16(0);
    size_t c = w.chunk("NAME");
    w.str("tiny dbm", 8);
    w.endChunk(c);
    c = w.chunk("INFO");
    w.u16(1), w.u16(1), w.u16(1), w.u16(1), w.u16(2);
    w.endChunk(c);
    c = w.chunk("SONG");
    w.str("main", 44), w.u16(1), w.u16(0);
    w.endChunk(c);
    c = w.chunk("INST");
    w.str("bass", 30), w.u16(1), w.u16(40), w.u32(8363), w.u32(10), w.u32(20), w.u16(0), w.u16(1);
    w.endChunk(c);
    c = w.chunk("PATT");
    w.u16(4);
    Be p;
    p.u8(1), p.u8(0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20), p.u8(0x40), p.u8(1);
    p.u8(0x0F), p.u8(0x06);  // effect 2 (read first)
    p.u8(0x0C), p.u8(0x20);  // effect 1
    p.u8(0);
    p.u8(2), p.u8(0x01), p.u8(0x1F), p.u8(0);
    p.u8(1), p.u8(0x01 | 0x04 | 0x08 | 0x10 | 0x20), p.u8(0x54);
    p.u8(0x08), p.u8(0x80);
    p.u8(0x0A), p.u8(0x12);
    p.u8(0), p.u8(0);
    w.u32((uint32_t)p.b.size());
    w.b.insert(w.b.end(), p.b.begin(), p.b.end());
    w.endChunk(c);
    c = w.chunk("SMPL");
    w.u32(2), w.u32(64);
    for (int i = 0; i < 64; ++i) w.u16(i * 100);
    w.endChunk(c);
    return w.b;
}

// DIGI 1.x: 4 channels, packed patterns, one sample of 32 bytes, row 0 ch 0: period 428, sample 1, C 30.
std::vector<uint8_t> tinyDigi() {
    Be w;
    w.str("DIGI Booster module", 20), w.str("V1.7", 4), w.u8(0x17), w.u8(4), w.u8(1), w.zeros(19);
    w.u8(0), w.u8(0);  // last pattern, last order
    w.zeros(128);
    for (int i = 0; i < 31; ++i) w.u32(i == 0 ? 32 : 0);
    for (int i = 0; i < 31; ++i) w.u32(0);
    for (int i = 0; i < 31; ++i) w.u32(0);
    for (int i = 0; i < 31; ++i) w.u8(i == 0 ? 50 : 0);
    for (int i = 0; i < 31; ++i) w.u8(0);
    w.str("tiny digi", 32);
    w.zeros(31 * 30);
    Be p;
    p.u8(0x80);
    p.zeros(63);
    p.u8(0x01), p.u8(0xAC), p.u8(0x1C), p.u8(0x30);  // sample 1, period 0x1AC = 428, effect C30
    w.u16((int)p.b.size());
    w.b.insert(w.b.end(), p.b.begin(), p.b.end());
    for (int i = 0; i < 32; ++i) w.u8(i * 3);
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

int countPlayable(const wchar_t* ext1, const wchar_t* ext2) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return -1;
    int count = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().wstring();
        for (auto& ch : ext) ch = (wchar_t)towlower(ch);
        if (ext != ext1 && ext != ext2) continue;
        const auto data = readAll(e.path());
        auto r = loadModule(data.data(), data.size());
        if (!r.ok()) {
            std::printf("  FAIL %ls: %s\n", e.path().filename().wstring().c_str(), r.error());
            CHECK(r.ok());
            continue;
        }
        std::vector<float> pcm;
        renderSong(r.value(), 22050, Interp::Linear, true, 10.f, pcm);
        float peak = 0;
        for (float v : pcm) peak = v > peak ? v : (-v > peak ? -v : peak);
        if (peak < 0.01f) std::printf("  SILENT %ls\n", e.path().filename().wstring().c_str());
        CHECK(peak >= 0.01f);
        ++count;
    }
    return count;
}
}  // namespace

TEST(dbm_basic_mapping) {
    const auto f = tinyDbm();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK_EQ(s.numChannels, 4);  // at least 4 like every other song
    CHECK(std::strcmp(s.name, "tiny dbm") == 0);
    CHECK_EQ(s.order.size(), 1u);
    CHECK_EQ(s.patterns[0].rows, 4);
    const Cell& a = s.patterns[0].at(0, 0);
    CHECK_EQ(a.note, 49);       // octave 4, semitone 0 = C-4
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.vol, 0x10 + 0x20);  // C20 goes to the volume column
    CHECK_EQ(a.fx, 0xF);
    CHECK_EQ(a.param, 6);
    CHECK_EQ(s.patterns[0].at(1, 1).note, NOTE_OFF);
    const Cell& c = s.patterns[0].at(2, 0);
    CHECK_EQ(c.note, 49 + 12 + 4);  // octave 5, semitone 4 = E-5
    CHECK_EQ(c.vol, 0xC0 + 8);      // panning 80 -> volume column
    CHECK_EQ(c.fx, 0xA);
    CHECK_EQ(c.param, 0x10);        // "up" nibble wins
    CHECK_EQ(s.samples.size(), 1u);
    const Sample& sm = s.samples[0];
    CHECK(sm.is16);
    CHECK_EQ(sm.length, 64u);
    CHECK_EQ(sm.data16[3], 300);
    CHECK_EQ(sm.volume, 40);
    CHECK(sm.loop == LoopType::Forward);
    CHECK_EQ(sm.loopStart, 10u);
    CHECK_EQ(sm.loopLength, 20u);
    CHECK_EQ(sm.relativeNote, -1);  // 8363 * 8303 / 8363 = 8303 Hz: a little flat of C-4
}

TEST(digi_basic_mapping) {
    const auto f = tinyDigi();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK_EQ(s.numChannels, 4);
    CHECK(s.quirks == Quirks::PT23);
    CHECK_EQ(s.patterns.size(), 1u);
    const Cell& a = s.patterns[0].at(0, 0);
    CHECK_EQ(a.note, 49);
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.fx, 0xC);
    CHECK_EQ(a.param, 0x30);
    CHECK_EQ(s.samples[0].length, 32u);
    CHECK_EQ(s.samples[0].volume, 50);
    CHECK_EQ(s.samples[0].data8[2], 6);
}

TEST(dbm_and_digi_survive_truncation) {
    for (const auto& f : {tinyDbm(), tinyDigi()})
        for (size_t cut = 4; cut < f.size(); cut += 37) {
            auto r = loadModule(f.data(), cut);
            (void)r;  // must not crash
        }
}

TEST(real_modules_digibooster_load_and_play) {
    const int n = countPlayable(L".digi", L".dbm");
    if (n < 0) std::printf("SKIP real_modules_digibooster (no modules folder)\n");
    else std::printf("  digibooster: %d modules\n", n);
}
