#include "player_util.h"
#include "test.h"

namespace {
enum { FX_G = 16, FX_H = 17, FX_K = 20, FX_R = 27, FX_X = 33 };

struct XRig {
    Mixer m{48000};
    Player p{m};
    Song s;
    XRig() {
        s = testSong(4, 64, Quirks::FT2);
        s.freqMode = FreqMode::Linear;
        Instrument in;
        for (auto& x : in.sampleMap) x = 0;
        s.instruments.push_back(in);
    }
    Instrument& inst() { return s.instruments[0]; }
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
int vol(const ChannelState& c) { return c.volume; }
int outVol(const ChannelState& c) { return c.outVolume; }
int per(const ChannelState& c) { return c.period; }
}  // namespace

TEST(xm_linear_note_freq) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).period, 4608);
    CHECK_NEAR(r.p.periodToHz(r.p.channel(0).period), 8363.0, 0.01);
}

TEST(xm_relative_note) {
    XRig r;
    r.s.samples[0].relativeNote = 12;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.start();
    r.p.tick();
    CHECK_NEAR(r.p.periodToHz(r.p.channel(0).period), 16726.0, 0.05);
}

TEST(xm_volcol_set_and_slide) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0x10 + 40, 0, 0};
    r.s.patterns[0].at(1, 0) = {0, 0, 0x64, 0, 0};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).volume, 40);
    for (int i = 0; i < 5; ++i) r.p.tick();
    CHECK(trace(r.p, 6, vol) == (std::vector<int>{40, 36, 32, 28, 24, 20}));
}

TEST(xm_volcol_pan) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0xCF, 0, 0};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).pan, 255);
}

TEST(xm_8xx_pan) {
    XRig r;
    r.s.samples[0].pan = 0x20;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 8, 0x80};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).pan, 0x80);
}

TEST(xm_keyoff_no_env) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.s.patterns[0].at(1, 0) = {NOTE_OFF, 0, 0, 0, 0};
    r.start();
    for (int i = 0; i < 6; ++i) r.p.tick();
    CHECK_EQ(r.p.channel(0).outVolume, 64);
    r.p.tick();
    CHECK_EQ(r.p.channel(0).outVolume, 0);
}

TEST(xm_env_interp) {
    XRig r;
    Envelope& e = r.inst().volEnv;
    e.pts[0] = {0, 64};
    e.pts[1] = {10, 0};
    e.count = 2;
    e.flags = ENV_ON;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.start();
    for (int i = 0; i < 6; ++i) r.p.tick();
    CHECK_EQ(r.p.channel(0).outVolume, 32);
}

TEST(xm_env_sustain_and_fade) {
    XRig r;
    Envelope& e = r.inst().volEnv;
    e.pts[0] = {0, 64};
    e.pts[1] = {4, 64};
    e.count = 2;
    e.sustain = 1;
    e.flags = ENV_ON | ENV_SUSTAIN;
    r.inst().fadeout = 0x1000;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.s.patterns[0].at(2, 0) = {NOTE_OFF, 0, 0, 0, 0};
    r.start();
    for (int i = 0; i < 12; ++i) r.p.tick();
    CHECK_EQ(r.p.channel(0).outVolume, 64);
    CHECK(trace(r.p, 3, outVol) == (std::vector<int>{56, 48, 40}));
}

TEST(xm_env_loop) {
    XRig r;
    Envelope& e = r.inst().volEnv;
    e.pts[0] = {0, 0};
    e.pts[1] = {4, 64};
    e.pts[2] = {8, 0};
    e.count = 3;
    e.loopStart = 1;
    e.loopEnd = 2;
    e.flags = ENV_ON | ENV_LOOP;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.start();
    for (int i = 0; i < 4; ++i) r.p.tick();
    CHECK(trace(r.p, 8, outVol) == (std::vector<int>{64, 48, 32, 16, 64, 48, 32, 16}));
}

TEST(xm_gxx_hxy) {
    {
        XRig r;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, FX_G, 0x20};
        r.start();
        r.p.tick();
        CHECK_EQ(r.p.channel(0).outVolume, 32);
    }
    {
        XRig r;
        r.s.patterns[0].at(0, 0) = {49, 1, 0, FX_H, 0x01};
        r.start();
        CHECK(trace(r.p, 6, outVol) == (std::vector<int>{64, 63, 62, 61, 60, 59}));
    }
}

TEST(xm_kxx_keyoff_at_tick) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, FX_K, 3};
    r.start();
    CHECK(trace(r.p, 5, outVol) == (std::vector<int>{64, 64, 64, 0, 0}));
}

TEST(xm_rxy_retrig) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, FX_R, 0x02};
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

TEST(xm_x1_extra_fine) {
    XRig r;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, FX_X, 0x14};
    r.start();
    CHECK(trace(r.p, 3, per) == (std::vector<int>{4604, 4604, 4604}));
}

TEST(xm_amiga_mode) {
    XRig r;
    r.s.freqMode = FreqMode::Amiga;
    r.s.patterns[0].at(0, 0) = {49, 1, 0, 0, 0};
    r.start();
    r.p.tick();
    CHECK_NEAR(r.p.periodToHz(r.p.channel(0).period), 8363.0, 2.0);
}

TEST(xm_instrument_sample_map) {
    XRig r;
    r.s.samples.push_back(r.s.samples[0]);
    r.inst().sampleMap[24] = 1;
    r.s.patterns[0].at(0, 0) = {25, 1, 0, 0, 0};
    r.start();
    r.p.tick();
    CHECK_EQ(r.p.channel(0).sample, 1);
}
