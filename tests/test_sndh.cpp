// Atari ST SNDH tunes: the 68000 player of the tune runs on an emulated ST (Musashi 68000, YM2149 through
// ayumi, MFP 68901 timers). Hand-assembled tiny tunes, hostile programs, ICE! packing and the real files.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/engine.h"
#include "core/ay_player.h"
#include "core/sndh_player.h"
#include "core/song.h"
#include "test.h"

namespace {
// ---------------------------------------------------------------- a tiny hand-assembled SNDH
// Offsets 0/4/8: BRA.W init / exit / play, then the header, then the code (all PC relative).
struct Asm {
    std::vector<uint8_t> b;
    void w(uint16_t v) { b.push_back((uint8_t)(v >> 8)), b.push_back((uint8_t)v); }
    void l(uint32_t v) { w((uint16_t)(v >> 16)), w((uint16_t)v); }
    void s(const char* t, bool nul = true) {
        b.insert(b.end(), t, t + std::strlen(t));
        if (nul) b.push_back(0);
    }
    void even() {
        if (b.size() & 1) b.push_back(0);
    }
    size_t pc() const { return b.size(); }
    void bra(size_t at, size_t target) {  // patch BRA.W at `at` (displacement from at + 2)
        const int d = (int)target - (int)(at + 2);
        b[at] = 0x60, b[at + 1] = 0x00, b[at + 2] = (uint8_t)(d >> 8), b[at + 3] = (uint8_t)d;
    }
    void leaPc(size_t target) {  // LEA target(PC),A0
        const size_t ext = pc() + 2;
        w(0x41FA), w((uint16_t)(int16_t)((int)target - (int)ext));
    }
    void ym(uint8_t reg, uint8_t val) {  // A0 = $FFFF8800: MOVE.B #reg,(A0) ; MOVE.B #val,2(A0)
        w(0x10BC), w(reg);
        w(0x117C), w(val), w(0x0002);
    }
};

constexpr size_t kInitD0 = 0x200, kPlayCount = 0x204;  // variables of the tiny tune (offsets in the file)

enum class Body { Tone, InitLoops, PlayLoops, WritesEverywhere, ToneThenPlayLoops, TimerA };

// tags: extra header tags (e.g. "##03" or "TC100"); the play routine counts its calls in kPlayCount
std::vector<uint8_t> tinySndh(Body body = Body::Tone, std::vector<std::string> tags = {"TC50"}) {
    Asm a;
    a.l(0), a.l(0), a.l(0);  // the three branches, patched below
    a.s("SNDH", false);
    a.s("TITL", false), a.s("Tiny Tone");
    a.s("COMM", false), a.s("FatTracker");
    for (const std::string& t : tags) a.s(t.c_str(), t[0] != '#');  // ##nn / #!nn have no terminator
    a.even();
    a.s("HDNS", false);
    const size_t init = a.pc();
    a.leaPc(kInitD0), a.w(0x2080);  // LEA initD0(PC),A0 ; MOVE.L D0,(A0)
    if (body == Body::InitLoops) a.w(0x60FE);  // BRA.S *
    if (body == Body::ToneThenPlayLoops) {
        a.w(0x41F8), a.w(0x8800);  // LEA $FFFF8800.W,A0
        a.ym(0, 0x1C), a.ym(1, 0x01), a.ym(7, 0x3E), a.ym(8, 15), a.ym(9, 0), a.ym(10, 0);
    }
    if (body == Body::TimerA) {  // timer A every 2457600 / 200 / 123 s (~100 Hz) counts in kInitD0 + 8
        a.w(0x41F8), a.w(0x8800);
        a.ym(7, 0x3E), a.ym(8, 0);
        // MOVE.L #handler,$134.W (timer A vector), patched below
        a.w(0x21FC);
        const size_t vec = a.pc();
        a.l(0), a.w(0x0134);
        a.w(0x11FC), a.w(0x007B), a.w(0xFA1F);  // MOVE.B #123,$FFFFFA1F.W (TADR)
        a.w(0x11FC), a.w(0x0007), a.w(0xFA19);  // MOVE.B #7,$FFFFFA19.W (TACR: /200)
        a.w(0x08F8), a.w(0x0005), a.w(0xFA07);  // BSET #5,$FFFFFA07.W (IERA)
        a.w(0x08F8), a.w(0x0005), a.w(0xFA13);  // BSET #5,$FFFFFA13.W (IMRA)
        a.w(0x4E75);                            // RTS
        const size_t handler = a.pc();
        const uint32_t abs = (uint32_t)handler + SndhPlayer::kLoadAddress;  // where the tune is in ST memory
        a.b[vec] = (uint8_t)(abs >> 24), a.b[vec + 1] = (uint8_t)(abs >> 16);
        a.b[vec + 2] = (uint8_t)(abs >> 8), a.b[vec + 3] = (uint8_t)abs;
        a.w(0x2F08);                            // MOVE.L A0,-(SP)
        a.leaPc(kInitD0 + 8), a.w(0x5290);      // LEA count(PC),A0 ; ADDQ.L #1,(A0)
        a.w(0x205F);                            // MOVEA.L (SP)+,A0
        a.w(0x08B8), a.w(0x0005), a.w(0xFA0F);  // BCLR #5,$FFFFFA0F.W (ISRA: end of interrupt)
        a.w(0x4E73);                            // RTE
    } else {
        a.w(0x4E75);  // RTS
    }
    const size_t exit = a.pc();
    a.w(0x4E75);
    const size_t play = a.pc();
    a.leaPc(kPlayCount), a.w(0x5290);  // ADDQ.L #1,count
    switch (body) {
        case Body::Tone:
            a.w(0x41F8), a.w(0x8800);  // LEA $FFFF8800.W,A0
            a.ym(0, 0x1C), a.ym(1, 0x01);  // period $11C: 2 MHz / 16 / 284 = 440 Hz
            a.ym(7, 0x3E);                 // tone A only
            a.ym(8, 15), a.ym(9, 0), a.ym(10, 0);
            break;
        case Body::PlayLoops:
        case Body::ToneThenPlayLoops: a.w(0x60FE); break;
        case Body::WritesEverywhere:
            // MOVEQ #-1,D0 ; SUBA.L A0,A0 ; loop: MOVE.L D0,(A0)+ ; ADDA.L #$1235,A0 ; BRA.S loop
            a.w(0x70FF), a.w(0x91C8);
            a.w(0x20C0), a.w(0xD1FC), a.l(0x1235), a.w(0x60F6);
            break;
        default: break;
    }
    a.w(0x4E75);
    a.bra(0, init), a.bra(4, exit), a.bra(8, play);
    if (a.b.size() < kPlayCount + 16) a.b.resize(kPlayCount + 16, 0);
    return a.b;
}

float peakOf(ChipPlayer& p, double seconds, std::vector<float>* keep = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (keep) keep->insert(keep->end(), buf.begin(), buf.end());
    }
    return peak;
}

// ---------------------------------------------------------------- ICE! 2.4 packer (test side)
// Writes the bit stream the depacker reads: the packed bytes are consumed from the end backwards and the
// output is produced from its end backwards. Literal runs and back references, picture flag 0.
class IcePacker {
public:
    std::vector<uint8_t> pack(const std::vector<uint8_t>& in) {
        seq_.assign(1, 0x80);  // the first info byte carries no bits (its marker bit is the top one)
        left_ = 0;
        const int n = (int)in.size();
        int p = n;  // bytes [p, n) are produced
        while (p > 0) {
            int lit = 0, len = 0, off = 0;  // literal bytes until a back reference can start
            while (lit < p && !findMatch(in, p - lit, n, len, off)) ++lit;
            emitLiterals(in, p, lit);
            p -= lit;
            if (p == 0) break;  // the end test after the literals stops
            emitMatch(len, off);
            p -= len;
            if (p == 0) bit(0);  // no literals after the last match: the end test stops
        }
        bit(0);  // not a picture
        std::vector<uint8_t> out = {'I', 'C', 'E', '!'};
        const uint32_t packed = (uint32_t)(seq_.size() + 12), size = (uint32_t)in.size();
        for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(packed >> s));
        for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(size >> s));
        out.insert(out.end(), seq_.rbegin(), seq_.rend());
        return out;
    }

private:
    std::vector<uint8_t> seq_;  // in the order the depacker reads them
    size_t slot_ = 0;
    int left_ = 0;

    void bit(int b) {
        if (left_ == 0) slot_ = seq_.size(), seq_.push_back(0), left_ = 8;
        if (b) seq_[slot_] |= (uint8_t)(1 << (left_ - 1));
        --left_;
    }
    void bits(unsigned v, int n) {
        for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1);
    }
    // A back reference producing [q - len, q) from [q + off, q + off + len) (already produced, off >= 0):
    // for each distance d = off + len the run of equal bytes is counted backwards from q.
    static bool findMatch(const std::vector<uint8_t>& in, int q, int n, int& len, int& off) {
        len = 0;
        for (int d = 2; d <= 1100 && q + d <= n; ++d) {
            const int cap = std::min({d, 1033, q});
            int k = 0;
            while (k < cap && in[q - 1 - k] == in[q + d - 1 - k]) ++k;
            if (k == 2 && d - k > 574) continue;  // a two byte reference reaches less far
            if (k >= 2 && k > len) len = k, off = d - k;
        }
        return len >= 2;
    }
    void emitLiterals(const std::vector<uint8_t>& in, int p, int lit) {
        if (lit == 0) {
            bit(0);
            return;
        }
        bit(1);
        const int d1 = lit - 1;  // copies d1 + 1 bytes
        if (d1 == 0) {
            bit(0);
        } else {
            bit(1);
            static const int kBits[5] = {2, 2, 3, 8, 15}, kMask[5] = {3, 3, 7, 255, 32767}, kBase[5] = {1, 4, 7, 14, 269};
            for (int i = 0; i < 5; ++i) {
                const int v = d1 - kBase[i];
                if (i == 4 || v < kMask[i]) {
                    bits((unsigned)v, kBits[i]);
                    break;
                }
                bits((unsigned)kMask[i], kBits[i]);
            }
        }
        for (int i = 0; i < lit; ++i) seq_.push_back(in[p - 1 - i]);
    }
    void emitMatch(int len, int off) {
        const int l2 = len - 2;
        if (l2 == 0) bit(0);
        else if (l2 == 1) bits(0b10, 2);
        else if (l2 <= 3) bits(0b110, 3), bit(l2 - 2);
        else if (l2 <= 7) bits(0b1110, 4), bits((unsigned)(l2 - 4), 2);
        else bits(0b1111, 4), bits((unsigned)(l2 - 8), 10);
        if (l2 == 0) {
            if (off <= 62) bit(0), bits((unsigned)(off + 1), 6);
            else bit(1), bits((unsigned)(off - 63), 9);
            return;
        }
        if (off <= 30) bits(0b10, 2), bits((unsigned)(off + 1), 5);
        else if (off <= 286) bit(0), bits((unsigned)(off - 31), 8);
        else bits(0b11, 2), bits((unsigned)(off - 287), 12);
    }
};

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

std::filesystem::path stFolder() {
    const std::filesystem::path main = "C:/development/FatTracker/modules/AtariST";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules" / "AtariST";
}

int zeroCrossings(const std::vector<float>& st, size_t from) {
    int n = 0;
    for (size_t i = from + 2; i < st.size(); i += 2) n += (st[i - 2] < 0) != (st[i] < 0);
    return n;
}
}  // namespace

// ---------------------------------------------------------------- detection and header
TEST(sndh_detect) {
    const auto f = tinySndh();
    CHECK(SndhPlayer::detect(f.data(), f.size()));
    CHECK(!SndhPlayer::detect(f.data(), 15));  // cut before the magic ends
    std::vector<uint8_t> bad = f;
    bad[13] = 'X';
    CHECK(!SndhPlayer::detect(bad.data(), bad.size()));
    CHECK(!AyPlayer::detect(f.data(), f.size()));  // the YM dump player does not claim it
    const auto ice = IcePacker().pack(f);
    CHECK(SndhPlayer::detect(ice.data(), ice.size()));
}

TEST(sndh_tiny_tune_sounds_on_channel_a) {
    const auto f = tinySndh();
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK(std::strcmp(p.title(), "Tiny Tone") == 0);
    CHECK(std::strcmp(p.composer(), "FatTracker") == 0);
    CHECK(std::strncmp(p.format(), "SNDH", 4) == 0);
    CHECK_EQ(p.voices(), 3);
    CHECK_EQ(p.subsongs(), 1);
    CHECK_EQ(p.playRate(), 50);
    CHECK_EQ(p.peek32(SndhPlayer::kLoadAddress + kInitD0), 1u);  // init got sub-song 1 in D0
    std::vector<float> keep;
    const float pk = peakOf(p, 1.0, &keep);
    CHECK(pk > 0.05f);
    const uint32_t calls = p.peek32(SndhPlayer::kLoadAddress + kPlayCount);
    CHECK(calls >= 49 && calls <= 51);  // 50 Hz
    CHECK_EQ(p.ymRegister(0), 0x1C);
    CHECK_EQ(p.ymRegister(1), 0x01);
    CHECK_EQ(p.ymRegister(7), 0x3E);
    CHECK_EQ(p.ymRegister(8), 15);
    CHECK(p.vu(0) > 0.3f);
    CHECK(p.vu(1) == 0.f);
    CHECK(p.vu(2) == 0.f);
    const int zc = zeroCrossings(keep, keep.size() / 2);  // the last half second: 440 Hz square
    CHECK(zc > 2 * 200 && zc < 2 * 240);
    CHECK(!p.crashed());
    Song s;
    CHECK(p.buildDisplay(s));
    CHECK_EQ(s.numChannels, 4);
    p.restart();
    CHECK_EQ(p.peek32(SndhPlayer::kLoadAddress + kPlayCount), 0u);  // a fresh machine
    CHECK(peakOf(p, 0.2) > 0.05f);
    p.setRate(44100);
    CHECK(peakOf(p, 0.2) > 0.05f);
}

TEST(sndh_subsongs_and_timer_tags) {
    const auto f = tinySndh(Body::Tone, {"##03", "#!02", "TC100"});
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.subsongs(), 3);
    CHECK_EQ(p.subsong(), 1);  // #!02: the second one is the default
    CHECK_EQ(p.peek32(SndhPlayer::kLoadAddress + kInitD0), 2u);
    CHECK_EQ(p.playRate(), 100);
    peakOf(p, 1.0);
    const uint32_t calls = p.peek32(SndhPlayer::kLoadAddress + kPlayCount);
    CHECK(calls >= 99 && calls <= 101);
    CHECK(p.selectSubsong(2));
    CHECK_EQ(p.subsong(), 2);
    CHECK_EQ(p.peek32(SndhPlayer::kLoadAddress + kInitD0), 3u);
    CHECK_EQ(p.peek32(SndhPlayer::kLoadAddress + kPlayCount), 0u);
    CHECK(!p.selectSubsong(3));
    CHECK(!p.selectSubsong(-1));
    CHECK_EQ(p.subsong(), 2);
    CHECK(p.selectSubsong(0));
    CHECK_EQ(p.peek32(SndhPlayer::kLoadAddress + kInitD0), 1u);
    SndhPlayer q;  // explicit sub-song at load
    q.setRate(48000);
    CHECK(q.load(f.data(), f.size(), 2));
    CHECK_EQ(q.subsong(), 2);

    const auto v = tinySndh(Body::Tone, {"!V60"});
    SndhPlayer r;
    r.setRate(48000);
    CHECK(r.load(v.data(), v.size()));
    CHECK_EQ(r.playRate(), 60);
}

TEST(sndh_mfp_timer_interrupts) {
    const auto f = tinySndh(Body::TimerA);
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    const uint32_t before = p.peek32(SndhPlayer::kLoadAddress + kInitD0 + 8);
    peakOf(p, 1.0);
    const uint32_t n = p.peek32(SndhPlayer::kLoadAddress + kInitD0 + 8) - before;
    CHECK(n >= 97 && n <= 103);  // 2457600 / 200 / 123 = 99.9 Hz
    CHECK(!p.crashed());
}

// ---------------------------------------------------------------- hostile programs
TEST(sndh_init_that_never_returns_is_refused_quickly) {
    const auto f = tinySndh(Body::InitLoops);
    SndhPlayer p;
    p.setRate(48000);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!p.load(f.data(), f.size()));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s < 1.0);
    std::vector<float> buf(960 * 2, 1.f);
    p.render(buf.data(), 960, 0.45f);  // not loaded: silence
    for (float v : buf) CHECK(v == 0.f);
}

TEST(sndh_play_that_never_returns_goes_silent) {
    const auto f = tinySndh(Body::ToneThenPlayLoops);
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(peakOf(p, 0.2) > 0.05f);  // the tone from init sounds until the runaway is noticed
    peakOf(p, 1.5);
    CHECK(p.crashed());
    CHECK(peakOf(p, 0.5) < 0.01f);  // then silence
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s < 2.0);
    p.restart();  // a restart starts afresh
    CHECK(!p.crashed());
}

TEST(sndh_writes_everywhere_stay_inside_the_machine) {
    const auto f = tinySndh(Body::WritesEverywhere);
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    const auto t0 = std::chrono::steady_clock::now();
    peakOf(p, 3.0);  // anything may happen inside, nothing outside
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s < 3.0);
    CHECK(p.crashed());
}

TEST(sndh_truncated_and_broken_headers) {
    const auto f = tinySndh();
    for (size_t cut = 0; cut < f.size(); cut += 3) {
        SndhPlayer p;
        p.setRate(48000);
        if (p.load(f.data(), cut)) peakOf(p, 0.1);  // refused or played, never a crash
    }
    std::vector<uint8_t> noEnd = f;  // no HDNS, tags running into code
    for (size_t i = 0; i + 4 <= noEnd.size(); ++i)
        if (!std::memcmp(&noEnd[i], "HDNS", 4)) noEnd[i] = 'X';
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(noEnd.data(), noEnd.size()));
    CHECK(peakOf(p, 0.2) > 0.05f);
    std::vector<uint8_t> big = f;  // more sub-songs than any tune has
    for (size_t i = 0; i + 4 <= big.size(); ++i)
        if (!std::memcmp(&big[i], "TC50", 4)) big[i] = '#', big[i + 1] = '#', big[i + 2] = '9', big[i + 3] = '9';
    SndhPlayer q;
    q.setRate(48000);
    CHECK(q.load(big.data(), big.size()));
    CHECK(q.subsongs() >= 1 && q.subsongs() <= 99);
    SndhPlayer r;
    CHECK(!r.load(nullptr, 0));
    std::vector<uint8_t> huge(5u << 20, 0);  // larger than the ST's memory
    std::memcpy(huge.data(), f.data(), f.size());
    CHECK(!r.load(huge.data(), huge.size()));
}

// ---------------------------------------------------------------- ICE! packing
TEST(sndh_ice_round_trip) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 3000; ++i) data.push_back((uint8_t)(i % 7 == 0 ? i * 13 : (i / 50) & 3));
    const char* text = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG. THE QUICK BROWN FOX JUMPS AGAIN. ";
    for (int k = 0; k < 20; ++k) data.insert(data.end(), text, text + std::strlen(text));
    for (int i = 0; i < 700; ++i) data.push_back(0);
    for (int i = 0; i < 300; ++i) data.push_back((uint8_t)(i * 37 + 11));  // incompressible run
    const auto packed = IcePacker().pack(data);
    CHECK(packed.size() < data.size());
    std::vector<uint8_t> out;
    CHECK(iceDepack(packed.data(), packed.size(), out));
    CHECK(out == data);
    // cut or damaged streams are refused or decode to something, never outside the buffer
    for (size_t cut = 0; cut < packed.size(); cut += packed.size() / 13 + 1) {
        std::vector<uint8_t> o;
        CHECK(!iceDepack(packed.data(), cut, o));
    }
    std::vector<uint8_t> bad = packed;
    for (size_t i = 12; i < bad.size(); i += 5) bad[i] ^= 0x5A;
    std::vector<uint8_t> o;
    iceDepack(bad.data(), bad.size(), o);
    std::vector<uint8_t> hugeHdr = packed;  // claims 1 GB unpacked
    hugeHdr[8] = 0x40;
    CHECK(!iceDepack(hugeHdr.data(), hugeHdr.size(), o));
    std::vector<uint8_t> notIce = data;
    CHECK(!iceDepack(notIce.data(), notIce.size(), o));
}

TEST(sndh_ice_packed_tune_plays) {
    const auto f = tinySndh();
    const auto packed = IcePacker().pack(f);
    SndhPlayer p;
    p.setRate(48000);
    CHECK(p.load(packed.data(), packed.size()));
    CHECK(std::strcmp(p.title(), "Tiny Tone") == 0);
    CHECK(peakOf(p, 0.3) > 0.05f);
}

// ---------------------------------------------------------------- engine
TEST(sndh_engine_load) {
    const auto f = tinySndh(Body::Tone, {"##03", "TC50"});
    Engine e;
    CHECK(e.loadSndh(f.data(), f.size(), "", false));
    CHECK(e.tfmx());
    CHECK_EQ(e.chipSubsongs(), 3);
    CHECK(std::strncmp(e.tfmxFormat(), "SNDH", 4) == 0);
    CHECK(e.song() != nullptr);
    if (e.song()) CHECK(_strnicmp(e.song()->name, "TINY TONE", 9) == 0);
    CHECK(e.selectChipSubsong(2));
    CHECK_EQ(e.chipSubsong(), 2);
    std::vector<uint8_t> junk(64, 0x4E);
    CHECK(!e.loadSndh(junk.data(), junk.size(), "", false));
}

// ---------------------------------------------------------------- the real tunes
TEST(sndh_real_files_play) {
    const std::filesystem::path dir = stFolder();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP sndh_real_files_play (no modules/AtariST folder)\n");
        return;
    }
    struct Known {
        const char* file;
        int songs;
        bool timers;  // plays SID voices / effects from MFP timer interrupts
    };
    const Known known[] = {
        {"Rob Hubbard - Thrust.sndh", 1, false},
        {"Rob Hubbard - Jupiter Probe.sndh", 1, false},
        {"David Whittaker - Bad Company.sndh", 10, false},
        {"Count Zero - Decade Demo-Boot.sndh", 1, false},
        {"David Whittaker - APB.sndh", 8, false},
        {"Big Alec - Action.sndh", 1, true},
    };
    int n = 0;
    for (const Known& k : known) {
        const auto path = dir / k.file;
        if (!std::filesystem::exists(path)) continue;
        const auto d = readFile(path);
        CHECK(SndhPlayer::detect(d.data(), d.size()));
        SndhPlayer p;
        p.setRate(48000);
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = p.load(d.data(), d.size());
        CHECK(ok);
        if (!ok) {
            std::printf("  FAIL %s\n", k.file);
            continue;
        }
        CHECK_EQ(p.subsongs(), k.songs);
        const float pk = peakOf(p, 3.0);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(pk > 0.02f);
        CHECK(!p.crashed());
        if (k.timers) CHECK(p.mfpInterrupts() > 1000);
        std::printf("  %s: \"%s\" by %s, %d song(s), %d Hz, %.1f s, peak %.2f, %u MFP interrupts (%.2f s to emulate 3 s)\n",
                    k.file, p.title(), p.composer(), p.subsongs(), p.playRate(), p.seconds(), pk, p.mfpInterrupts(), s);
        if (n == 0) {  // ICE! packed, the same tune plays the same
            const auto packed = IcePacker().pack(d);
            SndhPlayer q;
            q.setRate(48000);
            CHECK(q.load(packed.data(), packed.size()));
            SndhPlayer r;
            r.setRate(48000);
            CHECK(r.load(d.data(), d.size()));
            peakOf(q, 1.0), peakOf(r, 1.0);
            for (int reg = 0; reg < 14; ++reg) CHECK_EQ(q.ymRegister(reg), r.ymRegister(reg));
        }
        if (p.subsongs() > 1) {  // the last sub-song too
            CHECK(p.selectSubsong(p.subsongs() - 1));
            CHECK(peakOf(p, 3.0) > 0.02f);
        }
        ++n;
    }
    // every other SNDH file in the folder at least loads and makes a sound
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const auto d = readFile(e.path());
        if (!SndhPlayer::detect(d.data(), d.size())) continue;
        bool listed = false;
        for (const Known& k : known) listed |= e.path().filename() == k.file;
        if (listed) continue;
        SndhPlayer p;
        p.setRate(48000);
        CHECK(p.load(d.data(), d.size()));
        const float pk = peakOf(p, 3.0);
        CHECK(pk > 0.02f);
        std::printf("  %s: \"%s\", %d song(s), %d Hz, peak %.2f%s\n", e.path().filename().string().c_str(), p.title(),
                    p.subsongs(), p.playRate(), pk, p.crashed() ? " (crashed)" : "");
        ++n;
    }
    if (!n) std::printf("SKIP sndh_real_files_play (no SNDH files)\n");
}
