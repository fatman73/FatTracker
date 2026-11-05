// Stage 4: PC tracker formats - Scream Tracker 3 (S3M), MultiTracker (MTM), Composer 669, Impulse Tracker (IT).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/render.h"
#include "formats/formats.h"
#include "player_util.h"
#include "test.h"

namespace {
struct Le {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back((uint8_t)v); }
    void u16(int v) {
        u8(v);
        u8(v >> 8);
    }
    void u32(uint32_t v) {
        u16((int)(v & 0xFFFF));
        u16((int)(v >> 16));
    }
    void str(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) u8(i < std::strlen(s) ? s[i] : 0);
    }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
    void put16(size_t at, int v) {
        b[at] = (uint8_t)v;
        b[at + 1] = (uint8_t)(v >> 8);
    }
    void pad16() {
        while (b.size() % 16) u8(0);
    }
};

// S3M: 4 channels (L R R L), 1 sample (unsigned 8-bit, 64 frames, 8363 Hz, loop 8..40), 1 pattern.
// Row 0 ch 0: C-4 sample 1, volume 32, A06 (speed). Row 1 ch 1: D-5, T96 (tempo). Row 2 ch 2: note cut,
// D04 (volume slide down). Row 3 ch 3: S8F (pan) . Row 4 ch 0: C08 break to row 8 (BCD).
std::vector<uint8_t> tinyS3m() {
    Le w;
    w.str("tiny s3m", 28);
    w.u8(0x1A), w.u8(16), w.u16(0);
    w.u16(2), w.u16(1), w.u16(1), w.u16(0), w.u16(0x1320), w.u16(2);
    w.str("SCRM", 4);
    w.u8(64), w.u8(5), w.u8(140), w.u8(0x80 | 48), w.u8(0), w.u8(0), w.u16(0), w.u32(0), w.u16(0), w.u16(0);
    const uint8_t chans[32] = {0, 8, 9, 1, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                               255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    for (uint8_t c : chans) w.u8(c);
    w.u8(0), w.u8(0xFF);  // orders
    const size_t smpPtr = w.b.size();
    w.u16(0);
    const size_t patPtr = w.b.size();
    w.u16(0);
    w.pad16();
    const size_t smpHdr = w.b.size();
    w.put16(smpPtr, (int)(smpHdr / 16));
    w.u8(1), w.str("bass.raw", 12);
    const size_t dataPtrAt = w.b.size();
    w.u8(0), w.u16(0);
    w.u32(64), w.u32(8), w.u32(40), w.u8(48), w.u8(0), w.u8(0), w.u8(1), w.u32(8363), w.zeros(12);
    w.str("bass", 28), w.str("SCRS", 4);
    w.pad16();
    const size_t pat = w.b.size();
    w.put16(patPtr, (int)(pat / 16));
    Le p;
    p.u8(0x20 | 0x40 | 0x80 | 0), p.u8(0x40), p.u8(1), p.u8(32), p.u8(1), p.u8(6), p.u8(0);   // row 0
    p.u8(0x20 | 0x80 | 1), p.u8(0x52), p.u8(0), p.u8(20), p.u8(0x96), p.u8(0);                // row 1
    p.u8(0x20 | 0x80 | 2), p.u8(0xFE), p.u8(0), p.u8(4), p.u8(0x04), p.u8(0);                 // row 2
    p.u8(0x80 | 3), p.u8(19), p.u8(0x8F), p.u8(0);                                           // row 3
    p.u8(0x80 | 0), p.u8(3), p.u8(0x08), p.u8(0);                                            // row 4
    for (int r = 5; r < 64; ++r) p.u8(0);
    w.u16((int)p.b.size() + 2);
    w.b.insert(w.b.end(), p.b.begin(), p.b.end());
    w.pad16();
    const size_t data = w.b.size();
    w.b[dataPtrAt + 1] = (uint8_t)((data / 16) & 0xFF);
    w.b[dataPtrAt + 2] = (uint8_t)((data / 16) >> 8);
    for (int i = 0; i < 64; ++i) w.u8(128 + i);  // unsigned
    return w.b;
}

// MTM: 4 channels, 1 sample (unsigned 8-bit), 1 track used on channel 0, 1 pattern.
std::vector<uint8_t> tinyMtm() {
    Le w;
    w.str("MTM", 3), w.u8(0x10), w.str("tiny mtm", 20);
    w.u16(1), w.u8(0), w.u8(0), w.u16(0), w.u8(1), w.u8(0), w.u8(64), w.u8(4);
    for (int i = 0; i < 32; ++i) w.u8(i % 4 == 0 || i % 4 == 3 ? 3 : 12);
    w.str("lead", 22), w.u32(32), w.u32(0), w.u32(0), w.u8(0), w.u8(40), w.u8(0);
    w.u8(0), w.zeros(127);  // order list
    // track 1: row 0 = note 24 (-> XM 49), instrument 1, effect C 20
    w.u8((24 << 2) | 0), w.u8((1 << 4) | 0xC), w.u8(0x20);
    w.zeros(63 * 3);
    w.u16(1);  // pattern 0, channel 0 -> track 1
    w.zeros(31 * 2);
    for (int i = 0; i < 32; ++i) w.u8(128 + i);
    return w.b;
}

// 669: 1 sample, 1 pattern, row 0 ch 0: note 24 (-> XM 49), instrument 0 (-> 1), volume 15, speed 3.
std::vector<uint8_t> tiny669() {
    Le w;
    w.str("if", 2), w.str("tiny 669", 108);
    w.u8(1), w.u8(1), w.u8(0);
    w.u8(0);
    for (int i = 1; i < 128; ++i) w.u8(0xFF);
    w.u8(3);
    for (int i = 1; i < 128; ++i) w.u8(0);
    w.u8(32);  // break after row 32
    for (int i = 1; i < 128; ++i) w.u8(0);
    w.str("piano.smp", 13), w.u32(16), w.u32(0), w.u32(0xFFFFF);
    for (int row = 0; row < 64; ++row)
        for (int ch = 0; ch < 8; ++ch) {
            if (row == 0 && ch == 0) {
                w.u8(24 << 2), w.u8((0 << 4) | 15), w.u8(0xFF);
            } else {
                w.u8(0xFF), w.u8(0), w.u8(0xFF);
            }
        }
    for (int i = 0; i < 16; ++i) w.u8(128 + 8 * i);
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

int playable(const std::vector<std::wstring>& exts) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return -1;
    int count = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().wstring();
        for (auto& ch : ext) ch = (wchar_t)towlower(ch);
        bool want = false;
        for (const auto& x : exts) want |= ext == x;
        if (!want) continue;
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

TEST(s3m_basic_mapping) {
    const auto f = tinyS3m();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK(std::strcmp(s.name, "tiny s3m") == 0);
    CHECK_EQ(s.numChannels, 4);
    CHECK_EQ(s.initialSpeed, 5);
    CHECK_EQ(s.initialBpm, 140);
    CHECK_EQ(s.order.size(), 1u);
    CHECK(s.channelPan[0] < 0x80 && s.channelPan[1] > 0x80);  // ST3 channels L1 / R1
    const Cell& a = s.patterns[0].at(0, 0);
    CHECK_EQ(a.note, 49);
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.vol, 0x10 + 32);
    CHECK_EQ(a.fx, 0xF);
    CHECK_EQ(a.param, 6);
    const Cell& b = s.patterns[0].at(1, 1);
    CHECK_EQ(b.note, 49 + 12 + 2);
    CHECK_EQ(b.vol, 0);  // no volume given on this row
    CHECK_EQ(b.fx, 0xF);
    CHECK_EQ(b.param, 0x96);  // tempo 150
    const Cell& c = s.patterns[0].at(2, 2);
    CHECK_EQ(c.note, NOTE_OFF);
    CHECK_EQ(c.fx, 0xA);
    CHECK_EQ(c.param, 0x04);
    CHECK_EQ(s.patterns[0].at(3, 3).fx, 0x8);  // S8F = pan right
    CHECK_EQ(s.patterns[0].at(3, 3).param, 0xFF);
    CHECK_EQ(s.patterns[0].at(4, 0).fx, 0xD);
    CHECK_EQ(s.patterns[0].at(4, 0).param, 0x08);
    const Sample& sm = s.samples[0];
    CHECK_EQ(sm.length, 64u);
    CHECK_EQ(sm.volume, 48);
    CHECK_EQ(sm.data8[5], 5);  // unsigned -> signed
    CHECK(sm.loop == LoopType::Forward);
    CHECK_EQ(sm.loopStart, 8u);
    CHECK_EQ(sm.loopLength, 32u);
    CHECK_EQ(sm.relativeNote, 0);
}

TEST(s3m_notes_keep_channel_panning) {
    // S3M samples have no panning of their own: a note with an instrument must not centre the channel
    const auto f = tinyS3m();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    Mixer m{48000};
    Player p{m};
    p.setSong(&r.value());
    p.play();
    p.tick();
    CHECK_EQ(p.channel(0).pan, r.value().channelPan[0]);
}

TEST(mtm_basic_mapping) {
    const auto f = tinyMtm();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK(std::strcmp(s.name, "tiny mtm") == 0);
    CHECK_EQ(s.numChannels, 4);
    const Cell& a = s.patterns[0].at(0, 0);
    CHECK_EQ(a.note, 49);
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.fx, 0xC);
    CHECK_EQ(a.param, 0x20);
    CHECK_EQ(s.samples[0].length, 32u);
    CHECK_EQ(s.samples[0].volume, 40);
    CHECK_EQ(s.samples[0].data8[3], 3);
}

TEST(c669_basic_mapping) {
    const auto f = tiny669();
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK_EQ(s.numChannels, 8);
    CHECK_EQ(s.initialBpm, 78);  // Composer 669 plays at 78 BPM
    const Cell& a = s.patterns[0].at(0, 0);
    CHECK_EQ(a.note, 49);
    CHECK_EQ(a.instr, 1);
    CHECK_EQ(a.vol, 0x10 + 64);
    bool speed = false, brk = false;
    for (int ch = 0; ch < 8; ++ch) {
        speed |= s.patterns[0].at(0, ch).fx == 0xF && s.patterns[0].at(0, ch).param == 3;
        brk |= s.patterns[0].at(32, ch).fx == 0xD;
    }
    CHECK(speed);  // the pattern's speed
    CHECK(brk);    // and its break row
    CHECK_EQ(s.samples[0].length, 16u);
    CHECK(s.samples[0].loop == LoopType::None);  // loop end past the sample = no loop
}

TEST(pc_formats_survive_truncation) {
    for (const auto& f : {tinyS3m(), tinyMtm(), tiny669()})
        for (size_t cut = 2; cut < f.size(); cut += 29) {
            auto r = loadModule(f.data(), cut);
            (void)r;
        }
}

TEST(real_modules_pc_load_and_play) {
    const int n = playable({L".s3m", L".mtm", L".669"});
    if (n < 0) std::printf("SKIP real_modules_pc (no modules folder)\n");
    else std::printf("  s3m/mtm/669: %d modules\n", n);
}
