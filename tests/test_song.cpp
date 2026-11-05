#include "core/song.h"
#include "core/tables.h"
#include "test.h"

TEST(pt_periods_finetune0) {
    CHECK_EQ(kPtPeriods[0][0], 856);
    CHECK_EQ(kPtPeriods[0][12], 428);
    CHECK_EQ(kPtPeriods[0][24], 214);
    CHECK_EQ(kPtPeriods[0][35], 113);
}

TEST(pt_periods_finetune_minus8) { CHECK_EQ(kPtPeriods[8][0], 907); }

TEST(pt_period_hz) { CHECK_NEAR(ptPeriodToHz(428), 8287.14, 0.01); }

TEST(pt_note_to_xm) {
    CHECK_EQ(ptNoteToXm(0), 37);
    CHECK_EQ(ptNoteToXm(12), 49);
}

TEST(xm_linear_c4) {
    CHECK_EQ(xmLinearPeriod(48, 0), 4608u);
    CHECK_NEAR(xmLinearHz(4608), 8363.0, 0.001);
    CHECK_NEAR(xmLinearHz(xmLinearPeriod(60, 0)), 16726.0, 0.01);
}

TEST(xm_amiga_c4) {
    CHECK_NEAR(xmAmigaHz(xmAmigaPeriod(48, 0)), 8363.0, 2.0);
    CHECK_NEAR(xmAmigaHz(xmAmigaPeriod(60, 0)) / xmAmigaHz(xmAmigaPeriod(48, 0)), 2.0, 0.01);
}

TEST(set_channel_count_grows_and_shrinks) {
    Song s = makeEmptySong(4);
    s.patterns[0].at(5, 3) = {49, 1, 0, 0xC, 0x20};
    setChannelCount(s, 6);
    CHECK_EQ(s.numChannels, 6);
    CHECK_EQ(s.patterns[0].channels, 6);
    CHECK_EQ(s.patterns[0].cells.size(), 64u * 6);
    CHECK_EQ(s.patterns[0].at(5, 3).note, 49);
    CHECK_EQ(s.patterns[0].at(5, 3).param, 0x20);
    CHECK_EQ(s.patterns[0].at(5, 5).note, 0);
    setChannelCount(s, 2);  // clamped to the minimum of 4
    CHECK_EQ(s.numChannels, 4);
    CHECK_EQ(s.patterns[0].at(5, 3).note, 49);
    setChannelCount(s, 99);  // clamped to 32
    CHECK_EQ(s.numChannels, 32);
    CHECK_EQ(s.patterns[0].at(5, 3).note, 49);
}

TEST(empty_song_defaults) {
    Song s = makeEmptySong(8);
    CHECK_EQ(s.numChannels, 8);
    CHECK_EQ(s.patterns.size(), 1u);
    CHECK_EQ(s.patterns[0].rows, 64);
    CHECK_EQ(s.patterns[0].cells.size(), 64u * 8);
    CHECK_EQ(s.initialSpeed, 6);
    CHECK_EQ(s.initialBpm, 125);
    CHECK_EQ(s.channelPan[0], 0x40);
    CHECK_EQ(s.channelPan[1], 0xC0);
    CHECK_EQ(s.channelPan[2], 0xC0);
    CHECK_EQ(s.channelPan[3], 0x40);
}
