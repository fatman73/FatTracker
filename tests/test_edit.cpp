#include "core/edit.h"
#include "test.h"

namespace {
Song song4(int rows = 64) {
    Song s = makeEmptySong(4);
    s.patterns[0] = makePattern(rows, 4);
    return s;
}
bool samePatterns(const Song& a, const Song& b) {
    if (a.patterns.size() != b.patterns.size() || a.order != b.order || a.numChannels != b.numChannels) return false;
    for (size_t p = 0; p < a.patterns.size(); ++p) {
        const Pattern &x = a.patterns[p], &y = b.patterns[p];
        if (x.rows != y.rows || x.channels != y.channels) return false;
        for (size_t i = 0; i < x.cells.size(); ++i) {
            const Cell &c = x.cells[i], &d = y.cells[i];
            if (c.note != d.note || c.instr != d.instr || c.vol != d.vol || c.fx != d.fx || c.param != d.param)
                return false;
        }
    }
    return true;
}
}  // namespace

TEST(edit_set_note_and_undo) {
    Song s = song4();
    const Song before = s;
    Editor ed;
    ed.setNote(s, 0, 3, 1, 49, 2);
    CHECK_EQ(s.patterns[0].at(3, 1).note, 49);
    CHECK_EQ(s.patterns[0].at(3, 1).instr, 2);
    CHECK(ed.undo(s));
    CHECK(samePatterns(s, before));
    CHECK(ed.redo(s));
    CHECK_EQ(s.patterns[0].at(3, 1).note, 49);
}

TEST(edit_digits) {
    Song s = song4();
    Editor ed;
    ed.setDigit(s, 0, 0, 0, kColInstrHi, 1);
    ed.setDigit(s, 0, 0, 0, kColInstrLo, 0xF);
    CHECK_EQ(s.patterns[0].at(0, 0).instr, 0x1F);
    ed.setDigit(s, 0, 0, 0, kColVolHi, 3);  // set volume 0x30
    CHECK_EQ(s.patterns[0].at(0, 0).vol, 0x10 + 0x30);
    ed.setDigit(s, 0, 0, 0, kColVolLo, 8);
    CHECK_EQ(s.patterns[0].at(0, 0).vol, 0x10 + 0x38);
    ed.setDigit(s, 0, 0, 0, kColVolHi, 4);  // 0x48 > 0x40 -> clamped
    CHECK_EQ(s.patterns[0].at(0, 0).vol, 0x10 + 0x40);
    ed.setDigit(s, 0, 0, 0, kColVolHi, 6);  // volume-column command (slide down)
    CHECK_EQ(s.patterns[0].at(0, 0).vol >> 4, 6);
    ed.setDigit(s, 0, 0, 0, kColFx, 0xC);
    ed.setDigit(s, 0, 0, 0, kColParamHi, 2);
    ed.setDigit(s, 0, 0, 0, kColParamLo, 0);
    CHECK_EQ(s.patterns[0].at(0, 0).fx, 0xC);
    CHECK_EQ(s.patterns[0].at(0, 0).param, 0x20);
    ed.setDigit(s, 0, 0, 0, kColFx, 20);  // XM letter K
    CHECK_EQ(s.patterns[0].at(0, 0).fx, 20);
}

TEST(edit_insert_delete_row) {
    Song s = song4(8);
    Editor ed;
    for (int r = 0; r < 8; ++r) s.patterns[0].at(r, 0).note = (uint8_t)(r + 1);
    s.patterns[0].at(2, 1).note = 99 - 90;
    ed.insertRow(s, 0, 2, 0);  // track 0 only
    CHECK_EQ(s.patterns[0].at(2, 0).note, 0);
    CHECK_EQ(s.patterns[0].at(3, 0).note, 3);
    CHECK_EQ(s.patterns[0].at(7, 0).note, 7);  // last row (8) dropped
    CHECK_EQ(s.patterns[0].at(2, 1).note, 9);   // other track untouched
    ed.deleteRow(s, 0, 2, 0);
    CHECK_EQ(s.patterns[0].at(2, 0).note, 3);
    CHECK_EQ(s.patterns[0].at(7, 0).note, 0);  // empty row appended
    ed.insertRow(s, 0, 0, -1);                 // all tracks
    CHECK_EQ(s.patterns[0].at(3, 1).note, 9);
}

TEST(edit_block_copy_paste_clear_and_clip) {
    Song s = song4(16);
    Editor ed;
    s.patterns[0].at(0, 0) = {49, 1, 0, 0xC, 0x10};
    s.patterns[0].at(1, 1) = {50, 2, 0, 0, 0};
    Block b{0, 0, 1, 0, 1};
    Clip c = ed.copy(s, b);
    CHECK_EQ(c.rows, 2);
    CHECK_EQ(c.channels, 2);
    ed.paste(s, 0, 15, 3, c);  // only 1 row x 1 channel fits
    CHECK_EQ(s.patterns[0].at(15, 3).note, 49);
    ed.clear(s, b);
    CHECK_EQ(s.patterns[0].at(0, 0).note, 0);
    CHECK_EQ(s.patterns[0].at(1, 1).note, 0);
    CHECK(ed.undo(s));
    CHECK_EQ(s.patterns[0].at(0, 0).note, 49);
}

TEST(edit_transpose_clamps_and_filters) {
    Song s = song4(4);
    Editor ed;
    s.patterns[0].at(0, 0) = {95, 1, 0, 0, 0};
    s.patterns[0].at(1, 0) = {NOTE_OFF, 0, 0, 0, 0};
    s.patterns[0].at(2, 0) = {10, 2, 0, 0, 0};
    ed.transpose(s, Block{0, 0, 3, 0, 0}, 12, 0);
    CHECK_EQ(s.patterns[0].at(0, 0).note, 96);
    CHECK_EQ(s.patterns[0].at(1, 0).note, NOTE_OFF);
    CHECK_EQ(s.patterns[0].at(2, 0).note, 22);
    ed.transpose(s, Block{0, 0, 3, 0, 0}, -1, 2);  // only sample 2
    CHECK_EQ(s.patterns[0].at(0, 0).note, 96);
    CHECK_EQ(s.patterns[0].at(2, 0).note, 21);
}

TEST(edit_pattern_rows_and_orders_undo) {
    Song s = song4(64);
    s.patterns[0].at(10, 0).note = 49;
    const Song before = s;
    Editor ed;
    ed.setPatternRows(s, 0, 32);
    CHECK_EQ(s.patterns[0].rows, 32);
    CHECK_EQ(s.patterns[0].at(10, 0).note, 49);
    int p = ed.newPattern(s, 64);
    CHECK_EQ(p, 1);
    ed.insertOrder(s, 1, (uint8_t)p);
    CHECK_EQ(s.order.size(), 2u);
    CHECK_EQ(s.order[1], 1);
    ed.deleteOrder(s, 0);
    CHECK_EQ(s.order.size(), 1u);
    ed.deleteOrder(s, 0);  // never below one position
    CHECK_EQ(s.order.size(), 1u);
    while (ed.undo(s)) {}
    CHECK(samePatterns(s, before));
}

TEST(edit_undo_limit_and_redo_cleared) {
    Song s = song4(4);
    Editor ed;
    for (int i = 0; i < 150; ++i) ed.setNote(s, 0, 0, 0, (uint8_t)(1 + i % 90), 1);
    int n = 0;
    while (ed.undo(s)) ++n;
    CHECK_EQ(n, 100);
    ed.redo(s);
    ed.setNote(s, 0, 1, 0, 49, 1);  // new edit clears redo
    CHECK(!ed.redo(s));
}

TEST(edit_rotate_and_flip) {
    Song s = song4(4);
    Editor ed;
    for (int r = 0; r < 4; ++r) s.patterns[0].at(r, 0).note = (uint8_t)(r + 1);
    ed.rotate(s, Block{0, 0, 3, 0, 0}, -1);  // up: row0 goes to the bottom
    CHECK_EQ(s.patterns[0].at(0, 0).note, 2);
    CHECK_EQ(s.patterns[0].at(3, 0).note, 1);
    ed.flip(s, Block{0, 0, 3, 0, 0});
    CHECK_EQ(s.patterns[0].at(0, 0).note, 1);
    CHECK_EQ(s.patterns[0].at(3, 0).note, 2);
}
