// Stage 5: Symphonie / Symphonie Pro ("SymM") loader.
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
    void value(int32_t id, uint32_t v) {
        u32((uint32_t)id);
        u32(v);
    }
    void block(int32_t id, const std::vector<uint8_t>& body) {
        u32((uint32_t)id);
        u32((uint32_t)body.size());
        b.insert(b.end(), body.begin(), body.end());
    }
};

constexpr int kCh = 6, kRows = 4;

// 6 channels, track length 4, tempo 100 (= 124 BPM), one instrument "bass" with a raw 8-bit sample of
// 100 bytes. Pattern 0 (events are command, note, param, instrument):
//   row 0 ch 0: key on note 36, volume 50        row 1 ch 0: set speed 3
//   row 2 ch 1: vibrato speed 0x20>>3, depth 6     row 3 ch 0: key off
// Positions: pattern 0 twice (loop 2), then pattern 0 transposed +12. One sequence plays both positions.
// "PACKÿÿ" run-length encoding: empty events (00 FF 00 00) as repeated dwords, the rest raw.
std::vector<uint8_t> pack(const std::vector<uint8_t>& d) {
    Be w;
    w.str("PACK", 4), w.u8(0xFF), w.u8(0xFF), w.u32((uint32_t)d.size());
    for (size_t i = 0; i < d.size();) {
        size_t run = 0;
        while (i + run * 4 + 4 <= d.size() && run < 255 && d[i + run * 4] == 0 && d[i + run * 4 + 1] == 0xFF &&
               d[i + run * 4 + 2] == 0 && d[i + run * 4 + 3] == 0)
            ++run;
        if (run >= 2) {
            w.u8(1), w.u8((int)run), w.u8(0), w.u8(0xFF), w.u8(0), w.u8(0);
            i += run * 4;
        } else {
            const size_t n = d.size() - i < 4 ? d.size() - i : 4;
            w.u8(0), w.u8((int)n);
            w.b.insert(w.b.end(), d.begin() + i, d.begin() + i + n);
            i += n;
        }
    }
    w.u8(0xFF);
    return w.b;
}

std::vector<uint8_t> tinySym(bool packed = false) {
    Be w;
    w.str("SymM", 4), w.u32(1), w.u32(0xFFFFFFFF), w.u32(kCh);
    w.value(-2, kRows);
    w.value(-5, 4);
    w.value(-6, 100);
    Be ins;
    ins.str("bass", 128);
    ins.u8(0), ins.zeros(5), ins.u8(100), ins.zeros(121);  // type Normal, volume 100
    w.block(-14, ins.b);
    Be pos;
    auto position = [&](int loops, int transpose) {
        pos.zeros(4), pos.u16(loops), pos.u16(0), pos.u16(0), pos.u16(0), pos.u16(kRows), pos.u16(5);
        pos.u16(transpose), pos.u16(0), pos.zeros(12);
    };
    position(2, 0);
    position(1, 12);
    w.block(-10, pos.b);
    Be seq;
    seq.u16(0), seq.u16(2), seq.u16(1), seq.u16(0), seq.u16(0), seq.zeros(6);
    seq.u16(0), seq.u16(0), seq.u16(0), seq.u16(0xFFFF), seq.u16(0), seq.zeros(6);
    w.block(-15, seq.b);
    std::vector<uint8_t> ev((size_t)kCh * kRows * 4, 0);
    for (size_t i = 0; i < ev.size(); i += 4) ev[i + 1] = 0xFF;  // note -1 = empty
    auto put = [&](int row, int ch, int cmd, int note, int param, int inst) {
        uint8_t* e = &ev[((size_t)row * kCh + ch) * 4];
        e[0] = (uint8_t)cmd, e[1] = (uint8_t)note, e[2] = (uint8_t)param, e[3] = (uint8_t)inst;
    };
    put(0, 0, 0, 36, 50, 0);
    put(1, 0, 9, -1, 3, 0);
    put(2, 1, 13, -1, 6, 0x20);
    put(3, 0, 0, -1, 251, 0);
    w.block(-13, packed ? pack(ev) : ev);
    std::vector<uint8_t> smp(100);
    for (int i = 0; i < 100; ++i) smp[i] = (uint8_t)(i - 50);
    if (packed) {  // 8-bit delta coding inside a packed chunk
        std::vector<uint8_t> delta(100);
        for (int i = 0; i < 100; ++i) delta[i] = (uint8_t)(smp[i] - (i ? smp[i - 1] : 0));
        w.block(-17, pack(delta));
    } else {
        w.block(-11, smp);
    }
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

int countPlayable(const wchar_t* ext1) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return -1;
    int count = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().wstring();
        for (auto& ch : ext) ch = (wchar_t)towlower(ch);
        if (ext != ext1) continue;
        const auto data = readAll(e.path());
        auto r = loadModule(data.data(), data.size());
        if (!r.ok()) {
            std::printf("  FAIL %ls: %s\n", e.path().filename().c_str(), r.error());
            CHECK(r.ok());
            continue;
        }
        std::vector<float> pcm;
        renderSong(r.value(), 22050, Interp::Linear, true, 10.f, pcm);
        float peak = 0;
        for (float v : pcm) peak = v > peak ? v : (-v > peak ? -v : peak);
        if (peak < 0.01f) std::printf("  SILENT %ls\n", e.path().filename().c_str());
        CHECK(peak >= 0.01f);
        ++count;
    }
    return count;
}
}  // namespace

TEST(symmod_basic_mapping) {
    const auto f = tinySym();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK_EQ(s.numChannels, kCh);
    CHECK_EQ(s.initialSpeed, 5);
    CHECK_EQ(s.initialBpm, 124);
    CHECK_EQ(s.order.size(), 3u);  // position 0 looped twice, then the transposed copy
    CHECK_EQ(s.patterns.size(), 2u);
    if (s.order.size() != 3 || s.patterns.size() != 2) return;
    CHECK_EQ(s.order[0], s.order[1]);
    CHECK(s.order[0] != s.order[2]);
    const Pattern& p = s.patterns[s.order[0]];
    CHECK_EQ(p.rows, kRows);
    const Cell& a = p.at(0, 0);
    CHECK_EQ(a.note, 49);  // Symphonie note 36 = C-4
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.vol, 0x10 + 32);  // volume 50 of 100
    CHECK_EQ(p.at(1, 0).fx, 0xF);  // set speed 3
    CHECK_EQ(p.at(1, 0).param, 3);
    CHECK_EQ(p.at(2, 1).fx, 0x4);  // vibrato: speed 0x20 >> 3 = 4, depth 6
    CHECK_EQ(p.at(2, 1).param, 0x46);
    CHECK_EQ(p.at(3, 0).note, NOTE_OFF);
    CHECK_EQ(s.patterns[s.order[2]].at(0, 0).note, 49 + 12);  // position transpose
    CHECK(s.samples.size() >= 1u);
    if (s.samples.empty()) return;
    CHECK_EQ(s.samples[0].length, 100u);
    CHECK(!s.samples[0].is16);
    CHECK(std::strcmp(s.samples[0].name, "bass") == 0);
}

TEST(symmod_packed_chunks_match_raw) {
    const auto a = tinySym(false), b = tinySym(true);
    auto ra = loadModule(a.data(), a.size()), rb = loadModule(b.data(), b.size());
    CHECK(ra.ok() && rb.ok());
    if (!ra.ok() || !rb.ok()) return;
    const Song &x = ra.value(), &y = rb.value();
    CHECK_EQ(y.loadFlags, 0);
    CHECK_EQ(x.patterns.size(), y.patterns.size());
    for (size_t p = 0; p < x.patterns.size() && p < y.patterns.size(); ++p)
        for (int row = 0; row < kRows; ++row)
            for (int ch = 0; ch < kCh; ++ch) {
                const Cell &c = x.patterns[p].at(row, ch), &d = y.patterns[p].at(row, ch);
                CHECK(c.note == d.note && c.instr == d.instr && c.vol == d.vol && c.fx == d.fx && c.param == d.param);
            }
    CHECK(!x.samples.empty() && !y.samples.empty());
    if (x.samples.empty() || y.samples.empty()) return;
    CHECK(x.samples[0].data8 == y.samples[0].data8);
}

TEST(symmod_survives_truncation) {
    for (const auto& f : {tinySym(false), tinySym(true)})
        for (size_t cut = 4; cut < f.size(); cut += 37) {
            auto r = loadModule(f.data(), cut);
            (void)r;  // must not crash
        }
}

TEST(real_modules_symphonie_load_and_play) {
    const int n = countPlayable(L".symmod");
    if (n < 0) std::printf("SKIP real_modules_symphonie (no modules folder)\n");
    else std::printf("  symphonie: %d modules\n", n);
}
