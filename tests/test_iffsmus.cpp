// IFF SMUS (Electronic Arts Deluxe Music / Sonix scores with external instrument files, ported from
// NostalgicPlayer): hand-built scores with one hand-built instrument of each kind (Sonix synthesis,
// SampledSound + .ss, IFF 8SVX) given through a resolver, missing / unsafe / broken companion files,
// broken scores, the real scores with their instruments, and the own-data rule.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "audio/engine.h"
#include "core/amiga_formats.h"
#include "core/amiga_synth.h"
#include "core/song.h"
#include "test.h"

namespace {
using Files = std::map<std::string, std::vector<uint8_t>>;

void be16(std::vector<uint8_t>& f, int v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void be32(std::vector<uint8_t>& f, uint32_t v) {
    be16(f, (int)(v >> 16));
    be16(f, (int)(v & 0xFFFF));
}
void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) {
    f[at] = (uint8_t)(v >> 24), f[at + 1] = (uint8_t)(v >> 16), f[at + 2] = (uint8_t)(v >> 8), f[at + 3] = (uint8_t)v;
}
void chunk(std::vector<uint8_t>& f, const char* id, const std::vector<uint8_t>& body) {
    f.insert(f.end(), id, id + 4);
    be32(f, (uint32_t)body.size());
    f.insert(f.end(), body.begin(), body.end());
    if (body.size() & 1) f.push_back(0);
}
std::vector<uint8_t> form(const char* type, const std::vector<uint8_t>& chunks) {
    std::vector<uint8_t> f = {'F', 'O', 'R', 'M'};
    be32(f, (uint32_t)(chunks.size() + 4));
    f.insert(f.end(), type, type + 4);
    f.insert(f.end(), chunks.begin(), chunks.end());
    return f;
}

// A 4 track score: track 0 selects instrument register 0 ("Tiny") and plays middle C (60) twice as whole notes
// (32 steps each); tracks 1-3 hold one whole rest each. Tempo 0x397C (about 115 quarter notes a minute).
struct Score {
    int channels = 4;
    std::string instrument = "Tiny";
    bool snx = true;
    int extraTracks = 0;
};
std::vector<uint8_t> tinyScore(const Score& s = {}) {
    std::vector<uint8_t> c, b;
    chunk(c, "NAME", {'T', 'i', 'n', 'y'});
    b.clear();
    be16(b, 0x397C);
    b.push_back(127), b.push_back((uint8_t)s.channels);
    chunk(c, "SHDR", b);
    if (s.snx) {
        b.clear();
        be16(b, 0x80), be16(b, 0x80), be32(b, 0);
        for (int i = 0; i < s.channels; ++i) be32(b, 1);
        chunk(c, "SNX1", b);
    }
    b = {0, 0, 0, 0};
    b.insert(b.end(), s.instrument.begin(), s.instrument.end());
    chunk(c, "INS1", b);
    chunk(c, "TRAK", {0x81, 0, 60, 0, 60, 0});
    for (int t = 1; t < s.channels + s.extraTracks; ++t) chunk(c, "TRAK", {0x80, 0});
    return form("SMUS", c);
}

// Sonix synthesis instrument: a square oscillator, filter open, envelope up at once, release to 0.
std::vector<uint8_t> synthInstr() {
    std::vector<uint8_t> f(0x44, 0);
    std::memcpy(f.data(), "Synthesis", 9);
    for (int i = 0; i < 128; ++i) f.push_back(i < 64 ? 100 : (uint8_t)-100);  // oscillator
    for (int i = 0; i < 256; ++i) f.push_back(0);                                 // LFO table
    be16(f, 0), be32(f, 0);                                                       // waveform, (skipped)
    be16(f, 0), be16(f, 0xFF), be16(f, 1), be16(f, 0);                            // wave amount, amplitude, envelope on, amp LFO
    be16(f, 0), be16(f, 0), be16(f, 0xFF), be16(f, 0), be16(f, 0);                // port, freq LFO, filter, filter EG, filter LFO
    be16(f, 0), be16(f, 0), be16(f, 0), be16(f, 0), be16(f, 0);                   // LFO speed / on / delay, phase speed / depth
    for (int v : {0xFF, 0xFF, 0xFF, 0}) be16(f, v);                               // envelope levels
    for (int i = 0; i < 4; ++i) be16(f, 0xFF);                                    // envelope rates
    return f;                                                                     // 502 bytes
}
// SampledSound instrument naming sample `sample` (<sample>.ss).
std::vector<uint8_t> sampledInstr(const std::string& sample) {
    std::vector<uint8_t> f(0x44, 0);
    std::memcpy(f.data(), "SampledSound", 12);
    std::vector<uint8_t> name(32, 0);
    std::memcpy(name.data(), sample.data(), sample.size() < 31 ? sample.size() : 31);
    f.insert(f.end(), name.begin(), name.end());
    be32(f, 0);                                       // pointer to data
    be16(f, 0xFF);                                    // volume
    for (int v : {0xFF, 0xFF, 0xFF, 0}) be16(f, v);   // envelope levels
    for (int i = 0; i < 4; ++i) be16(f, 0xFF);        // envelope rates
    be16(f, 0), be16(f, 0), be16(f, 0);               // vibrato depth, speed, delay
    return f;                                         // 128 bytes
}
// .ss sample: one octave (5, the one middle C plays), 32 bytes for octave "one" so 1024 bytes of square wave.
std::vector<uint8_t> ssFile(int startOct = 5, int endOct = 5, size_t bytes = 1024) {
    std::vector<uint8_t> f;
    be16(f, 32), be16(f, 0);
    f.push_back((uint8_t)startOct), f.push_back((uint8_t)endOct);
    f.resize(0x3E, 0);
    for (size_t i = 0; i < bytes; ++i) f.push_back((i / 32) & 1 ? 100 : (uint8_t)-100);
    return f;
}
// IFF 8SVX instrument: one octave, 64 byte looped square.
std::vector<uint8_t> svxInstr(int octaves = 1, int compression = 0) {
    std::vector<uint8_t> c, b;
    be32(b, 0), be32(b, 64), be32(b, 32), be16(b, 8363);
    b.push_back((uint8_t)octaves), b.push_back((uint8_t)compression);
    be32(b, 0x10000);
    chunk(c, "VHDR", b);
    b.clear();
    for (int i = 0; i < 64; ++i) b.push_back(i < 32 ? 100 : (uint8_t)-100);
    chunk(c, "BODY", b);
    return form("8SVX", c);
}

AmigaFileResolver mapResolver(const Files& files, std::vector<std::string>* asked = nullptr) {
    return [&files, asked](const std::string& name, std::vector<uint8_t>& out) {
        if (asked) asked->push_back(name);
        auto it = files.find(name);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    };
}

float peakOf(AmigaSynthPlayer& p, double seconds, float* vu = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (vu && i == 4)
            for (int c = 0; c < 4; ++c) vu[c] = p.vu(c);
    }
    return peak;
}

void checkTinyPlays(const Files& files) {
    const auto f = tinyScore();
    CHECK(isIffSmus(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::needsFiles(f.data(), f.size()));
    CHECK(!AmigaSynthPlayer::detect(f.data(), f.size()));  // needs its instruments: never through open()
    CHECK(AmigaSynthPlayer::open(f.data(), f.size()) == nullptr);
    std::string err;
    auto p = AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files), &err);
    CHECK(p != nullptr);
    if (!p) {
        std::printf("  error: %s\n", err.c_str());
        return;
    }
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "IFF SMUS", 8) == 0);
    CHECK_EQ(p->voices(), 4);
    // 64 steps at tempo index 64: 3 ticks a step at about 45.6 Hz -> about 4.2 s
    CHECK(p->seconds() > 3.5 && p->seconds() < 5.0);
    float vu[4] = {};
    CHECK(peakOf(*p, 1.0, vu) > 0.05f);
    CHECK(vu[0] > 0.1f);
    CHECK(vu[1] < 0.01f && vu[2] < 0.01f && vu[3] < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

// Loads or is refused; when it loads it plays briefly without crashing, reading out of range or hanging.
void playSafe(const std::vector<uint8_t>& f, const Files& files) {
    auto p = AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files));
    if (!p) return;
    p->setRate(48000);
    peakOf(*p, 0.3);
    CHECK(p->seconds() >= 0 && p->seconds() <= 30 * 60 + 1);
}

uint32_t lcg(uint32_t& s) { return s = s * 1664525u + 1013904223u; }

std::filesystem::path smusDir() {
    const std::filesystem::path main = "C:/development/FatTracker/modules/Dr. Awesome/IFF-SMUS";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules" / "Dr. Awesome" / "IFF-SMUS";
}
std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}
bool isScoreName(const std::filesystem::path& p) {
    std::string n = p.filename().string();
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    return n.size() > 5 && n.compare(n.size() - 5, 5, ".smus") == 0;
}
}  // namespace

TEST(iffsmus_tiny_synthesis_score_plays) { checkTinyPlays({{"Tiny.instr", synthInstr()}}); }
TEST(iffsmus_tiny_sampled_sound_score_plays) {
    checkTinyPlays({{"Tiny.instr", sampledInstr("TinySample")}, {"TinySample.ss", ssFile()}});
}
TEST(iffsmus_tiny_8svx_score_plays) { checkTinyPlays({{"Tiny.instr", svxInstr()}}); }

TEST(iffsmus_instrument_names_are_fixed_like_nostalgicplayer) {
    // '*' becomes '+', '?' '<' '>' become '!' (as the instruments were saved on the PC)
    Score s;
    s.instrument = "A*B?";
    const auto f = tinyScore(s);
    const Files files = {{"A+B!.instr", synthInstr()}};
    CHECK(AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files)) != nullptr);
}

TEST(iffsmus_missing_instrument_is_refused) {
    // NostalgicPlayer refuses a score whose instrument (or SampledSound sample) cannot be opened
    const auto f = tinyScore();
    std::string err;
    const Files none;
    CHECK(AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(none), &err) == nullptr);
    CHECK(err.find("Tiny.instr") != std::string::npos);
    const Files noSample = {{"Tiny.instr", sampledInstr("Gone")}};
    err.clear();
    CHECK(AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(noSample), &err) == nullptr);
    CHECK(err.find("Gone.ss") != std::string::npos);
    CHECK(AmigaSynthPlayer::openWithFiles(f.data(), f.size(), AmigaFileResolver{}) == nullptr);  // no resolver
}

TEST(iffsmus_unsafe_instrument_names_never_reach_the_resolver) {
    for (const char* bad : {"../Tiny", "..\\Tiny", "sub/Tiny", "sub\\Tiny", "C:Tiny", "..", "a..b"}) {
        Score s;
        s.instrument = bad;
        const auto f = tinyScore(s);
        std::vector<std::string> asked;
        const Files files = {{std::string(bad) + ".instr", synthInstr()}};
        CHECK(AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files, &asked)) == nullptr);
        CHECK(asked.empty());
    }
    // the sample name inside a SampledSound instrument is checked the same way
    const auto f = tinyScore();
    std::vector<std::string> asked;
    const Files files = {{"Tiny.instr", sampledInstr("../x")}, {"../x.ss", ssFile()}};
    CHECK(AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files, &asked)) == nullptr);
    CHECK(asked.size() == 1 && asked[0] == "Tiny.instr");
}

TEST(iffsmus_folder_resolver_reads_only_plain_names_in_its_folder) {
    const std::filesystem::path dir = smusDir();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP iffsmus_folder_resolver_reads_only_plain_names_in_its_folder (no IFF-SMUS folder)\n");
        return;
    }
    AmigaFileResolver r = folderFileResolver(dir.wstring());
    std::vector<uint8_t> out;
    CHECK(r("dream.smus", out) && out.size() == readAll(dir / "dream.smus").size());
    CHECK(r("DREAM.SMUS", out) && !out.empty());     // case-insensitive
    CHECK(r("hornblast.ss", out) && !out.empty());   // HornBlast.ss
    CHECK(!r("nothere.instr", out) && out.empty());
    for (const char* bad : {"../IFF-SMUS/dream.smus", "..\\IFF-SMUS\\dream.smus", "./dream.smus", ".\\dream.smus",
                            "C:dream.smus", "..", "", "dream.smus/", "a..b"})
        CHECK(!r(bad, out));
    CHECK(!r((dir / "dream.smus").string(), out));  // a full path is not a plain name
    CHECK(isPlainFileName("Softest of'em All!.instr"));
    CHECK(!isPlainFileName("x\ty"));
}

TEST(iffsmus_broken_scores) {
    const Files files = {{"Tiny.instr", synthInstr()}};
    const auto f = tinyScore();
    for (size_t cut = 0; cut < f.size(); ++cut) playSafe(std::vector<uint8_t>(f.begin(), f.begin() + cut), files);
    for (size_t cut = 0; cut < 12; ++cut) CHECK(AmigaSynthPlayer::openWithFiles(f.data(), cut, mapResolver(files)) == nullptr);
    // a chunk longer than the file
    auto g = f;
    put32(g, 16, 0x7FFFFFF0);
    CHECK(AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(files)) == nullptr);
    put32(g, 16, 0xFFFFFFF0);  // negative
    CHECK(AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(files)) == nullptr);
    // no tracks / no channels / more tracks than channels / more channels than the Amiga synth layer has
    Score s;
    s.channels = 0;
    g = tinyScore(s);
    CHECK(AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(files)) == nullptr);
    s = {};
    s.extraTracks = 1;
    g = tinyScore(s);
    CHECK(AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(files)) == nullptr);
    s = {};
    s.channels = 200;
    g = tinyScore(s);
    CHECK(AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(files)) == nullptr);
    s = {};
    s.channels = 8;  // as many as the layer has: plays
    g = tinyScore(s);
    CHECK(AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(files)) != nullptr);
    // random damage
    uint32_t seed = 1234;
    for (int k = 0; k < 400; ++k) {
        g = f;
        for (int j = 0; j < 4; ++j) g[12 + lcg(seed) % (g.size() - 12)] = (uint8_t)(lcg(seed) >> 24);
        playSafe(g, files);
    }
}

TEST(iffsmus_broken_instruments) {
    const auto f = tinyScore();
    auto refused = [&](const Files& files) {
        return AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files)) == nullptr;
    };
    const auto synth = synthInstr(), sampled = sampledInstr("S"), ss = ssFile(), svx = svxInstr();
    for (size_t cut = 0; cut < synth.size(); cut += 5) CHECK(refused({{"Tiny.instr", std::vector<uint8_t>(synth.begin(), synth.begin() + cut)}}));
    for (size_t cut = 0; cut < sampled.size(); cut += 3)
        CHECK(refused({{"Tiny.instr", std::vector<uint8_t>(sampled.begin(), sampled.begin() + cut)}, {"S.ss", ss}}));
    for (size_t cut = 0; cut < 0x3E; ++cut)
        CHECK(refused({{"Tiny.instr", sampled}, {"S.ss", std::vector<uint8_t>(ss.begin(), ss.begin() + cut)}}));
    for (size_t cut = 0; cut + 1 < svx.size(); cut += 3)
        CHECK(refused({{"Tiny.instr", std::vector<uint8_t>(svx.begin(), svx.begin() + cut)}}));
    CHECK(refused({{"Tiny.instr", svxInstr(0)}}));     // no octaves
    CHECK(refused({{"Tiny.instr", svxInstr(1, 1)}}));  // Fibonacci-delta compressed
    std::vector<uint8_t> unknown(64, 'x');
    CHECK(refused({{"Tiny.instr", unknown}}));
    // odd but complete instruments play safely: octave ranges, lengths and loops that do not fit the data
    playSafe(f, {{"Tiny.instr", sampled}, {"S.ss", ssFile(0, 10, 100)}});
    playSafe(f, {{"Tiny.instr", sampled}, {"S.ss", ssFile(255, 0, 100)}});
    playSafe(f, {{"Tiny.instr", sampled}, {"S.ss", ssFile(0, 255, 0)}});
    auto bigLoop = ssFile(5, 5, 10);
    bigLoop[0] = bigLoop[1] = 0xFF, bigLoop[2] = 0x80;
    playSafe(f, {{"Tiny.instr", sampled}, {"S.ss", bigLoop}});
    playSafe(f, {{"Tiny.instr", svxInstr(200)}});
    // random damage to each kind of instrument
    uint32_t seed = 99;
    for (int k = 0; k < 300; ++k) {
        auto a = synthInstr(), b = sampledInstr("S"), c = ssFile(), d = svxInstr();
        for (auto* v : {&a, &b, &c, &d})
            for (int j = 0; j < 6; ++j) (*v)[lcg(seed) % v->size()] = (uint8_t)(lcg(seed) >> 24);
        playSafe(f, {{"Tiny.instr", a}});
        playSafe(f, {{"Tiny.instr", b}, {"S.ss", c}});
        playSafe(f, {{"Tiny.instr", d}});
    }
}

TEST(iffsmus_real_scores_play_with_their_instruments) {
    const std::filesystem::path dir = smusDir();
    if (!std::filesystem::exists(dir)) {
        std::printf("SKIP iffsmus_real_scores_play_with_their_instruments (no IFF-SMUS folder)\n");
        return;
    }
    const AmigaFileResolver files = folderFileResolver(dir.wstring());
    int n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file() || !isScoreName(e.path())) continue;
        const auto d = readAll(e.path());
        CHECK(isIffSmus(d.data(), d.size()));
        std::string err;
        auto p = AmigaSynthPlayer::openWithFiles(d.data(), d.size(), files, &err);
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s: %s\n", e.path().filename().string().c_str(), err.c_str());
            continue;
        }
        p->setRate(48000);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.02f);
        CHECK(p->seconds() > 5.0);
        std::printf("  %s: %s, %d voices, %.1f s, peak %.2f\n", e.path().filename().string().c_str(), p->format(),
                    p->voices(), p->seconds(), pk);
        ++n;
    }
    CHECK(n >= 6);
    std::printf("  iff smus: %d scores\n", n);
}

TEST(iffsmus_player_keeps_its_own_data) {
    // the score buffer and the resolver's files may go away after loading
    Files files = {{"Tiny.instr", sampledInstr("S")}, {"S.ss", ssFile()}};
    std::vector<uint8_t> f = tinyScore();
    auto ref = AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files));
    auto p = AmigaSynthPlayer::openWithFiles(f.data(), f.size(), mapResolver(files));
    CHECK(ref && p);
    if (!ref || !p) return;
    for (auto& b : f) b = 0x5A;
    for (auto& kv : files)
        for (auto& b : kv.second) b = 0x5A;
    ref->setRate(22050), p->setRate(22050);
    std::vector<float> a(2205 * 2), b(2205 * 2);
    bool same = true;
    for (int i = 0; i < 20 && same; ++i) {
        ref->render(a.data(), 2205, 0.45f);
        p->render(b.data(), 2205, 0.45f);
        same = a == b;
    }
    CHECK(same);
    // and the same for a real score and its instrument files
    const std::filesystem::path dir = smusDir();
    if (!std::filesystem::exists(dir)) return;
    Files real;
    const AmigaFileResolver folder = folderFileResolver(dir.wstring());
    AmigaFileResolver copying = [&](const std::string& name, std::vector<uint8_t>& out) {
        if (!folder(name, out)) return false;
        real[name] = out;
        return true;
    };
    std::vector<uint8_t> g = readAll(dir / "dream.smus");
    auto r1 = AmigaSynthPlayer::openWithFiles(g.data(), g.size(), copying);
    auto r2 = AmigaSynthPlayer::openWithFiles(g.data(), g.size(), mapResolver(real));
    CHECK(r1 && r2);
    if (!r1 || !r2) return;
    for (auto& x : g) x = 0x5A;
    for (auto& kv : real)
        for (auto& x : kv.second) x = 0x5A;
    r1->setRate(22050), r2->setRate(22050);
    same = true;
    for (int i = 0; i < 40 && same; ++i) {
        r1->render(a.data(), 2205, 0.45f);
        r2->render(b.data(), 2205, 0.45f);
        same = a == b;
    }
    CHECK(same);
}

TEST(iffsmus_engine_loads_with_files) {
    const Files files = {{"Tiny.instr", synthInstr()}};
    const auto f = tinyScore();
    Engine e;
    CHECK(!e.loadAmigaSynth(f.data(), f.size(), "TINY", false));  // needs its instrument
    CHECK(e.loadAmigaSynth(f.data(), f.size(), mapResolver(files), "TINY", false));
    CHECK(std::strncmp(e.tfmxFormat(), "IFF SMUS", 8) == 0);
}
