// One line of text being edited (song and sample names): printable ASCII, a length limit, a cursor.
#pragma once
#include <cstddef>
#include <string>

struct LineEdit {
    std::string text;
    size_t cursor = 0, maxLen = 20;

    void begin(const char* s, size_t max) {
        text.clear();
        for (size_t i = 0; s && s[i] && i < max; ++i) text += (s[i] >= 32 && s[i] < 127) ? s[i] : ' ';
        while (!text.empty() && text.back() == ' ') text.pop_back();
        maxLen = max;
        cursor = text.size();
    }
    bool insert(char c) {
        if (c < 32 || c > 126 || text.size() >= maxLen) return false;
        text.insert(text.begin() + (std::ptrdiff_t)cursor, c);
        ++cursor;
        return true;
    }
    void backspace() {
        if (!cursor) return;
        text.erase(--cursor, 1);
    }
    void del() {
        if (cursor < text.size()) text.erase(cursor, 1);
    }
    void left() { cursor = cursor ? cursor - 1 : 0; }
    void right() { cursor = cursor < text.size() ? cursor + 1 : cursor; }
    void home() { cursor = 0; }
    void end() { cursor = text.size(); }
};
