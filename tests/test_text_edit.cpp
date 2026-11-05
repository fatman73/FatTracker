// Line editing for song / sample names.
#include <string>

#include "core/text_edit.h"
#include "test.h"

TEST(line_edit_insert_and_limit) {
    LineEdit e;
    e.begin("SONG", 6);
    CHECK_EQ(e.cursor, 4u);  // starts at the end
    CHECK(e.insert('X'));
    CHECK(e.insert('Y'));
    CHECK(!e.insert('Z'));   // 6 characters max
    CHECK(e.text == "SONGXY");
    CHECK(!e.insert('\t'));  // not printable
}

TEST(line_edit_cursor_and_delete) {
    LineEdit e;
    e.begin("ABCD", 20);
    e.home();
    e.backspace();  // nothing before the cursor
    CHECK(e.text == "ABCD");
    e.right();
    e.insert('-');
    CHECK(e.text == "A-BCD");
    e.del();
    CHECK(e.text == "A-CD");
    e.end();
    e.del();  // nothing after the cursor
    CHECK(e.text == "A-CD");
    e.backspace();
    CHECK(e.text == "A-C");
    e.left();
    e.left();
    e.left();
    e.left();  // stops at 0
    CHECK_EQ(e.cursor, 0u);
}
