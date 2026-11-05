// Helpers for player tests.
#pragma once
#include <vector>

#include "core/mixer.h"
#include "core/player.h"
#include "core/song.h"

inline Song testSong(int ch, int rows, Quirks q = Quirks::PT23, int numPatterns = 1) {
    Song s = makeEmptySong(ch);
    s.patterns.clear();
    for (int i = 0; i < numPatterns; ++i) s.patterns.push_back(makePattern(rows, ch));
    s.quirks = q;
    s.freqMode = FreqMode::Amiga;
    Sample sm;
    sm.length = 4000;
    sm.data8.assign(4000, 50);
    sm.volume = 64;
    s.samples.push_back(sm);
    return s;
}

// Rows visited (recorded at tick 0) during n ticks.
inline std::vector<int> rowTrace(Player& p, int ticks) {
    std::vector<int> rows;
    for (int i = 0; i < ticks; ++i) {
        if (p.pos().tick == 0) rows.push_back(p.pos().row);
        p.tick();
    }
    return rows;
}
