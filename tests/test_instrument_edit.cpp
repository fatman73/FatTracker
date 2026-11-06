// XM instrument editor operations (core/instrument_edit) and their undo through the Editor.
#include <cmath>
#include <cstring>

#include "build_mod.h"
#include "core/edit.h"
#include "core/instrument_edit.h"
#include "core/render.h"
#include "formats/formats.h"
#include "formats/writers.h"
#include "test.h"

namespace {
Envelope env(std::initializer_list<EnvPoint> pts) {
    Envelope e;
    for (const EnvPoint& p : pts) e.pts[e.count++] = p;
    return e;
}

bool increasing(const Envelope& e) {
    if (e.count > 0 && e.pts[0].tick != 0) return false;
    for (int i = 1; i < e.count; ++i)
        if (e.pts[i].tick <= e.pts[i - 1].tick) return false;
    return true;
}

std::vector<int8_t> wave(int n, int period, int amp) {
    std::vector<int8_t> d((size_t)n);
    for (int i = 0; i < n; ++i) d[(size_t)i] = (int8_t)std::lround(amp * std::sin(i * 6.283185307 / period));
    return d;
}

// A 4 channel MOD that exercises sample changes, an instrument number without a note before the first
// note of a channel, effects with memory, a sample offset and volume slides.
Song testMod() {
    std::vector<ModSample> smp(3);
    smp[0] = {512, 0, 64, 0, 512, wave(1024, 32, 100)};
    smp[1] = {300, 3, 40, 100, 200, wave(600, 50, 90)};
    smp[2] = {800, -2, 50, 0, 1, wave(1600, 20, 110)};  // no loop
    std::vector<ModCell> p0(64 * 4, ModCell{-1, 0, 0, 0}), p1(64 * 4, ModCell{-1, 0, 0, 0});
    auto at = [](std::vector<ModCell>& p, int row, int ch) -> ModCell& { return p[(size_t)row * 4 + ch]; };
    at(p0, 0, 0) = {12, 1, 0, 0};
    at(p0, 4, 0) = {14, 0, 4, 0x46};    // note without sample number, vibrato
    at(p0, 8, 0) = {16, 2, 0, 0};       // sample change
    at(p0, 12, 0) = {19, 0, 3, 0x08};   // tone porta
    at(p0, 0, 1) = {-1, 3, 0, 0};       // sample number alone before any note
    at(p0, 2, 1) = {24, 0, 0, 0};       // plays sample 3 at its volume
    at(p0, 6, 1) = {24, 2, 9, 0x01};    // sample offset
    at(p0, 10, 1) = {-1, 0, 0xA, 0x02};
    at(p0, 16, 1) = {-1, 1, 0, 0};      // sample number alone: volume reset
    at(p0, 20, 1) = {7, 0, 0, 0};
    at(p0, 0, 2) = {5, 2, 0xC, 0x20};
    at(p0, 3, 2) = {5, 0, 0xE, 0x93};   // retrigger
    at(p0, 30, 2) = {9, 3, 1, 0x02};
    at(p0, 0, 3) = {20, 1, 0, 0};
    at(p0, 1, 3) = {-1, 0, 0, 0x37};    // arpeggio
    at(p1, 0, 0) = {10, 3, 0, 0};
    at(p1, 0, 3) = {22, 2, 0xE, 0x52};  // finetune for this note
    at(p1, 8, 3) = {-1, 0, 0xB, 0x00};  // back to the start: the song loops
    auto bytes = buildMod("M.K.", 4, {0, 1}, {p0, p1}, smp);
    auto r = loadModule(bytes.data(), bytes.size());
    CHECK(r.ok());
    return r.ok() ? r.value() : Song{};
}

std::vector<float> render(const Song& s) {
    std::vector<float> out;
    renderSong(s, 44100, Interp::Linear, true, 30.f, out);
    return out;
}

const Sample* sampleOf(const Song& s, const Instrument& in, int key) {
    const uint16_t g = in.sampleMap[key];
    return g < s.samples.size() ? &s.samples[g] : nullptr;
}
}  // namespace

// ---------------------------------------------------------------- envelopes

TEST(inst_env_add_points_ft2_rules) {
    Envelope e;
    CHECK_EQ(envAddPoint(e, 10, 30), 0);  // the first point always sits at tick 0
    CHECK_EQ(e.count, 1);
    CHECK_EQ(e.pts[0].tick, 0);
    CHECK_EQ(e.pts[0].value, 30);
    CHECK_EQ(envAddPoint(e, 20, 99), 1);  // values are clamped to 64
    CHECK_EQ(e.pts[1].value, 64);
    CHECK_EQ(envAddPoint(e, 10, 5), 1);   // inserted between its neighbours
    CHECK_EQ(e.pts[1].tick, 10);
    CHECK_EQ(e.pts[2].tick, 20);
    CHECK_EQ(envAddPoint(e, 20, 5), -1);  // tick already taken
    CHECK_EQ(envAddPoint(e, 0, 5), -1);
    CHECK_EQ(envAddPoint(e, 999, 7), 3);  // clamped to the end of the editor
    CHECK_EQ(e.pts[3].tick, kEnvMaxTick);
    for (int t = 30; t <= 100; t += 10) CHECK(envAddPoint(e, t, 1) >= 0);
    CHECK_EQ(e.count, 12);
    CHECK_EQ(envAddPoint(e, 301, 1), -1);  // 12 points maximum
    CHECK(increasing(e));
    CHECK(envValid(e));
}

TEST(inst_env_add_keeps_sustain_and_loop_on_their_points) {
    Envelope e = env({{0, 64}, {10, 50}, {20, 40}, {30, 30}});
    e.sustain = 2;
    e.loopStart = 1;
    e.loopEnd = 3;
    e.flags = ENV_ON | ENV_SUSTAIN | ENV_LOOP;
    CHECK_EQ(envAddPoint(e, 5, 60), 1);  // before all three marked points
    CHECK_EQ(e.sustain, 3);
    CHECK_EQ(e.loopStart, 2);
    CHECK_EQ(e.loopEnd, 4);
    CHECK_EQ(e.pts[e.sustain].tick, 20);
    CHECK_EQ(envAddPoint(e, 25, 60), 4);  // between sustain and loop end
    CHECK_EQ(e.sustain, 3);
    CHECK_EQ(e.loopEnd, 5);
    CHECK_EQ(e.pts[e.loopEnd].tick, 30);
}

TEST(inst_env_move_point_clamped) {
    Envelope e = env({{0, 64}, {10, 50}, {20, 40}});
    envMovePoint(e, 0, 7, 70);  // the first point stays at tick 0
    CHECK_EQ(e.pts[0].tick, 0);
    CHECK_EQ(e.pts[0].value, 64);
    envMovePoint(e, 1, 25, -3);  // between the neighbours
    CHECK_EQ(e.pts[1].tick, 19);
    CHECK_EQ(e.pts[1].value, 0);
    envMovePoint(e, 1, 0, 12);
    CHECK_EQ(e.pts[1].tick, 1);
    CHECK_EQ(e.pts[1].value, 12);
    envMovePoint(e, 2, 5000, 33);  // the last point ends at the editor's end
    CHECK_EQ(e.pts[2].tick, kEnvMaxTick);
    envMovePoint(e, 7, 30, 30);  // no such point: nothing happens
    CHECK(increasing(e));
}

TEST(inst_env_delete_fixes_sustain_and_loop) {
    Envelope e = env({{0, 64}, {10, 50}, {20, 40}, {30, 30}, {40, 20}});
    e.sustain = 3;
    e.loopStart = 1;
    e.loopEnd = 4;
    e.flags = ENV_ON | ENV_SUSTAIN | ENV_LOOP;
    CHECK(envDeletePoint(e, 2));
    CHECK_EQ(e.count, 4);
    CHECK_EQ(e.pts[2].tick, 30);
    CHECK_EQ(e.sustain, 2);  // still on the point at tick 30
    CHECK_EQ(e.loopStart, 1);
    CHECK_EQ(e.loopEnd, 3);
    CHECK(envDeletePoint(e, 3));  // the loop end point: the loop ends on the new last point
    CHECK_EQ(e.loopEnd, 2);
    CHECK(e.sustain < e.count);
    CHECK(envDeletePoint(e, 0));  // the next point becomes the first, at tick 0
    CHECK_EQ(e.count, 2);
    CHECK_EQ(e.pts[0].tick, 0);
    CHECK_EQ(e.pts[0].value, 50);
    CHECK(e.loopStart <= e.loopEnd);
    CHECK(e.loopEnd < e.count);
    CHECK(!envDeletePoint(e, 1));  // two points minimum, as in FT2
    CHECK(!envDeletePoint(e, 5));
    CHECK(envValid(e));
}

TEST(inst_env_sustain_and_loop_setters) {
    Envelope e = env({{0, 64}, {10, 50}, {20, 40}, {30, 30}});
    envSetSustain(e, 2);
    CHECK_EQ(e.sustain, 2);
    envSetSustain(e, 9);
    CHECK_EQ(e.sustain, 3);
    envSetSustain(e, -1);
    CHECK_EQ(e.sustain, 0);
    envSetLoopEnd(e, 2);
    envSetLoopStart(e, 3);  // never after the loop end
    CHECK_EQ(e.loopStart, 2);
    envSetLoopEnd(e, 0);    // never before the loop start
    CHECK_EQ(e.loopEnd, 2);
    envSetLoopEnd(e, 99);
    CHECK_EQ(e.loopEnd, 3);
    envSetFlag(e, ENV_LOOP, true, false);
    CHECK(e.flags & ENV_LOOP);
    envSetFlag(e, ENV_LOOP, false, false);
    CHECK(!(e.flags & ENV_LOOP));
}

TEST(inst_env_on_without_points_gets_a_shape) {
    Envelope e;
    envSetFlag(e, ENV_ON, true, false);
    CHECK(e.flags & ENV_ON);
    CHECK(e.count >= 2);
    CHECK(envValid(e));
    Envelope p;
    envSetFlag(p, ENV_ON, true, true);
    CHECK(p.count >= 2);
    CHECK(envValid(p));
}

TEST(inst_env_fix_repairs_anything) {
    Envelope e;
    e.count = 30;
    for (int i = 0; i < 12; ++i) e.pts[i] = {(uint16_t)(100 - i * 5), (uint8_t)(i * 20)};
    e.sustain = 40;
    e.loopStart = 9;
    e.loopEnd = 3;
    e.flags = 0xFF;
    envFix(e);
    CHECK(envValid(e));
    CHECK(e.count <= 12);
    CHECK(increasing(e));
}

// ---------------------------------------------------------------- key map and parameters

TEST(inst_key_map_ranges) {
    Instrument in;
    for (auto& m : in.sampleMap) m = 0;
    CHECK(instSetKeys(in, 50, 40, 3));  // any order
    for (int k = 0; k < 96; ++k) CHECK_EQ(in.sampleMap[k], (k >= 40 && k <= 50) ? 3 : 0);
    CHECK(instSetKeys(in, -5, 2, kKeyNoSample));  // clamped to the keyboard
    CHECK_EQ(in.sampleMap[0], kKeyNoSample);
    CHECK_EQ(in.sampleMap[2], kKeyNoSample);
    CHECK_EQ(in.sampleMap[3], 0);
    CHECK(instSetKeys(in, 95, 200, 7));
    CHECK_EQ(in.sampleMap[95], 7);
    CHECK_EQ(instSampleCount(in), 3);  // 0, 3, 7 (no-sample keys do not count)
}

TEST(inst_key_map_sixteen_samples_maximum) {
    Instrument in;
    for (int k = 0; k < 96; ++k) in.sampleMap[k] = (uint16_t)(k < 16 ? k : 0);
    CHECK_EQ(instSampleCount(in), 16);
    Instrument before = in;
    CHECK(!instSetKeys(in, 20, 30, 16));  // a 17th sample does not fit in an XM instrument
    CHECK(std::memcmp(before.sampleMap, in.sampleMap, sizeof in.sampleMap) == 0);
    CHECK(instSetKeys(in, 15, 15, 16));  // replaces sample 15 (its only key): still 16
    CHECK_EQ(in.sampleMap[15], 16);
}

TEST(inst_fadeout_and_vibrato_ranges) {
    Instrument in;
    instSetFadeout(in, 5000);
    CHECK_EQ(in.fadeout, 4095);
    instSetFadeout(in, -1);
    CHECK_EQ(in.fadeout, 0);
    instSetVibrato(in, 5, 300, 20, 99);
    CHECK_EQ(in.vibType, 3);
    CHECK_EQ(in.vibSweep, 255);
    CHECK_EQ(in.vibDepth, 15);
    CHECK_EQ(in.vibRate, 63);
    instSetVibrato(in, -1, -1, -1, -1);
    CHECK_EQ(in.vibType, 0);
    CHECK_EQ(in.vibSweep, 0);
    CHECK_EQ(in.vibDepth, 0);
    CHECK_EQ(in.vibRate, 0);
    instSetName(in, "a very long instrument name that does not fit");
    CHECK_EQ(std::strlen(in.name), 22u);
}

// ---------------------------------------------------------------- songs

TEST(inst_convert_mod_plays_identically) {
    Song s = testMod();
    CHECK_EQ(s.samples.size(), 31u);
    CHECK(s.instruments.empty());
    const std::vector<float> before = render(s);
    CHECK(before.size() > 44100);
    double energy = 0;
    for (float v : before) energy += std::fabs(v);
    CHECK(energy > 100);  // the song really plays
    CHECK(convertToInstruments(s));
    CHECK_EQ(s.instruments.size(), s.samples.size());
    if (s.instruments.empty()) return;
    for (int i = 0; i < (int)s.instruments.size(); ++i)
        for (int k = 0; k < 96; ++k) CHECK_EQ(s.instruments[i].sampleMap[k], i);
    CHECK(!(s.instruments[0].volEnv.flags & ENV_ON));
    const std::vector<float> after = render(s);
    CHECK_EQ(after.size(), before.size());
    size_t diff = 0;
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
        if (before[i] != after[i]) ++diff;
    CHECK_EQ(diff, 0u);
    CHECK(!convertToInstruments(s));  // once only
}

TEST(inst_envelope_is_heard_in_converted_mod) {
    Song s = testMod();
    CHECK(convertToInstruments(s));
    const std::vector<float> open = render(s);
    for (Instrument& in : s.instruments) {  // every instrument silent from tick 0
        in.volEnv = env({{0, 0}, {10, 0}});
        in.volEnv.flags = ENV_ON;
    }
    const std::vector<float> shut = render(s);
    double e = 0;
    for (float v : shut) e += std::fabs(v);
    CHECK(e < 1.0);
    CHECK_EQ(open.size(), shut.size());
}

TEST(inst_convert_refused) {
    Song empty = makeEmptySong(4);
    CHECK(!convertToInstruments(empty));  // no samples
    Song s = testMod();
    CHECK(convertToInstruments(s));
    CHECK(!convertToInstruments(s));
}

TEST(inst_add_instrument) {
    Song s = testMod();
    CHECK_EQ(addInstrument(s, 1), -1);  // a MOD style song must be converted first
    CHECK(s.instruments.empty());
    CHECK(convertToInstruments(s));
    const int n = (int)s.instruments.size();
    CHECK_EQ(addInstrument(s, 1), n);
    if ((int)s.instruments.size() <= n) return;
    CHECK_EQ((int)s.instruments.size(), n + 1);
    for (int k = 0; k < 96; ++k) CHECK_EQ(s.instruments[n].sampleMap[k], 1);
    CHECK(envValid(s.instruments[n].volEnv));
    CHECK(s.instruments[n].volEnv.count >= 2);  // ready to edit, switched off
    CHECK(!(s.instruments[n].volEnv.flags & ENV_ON));
    for (int i = n + 1; i < kMaxInstruments; ++i) CHECK_EQ(addInstrument(s, 0), i);
    CHECK_EQ(addInstrument(s, 0), -1);  // 128 instruments maximum
    Song e = makeEmptySong(4);  // no samples: an instrument that plays nothing yet
    CHECK_EQ(addInstrument(e, -1), 0);
    if (e.instruments.empty()) return;
    CHECK_EQ(e.instruments[0].sampleMap[48], kKeyNoSample);
}

TEST(inst_undo_through_editor) {
    Song s = testMod();
    Editor ed;
    ed.recordInstruments(s);
    CHECK(convertToInstruments(s));
    if (s.instruments.empty()) return;
    CHECK(ed.dirty());
    ed.recordInstruments(s);
    envSetFlag(s.instruments[0].volEnv, ENV_ON, true, false);
    instSetFadeout(s.instruments[0], 300);
    CHECK(ed.undo(s));
    CHECK(!(s.instruments[0].volEnv.flags & ENV_ON));
    CHECK_EQ(s.instruments[0].fadeout, 0);
    CHECK(ed.undo(s));
    CHECK(s.instruments.empty());  // back to a MOD style song
    CHECK(ed.redo(s));
    CHECK_EQ(s.instruments.size(), s.samples.size());
    CHECK(ed.redo(s));
    if (s.instruments.empty()) return;
    CHECK_EQ(s.instruments[0].fadeout, 300);
    // pattern edits and instrument edits share one history
    ed.setNote(s, 0, 5, 0, 49, 1);
    CHECK(ed.undo(s));
    CHECK_EQ(s.patterns[0].at(5, 0).note, 0);
    CHECK_EQ(s.instruments[0].fadeout, 300);
}

TEST(inst_xm_roundtrip_keeps_edited_instrument) {
    Song s = testMod();
    CHECK(convertToInstruments(s));
    if (s.instruments.empty()) return;
    Instrument& in = s.instruments[0];
    instSetName(in, "edited lead");
    in.volEnv = env({{0, 64}});
    CHECK(envAddPoint(in.volEnv, 12, 40) == 1);
    CHECK(envAddPoint(in.volEnv, 40, 10) == 2);
    CHECK(envAddPoint(in.volEnv, 90, 0) == 3);
    envSetSustain(in.volEnv, 1);
    envSetLoopEnd(in.volEnv, 2);
    envSetLoopStart(in.volEnv, 1);
    envSetFlag(in.volEnv, ENV_ON, true, false);
    envSetFlag(in.volEnv, ENV_SUSTAIN, true, false);
    envSetFlag(in.volEnv, ENV_LOOP, true, false);
    envSetFlag(in.panEnv, ENV_ON, true, true);
    envMovePoint(in.panEnv, 1, 15, 60);
    instSetFadeout(in, 0x234);
    instSetVibrato(in, 2, 30, 7, 20);
    CHECK(instSetKeys(in, 60, 95, 2));  // upper keys play the third sample
    const Instrument want = in;
    auto bytes = saveXm(s);
    auto r = loadModule(bytes.data(), bytes.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& t = r.value();
    CHECK_EQ(t.instruments.size(), s.instruments.size());
    const Instrument& got = t.instruments[0];
    CHECK(std::strcmp(got.name, "edited lead") == 0);
    for (const auto& [a, b] : {std::pair{&want.volEnv, &got.volEnv}, std::pair{&want.panEnv, &got.panEnv}}) {
        CHECK_EQ(b->count, a->count);
        CHECK_EQ(b->flags, a->flags);
        CHECK_EQ(b->sustain, a->sustain);
        CHECK_EQ(b->loopStart, a->loopStart);
        CHECK_EQ(b->loopEnd, a->loopEnd);
        for (int i = 0; i < a->count; ++i) {
            CHECK_EQ(b->pts[i].tick, a->pts[i].tick);
            CHECK_EQ(b->pts[i].value, a->pts[i].value);
        }
    }
    CHECK_EQ(got.fadeout, 0x234);
    CHECK_EQ(got.vibType, 2);
    CHECK_EQ(got.vibSweep, 30);
    CHECK_EQ(got.vibDepth, 7);
    CHECK_EQ(got.vibRate, 20);
    for (int k : {0, 59, 60, 95}) {  // the key map plays the same sample data
        const Sample* a = sampleOf(s, want, k);
        const Sample* b = sampleOf(t, got, k);
        CHECK(a && b);
        if (a && b) CHECK(a->data8 == b->data8);
    }
}
