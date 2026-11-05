// Regression tests for the stage 2 final review.
#include <cstring>

#include "core/edit.h"
#include "formats/formats.h"
#include "formats/sample_io.h"
#include "formats/writers.h"
#include "player_util.h"
#include "test.h"

// Critical 1: undoing a new pattern while it plays must not leave the player out of range.
TEST(player_survives_pattern_removed_while_playing) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 64);
    Editor ed;
    const int np = ed.newPattern(s, 64);
    CHECK_EQ(np, 1);
    p.setSong(&s);
    p.playPattern(1);
    p.tick();
    ed.undo(s);  // pattern 1 disappears
    p.songEdited();
    CHECK(p.pos().pattern < s.patterns.size());
    for (int i = 0; i < 20; ++i) p.tick();
    CHECK(p.pos().pattern < s.patterns.size());
}

TEST(player_survives_order_shrink_while_playing) {
    Mixer m(48000);
    Player p(m);
    Song s = testSong(4, 4, Quirks::PT23, 3);
    s.order = {0, 1, 2};
    s.initialSpeed = 1;
    p.setSong(&s);
    p.play(2);
    p.tick();
    s.order = {0};
    s.patterns.resize(1);
    p.songEdited();
    CHECK(p.pos().order < s.order.size());
    CHECK(p.pos().pattern < s.patterns.size());
    for (int i = 0; i < 10; ++i) p.tick();
    CHECK(p.pos().pattern < s.patterns.size());
}

// Important 6: channel count changes are undoable and keep every pattern consistent.
TEST(edit_channels_undo_consistent) {
    Song s = makeEmptySong(4);
    s.patterns.push_back(makePattern(64, 4));
    Editor ed;
    auto consistent = [&]() {
        for (const Pattern& p : s.patterns)
            if (p.channels != s.numChannels) return false;
        return true;
    };
    ed.setNote(s, 0, 0, 0, 49, 1);
    ed.setChannels(s, 8);
    CHECK_EQ(s.numChannels, 8);
    CHECK(consistent());
    ed.setNote(s, 0, 1, 7, 50, 1);
    CHECK(ed.undo(s) && consistent());
    CHECK(ed.undo(s) && consistent());
    CHECK_EQ(s.numChannels, 4);
    CHECK(ed.undo(s) && consistent());
    CHECK(ed.redo(s) && ed.redo(s) && ed.redo(s));
    CHECK(consistent());
    CHECK_EQ(s.numChannels, 8);
    CHECK_EQ(s.patterns[0].at(1, 7).note, 50);
}

// Important 4: MOD export remaps patterns, keeps tempo, uses M!K! above 64 patterns.
TEST(mod_export_remaps_patterns_and_keeps_tempo) {
    Song s = makeEmptySong(4);
    s.quirks = Quirks::PT23;
    for (int i = 0; i < 2; ++i) s.patterns.push_back(makePattern(64, 4));
    s.patterns[2].at(5, 1) = {49, 0, 0, 0xC, 0x10};
    s.order = {2, 0};
    s.initialSpeed = 3;
    s.initialBpm = 150;
    ModExport out = exportMod(s);
    auto r = loadModule(out.data.data(), out.data.size());
    CHECK(r.ok());
    if (!r.ok()) return;
    const Song& t = r.value();
    CHECK_EQ(t.patterns.size(), 2u);  // only used patterns, renumbered 0..1
    CHECK(t.order == (std::vector<uint8_t>{0, 1}));
    CHECK_EQ(t.patterns[0].at(5, 1).param, 0x10);
    bool speed = false, bpm = false;
    for (int c = 0; c < 4; ++c) {
        const Cell& cell = t.patterns[0].at(0, c);
        if (cell.fx == 0xF && cell.param == 3) speed = true;
        if (cell.fx == 0xF && cell.param == 150) bpm = true;
    }
    CHECK(speed && bpm);
}

TEST(mod_export_many_patterns_uses_mk_bang) {
    Song s = makeEmptySong(4);
    s.order.clear();
    for (int i = 0; i < 70; ++i) {
        if (i) s.patterns.push_back(makePattern(64, 4));
        s.order.push_back((uint8_t)i);
    }
    ModExport out = exportMod(s);
    CHECK_EQ(std::memcmp(out.data.data() + 1080, "M!K!", 4), 0);
    auto r = loadModule(out.data.data(), out.data.size());
    CHECK(r.ok() && r.value().patterns.size() == 70);
}

// Important 8: empty sample files are rejected.
TEST(sample_load_rejects_empty) {
    std::vector<uint8_t> b = {'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                              1, 0, 1, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    CHECK(!loadSampleFile(b.data(), b.size()).ok());
    std::vector<uint8_t> svx = {'F', 'O', 'R', 'M', 0, 0, 0, 32, '8', 'S', 'V', 'X', 'V', 'H', 'D', 'R', 0, 0, 0, 20};
    svx.resize(svx.size() + 20, 0);
    CHECK(!loadSampleFile(svx.data(), svx.size()).ok());
}
