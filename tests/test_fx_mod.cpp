#include "player_util.h"
#include "test.h"

namespace {
struct Rig {
    Mixer m{48000};
    Player p{m};
    Song s = testSong(4, 64);
    void start() {
        p.setSong(&s);
        p.play();
    }
};

template <class F>
std::vector<int> trace(Player& p, int n, F get) {
    std::vector<int> v;
    for (int i = 0; i < n; ++i) {
        p.tick();
        v.push_back(get(p.channel(0)));
    }
    return v;
}
int per(const ChannelState& c) { return c.period; }
int outPer(const ChannelState& c) { return c.outPeriod; }
int vol(const ChannelState& c) { return c.volume; }
int outVol(const ChannelState& c) { return c.outVolume; }
}  // namespace

TEST(mod_1xx_porta_up) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 1, 3};
    r.start();
    CHECK(trace(r.p, 6, per) == (std::vector<int>{428, 425, 422, 419, 416, 413}));
}

TEST(mod_1xx_clamp) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {71, 1, 0, 1, 5};
    r.start();
    CHECK(trace(r.p, 6, per) == (std::vector<int>{120, 115, 113, 113, 113, 113}));
}

TEST(mod_2xx_porta_down) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 2, 3};
    r.start();
    CHECK(trace(r.p, 6, per) == (std::vector<int>{428, 431, 434, 437, 440, 443}));
}

TEST(mod_3xx_tone_porta) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.s.patterns[0].at(1, 0) = {61, 0, 0, 3, 0x10};
    r.start();
    std::vector<float> buf(2000);
    for (int i = 0; i < 6; ++i) {
        r.p.tick();
        r.m.mix(buf.data(), 100);
    }
    double before = r.m.position(0);
    CHECK(trace(r.p, 6, per) == (std::vector<int>{428, 412, 396, 380, 364, 348}));
    CHECK(r.m.position(0) >= before);  // not retriggered
}

TEST(mod_3xx_reaches_target) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.s.patterns[0].at(1, 0) = {61, 0, 0, 3, 0xFF};
    r.start();
    for (int i = 0; i < 6; ++i) r.p.tick();
    CHECK(trace(r.p, 3, per) == (std::vector<int>{428, 214, 214}));
}

TEST(mod_arpeggio) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0x37};
    r.start();
    CHECK(trace(r.p, 6, outPer) == (std::vector<int>{428, 360, 285, 428, 360, 285}));
    CHECK_EQ(r.p.channel(0).period, 428);
}

TEST(mod_vibrato) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 4, 0x4F};
    r.start();
    CHECK(trace(r.p, 4, outPer) == (std::vector<int>{428, 428, 439, 449}));
    CHECK_EQ(r.p.channel(0).period, 428);
}

TEST(mod_axy_vol_slide) {
    {
        Rig r;
        r.s.samples[0].volume = 32;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xA, 0x04};
        r.start();
        CHECK(trace(r.p, 6, vol) == (std::vector<int>{32, 28, 24, 20, 16, 12}));
    }
    {
        Rig r;
        r.s.samples[0].volume = 32;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xA, 0x40};
        r.start();
        CHECK(trace(r.p, 6, vol) == (std::vector<int>{32, 36, 40, 44, 48, 52}));
    }
    {
        Rig r;
        r.s.samples[0].volume = 32;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xA, 0x0F};
        r.start();
        CHECK(trace(r.p, 6, vol) == (std::vector<int>{32, 17, 2, 0, 0, 0}));
    }
}

TEST(mod_cxx_clamp) {
    Rig r;
    r.s.samples[0].volume = 32;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xC, 0x50};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).volume, 64);
}

TEST(mod_5xy_6xy) {
    {
        Rig r;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
        r.s.patterns[0].at(1, 0) = {61, 0, 0, 3, 0x10};
        r.s.patterns[0].at(2, 0) = {0, 0, 0, 5, 0x04};
        r.start();
        for (int i = 0; i < 12; ++i) r.p.tick();
        CHECK_EQ(r.p.channel(0).period, 348);
        r.p.tick();
        r.p.tick();
        CHECK_EQ(r.p.channel(0).period, 332);
        CHECK_EQ(r.p.channel(0).volume, 60);
    }
    {
        Rig r;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 4, 0x4F};
        r.s.patterns[0].at(1, 0) = {0, 0, 0, 6, 0x04};
        r.start();
        for (int i = 0; i < 6; ++i) r.p.tick();
        r.p.tick();
        CHECK_EQ(r.p.channel(0).outPeriod, 428);
        r.p.tick();
        CHECK_EQ(r.p.channel(0).outPeriod, 455);
        CHECK_EQ(r.p.channel(0).volume, 60);
    }
}

TEST(mod_7xy_tremolo) {
    Rig r;
    r.s.samples[0].volume = 32;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 7, 0x4F};
    r.start();
    CHECK(trace(r.p, 3, outVol) == (std::vector<int>{32, 32, 54}));
    CHECK_EQ(r.p.channel(0).volume, 32);
}

TEST(mod_9xx_offset) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 9, 0x02};
    r.start();
    r.p.tick();
    CHECK_NEAR(r.m.position(0), 512.0, 1e-9);
}

TEST(mod_9xx_offset_past_end) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 9, 0xFF};
    r.start();
    r.p.tick();
    CHECK(!r.m.active(0));
}

TEST(mod_e1_e2_fine_porta) {
    {
        Rig r;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0x12};
        r.start();
        CHECK(trace(r.p, 3, per) == (std::vector<int>{426, 426, 426}));
    }
    {
        Rig r;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0x22};
        r.start();
        CHECK(trace(r.p, 3, per) == (std::vector<int>{430, 430, 430}));
    }
}

TEST(mod_e9_retrig) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0x92};
    r.start();
    std::vector<float> buf(400);
    for (int t = 0; t < 6; ++t) {
        r.p.tick();
        double pos = r.m.position(0);
        if (t % 2 == 0)
            CHECK_NEAR(pos, 0.0, 1e-9);
        else
            CHECK(pos > 1.0);
        r.m.mix(buf.data(), 100);
    }
}

TEST(mod_ea_eb_fine_vol) {
    {
        Rig r;
        r.s.samples[0].volume = 32;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0xA4};
        r.start();
        CHECK(trace(r.p, 2, vol) == (std::vector<int>{36, 36}));
    }
    {
        Rig r;
        r.s.samples[0].volume = 32;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0xB4};
        r.start();
        CHECK(trace(r.p, 2, vol) == (std::vector<int>{28, 28}));
    }
}

TEST(mod_ec_note_cut) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0xC3};
    r.start();
    CHECK(trace(r.p, 6, outVol) == (std::vector<int>{64, 64, 64, 0, 0, 0}));
}

TEST(mod_ed_note_delay) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0xE, 0xD2};
    r.start();
    r.p.tick();
    CHECK(!r.m.active(0));
    r.p.tick();
    CHECK(!r.m.active(0));
    r.p.tick();
    CHECK(r.m.active(0));
}

TEST(mod_8xx_ignored) {
    Rig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 8, 0x80};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).pan, 0x40);
}
