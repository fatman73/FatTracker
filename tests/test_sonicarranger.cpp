// Sonic Arranger (ported from NostalgicPlayer): hand-built songs in the editor format ("SOARV1.0"), the
// lh.library packed form ("@OARV1.0") and the ripped player + data / data-only files NostalgicPlayer converts;
// sub-songs, broken files, the real modules, and a scan that no other module in the collection passes as it.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "build_mod.h"
#include "core/amiga_formats.h"
#include "core/amiga_synth.h"
#include "core/ay_player.h"
#include "core/gme_player.h"
#include "core/pt3_player.h"
#include "core/sid_player.h"
#include "core/sndh_player.h"
#include "core/song.h"
#include "core/tfmx_player.h"
#include "test.h"

namespace {
float peakOf(AmigaSynthPlayer& p, double seconds, float* vu = nullptr) {
    std::vector<float> buf(4800 * 2);
    float peak = 0;
    for (int i = 0; i < (int)(seconds * 10); ++i) {
        p.render(buf.data(), 4800, 0.45f);
        for (float v : buf) peak = std::fabs(v) > peak ? std::fabs(v) : peak;
        if (vu && i == 2)
            for (int c = 0; c < 4; ++c) vu[c] = p.vu(c);
    }
    return peak;
}
void be16(std::vector<uint8_t>& f, int v) { f.push_back((uint8_t)(v >> 8)), f.push_back((uint8_t)v); }
void be32(std::vector<uint8_t>& f, uint32_t v) { be16(f, (int)(v >> 16)), be16(f, (int)(v & 0xFFFF)); }
void put16(std::vector<uint8_t>& f, size_t at, int v) { f[at] = (uint8_t)(v >> 8), f[at + 1] = (uint8_t)v; }
void put32(std::vector<uint8_t>& f, size_t at, uint32_t v) { put16(f, at, (int)(v >> 16)), put16(f, at + 2, (int)(v & 0xFFFF)); }
void mark(std::vector<uint8_t>& f, const char* m) { f.insert(f.end(), m, m + std::strlen(m)); }
void append(std::vector<uint8_t>& f, const std::vector<uint8_t>& g) { f.insert(f.end(), g.begin(), g.end()); }

// The parts of a tiny song (each the raw records of one section). Sub-song 0 plays position 0, where voice 0
// plays track rows 0-15 (row 0: note 37, instrument 1) and voices 1-3 the empty rows 16-31; sub-song 1 plays
// position 1, the same with voices 0 and 1 swapped. Instrument 1 is a sample (8 words, then an 8 word loop:
// a square) or a synth sound over the first 16 words of wave table 0 (a square). 16 rows of speed 6 at 50 Hz.
struct Parts {
    uint32_t songs = 0, positions = 0, rows = 0, instruments = 0, samples = 0, waves = 0, adsr = 0, amf = 0;
    std::vector<uint8_t> song, pos, row, inst, sampleLens, sampleData, wave, adsrData, amfData;
};
Parts tinyParts(bool synth, bool twoSongs = false) {
    Parts p;
    p.songs = twoSongs ? 2 : 1;
    for (uint32_t s = 0; s < p.songs; ++s)
        for (int v : {6, 16, (int)s, (int)s, (int)s, 50}) be16(p.song, v);  // speed, rows, first, last, restart, Hz
    p.positions = 2;
    for (int pos = 0; pos < 2; ++pos)
        for (int c = 0; c < 4; ++c) be16(p.pos, c == pos ? 0 : 16), p.pos.push_back(0), p.pos.push_back(0);
    p.rows = 32;
    for (int r = 0; r < 32; ++r) {
        const bool on = r == 0;
        p.row.push_back(on ? 37 : 0), p.row.push_back(on ? 1 : 0), p.row.push_back(0), p.row.push_back(0);
    }
    p.instruments = 1;
    std::vector<uint8_t> in(152, 0);
    put16(in, 0, synth ? 1 : 0);
    put16(in, 2, 0);              // sample / wave table 0
    put16(in, 4, synth ? 16 : 8);  // one shot length (words)
    put16(in, 6, synth ? 0 : 8);  // repeat length (words)
    put16(in, 16, 64);            // volume
    std::memcpy(in.data() + 122, "TINY", 4);
    p.inst = in;
    if (!synth) {
        p.samples = 1;
        be32(p.sampleLens, 32);
        for (int i = 0; i < 32; ++i) p.sampleData.push_back((i & 15) < 8 ? 100 : (uint8_t)-100);
    }
    p.waves = 1;
    for (int i = 0; i < 128; ++i) p.wave.push_back((i & 31) < 16 ? 100 : (uint8_t)-100);
    return p;
}

std::vector<uint8_t> editorFile(const Parts& p) {
    std::vector<uint8_t> f;
    mark(f, "SOARV1.0");
    mark(f, "STBL"), be32(f, p.songs), append(f, p.song);
    mark(f, "OVTB"), be32(f, p.positions), append(f, p.pos);
    mark(f, "NTBL"), be32(f, p.rows), append(f, p.row);
    mark(f, "INST"), be32(f, p.instruments), append(f, p.inst);
    mark(f, "SD8B"), be32(f, p.samples);
    for (uint32_t i = 0; i < p.samples * (4 + 4 + 30); ++i) f.push_back(0);  // word lengths, repeats, names
    append(f, p.sampleLens), append(f, p.sampleData);
    mark(f, "SYWT"), be32(f, p.waves), append(f, p.wave);
    mark(f, "SYAR"), be32(f, p.adsr), append(f, p.adsrData);
    mark(f, "SYAF"), be32(f, p.amf), append(f, p.amfData);
    return f;
}

// Ripped data: eight offsets (from the start of the data) to the sub-songs, positions, rows, instruments,
// wave tables, ADSR tables, AMF tables and the sample block; with a player in front of it (seven JMPs and
// an init routine pointing at the data), or alone.
std::vector<uint8_t> rippedFile(const Parts& p, bool withPlayer) {
    std::vector<uint8_t> data(32, 0);
    const std::vector<const std::vector<uint8_t>*> parts = {&p.song, &p.pos, &p.row, &p.inst, &p.wave, &p.adsrData, &p.amfData};
    for (size_t i = 0; i < parts.size(); ++i) put32(data, 4 * i, (uint32_t)data.size()), append(data, *parts[i]);
    put32(data, 28, (uint32_t)data.size());
    be32(data, p.samples), append(data, p.sampleLens), append(data, p.sampleData);
    if (!withPlayer) return data;
    std::vector<uint8_t> f;
    for (int i = 0; i < 7; ++i) be16(f, 0x4EFA), be16(f, i == 0 ? 0x3E : 0x100);  // the first JMP to the init at 0x40
    f.resize(0x40, 0);
    be32(f, 0x48E7FFFE), be16(f, 0x41FA), be16(f, 0x100 - 0x46);  // lea data(pc),a0: the data at 0x100
    f.resize(0x100, 0x4E);
    append(f, data);
    f.resize(0x1700, 0x71);  // NostalgicPlayer wants at least 0x1630 bytes with a player
    return f;
}

// ---- lh.library encoder for the packed form: the decoder's adaptive Huffman tree (317 codes) mirrored
struct LhTree {
    static constexpr uint32_t kMax = 317, kRoot = kMax * 2 - 2;
    struct Node {
        uint32_t freq, index, parent, left, right;
    };
    std::vector<Node> n = std::vector<Node>(kMax * 2 - 1);
    std::vector<uint32_t> map = std::vector<uint32_t>(kMax * 2 - 1);
    LhTree() {
        for (uint32_t i = 0; i < kMax; ++i) n[i] = {1, i, kMax + (i >> 1), 0, 0}, map[i] = i;
        for (uint32_t i = kMax, j = 0; i < kMax * 2 - 1; ++i, j += 2) {
            n[i] = {n[j].freq + n[j + 1].freq, i, kMax + (i >> 1), j, j + 1};
            map[i] = i;
        }
    }
    uint32_t& slot(uint32_t c) {
        Node& p = n[n[c].parent];
        return p.left == c ? p.left : p.right;
    }
    void update(uint32_t c) {
        while (c != kRoot) {
            ++n[c].freq;
            const uint32_t index = n[c].index;
            uint32_t dest = index;
            while (dest != kRoot && n[c].freq > n[map[dest + 1]].freq) ++dest;
            if (index != dest) {
                const uint32_t d = map[dest];
                std::swap(n[c].index, n[d].index);
                std::swap(map[index], map[dest]);
                uint32_t& a = slot(c);
                uint32_t& b = slot(d);
                std::swap(a, b);
                std::swap(n[c].parent, n[d].parent);
            }
            c = n[c].parent;
        }
        ++n[c].freq;
    }
};
struct BitWriter {
    std::vector<uint8_t> out;
    int used = 8;
    void bit(int b) {
        if (used == 8) out.push_back(0), used = 0;
        if (b) out.back() |= (uint8_t)(0x80 >> used);
        ++used;
    }
    void bits(uint32_t v, int count) {
        for (int i = count - 1; i >= 0; --i) bit((v >> i) & 1);
    }
};
void lhCode(LhTree& t, BitWriter& w, uint32_t code) {
    std::vector<int> path;
    for (uint32_t c = code; c != LhTree::kRoot; c = t.n[c].parent) path.push_back(t.n[t.n[c].parent].right == c);
    for (size_t i = path.size(); i-- > 0;) w.bit(path[i]);
    if (t.n[LhTree::kRoot].freq < 0x8000) t.update(code);
}
// Literals, except that a run of 3+ equal bytes after the first becomes a copy at distance 1.
std::vector<uint8_t> lhPack(const std::vector<uint8_t>& raw) {
    LhTree t;
    BitWriter w;
    for (size_t i = 0; i < raw.size();) {
        size_t run = 0;
        while (i > 0 && i + run < raw.size() && raw[i + run] == raw[i - 1] && run < 60) ++run;
        if (run >= 3) {
            lhCode(t, w, 255 + (uint32_t)run);
            w.bits(0, 4), w.bits(1, 5);  // distance 1: base 0 (5 bits)
            i += run;
        } else {
            lhCode(t, w, raw[i++]);
        }
    }
    return w.out;
}
void packed(std::vector<uint8_t>& f, const char* m, uint32_t count, const std::vector<uint8_t>& raw) {
    mark(f, m), be32(f, count);
    if (count == 0) return;
    const auto z = lhPack(raw);
    be32(f, (uint32_t)z.size()), append(f, z);
}
std::vector<uint8_t> packedFile(const Parts& p) {
    std::vector<uint8_t> f;
    mark(f, "@OARV1.0");
    mark(f, "@TBL"), be32(f, p.songs), append(f, p.song);
    packed(f, "@VTB", p.positions, p.pos);
    packed(f, "@TBL", p.rows, p.row);
    packed(f, "@NST", p.instruments, p.inst);
    mark(f, "@D8B"), be32(f, p.samples);
    for (uint32_t i = 0; i < p.samples * (4 + 4 + 30); ++i) f.push_back(0);
    append(f, p.sampleLens);
    if (p.samples) {
        const auto z = lhPack(p.sampleData);
        be32(f, (uint32_t)z.size()), append(f, z);
    }
    packed(f, "@YWT", p.waves, p.wave);
    packed(f, "@YAR", p.adsr, p.adsrData);
    packed(f, "@YAF", p.amf, p.amfData);
    return f;
}

void checkTinyPlays(const std::vector<uint8_t>& f) {
    CHECK(isSonicArranger(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::detect(f.data(), f.size()));
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (!p) return;
    p->setRate(48000);
    CHECK(std::strncmp(p->format(), "SONIC ARRANGER", 14) == 0);
    CHECK_EQ(p->voices(), 4);
    CHECK_EQ(p->subsongs(), 1);
    CHECK(p->seconds() > 1.5 && p->seconds() < 2.5);  // 16 rows of 6 ticks
    float vu[4] = {};
    CHECK(peakOf(*p, 0.5, vu) > 0.05f);
    CHECK(vu[0] > 0.1f);
    CHECK(vu[1] < 0.01f && vu[2] < 0.01f && vu[3] < 0.01f);
    Song s;
    CHECK(p->buildDisplay(s));
}

// Odd but complete files load and play (briefly) without crashing, reading out of range or hanging.
void playOdd(const std::vector<uint8_t>& f) {
    const auto t0 = std::chrono::steady_clock::now();
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    if (p) {
        p->setRate(48000);
        peakOf(*p, 0.3);
        CHECK(p->seconds() >= 0 && p->seconds() <= 30 * 60 + 1);
    }
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0);
}

std::filesystem::path modulesRoot() {
    const std::filesystem::path main = "C:/development/FatTracker/modules";
    if (std::filesystem::exists(main)) return main;
    return std::filesystem::path(FT_SOURCE_DIR) / "modules";
}
std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}
// The players main.cpp tries before the Amiga synth formats must leave the file alone.
bool claimedFirst(const std::vector<uint8_t>& d) {
    return TfmxPlayer::detect(d.data(), d.size()) || SidPlayer::detect(d.data(), d.size()) ||
           SndhPlayer::detect(d.data(), d.size()) || Pt3Player::detect(d.data(), d.size()) ||
           GmePlayer::detect(d.data(), d.size()) || AyPlayer::detect(d.data(), d.size());
}
}  // namespace

TEST(sonicarranger_tiny_sample_song_plays) { checkTinyPlays(editorFile(tinyParts(false))); }
TEST(sonicarranger_tiny_synth_song_plays) { checkTinyPlays(editorFile(tinyParts(true))); }
TEST(sonicarranger_ripped_with_player_plays) {
    checkTinyPlays(rippedFile(tinyParts(false), true));
    checkTinyPlays(rippedFile(tinyParts(true), true));
}
TEST(sonicarranger_ripped_data_only_plays) {
    checkTinyPlays(rippedFile(tinyParts(false), false));
    checkTinyPlays(rippedFile(tinyParts(true), false));
}
TEST(sonicarranger_lh_packed_plays) {
    checkTinyPlays(packedFile(tinyParts(false)));
    checkTinyPlays(packedFile(tinyParts(true)));
}

TEST(sonicarranger_subsongs) {
    for (const auto& f : {editorFile(tinyParts(false, true)), rippedFile(tinyParts(true, true), true)}) {
        auto p = AmigaSynthPlayer::open(f.data(), f.size());
        CHECK(p != nullptr);
        if (!p) continue;
        p->setRate(48000);
        CHECK_EQ(p->subsongs(), 2);
        CHECK_EQ(p->subsong(), 0);
        float vu[4] = {};
        peakOf(*p, 0.5, vu);
        CHECK(vu[0] > 0.1f && vu[1] < 0.01f);
        CHECK(p->selectSubsong(1));
        CHECK_EQ(p->subsong(), 1);
        CHECK(p->seconds() > 1.5 && p->seconds() < 2.5);
        peakOf(*p, 0.5, vu);
        CHECK(vu[1] > 0.1f && vu[0] < 0.01f);  // the second sub-song plays the rows on voice 2
        CHECK(!p->selectSubsong(2));
    }
    // sub-songs that end at position 0xFFFF are unused, and so are repeats of an earlier one
    Parts q = tinyParts(false, true);
    put16(q.song, 12 + 6, 0xFFFF);
    auto f = editorFile(q);
    auto p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (p) CHECK_EQ(p->subsongs(), 1);
    q = tinyParts(false, true);
    put16(q.song, 12 + 4, 0), put16(q.song, 12 + 6, 0);  // the same positions as sub-song 0
    f = editorFile(q);
    p = AmigaSynthPlayer::open(f.data(), f.size());
    CHECK(p != nullptr);
    if (p) CHECK_EQ(p->subsongs(), 1);
    q = tinyParts(false);
    put16(q.song, 6, 0xFFFF);  // no usable sub-song at all
    f = editorFile(q);
    CHECK(!isSonicArranger(f.data(), f.size()));
    CHECK(AmigaSynthPlayer::open(f.data(), f.size()) == nullptr);
}

TEST(sonicarranger_broken_files) {
    for (int synth = 0; synth < 2; ++synth) {
        const Parts q = tinyParts(synth != 0);
        for (const auto& f : {editorFile(q), packedFile(q), rippedFile(q, false)}) {
            for (size_t cut = 0; cut < f.size(); cut += 5) {
                CHECK(!isSonicArranger(f.data(), cut));
                CHECK(AmigaSynthPlayer::open(f.data(), cut) == nullptr);
            }
        }
        const auto r = rippedFile(q, true);  // with a player the data is followed by padding: cut or not, safe
        for (size_t cut = 0; cut < r.size(); cut += 97) playOdd(std::vector<uint8_t>(r.begin(), r.begin() + (long)cut));
    }
    // counts far past the end of the file are refused at once (no huge allocations)
    const auto base = editorFile(tinyParts(false));
    const size_t stbl = 8 + 4, ovtb = stbl + 4 + 12 + 4, ntbl = ovtb + 4 + 32 + 4, inst = ntbl + 4 + 128 + 4;
    const size_t sd8b = inst + 4 + 152 + 4;
    for (size_t at : {stbl, ovtb, ntbl, inst, sd8b})
        for (uint32_t v : {0x7FFFFFFFu, 0xFFFFFFFFu, 0x10000000u}) {
            auto g = base;
            put32(g, at, v);
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
            CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 1.0);
        }
    auto g = base;
    put32(g, sd8b + 4 + 38, 0xFFFFFFF0u);  // a sample of 4 GB
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = base;
    put32(g, ovtb, 0);  // no positions
    g.erase(g.begin() + (long)ovtb + 4, g.begin() + (long)ovtb + 4 + 32);
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    g = base;
    std::memcpy(g.data() + 8, "XTBL", 4);  // a section mark missing
    CHECK(AmigaSynthPlayer::open(g.data(), g.size()) == nullptr);
    // packed: garbage instead of the packed positions, or packed sizes past the end
    auto z = packedFile(tinyParts(false));
    const size_t vtb = 8 + 4 + 4 + 12;
    for (size_t i = vtb + 12; i < vtb + 20 && i < z.size(); ++i) z[i] ^= 0xA5;
    playOdd(z);
    z = packedFile(tinyParts(false));
    put32(z, vtb + 8, 0xFFFFFF00u);
    CHECK(AmigaSynthPlayer::open(z.data(), z.size()) == nullptr);
    z = packedFile(tinyParts(false));
    put32(z, vtb + 4, 0x40000000u);  // 16 GB of positions
    CHECK(AmigaSynthPlayer::open(z.data(), z.size()) == nullptr);
    // ripped: offsets out of order or past the end
    auto r = rippedFile(tinyParts(false), false);
    put32(r, 4, 0xFFFF);
    CHECK(AmigaSynthPlayer::open(r.data(), r.size()) == nullptr);
    r = rippedFile(tinyParts(false), false);
    put32(r, 28, 0xFFFF);
    CHECK(AmigaSynthPlayer::open(r.data(), r.size()) == nullptr);
    r = rippedFile(tinyParts(false), true);
    put16(r, 0x46, 0x7FF0);  // the init routine points past the end of the file
    CHECK(AmigaSynthPlayer::open(r.data(), r.size()) == nullptr);
}

TEST(sonicarranger_odd_files_play_safely) {
    std::vector<std::vector<uint8_t>> odd;
    auto add = [&](const Parts& q) { odd.push_back(editorFile(q)); };
    const size_t row0 = 0;
    for (int synth = 0; synth < 2; ++synth) {
        const bool s = synth != 0;
        Parts q = tinyParts(s);
        q.row[row0 + 1] = 200;  // instrument 200 of 1
        add(q);
        q = tinyParts(s);
        q.row[row0] = 0x7E, q.pos[3] = 0x7F, q.row[row0 + 2] = 0x00;  // a note far past the table
        add(q);
        q = tinyParts(s);
        q.pos[2] = 0x80;  // instrument transposed below 0
        add(q);
        q = tinyParts(s);
        q.pos[2] = 0x7F;  // and past 255 (instrument 0 after & 0xFF)
        q.row[row0 + 1] = 0x81;
        add(q);
        q = tinyParts(s);
        put16(q.inst, 2, 999);  // sample / wave table 999
        add(q);
        q = tinyParts(s);
        put16(q.inst, 4, 0xFFFF), put16(q.inst, 6, 0xFFFF);  // lengths far past the data
        add(q);
        q = tinyParts(s);
        put16(q.inst, 6, 0);  // loop all
        add(q);
        q = tinyParts(s);
        put16(q.inst, 22, 0);   // vibrato delay 0
        put16(q.inst, 24, 77);  // speed
        put16(q.inst, 26, 0);   // level 0 (NostalgicPlayer would divide by it)
        add(q);
        q = tinyParts(s);
        put16(q.inst, 28, 50), put16(q.inst, 30, 1), put16(q.inst, 32, 300), put16(q.inst, 34, 7);  // AMF 50, past the tables
        put16(q.inst, 36, 60), put16(q.inst, 38, 1), put16(q.inst, 40, 400), put16(q.inst, 42, 9);  // ADSR 60
        put16(q.inst, 44, 2), put16(q.inst, 46, 3);
        add(q);
        q = tinyParts(s);
        put16(q.inst, 28, 0), put16(q.inst, 30, 0), put16(q.inst, 32, 200), put16(q.inst, 34, 0);  // AMF delay 0
        q.amf = 1, q.amfData.assign(128, 0x40);
        put16(q.inst, 36, 0), put16(q.inst, 38, 1), put16(q.inst, 40, 300), put16(q.inst, 44, 2), put16(q.inst, 46, 1);
        q.adsr = 1, q.adsrData.assign(128, 0xFF);
        q.row[4 * 1] = 0x80;  // hold for the sustain
        add(q);
        // arpeggio tables longer than their 14 values, used by the rows
        q = tinyParts(s);
        for (int a = 0; a < 3; ++a) q.inst[74 + 16 * a] = 200, q.inst[75 + 16 * a] = 100, q.inst[76 + 16 * a] = 0x7F;
        q.row[row0 + 2] = 0x30;
        add(q);
        // every track effect with odd arguments
        for (int fx = 0; fx < 16; ++fx)
            for (int arg : {0, 1, 0x40, 0x7F, 0x80, 0xFF}) {
                q = tinyParts(s);
                q.row[row0 + 2] = (uint8_t)(0xC0 | fx), q.row[row0 + 3] = (uint8_t)arg;
                q.row[4 * 5 + 2] = (uint8_t)fx, q.row[4 * 5 + 3] = (uint8_t)arg;
                add(q);
            }
        // tempo 0 and a very high one, speed 0, rows per track 0
        for (int v : {0, 0xFFFF}) {
            q = tinyParts(s);
            put16(q.song, 10, v);
            add(q);
        }
        q = tinyParts(s);
        put16(q.song, 0, 0), put16(q.song, 2, 0);
        add(q);
        q = tinyParts(s);
        put16(q.song, 4, 500), put16(q.song, 6, 600), put16(q.song, 8, 700);  // positions past the list
        add(q);
        q = tinyParts(s);
        put16(q.pos, 0, 0xFFF0);  // start row past the rows
        add(q);
    }
    // every synth effect with start / stop positions in and past the 128 byte buffer
    for (int fx = 0; fx <= 18; ++fx)
        for (int variant = 0; variant < 4; ++variant) {
            Parts q = tinyParts(true);
            const int a1[] = {0, 3, 0x80, 0xFFFF}, a2[] = {0, 10, 127, 0xFFF0}, a3[] = {127, 5, 200, 0xFFFF};
            put16(q.inst, 64, a1[variant]);
            put16(q.inst, 66, fx);
            put16(q.inst, 68, a2[variant]);
            put16(q.inst, 70, a3[variant]);
            put16(q.inst, 72, variant == 3 ? 0 : 1);  // effect delay (0 counts down from 65535)
            put16(q.inst, 4, variant == 2 ? 0x7FFF : 64);  // wave length (words): past the buffer
            q.waves = 2, q.wave.resize(256, 0x55);
            add(q);
        }
    for (const auto& f : odd) playOdd(f);
}

TEST(sonicarranger_detect_is_strict) {
    // MODs whose song name starts like a Sonic Arranger file, and lone marks with nothing behind them
    std::vector<std::vector<ModCell>> pat(1, std::vector<ModCell>(64 * 4, ModCell{-1, 0, 0, 0}));
    for (const char* name : {"SOARV1.0STBL", "@OARV1.0@TBL", "\x4E\xFA\x00\x2A\x4E\xFA"}) {
        auto mod = buildMod("M.K.", 4, {0}, pat, {ModSample{16, 0, 64, 0, 0, std::vector<int8_t>(32, 50)}});
        std::memcpy(mod.data(), name, std::strlen(name));
        CHECK(!isSonicArranger(mod.data(), mod.size()));
        auto p = AmigaSynthPlayer::open(mod.data(), mod.size());
        CHECK(p == nullptr || std::strncmp(p->format(), "SONIC ARRANGER", 14) != 0);
    }
    for (const char* m : {"SOARV1.0", "@OARV1.0"}) {
        std::vector<uint8_t> big(8192, 0);
        std::memcpy(big.data(), m, 8);
        CHECK(!isSonicArranger(big.data(), big.size()));
    }
    std::vector<uint8_t> zeros(8192, 0);
    CHECK(!isSonicArranger(zeros.data(), zeros.size()));
}

TEST(sonicarranger_real_modules_play) {
    const std::filesystem::path root = modulesRoot();
    std::vector<std::filesystem::path> files = {root / "Shadow of the Beast" / "Amiga Sonic Arranger - Skillion - shadow of the beast.sa"};
    if (std::filesystem::exists(root / "SonicArranger"))
        for (const auto& e : std::filesystem::directory_iterator(root / "SonicArranger"))
            if (e.is_regular_file()) files.push_back(e.path());
    int n = 0;
    for (const auto& path : files) {
        if (!std::filesystem::exists(path)) continue;
        const auto d = readAll(path);
        CHECK(isSonicArranger(d.data(), d.size()));
        CHECK(AmigaSynthPlayer::detect(d.data(), d.size()));
        CHECK(!claimedFirst(d));
        auto p = AmigaSynthPlayer::open(d.data(), d.size());
        CHECK(p != nullptr);
        if (!p) {
            std::printf("  FAIL %s\n", path.filename().string().c_str());
            continue;
        }
        p->setRate(48000);
        CHECK(std::strncmp(p->format(), "SONIC ARRANGER", 14) == 0);
        CHECK(p->seconds() > 5);
        const float pk = peakOf(*p, 3.0);
        CHECK(pk > 0.02f);
        std::printf("  %s: %s, %d sub-songs, %.1f s, peak %.2f\n", path.filename().string().c_str(), p->format(),
                    p->subsongs(), p->seconds(), pk);
        ++n;
    }
    if (n == 0) std::printf("SKIP sonicarranger_real_modules_play (no Sonic Arranger modules)\n");
}

// Every module in the collection that is not a Sonic Arranger file: the format does not claim it.
TEST(sonicarranger_no_false_positives) {
    const std::filesystem::path root = modulesRoot();
    if (!std::filesystem::exists(root)) {
        std::printf("SKIP sonicarranger_no_false_positives (no modules folder)\n");
        return;
    }
    int scanned = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || e.file_size() > (16u << 20)) continue;
        const std::string name = e.path().filename().string();
        if (e.path().parent_path().filename() == "SonicArranger" || name.rfind("Amiga Sonic Arranger", 0) == 0) continue;
        const auto d = readAll(e.path());
        const bool sa = isSonicArranger(d.data(), d.size());
        if (sa) std::printf("  FALSE POSITIVE %s\n", e.path().string().c_str());
        CHECK(!sa);
        ++scanned;
    }
    CHECK(scanned > 0);
}
