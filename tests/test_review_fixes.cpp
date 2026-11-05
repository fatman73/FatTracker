// Regression tests for the final-review findings (stage 1).
#include <cstring>
#include <string_view>

#include "build_xm.h"
#include "formats/formats.h"
#include "player_util.h"
#include "test.h"

namespace {
size_t findBytes(const std::vector<uint8_t>& b, std::string_view s) {
    for (size_t i = 0; i + s.size() <= b.size(); ++i)
        if (std::memcmp(b.data() + i, s.data(), s.size()) == 0) return i;
    return (size_t)-1;
}
}  // namespace

// Critical 1: pattern cells must not exhaust (and underflow) the sample allocation budget.
TEST(xm_huge_sample_length_capped) {
    std::vector<Pattern> pats(256, makePattern(256, 32));
    XmInstSpec in;
    XmSampleSpec smp;
    smp.pcm = {1, 2, 3, 4};
    in.samples.push_back(smp);
    auto b = buildXm(32, {0}, pats, {in}, true, 6, 125);
    size_t name = findBytes(b, "smp");
    CHECK(name != (size_t)-1);
    if (name == (size_t)-1) return;
    const size_t lenPos = name - 18;  // length field precedes loop start/len, vol, ft, type, pan, rel, res
    b[lenPos] = b[lenPos + 1] = b[lenPos + 2] = b[lenPos + 3] = 0xFF;
    auto r = loadModule(b.data(), b.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    size_t sampleBytes = 0;
    for (const Sample& s : r.value().samples) sampleBytes += (size_t)s.length * (s.is16 ? 2 : 1);
    CHECK(sampleBytes <= b.size() * 4 + 65536);
    CHECK(r.value().loadFlags & LOAD_TRUNCATED);
}

// Important 2: F00 must stop notes triggered by later channels on the same row.
TEST(player_f00_stops_later_channels) {
    Mixer m(48000);
    m.setRamp(false);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0) = {0, 0, 0, 0xF, 0x00};
    s.patterns[0].at(0, 1) = {49, 1, 0, 0, 0};
    p.setSong(&s);
    p.play();
    p.tick();
    CHECK(!p.playing());
    CHECK(!m.active(1));
}

// Important 4: FT2 autovibrato peaks at +-depth period units, sine starts negative.
TEST(xm_autovibrato_depth_and_phase) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::FT2);
    s.freqMode = FreqMode::Linear;
    Instrument in;
    for (auto& x : in.sampleMap) x = 0;
    in.vibType = 0;
    in.vibDepth = 8;
    in.vibRate = 64;
    s.instruments.push_back(in);
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    p.setSong(&s);
    p.play();
    std::vector<int> d;
    for (int i = 0; i < 4; ++i) {
        p.tick();
        d.push_back(p.channel(0).avDelta);
    }
    CHECK(d == (std::vector<int>{-8, 0, 8, 0}));
}

// Important 5: FT2 arpeggio order follows the down-counting tick timer (base, y, x at speed 6).
TEST(xm_arpeggio_ft2_order) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::FT2);
    s.freqMode = FreqMode::Linear;
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0x37};
    p.setSong(&s);
    p.play();
    std::vector<int> v;
    for (int i = 0; i < 6; ++i) {
        p.tick();
        v.push_back(p.channel(0).outPeriod);
    }
    CHECK(v == (std::vector<int>{4608, 4608 - 7 * 64, 4608 - 3 * 64, 4608, 4608 - 7 * 64, 4608 - 3 * 64}));
}

// Important 6: tone porta never retriggers a finished one-shot sample.
TEST(tone_porta_does_not_retrigger_finished_sample) {
    Mixer m(48000);
    m.setRamp(false);
    Player p(m);
    Song s = testSong(4, 64);
    s.samples[0].length = 100;
    s.samples[0].data8.assign(100, 50);
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    s.patterns[0].at(1, 0) = {61, 0, 0, 3, 0x10};
    p.setSong(&s);
    p.play();
    std::vector<float> buf(20000);
    for (int i = 0; i < 6; ++i) {
        p.tick();
        m.mix(buf.data(), 2000);
    }
    CHECK(!m.active(0));
    p.tick();
    CHECK(!m.active(0));
    p.tick();
    CHECK_EQ(p.channel(0).period, 412);
}
