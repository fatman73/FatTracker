// Per-voice VU meters and scopes of the playback-only chip players (AY / YM through ayumi, C64 SID through
// cRSID, the Game_Music_Emu formats): a voice that plays moves its own meter and scope, a silent voice stays
// at zero, voices that play different things show different scopes, and the mixed output stays the same.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/ay_player.h"
#include "core/gme/gme.h"
#include "core/gme_player.h"
#include "core/inflate.h"
#include "core/pt3_player.h"
#include "core/sc68_player.h"
#include "core/sid_player.h"
#include "core/sndh_player.h"
#include "core/zx_trackers.h"
#include "test.h"

namespace {
constexpr int kBlock = 1024;
constexpr int kV = ChipPlayer::kMaxVoices;

std::filesystem::path modulesDir() { return std::filesystem::path(FT_SOURCE_DIR) / "modules"; }

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// What the meters showed while playing.
struct Watch {
    float vuMax[kV] = {};
    float scopeMax[kV] = {};
    int crossings[kV] = {};        // sign changes of each voice's scope
    int bothMoving[kV][kV] = {};   // blocks in which both scopes moved
    int differing[kV][kV] = {};    // ... and showed different waves
    uint64_t hash = 1469598103934665603ull;  // FNV-1a of the mixed output
    float peak = 0;

    bool differ(int a, int b) const { return differing[a < b ? a : b][a < b ? b : a] > 0; }
    int active(int voices) const {
        int n = 0;
        for (int v = 0; v < voices; ++v) n += scopeMax[v] > 0.02f;
        return n;
    }
    // differing pairs, at least 'want' when that many voices (+1) played at all (some tunes start with one)
    bool voicesDiffer(int voices, int want = 1) const {
        const int a = active(voices);
        if (a < 2) return true;
        return differingPairs(voices) >= (a - 1 < want ? a - 1 : want);
    }
    int differingPairs(int voices) const {
        int n = 0;
        for (int a = 0; a < voices; ++a)
            for (int b = a + 1; b < voices; ++b) n += differ(a, b);
        return n;
    }
};

Watch watch(ChipPlayer& p, double seconds, double skip = 0) {
    Watch w;
    std::vector<float> buf(kBlock * 2);
    std::vector<float> sc[kV];
    for (auto& s : sc) s.resize(kBlock);
    const int blocks = (int)(seconds * 48000 / kBlock);
    const int skipBlocks = (int)(skip * 48000 / kBlock);
    for (int b = 0; b < skipBlocks + blocks; ++b) {
        p.render(buf.data(), kBlock, 0.5f);
        if (b < skipBlocks) continue;
        for (float v : buf) {
            uint32_t bits;
            std::memcpy(&bits, &v, 4);
            for (int k = 0; k < 4; ++k) w.hash = (w.hash ^ ((bits >> (8 * k)) & 0xFF)) * 1099511628211ull;
            w.peak = std::fabs(v) > w.peak ? std::fabs(v) : w.peak;
        }
        bool moving[kV] = {};
        for (int v = 0; v < kV; ++v) {
            const float vu = p.vu(v);
            w.vuMax[v] = vu > w.vuMax[v] ? vu : w.vuMax[v];
            p.scope(v, sc[v].data(), kBlock);
            float lo = 1e9f, hi = -1e9f;
            for (int k = 0; k < kBlock; ++k) {
                const float x = sc[v][k];
                w.scopeMax[v] = std::fabs(x) > w.scopeMax[v] ? std::fabs(x) : w.scopeMax[v];
                lo = x < lo ? x : lo;
                hi = x > hi ? x : hi;
                if (k && ((x > 0.01f && sc[v][k - 1] <= 0.01f) || (x < -0.01f && sc[v][k - 1] >= -0.01f))) ++w.crossings[v];
            }
            moving[v] = hi - lo > 0.01f;
        }
        for (int a = 0; a < kV; ++a)
            for (int c = a + 1; c < kV; ++c) {
                if (!moving[a] || !moving[c]) continue;
                ++w.bothMoving[a][c];
                float diff = 0;  // where both show something (a scope gated to zero is not a wave of its own)
                int both = 0;
                for (int k = 0; k < kBlock; ++k) {
                    if (sc[a][k] == 0.f || sc[c][k] == 0.f) continue;
                    ++both;
                    diff = std::fmax(diff, std::fabs(sc[a][k] - sc[c][k]));
                }
                if (both > kBlock / 4 && diff > 0.02f) ++w.differing[a][c];
            }
    }
    return w;
}

void print(const char* what, const ChipPlayer& p, const Watch& w) {
    std::printf("  %s (%s, %d voices): hash %016llx\n    vu   ", what, p.format(), p.voices(), (unsigned long long)w.hash);
    for (int v = 0; v < p.voices() && v < kV; ++v) std::printf(" %.2f", w.vuMax[v]);
    std::printf("\n    scope");
    for (int v = 0; v < p.voices() && v < kV; ++v) std::printf(" %.2f", w.scopeMax[v]);
    std::printf("\n    differing pairs %d\n", w.differingPairs(p.voices() < kV ? p.voices() : kV));
}

// Raw PSG: channel A at period 0x080, channel B at period 0x180 (a third of A's pitch, quieter), C silent.
std::vector<uint8_t> twoChannelPsg() {
    std::vector<uint8_t> f(16, 0);
    std::memcpy(f.data(), "PSG\x1A", 4);
    const uint8_t setup[] = {0, 0x80, 1, 0x00, 2, 0x80, 3, 0x01, 7, 0x3C, 8, 15, 9, 12, 10, 0};
    f.insert(f.end(), setup, setup + sizeof setup);
    for (int i = 0; i < 100; ++i) f.push_back(0xFF);
    f.push_back(0xFD);
    return f;
}

void put16(std::vector<uint8_t>& f, size_t at, size_t v) {
    f[at] = (uint8_t)(v & 0xFF);
    f[at + 1] = (uint8_t)(v >> 8);
}

// PT3: channel A plays a held note with sample 1 (tone, amplitude 15); B and C stay silent.
std::vector<uint8_t> oneChannelPt3() {
    std::vector<uint8_t> f(0xC9, 0);
    std::string name = "ProTracker 3.5 compilation of ";
    std::string title = "VOICES", author = "TESTER";
    title.resize(32, ' ');
    author.resize(32, ' ');
    name += title + " by " + author + " ";
    std::memcpy(f.data(), name.data(), 0x63);
    f[0x64] = 3, f[0x65] = 1;
    f.push_back(0x00), f.push_back(0xFF);
    const size_t patTable = f.size();
    f.resize(f.size() + 6, 0);
    const size_t patA = f.size();
    const uint8_t a[] = {0xF0, 0x02, 0xB1, 64, 0xCF, 0x50 + 48, 0x00};
    f.insert(f.end(), a, a + sizeof a);
    const size_t patB = f.size();
    const uint8_t b[] = {0xB1, 64, 0xD0, 0x00};
    f.insert(f.end(), b, b + sizeof b);
    const size_t patC = f.size();
    f.insert(f.end(), b, b + sizeof b);
    const size_t sample = f.size();
    const uint8_t s[] = {0, 1, 0x00, 0x8F, 0x00, 0x00};
    f.insert(f.end(), s, s + sizeof s);
    const size_t orn = f.size();
    const uint8_t o[] = {0, 1, 0};
    f.insert(f.end(), o, o + sizeof o);
    put16(f, 0x67, patTable);
    put16(f, patTable, patA);
    put16(f, patTable + 2, patB);
    put16(f, patTable + 4, patC);
    put16(f, 0x69 + 2 * 1, sample);
    put16(f, 0xA9, orn);
    return f;
}

// PSID: voice 1 plays a gated sawtooth (~250 Hz), voices 2 and 3 are never touched.
std::vector<uint8_t> oneVoicePsid() {
    std::vector<uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[5] = 2, f[7] = 0x7C;
    f[8] = 0x10, f[9] = 0x00;
    f[0xA] = 0x10, f[0xB] = 0x00;
    f[0xC] = 0x10, f[0xD] = 0x20;
    f[0xF] = 1, f[0x11] = 1;
    std::memcpy(f.data() + 0x16, "ONE VOICE", 9);
    const uint8_t code[] = {
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,  // volume 15
        0xA9, 0x00, 0x8D, 0x05, 0xD4,  // attack / decay 0
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,  // sustain 15
        0xA9, 0x00, 0x8D, 0x00, 0xD4,  // frequency lo
        0xA9, 0x10, 0x8D, 0x01, 0xD4,  // frequency hi
        0xA9, 0x21, 0x8D, 0x04, 0xD4,  // sawtooth + gate
        0x60};
    f.insert(f.end(), code, code + sizeof code);
    f.resize(0x7C + 0x20, 0x60);
    return f;
}

// NSF: init turns pulse 1 on (50 % duty, constant volume 15) at ~440 Hz; the other NES voices stay silent.
std::vector<uint8_t> onePulseNsf() {
    std::vector<uint8_t> f(0x80, 0);
    std::memcpy(f.data(), "NESM\x1A", 5);
    f[5] = 1, f[6] = 1, f[7] = 1;
    f[8] = 0x00, f[9] = 0x80;
    f[0xA] = 0x00, f[0xB] = 0x80;
    f[0xC] = 0x20, f[0xD] = 0x80;
    std::memcpy(f.data() + 0x0E, "ONE PULSE", 9);
    f[0x6E] = 0x1A, f[0x6F] = 0x41;
    f[0x78] = 0x20, f[0x79] = 0x4E;
    const uint8_t code[] = {
        0xA9, 0x01, 0x8D, 0x15, 0x40,  // pulse 1 on
        0xA9, 0xBF, 0x8D, 0x00, 0x40,  // duty 50 %, length halted, constant volume 15
        0xA9, 0x00, 0x8D, 0x01, 0x40,  // no sweep
        0xA9, 0xFD, 0x8D, 0x02, 0x40,  // timer low 253
        0xA9, 0x00, 0x8D, 0x03, 0x40,  // timer high 0
        0x60};
    f.insert(f.end(), code, code + sizeof code);
    f.resize(0x80 + 0x20, 0xEA);
    f.push_back(0x60);
    f.resize(0x80 + 0x100, 0);
    return f;
}

// VGM 1.50 for the Master System PSG only: square 1 at full volume (period 0x0FE, ~440 Hz), the other squares
// and the noise silent, two seconds long.
std::vector<uint8_t> oneSquareVgm() {
    std::vector<uint8_t> f(0x40, 0);
    std::memcpy(f.data(), "Vgm ", 4);
    auto le32 = [&f](size_t at, uint32_t v) {
        for (int k = 0; k < 4; ++k) f[at + k] = (uint8_t)(v >> (8 * k));
    };
    le32(0x08, 0x150);
    le32(0x0C, 3579545);    // SN76489 clock
    le32(0x18, 44100 * 2);  // samples
    f[0x28] = 0x09, f[0x2A] = 16;  // noise feedback / width
    le32(0x34, 0x40 - 0x34);       // data offset
    const uint8_t setup[] = {0x50, 0x8E, 0x50, 0x0F,  // tone 1 period 0x0FE
                             0x50, 0x90,              // volume 1: loudest
                             0x50, 0xBF, 0x50, 0xDF, 0x50, 0xFF};  // squares 2, 3 and the noise off
    f.insert(f.end(), setup, setup + sizeof setup);
    for (int i = 0; i < 120; ++i) f.push_back(0x63);  // wait 1/50 s
    f.push_back(0x66);
    le32(0x04, (uint32_t)f.size() - 4);
    return f;
}

// Mixed output of GmePlayer against Game_Music_Emu itself (gme_open_data, as GmePlayer played before the
// per-voice meters): the number of samples that differ in 3 s.
int gmeMixDiffers(const std::vector<uint8_t>& d) {
    GmePlayer p;
    p.setRate(48000);
    if (!p.load(d.data(), d.size())) return -1;
    std::vector<uint8_t> u;
    if (isGzip(d.data(), d.size())) gunzip(d.data(), d.size(), u, 64u << 20);
    else u = d;
    u.resize(u.size() + 4096, 0);  // as GmePlayer pads it
    Music_Emu* emu = nullptr;
    if (gme_open_data(u.data(), (long)(u.size() - 4096), &emu, 48000) || !emu) return -1;
    gme_set_autoload_playback_limit(emu, 0);
    gme_ignore_silence(emu, 1);
    gme_start_track(emu, 0);
    std::vector<float> out(kBlock * 2);
    std::vector<short> raw(kBlock * 2);
    int bad = 0;
    for (int b = 0; b < 48000 * 3 / kBlock; ++b) {
        p.render(out.data(), kBlock, 0.5f);
        if (gme_track_ended(emu)) gme_start_track(emu, 0);  // (as GmePlayer loops)
        gme_play(emu, kBlock * 2, raw.data());
        for (int k = 0; k < kBlock * 2; ++k)
            if (out[k] != raw[k] / 32768.f * 0.5f * 2.f) ++bad;
    }
    gme_delete(emu);
    return bad;
}

// The first such file by name, case-insensitive: the order NTFS lists a folder in, so every system checks the
// same file (APFS lists in no particular order).
std::filesystem::path firstFile(const char* sub, const char* ext) {
    const auto dir = modulesDir() / sub;
    if (!std::filesystem::exists(dir)) return {};
    std::filesystem::path best;
    std::wstring bestKey;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ext) continue;
        std::wstring key = e.path().filename().wstring();
        for (wchar_t& c : key) c = (wchar_t)std::towupper(c);
        if (best.empty() || key < bestKey) best = e.path(), bestKey = key;
    }
    return best;
}

// A real file loaded into a fresh player (null when the file is not there).
template <class P>
std::unique_ptr<P> loadReal(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) return nullptr;
    const auto d = readFile(path);
    auto p = std::make_unique<P>();
    p->setRate(48000);
    if (!p->load(d.data(), d.size())) return nullptr;
    return p;
}
}  // namespace

// ---------------------------------------------------------------- AY / YM (ayumi)

TEST(chipvoices_psg_each_channel_its_own_scope) {
    const auto f = twoChannelPsg();
    AyPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    const Watch w = watch(p, 1.0, 0.1);
    print("two channel PSG", p, w);
    CHECK(w.scopeMax[0] > 0.1f);
    CHECK(w.scopeMax[1] > 0.02f);
    CHECK(w.scopeMax[2] == 0.f);
    CHECK(w.vuMax[0] > w.vuMax[1]);  // volume 15 against volume 12
    CHECK(w.vuMax[1] > 0.f);
    CHECK(w.vuMax[2] == 0.f);
    CHECK(w.differ(0, 1));
    // A is three times B's pitch: its scope crosses zero three times as often
    const double ratio = (double)w.crossings[0] / (w.crossings[1] ? w.crossings[1] : 1);
    std::printf("  crossings A %d B %d (ratio %.2f)\n", w.crossings[0], w.crossings[1], ratio);
    CHECK(ratio > 2.5 && ratio < 3.5);
}

TEST(chipvoices_pt3_one_channel) {
    const auto f = oneChannelPt3();
    Pt3Player p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    const Watch w = watch(p, 1.0, 0.1);
    print("one channel PT3", p, w);
    CHECK(w.scopeMax[0] > 0.1f);
    CHECK(w.vuMax[0] > 0.1f);
    CHECK(w.scopeMax[1] == 0.f && w.scopeMax[2] == 0.f);
    CHECK(w.vuMax[1] == 0.f && w.vuMax[2] == 0.f);
}

TEST(chipvoices_ay_real_files_voices_differ) {
    int n = 0;
    if (auto p = loadReal<AyPlayer>(firstFile("AtariST", ".ym"))) {
        const Watch w = watch(*p, 4.0);
        print("YM", *p, w);
        CHECK(w.voicesDiffer(3));
        ++n;
    }
    if (auto p = loadReal<Pt3Player>(firstFile("Spectrum", ".pt3"))) {
        const Watch w = watch(*p, 4.0);
        print("PT3", *p, w);
        CHECK(w.voicesDiffer(3));
        ++n;
    }
    for (const char* ext : {".pt2", ".stc", ".asc", ".sqt"})
        if (auto p = loadReal<ZxTrackerPlayer>(firstFile("Spectrum", ext))) {
            const Watch w = watch(*p, 4.0);
            print(ext, *p, w);
            CHECK(w.voicesDiffer(3));
            ++n;
        }
    if (auto p = loadReal<SndhPlayer>(firstFile("AtariST", ".sndh"))) {
        const Watch w = watch(*p, 4.0);
        print("SNDH", *p, w);
        CHECK(w.voicesDiffer(3));
        ++n;
    }
    std::printf("  %d files\n", n);
}

TEST(chipvoices_sc68_real_file_voices_differ) {
    const auto path = firstFile("AtariST", ".sc68");
    if (path.empty()) return;
    const auto d = readFile(path);
    Sc68Player p;
    p.setRate(48000);
    if (!p.load(d.data(), d.size())) return;  // (needs files beside it: covered by the SC68 tests)
    const Watch w = watch(p, 4.0);
    print("SC68", p, w);
    CHECK(w.voicesDiffer(3));
}

// ---------------------------------------------------------------- C64 SID (cRSID)

TEST(chipvoices_sid_one_voice) {
    const auto f = oneVoicePsid();
    SidPlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    const Watch w = watch(p, 1.0, 0.1);
    print("one voice PSID", p, w);
    CHECK(w.scopeMax[0] > 0.1f);
    CHECK(w.vuMax[0] > 0.1f);
    CHECK(w.scopeMax[1] == 0.f && w.scopeMax[2] == 0.f);
    CHECK(w.vuMax[1] == 0.f && w.vuMax[2] == 0.f);
}

TEST(chipvoices_sid_real_file_voices_differ) {
    auto p = loadReal<SidPlayer>(firstFile("C64", ".sid"));
    if (!p) return;
    const Watch w = watch(*p, 4.0);
    print("SID", *p, w);
    CHECK(w.voicesDiffer(3));
}

// ---------------------------------------------------------------- Game_Music_Emu

TEST(chipvoices_gme_nsf_one_pulse) {
    const auto f = onePulseNsf();
    GmePlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK(p.voices() >= 5);
    const Watch w = watch(p, 1.0, 0.1);
    print("one pulse NSF", p, w);
    CHECK(w.scopeMax[0] > 0.05f);
    CHECK(w.vuMax[0] > 0.05f);
    for (int v = 1; v < p.voices() && v < kV; ++v) {
        CHECK(w.scopeMax[v] == 0.f);
        CHECK(w.vuMax[v] == 0.f);
    }
}

TEST(chipvoices_gme_real_files_voices_differ) {
    struct {
        const char* dir;
        const char* ext;
    } const files[] = {{"SNES", ".spc"}, {"Atari8", ".sap"}, {"NES", ".nsf"}, {"Spectrum", ".emul"}, {"MegaDrive", ".vgz"}};
    int n = 0;
    for (const auto& f : files) {
        auto p = loadReal<GmePlayer>(firstFile(f.dir, f.ext));
        if (!p) continue;
        const Watch w = watch(*p, 6.0);
        print(f.ext, *p, w);
        CHECK(w.voicesDiffer(p->voices() < kV ? p->voices() : kV, 2));
        ++n;
    }
    std::printf("  %d files\n", n);
}

TEST(chipvoices_gme_psg_vgm_one_square) {
    const auto f = oneSquareVgm();
    GmePlayer p;
    p.setRate(48000);
    CHECK(p.load(f.data(), f.size()));
    CHECK_EQ(p.voices(), 4);
    const Watch w = watch(p, 1.0, 0.1);
    print("one square VGM", p, w);
    CHECK(w.scopeMax[0] > 0.05f);
    CHECK(w.vuMax[0] > 0.05f);
    for (int v = 1; v < 4; ++v) {
        CHECK(w.scopeMax[v] == 0.f);
        CHECK(w.vuMax[v] == 0.f);
    }
    // ~440 Hz: about 2 * 440 crossings a second
    std::printf("  crossings %d\n", w.crossings[0]);
    CHECK(w.crossings[0] > 700 && w.crossings[0] < 1000);
}

// The mixed output is what Game_Music_Emu itself plays (the voice meters must not change it).
TEST(chipvoices_gme_mix_unchanged) {
    CHECK_EQ(gmeMixDiffers(onePulseNsf()), 0);
    CHECK_EQ(gmeMixDiffers(oneSquareVgm()), 0);
    const char* subs[] = {"SNES", "Atari8", "NES", "Spectrum", "MegaDrive"};
    int n = 0;
    for (const char* sub : subs) {
        const auto dir = modulesDir() / sub;
        if (!std::filesystem::exists(dir)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            const auto d = readFile(e.path());
            if (!GmePlayer::detect(d.data(), d.size())) continue;
            const int bad = gmeMixDiffers(d);
            if (bad < 0) continue;
            if (bad) std::printf("  %s: %d samples differ\n", e.path().filename().string().c_str(), bad);
            CHECK_EQ(bad, 0);
            ++n;
        }
    }
    std::printf("  %d files\n", n);
}

// The mixed output of every chip player, as rendered before the per-voice meters (FNV-1a of 3 s at 48 kHz).
TEST(chipvoices_mix_hashes_unchanged) {
    struct Ref {
        const char* dir;
        const char* ext;
        uint64_t hash;
    } const refs[] = {
        {"AtariST", ".ym", 0x99cefc0d5720ba03ull},     {"Spectrum", ".pt3", 0x981a438209e4c0d2ull},
        {"Spectrum", ".stc", 0xf057295ef2f769f0ull},   {"AtariST", ".sndh", 0xe25dda40ba542d9bull},
        {"C64", ".sid", 0x5d6cfd1783b3a6b3ull},        {"SNES", ".spc", 0xaa62e90e51301567ull},
        {"Atari8", ".sap", 0x3041312e84276ea3ull},     {"NES", ".nsf", 0x3213a22eae046dffull},
        {"Spectrum", ".emul", 0xe6586368a1d5040bull},  {"MegaDrive", ".vgz", 0x072ba2650229f480ull},
    };
    for (const Ref& r : refs) {
        const auto path = firstFile(r.dir, r.ext);
        if (path.empty()) continue;
        std::unique_ptr<ChipPlayer> p;
        const std::string ext = r.ext;
        if (ext == ".ym") p = loadReal<AyPlayer>(path);
        else if (ext == ".pt3") p = loadReal<Pt3Player>(path);
        else if (ext == ".stc") p = loadReal<ZxTrackerPlayer>(path);
        else if (ext == ".sndh") p = loadReal<SndhPlayer>(path);
        else if (ext == ".sid") p = loadReal<SidPlayer>(path);
        else p = loadReal<GmePlayer>(path);
        CHECK(p != nullptr);
        if (!p) continue;
        const Watch w = watch(*p, 3.0);
        std::printf("  %-38s %016llx\n", path.filename().string().c_str(), (unsigned long long)w.hash);
        if (r.hash) CHECK(w.hash == r.hash);
    }
    // the tiny tunes too
    {
        const auto f = twoChannelPsg();
        AyPlayer p;
        p.setRate(48000);
        CHECK(p.load(f.data(), f.size()));
        CHECK(watch(p, 1.0).hash == 0x9bfb5961fe5d93ffull);
    }
    {
        const auto f = oneVoicePsid();
        SidPlayer p;
        p.setRate(48000);
        CHECK(p.load(f.data(), f.size()));
        CHECK(watch(p, 1.0).hash == 0x9990f321050b73cbull);
    }
}
