#include "player_util.h"
#include "test.h"

TEST(player_samples_per_tick) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(1, 0) = {0, 0, 0, 0xF, 0x20};
    s.patterns[0].at(2, 0) = {0, 0, 0, 0xF, 0xFF};
    s.initialSpeed = 1;
    p.setSong(&s);
    p.play();
    CHECK_EQ(p.samplesPerTick(), 960u);
    p.tick();
    p.tick();
    CHECK_EQ(p.samplesPerTick(), 3750u);
    p.tick();
    CHECK_EQ(p.samplesPerTick(), 470u);
}

TEST(player_trigger_mod_note) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    p.setSong(&s);
    p.play();
    p.tick();
    CHECK_EQ(p.channel(0).period, 428);
    CHECK_EQ(p.channel(0).outVolume, 64);
    CHECK_NEAR(p.periodToHz(428), 8287.14, 0.01);
    CHECK(m.active(0));
}

TEST(player_row_advance_speed) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.initialSpeed = 3;
    p.setSong(&s);
    p.play();
    for (int i = 0; i < 3; ++i) p.tick();
    CHECK_EQ(p.pos().row, 1);
    for (int i = 0; i < 3; ++i) p.tick();
    CHECK_EQ(p.pos().row, 2);
}

TEST(player_fxx_speed_bpm_stop) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0) = {0, 0, 0, 0xF, 0x03};
    s.patterns[0].at(1, 0) = {0, 0, 0, 0xF, 0x20};
    s.patterns[0].at(2, 0) = {0, 0, 0, 0xF, 0x7D};
    s.patterns[0].at(3, 0) = {0, 0, 0, 0xF, 0x00};
    p.setSong(&s);
    p.play();
    p.tick();
    CHECK_EQ(p.pos().speed, 3);
    for (int i = 0; i < 3; ++i) p.tick();
    CHECK_EQ(p.pos().bpm, 32);
    for (int i = 0; i < 3; ++i) p.tick();
    CHECK_EQ(p.pos().bpm, 125);
    for (int i = 0; i < 3; ++i) p.tick();
    CHECK(!p.playing());
}

TEST(player_bxx_dxx) {
    Mixer m(48000);
    Player p(m);
    {
        Song s = testSong(4, 64, Quirks::PT23, 3);
        s.order = {0, 1, 2};
        s.patterns[0].at(0, 0) = {0, 0, 0, 0xB, 0x02};
        p.setSong(&s);
        p.play();
        for (int i = 0; i < 6; ++i) p.tick();
        CHECK_EQ(p.pos().order, 2);
        CHECK_EQ(p.pos().row, 0);
    }
    {
        Song s = testSong(4, 64, Quirks::PT23, 3);
        s.order = {0, 1, 2};
        s.patterns[0].at(0, 0) = {0, 0, 0, 0xD, 0x10};
        p.setSong(&s);
        p.play();
        for (int i = 0; i < 6; ++i) p.tick();
        CHECK_EQ(p.pos().order, 1);
        CHECK_EQ(p.pos().row, 10);
    }
    {
        Song s = testSong(4, 64, Quirks::PT23, 3);
        s.order = {0, 1, 2};
        s.patterns[0].at(0, 0) = {0, 0, 0, 0xB, 0x01};
        s.patterns[0].at(0, 1) = {0, 0, 0, 0xD, 0x05};
        p.setSong(&s);
        p.play();
        for (int i = 0; i < 6; ++i) p.tick();
        CHECK_EQ(p.pos().order, 1);
        CHECK_EQ(p.pos().row, 5);
    }
}

TEST(player_e6_pattern_loop) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.initialSpeed = 1;
    s.patterns[0].at(0, 0) = {0, 0, 0, 0xE, 0x60};
    s.patterns[0].at(2, 0) = {0, 0, 0, 0xE, 0x62};
    p.setSong(&s);
    p.play();
    std::vector<int> want = {0, 1, 2, 0, 1, 2, 0, 1, 2, 3};
    CHECK(rowTrace(p, 10) == want);
}

TEST(player_ee_pattern_delay) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0) = {0, 0, 0, 0xE, 0xE2};
    p.setSong(&s);
    p.play();
    int ticks = 0;
    while (p.pos().row == 0 && ticks < 100) {
        p.tick();
        ++ticks;
    }
    CHECK_EQ(ticks, 18);
}

TEST(player_looped_once_on_end) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::PT23, 2);
    s.order = {0, 1};
    s.initialSpeed = 1;
    p.setSong(&s);
    p.play();
    for (int i = 0; i < 127; ++i) p.tick();
    CHECK(!p.loopedOnce());
    p.tick();
    CHECK(p.loopedOnce());
}

TEST(player_looped_once_on_backjump) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64, Quirks::PT23, 2);
    s.order = {0, 1};
    s.initialSpeed = 1;
    s.patterns[1].at(10, 0) = {0, 0, 0, 0xB, 0x00};
    p.setSong(&s);
    p.play();
    for (int i = 0; i < 64 + 10; ++i) p.tick();
    CHECK(!p.loopedOnce());
    p.tick();
    CHECK(p.loopedOnce());
    CHECK_EQ(p.pos().order, 0);
}

TEST(player_pattern_mode_loops) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 4, Quirks::PT23, 2);
    s.order = {0, 1};
    s.initialSpeed = 1;
    p.setSong(&s);
    p.playPattern(1);
    for (int i = 0; i < 4; ++i) p.tick();
    CHECK_EQ(p.pos().row, 0);
    CHECK_EQ(p.pos().pattern, 1);
    CHECK_EQ(p.pos().order, 0);
}

TEST(player_missing_sample_no_crash) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0) = {49, 31, 0, 0, 0};
    p.setSong(&s);
    p.play();
    p.tick();
    CHECK(!m.active(0));
}

TEST(player_mute) {
    Mixer m(48000);
    m.setRamp(false);
    Player p(m);
    Song s = testSong(4, 64);
    s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    p.setSong(&s);
    p.setMute(0, true);
    p.play();
    std::vector<float> buf(960 * 2);
    p.render(buf.data(), 960);
    float mx = 0;
    for (float v : buf) mx = v > mx ? v : (-v > mx ? -v : mx);
    CHECK_EQ(mx, 0.f);
    CHECK_EQ(p.channel(0).outVolume, 64);
}
