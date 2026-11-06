// FatTracker's credits and licences as shown in the program (HELP: CREDITS, Pure mode: click the
// machine name). The one table in credits.cpp must stay in step with CREDITS.txt (a test checks it).
// Pure C++ so it is unit tested.
#pragma once
#include <string>
#include <vector>

namespace credits {
struct Line {
    std::string text;
    bool heading;  // what the component does (drawn in the heading colour)
};
// The credits word wrapped to `cols` characters (longer words are cut); entries are separated by blank lines.
std::vector<Line> lines(int cols);
}  // namespace credits
