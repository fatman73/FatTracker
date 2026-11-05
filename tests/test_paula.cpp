// Stage 3: Paula emulation (Pure mode) - mixer path, analog filters, LED effect.
#include <cmath>
#include <vector>

#include "core/mixer.h"
#include "core/paula.h"
#include "player_util.h"
#include "test.h"

namespace {
Sample constant8(int8_t v, uint32_t len = 48000) {
    Sample s;
    s.length = len;
    s.data8.assign(len, v);
    s.loop = LoopType::Forward;
    s.loopStart = 0;
    s.loopLength = len;
    return s;
}

// RMS of the left channel of a sine through the Paula filter (after settling).
double filteredRms(PaulaModel model, bool led, double hz) {
    PaulaFilter f;
    f.setRate(48000);
    f.setModel(model);
    f.setLed(led);
    std::vector<float> buf(2 * 48000);
    for (int i = 0; i < 48000; ++i) buf[2 * i] = buf[2 * i + 1] = (float)std::sin(2 * 3.14159265358979 * hz * i / 48000.0);
    f.process(buf.data(), 48000);
    double acc = 0;
    for (int i = 24000; i < 48000; ++i) acc += buf[2 * i] * buf[2 * i];
    return std::sqrt(acc / 24000) * std::sqrt(2.0);  // 1.0 = unchanged amplitude
}

void paulaMixer(Mixer& m, PaulaModel model = PaulaModel::A1200) {
    m.setNumChannels(4);
    m.setPaula(model);
}
}  // namespace

TEST(paula_hard_pan_lrrl) {
    Mixer m(48000);
    paulaMixer(m);
    Sample s = constant8(100);
    for (int ch = 0; ch < 8; ++ch) {
        m.reset();
        m.setVolume(ch, 1.f, 0.5f);  // the song asks for centre, Paula ignores it
        m.trigger(ch, &s, 0);
        m.setFrequency(ch, 8000);
        std::vector<float> out(2 * 4800);
        m.mix(out.data(), 4800);
        const bool left = ch % 4 == 0 || ch % 4 == 3;
        const float l = std::fabs(out[2 * 400]), r = std::fabs(out[2 * 400 + 1]);  // before the 5 Hz high-pass drains it
        CHECK(left ? (l > 0.05f && r < 1e-6f) : (r > 0.05f && l < 1e-6f));
    }
}

TEST(paula_8bit_dac) {
    Sample s;
    s.length = 1000;
    s.is16 = true;
    s.data16.assign(1000, 0x7F);  // below one 8-bit step
    s.loop = LoopType::Forward;
    s.loopLength = 1000;
    Mixer pure(48000), normal(48000);
    paulaMixer(pure);
    normal.setNumChannels(4);
    normal.setRamp(false);
    for (Mixer* m : {&pure, &normal}) {
        m->setVolume(0, 1.f, 0.f);
        m->trigger(0, &s, 0);
        m->setFrequency(0, 48000);
    }
    std::vector<float> a(2 * 2000), b(2 * 2000);
    pure.mix(a.data(), 2000);
    normal.mix(b.data(), 2000);
    CHECK(std::fabs(a[2 * 1999]) < 1e-6f);
    CHECK(std::fabs(b[2 * 1999]) > 1e-5f);
}

TEST(paula_zoh_holds_samples) {
    // half-rate playback: each source sample is held for two output samples
    Mixer m(48000);
    paulaMixer(m);
    Sample s;
    s.length = 8;
    s.data8 = {0, 64, 0, 64, 0, 64, 0, 64};
    m.setVolume(0, 1.f, 0.f);
    m.trigger(0, &s, 0);
    m.setFrequency(0, 24000);
    std::vector<float> out(2 * 8);
    m.mix(out.data(), 8);
    CHECK(std::fabs(out[0]) < 1e-6f && std::fabs(out[2]) < 1e-6f);
    CHECK(out[4] > 0.01f);
    CHECK_NEAR(out[4], out[6], 1e-3);  // only the 5 Hz high-pass moves it
}

TEST(paula_a500_attenuates_highs) {
    CHECK(filteredRms(PaulaModel::A500, false, 100) > 0.95);
    CHECK(filteredRms(PaulaModel::A500, false, 15000) < 0.4);
}

TEST(paula_led_attenuates_more) {
    const double off = filteredRms(PaulaModel::A500, false, 6000), on = filteredRms(PaulaModel::A500, true, 6000);
    CHECK(on < off * 0.5);
    CHECK(filteredRms(PaulaModel::A500, true, 100) > 0.95);
}

TEST(paula_a1200_keeps_highs) {
    CHECK(filteredRms(PaulaModel::A1200, false, 15000) > 0.95);
    CHECK(filteredRms(PaulaModel::A1200, false, 2) < 0.6);  // 5.2 Hz high-pass removes DC drift
}

TEST(paula_off_is_unchanged) {
    CHECK(filteredRms(PaulaModel::None, true, 15000) > 0.99);
}

// ---- LED via E0x (Task 2)

TEST(mod_e00_turns_led_on_e01_off) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0).fx = 0xE;
    s.patterns[0].at(0, 0).param = 0x00;
    s.patterns[0].at(1, 0).fx = 0xE;
    s.patterns[0].at(1, 0).param = 0x01;
    s.initialSpeed = 1;
    p.setSong(&s);
    p.play();
    CHECK(!m.led());
    p.tick();  // row 0
    CHECK(m.led());
    p.tick();  // row 1
    CHECK(!m.led());
}

TEST(xm_e0x_ignored) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::FT2);
    s.patterns[0].at(0, 0).fx = 0xE;
    s.patterns[0].at(0, 0).param = 0x00;
    p.setSong(&s);
    p.play();
    p.tick();
    CHECK(!m.led());
}

TEST(play_from_start_turns_led_off) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    p.setSong(&s);
    m.setLed(true);
    p.play();
    CHECK(!m.led());
}

TEST(new_song_turns_led_off) {
    Mixer m(48000);
    Player p(m);
    Song a = testSong(4, 64), b = testSong(4, 64);
    p.setSong(&a);
    m.setLed(true);
    p.setSong(&b);
    CHECK(!m.led());
}
