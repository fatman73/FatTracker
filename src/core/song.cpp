#include "core/song.h"

Pattern makePattern(int rows, int channels) {
    Pattern p;
    p.rows = (uint16_t)rows;
    p.channels = (uint8_t)channels;
    p.cells.resize((size_t)rows * channels);
    return p;
}

void setDefaultPanning(Song& s) {
    for (int c = 0; c < kMaxChannels; ++c) {
        int m = c & 3;
        s.channelPan[c] = (m == 0 || m == 3) ? 0x40 : 0xC0;
    }
}

void setChannelCount(Song& s, int channels) {
    if (channels < 4) channels = 4;
    if (channels > kMaxChannels) channels = kMaxChannels;
    for (Pattern& p : s.patterns) {
        Pattern q = makePattern(p.rows, channels);
        const int keep = p.channels < channels ? p.channels : channels;
        for (int r = 0; r < p.rows; ++r)
            for (int c = 0; c < keep; ++c) q.at(r, c) = p.at(r, c);
        p = static_cast<Pattern&&>(q);
    }
    s.numChannels = (uint8_t)channels;
}

Song makeEmptySong(int channels) {
    Song s;
    s.numChannels = (uint8_t)channels;
    s.order.push_back(0);
    s.patterns.push_back(makePattern(64, channels));
    setDefaultPanning(s);
    return s;
}
