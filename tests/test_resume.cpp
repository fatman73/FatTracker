// STOP keeps the player state: PLAY resumes where it stopped, PLAY from elsewhere fast-forwards the
// song to that position (speed, BPM, global volume, effect memory), pattern play uses the tempo in effect.
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "audio/engine.h"
#include "player_util.h"
#include "test.h"

namespace {
// three 64-row patterns in order 0 1 2; `fx`/`param` at row 0 of order 0, channel 0
std::unique_ptr<Song> tempoSong(uint8_t fx, uint8_t param) {
    auto s = std::make_unique<Song>(testSong(4, 64, Quirks::PT23, 3));
    s->order = {0, 1, 2};
    s->patterns[0].at(0, 0) = {49, 1, 0, fx, param};
    return s;
}

void renderFrames(Engine& e, uint32_t frames) {
    std::vector<float> buf(960 * 2);
    while (frames) {
        const uint32_t n = frames < 960 ? frames : 960;
        e.render(buf.data(), n);
        frames -= n;
    }
}

Telemetry tel(const Engine& e) {
    Telemetry t;
    e.snapshot(t);
    return t;
}

float peak(Engine& e, uint32_t frames) {
    std::vector<float> buf(frames * 2);
    e.render(buf.data(), frames);
    float p = 0;
    for (float v : buf) p = std::fabs(v) > p ? std::fabs(v) : p;
    return p;
}
}  // namespace

TEST(resume_after_stop_keeps_speed_and_position) {
    Engine e;
    e.setSong(tempoSong(0xF, 3));
    e.play(0);
    renderFrames(e, 74 * 3 * 960);  // speed 3 at 125 BPM: 960 frames a tick, 74 rows
    Telemetry t = tel(e);
    CHECK_EQ(t.order, 1);
    CHECK_EQ(t.speed, 3);
    const uint16_t row = t.row;
    CHECK(row > 5);
    e.stop();
    CHECK(!tel(e).playing);
    e.play(1);  // the UI asks for the order it stopped at
    t = tel(e);
    CHECK(t.playing);
    CHECK_EQ(t.speed, 3);
    CHECK_EQ(t.order, 1);
    CHECK_EQ(t.row, row);
    renderFrames(e, 3 * 3 * 960);
    t = tel(e);
    CHECK_EQ(t.speed, 3);
    CHECK_EQ(t.order, 1);
    CHECK(t.row >= row + 2);
}

TEST(resume_play_elsewhere_fast_forwards_speed) {
    Engine e;
    e.setSong(tempoSong(0xF, 3));
    e.play(2);
    Telemetry t = tel(e);
    CHECK(t.playing);
    CHECK_EQ(t.order, 2);
    CHECK_EQ(t.row, 0);
    CHECK_EQ(t.speed, 3);
    // stopped at order 0, the user moves to order 1: fast-forward there
    Engine f;
    f.setSong(tempoSong(0xF, 3));
    f.play(0);
    renderFrames(f, 10 * 3 * 960);
    f.stop();
    f.play(1);
    t = tel(f);
    CHECK_EQ(t.order, 1);
    CHECK_EQ(t.row, 0);
    CHECK_EQ(t.speed, 3);
}

TEST(resume_play_elsewhere_fast_forwards_bpm) {
    Engine e;
    e.setSong(tempoSong(0xF, 0x90));
    e.play(2);
    Telemetry t = tel(e);
    CHECK_EQ(t.order, 2);
    CHECK_EQ(t.bpm, 0x90);
    CHECK_EQ(t.speed, 6);
    e.stop();
    e.play(2);  // resume
    CHECK_EQ(tel(e).bpm, 0x90);
}

TEST(resume_fast_forward_keeps_effect_memory_and_global_volume) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::FT2, 3);
    s.order = {0, 1, 2};
    s.patterns[0].at(0, 0) = {49, 1, 0, 0xA, 0x40};  // volume slide up 4: remembered for A00
    s.patterns[0].at(1, 1) = {0, 0, 0, 16, 0x20};    // FT2 Gxx global volume 32
    p.setSong(&s);
    p.playFrom(2);
    CHECK(p.playing());
    CHECK_EQ(p.pos().order, 2);
    CHECK_EQ(p.pos().row, 0);
    CHECK_EQ(p.channel(0).memA, 0x40);
    CHECK_EQ(p.globalVolume(), 32);
    std::vector<float> buf(2 * 2000);  // starts silent (stopped voices ramp out from zero)
    m.mix(buf.data(), 2000);
    for (float v : buf) CHECK(std::fabs(v) < 1e-6f);
    for (int v = 0; v < 4; ++v) CHECK(!m.active(v));
}

TEST(resume_fast_forward_unreached_target_keeps_last_tempo) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::PT23, 3);
    s.order = {0, 1, 2};
    s.patterns[0].at(0, 0) = {0, 0, 0, 0xF, 4};
    s.patterns[1].at(0, 0) = {0, 0, 0, 0xB, 0};  // order 1 jumps back: order 2 is never reached
    p.setSong(&s);
    p.playFrom(2);
    CHECK(p.playing());
    CHECK_EQ(p.pos().order, 2);
    CHECK_EQ(p.pos().row, 0);
    CHECK_EQ(p.pos().speed, 4);
}

TEST(resume_pattern_play_uses_tempo_in_effect) {
    Engine e;
    e.setSong(tempoSong(0xF, 3));
    e.playPattern(1);  // nothing played yet: the song's initial speed
    CHECK_EQ(tel(e).speed, 6);
    e.stop();
    e.play(0);
    renderFrames(e, 4 * 3 * 960);
    e.stop();
    e.playPattern(1);
    Telemetry t = tel(e);
    CHECK(t.playing);
    CHECK_EQ(t.pattern, 1);
    CHECK_EQ(t.speed, 3);
}

TEST(resume_chip_song_after_stop) {
    // PSG: channel A tone at full volume for 25 frames (0.5 s), then volume 0 for 75 frames
    std::vector<uint8_t> f(16, 0);
    std::memcpy(f.data(), "PSG\x1A", 4);
    const uint8_t setup[] = {0, 0x00, 1, 0x01, 7, 0x3E, 8, 15};
    f.insert(f.end(), setup, setup + sizeof setup);
    for (int i = 0; i < 25; ++i) f.push_back(0xFF);
    f.push_back(8);
    f.push_back(0);
    for (int i = 0; i < 75; ++i) f.push_back(0xFF);
    f.push_back(0xFD);
    Engine e;
    CHECK(e.loadAy(f.data(), f.size(), "TONE", false));
    e.play(0);
    CHECK(peak(e, 4800) > 0.05f);  // the tone
    renderFrames(e, 48000);         // past it
    CHECK(peak(e, 4800) < 0.01f);
    e.stop();
    CHECK(!tel(e).playing);
    e.play(0);
    CHECK(tel(e).playing);
    CHECK(peak(e, 4800) < 0.01f);  // resumed in the silent part, not restarted at the tone
    // a new sub-song selection or song still starts from the beginning
    e.stop();
    CHECK(e.loadAy(f.data(), f.size(), "TONE", false));
    e.play(0);
    CHECK(peak(e, 4800) > 0.05f);
}
