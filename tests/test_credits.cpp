// The credits shown in the program (src/ui/credits.cpp) against CREDITS.txt.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test.h"
#include "ui/credits.h"
#include "ui/pixel_canvas.h"

namespace {
std::string programText() {  // unwrapped
    std::string s;
    for (const credits::Line& l : credits::lines(1 << 20)) s += l.text + "\n";
    return s;
}

std::string creditsFile() {
    std::ifstream f(std::string(FT_SOURCE_DIR) + "/CREDITS.txt", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}

// From every "<component> by <Author>" in CREDITS.txt: the component word and the author (up to a bracket,
// comma, semicolon, two spaces or the line end). "by the authors of ..." (lower case) is not a name.
void namesAndAddresses(const std::string& file, std::vector<std::string>& out) {
    std::istringstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        for (size_t at = line.find(" by "); at != std::string::npos; at = line.find(" by ", at + 4)) {
            size_t b = at + 4, e = b;
            if (b >= line.size() || line[b] < 'A' || line[b] > 'Z') continue;
            while (e < line.size() && line[e] != '(' && line[e] != ')' && line[e] != ',' && line[e] != ';' &&
                   line.compare(e, 2, "  ") != 0)
                ++e;
            out.push_back(trim(line.substr(b, e - b)));
            size_t w = line.rfind(' ', at - 1);
            std::string word = line.substr(w == std::string::npos ? 0 : w + 1, at - (w == std::string::npos ? 0 : w + 1));
            std::string clean;
            for (char c : word)
                if (c != '(' && c != ')') clean += c;
            if (!clean.empty()) out.push_back(clean);
        }
        for (size_t at = line.find("https://"); at != std::string::npos; at = line.find("https://", at + 1)) {
            size_t e = at;
            while (e < line.size() && line[e] != ' ') ++e;
            out.push_back(line.substr(at, e - at));
        }
    }
}
}  // namespace

TEST(credits_mention_everything_in_credits_txt) {
    const std::string file = creditsFile();
    CHECK(file.size() > 1000);
    std::vector<std::string> names;
    namesAndAddresses(file, names);
    CHECK(names.size() >= 40);  // authors, components and web addresses of about 16 entries
    const std::string text = programText();
    for (const std::string& n : names) {
        const bool found = text.find(n) != std::string::npos;
        if (!found) std::printf("  credits.cpp does not mention \"%s\" (CREDITS.txt)\n", n.c_str());
        CHECK(found);
    }
    for (const char* s : {"FatTracker - Copyright (C) 2026 fatman73 - https://github.com/fatman73/FatTracker",
                          "GNU GPL v2 or later", "WITHOUT ANY WARRANTY", "LICENSE","belongs to its composers",
                          "Mihaly \"Hermit\" Horvath", "Dexter / Abyss", "Nuke.YKT", "(blargg)", "OpenMPT"}) {
        CHECK(text.find(s) != std::string::npos);
        CHECK(file.find(s) != std::string::npos);
    }
}

TEST(credits_wrap_to_the_pure_screen) {
    const std::vector<credits::Line> ls = credits::lines(39);
    CHECK(ls.size() > 60);
    bool heading = false;
    for (const credits::Line& l : ls) {
        CHECK(l.text.size() <= 39);
        heading |= l.heading;
        for (char c : l.text) {  // every character has a glyph in the Pure mode font
            if (c == ' ') continue;
            const uint8_t* g = amigaGlyph(c);
            bool ink = false;
            for (int y = 0; y < 7; ++y) ink |= g[y] != 0;
            if (!ink) std::printf("  no pixel font glyph for '%c'\n", c);
            CHECK(ink);
        }
    }
    CHECK(heading);
    // FatTracker's own address stays on one line; longer addresses are broken after a '/', nothing is lost
    std::string joined;
    bool own = false;
    for (const credits::Line& l : ls) {
        const size_t b = l.text.find_first_not_of(' ');
        if (b == std::string::npos) continue;
        joined += l.text.substr(b);
        own |= l.text == "https://github.com/fatman73/FatTracker";
    }
    CHECK(own);
    CHECK(joined.find("https://github.com/mschwendt/libtfmxaudiodecoder") != std::string::npos);
}
