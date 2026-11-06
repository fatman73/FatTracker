// Stage 5: Symphonie / Symphonie Pro ("SymM") loader.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "core/player.h"
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

// ---- a more flexible builder: any instruments, samples and one pattern of events
namespace {
struct SymSpec {
    std::vector<std::vector<uint8_t>> ins;  // 256-byte instrument records
    std::vector<std::vector<int8_t>> smp;   // raw 8-bit sample per instrument (empty = empty-sample chunk)
    std::vector<uint8_t> ev;                // kCh * kRows events (command, note, param, instrument)
    uint32_t sampleBoost = 0;               // 0 = no chunk (Symphonie, not Pro)
    int speed = 6;
    SymSpec() {
        ev.assign((size_t)kCh * kRows * 4, 0);
        for (size_t i = 0; i < ev.size(); i += 4) ev[i + 1] = 0xFF;
    }
    void put(int row, int ch, int cmd, int note, int param, int inst) {
        uint8_t* e = &ev[((size_t)row * kCh + ch) * 4];
        e[0] = (uint8_t)cmd, e[1] = (uint8_t)note, e[2] = (uint8_t)param, e[3] = (uint8_t)inst;
    }
};

std::vector<uint8_t> normalIns(const char* name, int type = 0, int volume = 100) {
    Be b;
    b.str(name, 128);
    b.u8(type), b.zeros(5), b.u8(volume), b.zeros(121);
    return b.b;
}

// "ViRT" header: version 0 = virtual (4-byte note events), 1 = transwave (2 points of 10 bytes)
void virtHeader(Be& b, int version, int numEvents) {
    b.str("ViRT", 4), b.u8(0), b.u8(0), b.u16(version), b.u16(0), b.u16(0), b.u16(0);
    b.u16(numEvents), b.u16(20), b.u16(version ? 10 : 4);
}

struct VEv {
    int note, inst, param;
};
std::vector<uint8_t> virtualIns(const std::vector<VEv>& evs, bool queue) {
    Be b;
    virtHeader(b, 0, (int)evs.size());
    for (const VEv& e : evs) b.u8(0), b.u8(e.note), b.u8(e.param), b.u8(e.inst);
    b.zeros(128 - b.b.size());
    b.u8(0), b.zeros(5), b.u8(100), b.zeros(5);
    b.u8(queue ? 2 : 0);  // sample flags: AsQueue
    b.zeros(256 - b.b.size());
    return b.b;
}

// loop start / length in 1/65536 percent of the source length
std::vector<uint8_t> transwaveIns(int src1, uint32_t ls1, uint32_t ll1, int src2, uint32_t ls2, uint32_t ll2) {
    Be b;
    virtHeader(b, 1, 2);
    b.u16(src1), b.u16(100), b.u32(ls1), b.u32(ll1), b.u32(0);
    b.u16(src2), b.u16(100), b.u32(ls2), b.u32(ll2), b.u32(0);
    b.zeros(128 - b.b.size());
    b.u8(0), b.zeros(5), b.u8(100), b.zeros(121);
    return b.b;
}

std::vector<uint8_t> buildSym(const SymSpec& sp) {
    Be w;
    w.str("SymM", 4), w.u32(1), w.u32(0xFFFFFFFF), w.u32(kCh);
    w.value(-2, kRows);
    w.value(-5, 4);
    w.value(-6, 100);
    if (sp.sampleBoost) w.value(10, sp.sampleBoost);
    Be ins;
    for (const auto& r : sp.ins) ins.b.insert(ins.b.end(), r.begin(), r.end());
    w.block(-14, ins.b);
    Be pos;
    pos.zeros(4), pos.u16(1), pos.u16(0), pos.u16(0), pos.u16(0), pos.u16(kRows), pos.u16(sp.speed);
    pos.u16(0), pos.u16(0), pos.zeros(12);
    w.block(-10, pos.b);
    Be seq;
    seq.u16(0), seq.u16(1), seq.u16(1), seq.u16(0), seq.u16(0), seq.zeros(6);
    seq.u16(0), seq.u16(0), seq.u16(0), seq.u16(0xFFFF), seq.u16(0), seq.zeros(6);
    w.block(-15, seq.b);
    w.block(-13, sp.ev);
    for (const auto& s : sp.smp) {
        if (s.empty()) {
            w.u32((uint32_t)-12);
            continue;
        }
        w.block(-11, std::vector<uint8_t>(s.begin(), s.end()));
    }
    return w.b;
}

std::vector<int8_t> dc(int n, int v) { return std::vector<int8_t>((size_t)n, (int8_t)v); }

int at16(const Sample& s, size_t i) { return s.is16 ? s.data16[i] : s.data8[i] * 256; }
}  // namespace

TEST(symmod_virtual_instrument_mixes_its_sources) {
    // source 0: 100 frames of +64, source 1: 50 frames of -64, both at the same note
    SymSpec sp;
    sp.ins = {normalIns("up"), normalIns("down"), virtualIns({{36, 0, 0}, {36, 1, 0}}, false)};
    sp.smp = {dc(100, 64), dc(50, -64), {}};
    sp.put(0, 0, 0, 36, 100, 2);
    const auto f = buildSym(sp);
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    CHECK(s.samples.size() >= 3u);
    if (s.samples.size() < 3) return;
    const Sample& v = s.samples[2];
    CHECK(std::strcmp(v.name, "Virtual") == 0);
    CHECK_EQ(v.length, 100u);  // as long as the first source
    if (v.length != 100) return;
    CHECK_NEAR(at16(v, 10), 0, 300);      // both sources: they cancel out
    CHECK_NEAR(at16(v, 80), 32767, 300);  // only the first one, normalised
}

TEST(symmod_virtual_instrument_ignores_source_volume) {
    // the sources mix before their instrument volume: a quiet "up" still cancels "down"
    SymSpec sp;
    sp.ins = {normalIns("up", 0, 25), normalIns("down"), virtualIns({{36, 0, 0}, {36, 1, 0}}, false)};
    sp.smp = {dc(100, 64), dc(100, -64), {}};
    const auto f = buildSym(sp);
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Sample& v = r.value().samples[2];
    CHECK_EQ(v.length, 100u);
    if (v.length != 100) return;
    CHECK_NEAR(at16(v, 50), 0, 300);
}

TEST(symmod_queued_virtual_instrument_plays_sources_in_turn) {
    SymSpec sp;
    sp.ins = {normalIns("up"), normalIns("down"), virtualIns({{36, 0, 0}, {48, 1, 0}}, true)};
    sp.smp = {dc(100, 64), dc(100, -64), {}};
    const auto f = buildSym(sp);
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Sample& v = r.value().samples[2];
    CHECK_EQ(v.length, 150u);  // the second source an octave up takes half the time
    if (v.length != 150) return;
    CHECK_NEAR(at16(v, 50), 32767, 300);
    CHECK_NEAR(at16(v, 120), -32767, 300);
}

TEST(symmod_transwave_instrument_morphs_between_loops) {
    SymSpec sp;
    sp.ins = {normalIns("up"), normalIns("down"), transwaveIns(0, 0, 100u << 16, 1, 0, 100u << 16)};
    sp.smp = {dc(100, 64), dc(100, -64), {}};
    const auto f = buildSym(sp);
    auto r = loadModule(f.data(), f.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& s = r.value();
    const Sample& t = s.samples[2];
    CHECK(std::strcmp(t.name, "Transwave") == 0);
    CHECK_EQ(t.length, 100u * 4 * 80);  // 80 cycles, 4x oversampled
    if (t.length != 32000) return;
    CHECK_NEAR(at16(t, 0), 32767, 300);
    CHECK_NEAR(at16(t, 16000), 0, 600);
    CHECK_NEAR(at16(t, 31999), -32767, 300);
    // an octave above a plain instrument with the same settings
    CHECK_EQ(t.relativeNote, s.samples[0].relativeNote + 12);
}

// Replay From / From Add / Sample Vib, as the Symphonie Pro source does them: offsets in 1/256 of the
// sample, From Add in 1/16384 (signed), and the sample vibrato scaling the Replay From value down.
namespace {
const Song* loadInto(Result<Song>& r, const SymSpec& sp) {
    const auto f = buildSym(sp);
    r = loadModule(f.data(), f.size());
    return r.ok() ? &r.value() : nullptr;
}
SymSpec offsetSpec() {  // one 51200-frame instrument played on row 0
    SymSpec sp;
    sp.ins = {normalIns("long")};
    std::vector<int8_t> d(51200);
    for (size_t i = 0; i < d.size(); ++i) d[i] = (int8_t)((i & 64) ? 100 : -100);
    sp.smp = {d};
    sp.put(0, 0, 0, 36, 0, 0);
    return sp;
}
}  // namespace

TEST(symmod_replay_from_is_in_256ths_of_the_sample) {
    SymSpec sp = offsetSpec();
    sp.put(2, 0, 5, -1, 128, 0);  // replay from 128/256: frame 25600
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    const Cell& c = s->patterns[s->order[0]].at(2, 0);
    CHECK_EQ(c.note, 49);
    CHECK_EQ(c.fx, 9);
    CHECK_EQ(c.param, 100);
}

TEST(symmod_from_add_moves_the_replay_start) {
    SymSpec sp = offsetSpec();
    sp.put(1, 0, 7, -1, 64, 0);    // set from add 64/256: 12800 frames
    sp.put(2, 0, 8, -1, 0xC0, 0);  // from add -64/16384: -200 frames
    sp.put(3, 0, 5, -1, 0, 0);     // replay from 0 -> frame 12600
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    const Cell& c = s->patterns[s->order[0]].at(3, 0);
    CHECK_EQ(c.fx, 9);
    CHECK_EQ(c.param, 49);
}

TEST(symmod_sample_vibrato_scales_the_replay_offset) {
    SymSpec sp = offsetSpec();
    sp.put(1, 0, 14, -1, 128, 0);  // sample vib depth 128, speed 0: the LFO stays at its peak
    sp.put(2, 0, 5, -1, 128, 0);   // 128 - (127 * 128 >> 8) = 65 -> frame 13000
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    const Cell& c = s->patterns[s->order[0]].at(2, 0);
    CHECK_EQ(c.fx, 9);
    CHECK_EQ(c.param, 51);
}

// ---- player: Symphonie continuous slides (Song::symphonieSlides), per tick and fractional
namespace {
constexpr uint8_t kFxU = 30, kFxV = 31, kFxW = 32, kFxY = 34;
Song slideSong(bool flag) {
    Song s = makeEmptySong(4);
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Linear;
    s.initialSpeed = 6;
    s.symphonieSlides = flag;
    Sample sm;
    sm.length = 1000;
    sm.data8.assign(1000, 50);
    sm.loop = LoopType::Forward;
    sm.loopLength = 1000;
    s.samples.push_back(sm);
    s.patterns[0].at(0, 0) = Cell{49, 1, 0x10 + 32, 0, 0};
    return s;
}
struct Rig {
    Mixer m{48000};
    Player p{m};
    explicit Rig(const Song& s) {
        p.setSong(&s);
        p.play();
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i) p.tick();
    }
};
}  // namespace

TEST(player_symphonie_volume_slide_runs_every_tick) {
    Song s = slideSong(true);
    s.patterns[0].at(0, 0).fx = kFxU, s.patterns[0].at(0, 0).param = 25;  // +1 volume per tick
    Rig* r = new Rig(s);
    r->ticks(6);
    CHECK_EQ(r->p.channel(0).volume, 38);
    r->ticks(6);  // keeps going on the next row without a command
    CHECK_EQ(r->p.channel(0).volume, 44);
    delete r;
}

TEST(player_symphonie_volume_slide_is_fractional_and_stops_at_zero) {
    Song s = slideSong(true);
    s.patterns[0].at(0, 0).fx = kFxV, s.patterns[0].at(0, 0).param = 10;  // -0.4 per tick
    Rig* r = new Rig(s);
    r->ticks(6);
    CHECK_EQ(r->p.channel(0).volume, 30);  // 32 - 2.4
    s.patterns[0].at(1, 0).fx = kFxV, s.patterns[0].at(1, 0).param = 255;
    r->ticks(18);
    CHECK_EQ(r->p.channel(0).volume, 0);
    s.patterns[0].at(4, 0).vol = 0x10 + 20;  // the slide stopped at 0: a new volume stays
    r->ticks(12);
    CHECK_EQ(r->p.channel(0).volume, 20);
    delete r;
}

TEST(player_symphonie_pitch_slide_runs_every_tick) {
    Song s = slideSong(true);
    s.patterns[0].at(0, 0).fx = kFxW, s.patterns[0].at(0, 0).param = 82;  // x(1 + 82/8192) per tick
    Rig* r = new Rig(s);
    r->ticks(6);
    CHECK_EQ(r->p.channel(0).period, 4608 - 66);
    s.patterns[0].at(1, 0).note = 49;  // a new note stops it
    r->ticks(12);
    CHECK_EQ(r->p.channel(0).period, 4608);
    s.patterns[0].at(3, 0).fx = kFxY, s.patterns[0].at(3, 0).param = 82;
    r->ticks(6);
    CHECK_EQ(r->p.channel(0).period, 4608 + 67);  // down: x(1 - 82/8192)
    delete r;
}

TEST(player_ignores_symphonie_slides_in_other_songs) {
    Song s = slideSong(false);
    s.patterns[0].at(0, 0).fx = kFxU, s.patterns[0].at(0, 0).param = 25;
    Rig* r = new Rig(s);
    r->ticks(6);
    CHECK_EQ(r->p.channel(0).volume, 32);
    delete r;
}

TEST(symmod_slides_become_continuous_slide_commands) {
    SymSpec sp;
    sp.ins = {normalIns("bass")};
    sp.smp = {dc(100, 64)};
    sp.put(0, 0, 0, 36, 50, 0);   // key on, volume 50
    sp.put(1, 0, 1, -1, 25, 0);   // volume slide up 25
    sp.put(2, 0, 0, -1, 99, 0);   // volume 99: the slide stops
    sp.put(0, 1, 0, 36, 50, 0);
    sp.put(1, 1, 3, -1, 82, 0);  // pitch slide up 82
    sp.put(2, 1, 4, -1, 10, 0);  // pitch slide down 10
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    CHECK(s->symphonieSlides);
    const Pattern& p = s->patterns[s->order[0]];
    CHECK_EQ(p.at(1, 0).fx, kFxU);
    CHECK_EQ(p.at(1, 0).param, 25);
    CHECK_EQ(p.at(1, 0).vol, 0);  // no volume column approximation
    CHECK_EQ(p.at(2, 0).vol, 0x10 + 63);
    CHECK_EQ(p.at(2, 0).fx, kFxU);
    CHECK_EQ(p.at(2, 0).param, 0);
    CHECK_EQ(p.at(3, 0).fx, 0);
    CHECK_EQ(p.at(1, 1).fx, kFxW);
    CHECK_EQ(p.at(1, 1).param, 82);
    CHECK_EQ(p.at(2, 1).fx, kFxY);
    CHECK_EQ(p.at(2, 1).param, 10);
    CHECK_EQ(p.at(3, 1).fx, 0);
}

TEST(symmod_slide_keeps_going_across_a_new_note) {
    // a key-on without volume does not stop the volume slide; the instrument's reset volume is
    // corrected to where the slide got to
    SymSpec sp;
    sp.ins = {normalIns("bass")};
    sp.smp = {dc(100, 64)};
    sp.speed = 5;
    sp.put(0, 0, 0, 36, 50, 0);  // volume 32 of 64
    sp.put(1, 0, 2, -1, 25, 0);  // down 1 per tick: 27 after row 1
    sp.put(2, 0, 0, 40, 0, 0);   // new note, no volume
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    const Pattern& p = s->patterns[s->order[0]];
    CHECK_EQ(p.at(1, 0).fx, kFxV);
    CHECK_EQ(p.at(2, 0).note, 53);
    CHECK_EQ(p.at(2, 0).vol, 0x10 + 27);
    CHECK_EQ(p.at(2, 0).fx, 0);
}

TEST(symmod_stopped_sample_pauses_the_slide) {
    SymSpec sp;
    sp.ins = {normalIns("bass")};
    sp.smp = {dc(100, 64)};
    sp.put(0, 0, 0, 36, 50, 0);
    sp.put(1, 0, 1, -1, 25, 0);   // volume slide up: 38 after row 1
    sp.put(2, 0, 0, -1, 254, 0);  // stop sample: silent, the slide waits
    sp.put(3, 0, 0, -1, 253, 0);  // continue: back at 38, sliding again
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    const Pattern& p = s->patterns[s->order[0]];
    CHECK_EQ(p.at(2, 0).vol, 0x10);
    CHECK_EQ(p.at(2, 0).fx, kFxU);
    CHECK_EQ(p.at(2, 0).param, 0);
    CHECK_EQ(p.at(3, 0).vol, 0x10 + 38);
    CHECK_EQ(p.at(3, 0).fx, kFxU);
    CHECK_EQ(p.at(3, 0).param, 25);
}

// ---- Symphonie DSP echo: a post-mix effect for the songs that switch it on
namespace {
constexpr uint8_t kFxQ = 26;
// a short click on the left channel at row 0; echo setting 0 switched on on the same row
Song echoSong(uint8_t type, bool withEcho) {
    Song s = makeEmptySong(4);
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Linear;
    s.initialSpeed = 6;
    s.initialBpm = 125;
    s.channelPan[0] = 0;
    Sample sm;
    sm.length = 50;
    sm.data8.assign(50, 100);
    sm.pan = 0;
    s.samples.push_back(sm);
    s.patterns[0].at(0, 0) = Cell{49, 1, 0, withEcho ? kFxQ : (uint8_t)0, 0};
    if (withEcho) s.symEcho.push_back({type, 2, 1});  // 2 ticks, feedback 1/2
    return s;
}
std::vector<float> renderFrames(const Song& s, uint32_t frames) {
    Mixer* m = new Mixer(48000);
    m->setInterp(Interp::None);
    m->setRamp(false);
    Player* p = new Player(*m);
    p->setSong(&s);
    p->play();
    std::vector<float> out(frames * 2);
    p->render(out.data(), frames);
    delete p;
    delete m;
    return out;
}
float peak(const std::vector<float>& st, int side, size_t from, size_t to) {
    float v = 0;
    for (size_t i = from; i < to; ++i) v = std::fabs(st[i * 2 + side]) > v ? std::fabs(st[i * 2 + side]) : v;
    return v;
}
constexpr size_t kTick = 960;  // 48000 Hz at 125 BPM
}  // namespace

TEST(player_symphonie_echo_repeats_after_its_delay) {
    const auto st = renderFrames(echoSong(1, true), kTick * 8);
    const float dry = peak(st, 0, 0, 400);
    CHECK(dry > 0.01f);
    CHECK_NEAR(peak(st, 0, 2 * kTick, 2 * kTick + 400), dry * 0.5f, dry * 0.02f);
    CHECK_NEAR(peak(st, 0, 4 * kTick, 4 * kTick + 400), dry * 0.25f, dry * 0.02f);
    CHECK(peak(st, 1, 0, kTick * 8) < 1e-6f);  // normal echo stays on its side
}

TEST(player_symphonie_cross_echo_bounces_between_sides) {
    const auto st = renderFrames(echoSong(2, true), kTick * 8);
    const float dry = peak(st, 0, 0, 400);
    CHECK_NEAR(peak(st, 1, 2 * kTick, 2 * kTick + 400), dry * 0.5f, dry * 0.02f);
    CHECK(peak(st, 0, 2 * kTick, 2 * kTick + 400) < 1e-6f);
    CHECK_NEAR(peak(st, 0, 4 * kTick, 4 * kTick + 400), dry * 0.25f, dry * 0.02f);
}

TEST(player_without_symphonie_echo_has_no_repeats) {
    const auto st = renderFrames(echoSong(1, false), kTick * 8);
    CHECK(peak(st, 0, 0, 400) > 0.01f);
    CHECK(peak(st, 0, 2 * kTick, kTick * 8) < 1e-6f);
}

TEST(symmod_dsp_echo_event_becomes_an_echo_setting) {
    SymSpec sp;
    sp.ins = {normalIns("bass")};
    sp.smp = {dc(100, 64)};
    sp.put(0, 0, 0, 36, 50, 0);
    sp.put(1, 2, 24, 2, 8, 1);  // DSP echo: cross, 8 ticks, feedback level 1
    Result<Song> r = Result<Song>::fail("");
    const Song* s = loadInto(r, sp);
    CHECK(s);
    if (!s) return;
    CHECK_EQ(s->symEcho.size(), 1u);
    if (s->symEcho.size() != 1) return;
    CHECK_EQ(s->symEcho[0].type, 2);
    CHECK_EQ(s->symEcho[0].delay, 8);
    CHECK_EQ(s->symEcho[0].feedback, 1);
    const Cell& c = s->patterns[s->order[0]].at(1, 2);
    CHECK_EQ(c.fx, kFxQ);
    CHECK_EQ(c.param, 0);
}

TEST(real_modules_symphonie_load_and_play) {
    const int n = countPlayable(L".symmod");
    if (n < 0) std::printf("SKIP real_modules_symphonie (no modules folder)\n");
    else std::printf("  symphonie: %d modules\n", n);
}
