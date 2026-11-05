#include <cstring>

#include "build_xm.h"
#include "formats/formats.h"
#include "formats/writers.h"
#include "test.h"

namespace {
Song richSong() {
    Song s = makeEmptySong(32);
    s.quirks = Quirks::FT2;
    s.freqMode = FreqMode::Linear;
    std::strcpy(s.name, "roundtrip");
    s.initialSpeed = 5;
    s.initialBpm = 140;
    s.patterns.push_back(makePattern(32, 32));
    s.order = {0, 1, 0};
    s.restartPos = 1;
    s.patterns[0].at(0, 0) = {49, 1, 0x10 + 0x30, 0xC, 0x20};
    s.patterns[0].at(63, 31) = {NOTE_OFF, 0, 0x64, 20, 3};
    s.patterns[1].at(5, 7) = {96, 2, 0, 0xF, 0x7D};
    Sample a;
    std::strcpy(a.name, "eight");
    a.length = 6;
    a.data8 = {0, 10, -10, 127, -128, 5};
    a.loop = LoopType::Forward;
    a.loopStart = 2;
    a.loopLength = 4;
    a.volume = 40;
    a.finetune = -16;
    a.relativeNote = 3;
    a.pan = 0x30;
    Sample b;
    std::strcpy(b.name, "sixteen");
    b.is16 = true;
    b.length = 5;
    b.data16 = {0, 1000, -1000, 32767, -32768};
    b.loop = LoopType::PingPong;
    b.loopStart = 1;
    b.loopLength = 3;
    s.samples = {a, b};
    return s;
}
}  // namespace

TEST(xm_save_roundtrip) {
    const Song s = richSong();
    auto bytes = saveXm(s);
    auto r = loadModule(bytes.data(), bytes.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& t = r.value();
    CHECK(std::strcmp(t.name, "roundtrip") == 0);
    CHECK_EQ(t.numChannels, 32);
    CHECK(t.order == s.order);
    CHECK_EQ(t.restartPos, 1);
    CHECK_EQ(t.initialSpeed, 5);
    CHECK_EQ(t.initialBpm, 140);
    CHECK(t.freqMode == FreqMode::Linear);
    CHECK_EQ(t.patterns.size(), 2u);
    CHECK_EQ(t.patterns[1].rows, 32);
    bool same = true;
    for (int p = 0; p < 2; ++p)
        for (size_t i = 0; i < s.patterns[p].cells.size(); ++i) {
            const Cell &x = s.patterns[p].cells[i], &y = t.patterns[p].cells[i];
            same = same && x.note == y.note && x.instr == y.instr && x.vol == y.vol && x.fx == y.fx && x.param == y.param;
        }
    CHECK(same);
    CHECK_EQ(t.samples.size(), 2u);
    CHECK_EQ(t.instruments.size(), 2u);
    const Sample &a = t.samples[0], &b = t.samples[1];
    CHECK(std::strcmp(a.name, "eight") == 0);
    CHECK(!a.is16);
    CHECK(a.data8 == s.samples[0].data8);
    CHECK(a.loop == LoopType::Forward);
    CHECK_EQ(a.loopStart, 2u);
    CHECK_EQ(a.loopLength, 4u);
    CHECK_EQ(a.volume, 40);
    CHECK_EQ(a.finetune, -16);
    CHECK_EQ(a.relativeNote, 3);
    CHECK_EQ(a.pan, 0x30);
    CHECK(b.is16);
    CHECK(b.data16 == s.samples[1].data16);
    CHECK(b.loop == LoopType::PingPong);
    CHECK_EQ(t.instruments[1].sampleMap[40], 1);
}

TEST(xm_save_keeps_instruments_and_envelopes) {
    XmInstSpec in;
    XmSampleSpec smp;
    smp.pcm = {1, 2, 3};
    in.samples.push_back(smp);
    in.samples.push_back(smp);
    in.noteMap[60] = 1;
    in.vol.pts[0] = {0, 64};
    in.vol.pts[1] = {10, 20};
    in.vol.count = 2;
    in.vol.flags = ENV_ON;
    in.fadeout = 0x300;
    auto src = buildXm(4, {0}, {makePattern(64, 4)}, {in}, true, 6, 125);
    auto first = loadModule(src.data(), src.size());
    CHECK(first.ok());
    if (!first.ok()) return;
    auto bytes = saveXm(first.value());
    auto r = loadModule(bytes.data(), bytes.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Instrument& i = r.value().instruments[0];
    CHECK_EQ(r.value().samples.size(), 2u);
    CHECK_EQ(i.sampleMap[60], 1);
    CHECK_EQ(i.sampleMap[0], 0);
    CHECK_EQ(i.volEnv.count, 2);
    CHECK_EQ(i.volEnv.pts[1].value, 20);
    CHECK_EQ(i.fadeout, 0x300);
}

TEST(mod_export_roundtrip_and_warnings) {
    Song s = makeEmptySong(8);
    s.quirks = Quirks::PT23;
    s.patterns[0].at(0, 0) = {49, 1, 0, 0xC, 0x20};      // PT C-2
    s.patterns[0].at(1, 1) = {49, 2, 0x10 + 0x20, 0, 0};  // volume column -> C20
    s.patterns[0].at(2, 2) = {49, 1, 0x10 + 0x10, 0xA, 1};  // volume column lost (effect present)
    s.patterns[0].at(3, 3) = {49, 1, 0, 20, 1};           // XM-only effect K -> dropped
    Sample a;
    a.length = 4;
    a.data8 = {1, 2, 3, 4};
    a.volume = 64;
    Sample b;
    b.is16 = true;
    b.length = 4;
    b.data16 = {256, -256, 32767, -32768};
    s.samples = {a, b};
    ModExport out = exportMod(s);
    CHECK(out.warnings.size() >= 3);  // 16-bit, lost volume column, dropped effect
    CHECK_EQ(std::memcmp(out.data.data() + 1080, "8CHN", 4), 0);
    auto r = loadModule(out.data.data(), out.data.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Pattern& p = r.value().patterns[0];
    CHECK_EQ(p.at(0, 0).note, 49);
    CHECK_EQ(p.at(0, 0).fx, 0xC);
    CHECK_EQ(p.at(1, 1).fx, 0xC);
    CHECK_EQ(p.at(1, 1).param, 0x20);
    CHECK_EQ(p.at(3, 3).fx, 0);
    CHECK(!r.value().samples[1].is16);
    CHECK_EQ(r.value().samples[1].data8[0], 1);  // 256 >> 8
}

TEST(mod_export_signatures) {
    for (int ch : {4, 6, 12, 32}) {
        Song s = makeEmptySong(ch);
        ModExport out = exportMod(s);
        const char* sig = (const char*)out.data.data() + 1080;
        if (ch == 4) CHECK_EQ(std::memcmp(sig, "M.K.", 4), 0);
        if (ch == 6) CHECK_EQ(std::memcmp(sig, "6CHN", 4), 0);
        if (ch == 12) CHECK_EQ(std::memcmp(sig, "12CH", 4), 0);
        if (ch == 32) CHECK_EQ(std::memcmp(sig, "32CH", 4), 0);
        auto r = loadModule(out.data.data(), out.data.size());
        CHECK(r.ok() && r.value().numChannels == ch);
    }
}

TEST(unsupported_formats_are_named) {
    struct {
        const char* magic;
        size_t offset;
        const char* expect;
    } cases[] = {{"TFMX-SONG ", 0, "UNSUPPORTED FORMAT: TFMX"},
                 {"MMD1", 0, "BAD OCTAMED FILE"},  // OctaMED is supported now: a header of zeros is broken
                 {"SymM", 0, "BAD SYMPHONIE FILE"},  // Symphonie is supported now: a header of zeros is broken
                 {"SCRM", 44, "BAD S3M FILE"},  // S3M is supported now
                 {"IMPM", 0, "BAD IT FILE"}};  // IT is supported now
    for (auto& c : cases) {
        std::vector<uint8_t> d(2048, 0);
        std::memcpy(d.data() + c.offset, c.magic, std::strlen(c.magic));
        auto r = loadModule(d.data(), d.size());
        CHECK(!r.ok());
        if (!r.ok()) CHECK(std::strncmp(r.error(), c.expect, std::strlen(c.expect)) == 0);
    }
}
