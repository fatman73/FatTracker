#include "build_mod.h"
#include "formats/formats.h"
#include "test.h"

#include <cstring>

namespace {
std::vector<ModCell> emptyPattern(int ch) { return std::vector<ModCell>((size_t)64 * ch, {-1, 0, 0, 0}); }
}  // namespace

TEST(mod_signature_channels) {
    struct {
        const char* sig;
        int ch;
    } cases[] = {{"M.K.", 4}, {"FLT8", 8}, {"6CHN", 6}, {"16CH", 16}, {"32CH", 32}};
    for (auto& c : cases) {
        auto b = buildMod(c.sig, c.ch, {0}, {emptyPattern(c.ch)}, {});
        auto r = loadModule(b.data(), b.size());
        CHECK(r.ok());
        if (r.ok()) CHECK_EQ(r.value().numChannels, c.ch);
    }
}

TEST(mod_cell_decode) {
    auto p = emptyPattern(4);
    p[0] = {12, 17, 0xC, 0x40};
    auto b = buildMod("M.K.", 4, {0}, {p}, std::vector<ModSample>(17, {1, 0, 64, 0, 1, {0, 0}}));
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& s = r.value();
    auto& c = s.patterns[0].at(0, 0);
    CHECK_EQ(c.note, 49);
    CHECK_EQ(c.instr, 17);
    CHECK_EQ(c.fx, 0xC);
    CHECK_EQ(c.param, 0x40);
    CHECK(s.quirks == Quirks::PT23);
    CHECK(s.freqMode == FreqMode::Amiga);
}

TEST(mod_sample_fields) {
    auto b = buildMod("M.K.", 4, {0}, {emptyPattern(4)}, {{100, -1, 70, 10, 20, std::vector<int8_t>(200, 5)}});
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& sm = r.value().samples[0];
    CHECK_EQ(sm.length, 200u);
    CHECK_EQ(sm.finetune, -16);
    CHECK_EQ(sm.volume, 64);
    CHECK(sm.loop == LoopType::Forward);
    CHECK_EQ(sm.loopStart, 20u);
    CHECK_EQ(sm.loopLength, 40u);
    CHECK_EQ(sm.data8[0], 5);
}

TEST(mod_replen_one_means_no_loop) {
    auto b = buildMod("M.K.", 4, {0}, {emptyPattern(4)}, {{100, 0, 64, 0, 1, {}}});
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (r.ok()) CHECK(r.value().samples[0].loop == LoopType::None);
}

TEST(mod_loop_end_clamped) {
    auto b = buildMod("M.K.", 4, {0}, {emptyPattern(4)}, {{100, 0, 64, 90, 20, {}}});
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& sm = r.value().samples[0];
    CHECK(sm.loop == LoopType::Forward);
    CHECK_EQ(sm.loopStart, 180u);
    CHECK_EQ(sm.loopLength, 20u);
}

TEST(mod_truncated_sample_data) {
    auto b = buildMod("M.K.", 4, {0}, {emptyPattern(4)}, {{100, 0, 64, 0, 1, std::vector<int8_t>(200, 7)}});
    b.resize(b.size() - 50);
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    auto& s = r.value();
    CHECK_EQ(s.samples[0].data8.size(), (size_t)s.samples[0].length);
    CHECK_EQ(s.samples[0].data8[149], 7);
    CHECK_EQ(s.samples[0].data8[150], 0);
    CHECK_EQ(s.samples[0].data8[199], 0);
    CHECK(s.loadFlags & LOAD_TRUNCATED);
}

TEST(mod_truncated_header_rejected) {
    std::vector<uint8_t> junk(500);
    for (size_t i = 0; i < junk.size(); ++i) junk[i] = (uint8_t)(i * 37 + 11);
    auto r = loadModule(junk.data(), junk.size());
    CHECK(!r.ok());
    if (!r.ok()) CHECK(std::strcmp(r.error(), "NOT A MODULE") == 0);
}

TEST(mod_soundtracker_15) {
    auto b = buildSoundtracker({0}, {emptyPattern(4)}, {{10, 0, 64, 0, 1, {}}});
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK_EQ(r.value().numChannels, 4);
    CHECK_EQ(r.value().samples.size(), 15u);
}
