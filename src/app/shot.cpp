// Dev tool: FatTracker --shot <file.png> [width height [module [seconds [view [tokens]]]]]
// Draws the screen offscreen after optional simulated input and saves it as PNG. Portable port of
// screenshotMode() in main.cpp (Windows keeps its own copy until the app logic is shared); the tokens
// are the same: SPACE UP DOWN LEFT RIGHT TAB DEL INS BACK ESC F1..F5 STOP, S+/C+/A+ modifiers,
// NAME:n TXT:abc FXT:s FX:n PURE:n REC RECMODE CLICK:x,y RCLICK:x,y DRAG:x0,y0,x1,y1 OP:n T:n P:panel.
#include "app/shot.h"

#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#include "audio/engine.h"
#include "platform/vk.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/play_screen.h"
#include "ui/text.h"
#include "ui/theme.h"

namespace {
bool sameNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::towupper((unsigned char)*a) != std::towupper((unsigned char)*b)) return false;
    return *a == *b;
}
}  // namespace

int runShot(int argc, char** argv, Gfx& gfx, Text& text, Font& font, PlayScreen& screen, Engine& engine,
            const std::function<bool(const std::wstring&)>& loadModuleFile) {
    if (argc < 3 || std::strcmp(argv[1], "--shot") != 0) return -1;
    const uint32_t w = argc >= 5 ? (uint32_t)std::atoi(argv[3]) : 1920;
    const uint32_t h = argc >= 5 ? (uint32_t)std::atoi(argv[4]) : 1080;
    const bool gfxOk = w && h && gfx.initOffscreen(w, h);
    auto drawFrame = [&]() {
        const Theme& t = currentTheme();
        if (!gfx.beginFrame(t.background)) return;
        screen.draw(gfx, text, font, t, engine);
        gfx.endFrame();
    };
    if (argc >= 6 && *argv[5] && loadModuleFile(std::filesystem::path(argv[5]).wstring())) {
        engine.play();
        const int seconds10 = argc >= 7 ? (int)(std::atof(argv[6]) * 10) : 15;
        std::vector<float> buf(4800 * 2);
        for (int i = 0; i < seconds10; ++i) engine.render(buf.data(), 4800);
    }
    if (argc >= 8) screen.setViewMode(std::atoi(argv[7]));
    if (argc >= 9) {  // simulated input, e.g. "SPACE DOWN Z X S+DOWN"
        drawFrame();   // establishes the layout
        struct Named {
            const char* name;
            unsigned vk;
        } names[] = {{"SPACE", VK_SPACE}, {"UP", VK_UP},     {"DOWN", VK_DOWN},    {"LEFT", VK_LEFT},
                     {"RIGHT", VK_RIGHT}, {"TAB", VK_TAB},   {"DEL", VK_DELETE},   {"INS", VK_INSERT},
                     {"BACK", VK_BACK},   {"ESC", VK_ESCAPE}, {"F1", VK_F1},        {"F2", VK_F2},
                     {"F3", VK_F3},       {"F4", VK_F4},     {"F5", VK_F5},        {"STOP", 0xFFFF}};
        std::string all = argv[8];
        size_t at = 0;
        while (at < all.size()) {
            const size_t end = all.find(' ', at);
            std::string word = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? all.size() : end + 1;
            if (word.empty()) continue;
            const char* tok = word.c_str();
            bool shift = false, ctrl = false, alt = false;
            for (;;) {
                if (!std::strncmp(tok, "S+", 2)) shift = true;
                else if (!std::strncmp(tok, "C+", 2)) ctrl = true;
                else if (!std::strncmp(tok, "A+", 2)) alt = true;
                else break;
                tok += 2;
            }
            if (!std::strncmp(tok, "NAME:", 5)) {  // start editing the song (1) or sample (2) name
                screen.startNameEdit(std::atoi(tok + 5), engine);
            } else if (!std::strncmp(tok, "TXT:", 4)) {  // type characters ('_' = space)
                for (const char* p = tok + 4; *p; ++p) screen.onChar(*p == '_' ? L' ' : (wchar_t)(unsigned char)*p, engine);
            } else if (!std::strncmp(tok, "FXT:", 4)) {  // logo box demo time in seconds
                screen.setDemoTime(std::atof(tok + 4));
            } else if (!std::strncmp(tok, "FX:", 3)) {  // logo box demo effect
                screen.setDemoEffect(std::atoi(tok + 3));
            } else if (!std::strncmp(tok, "PURE:", 5)) {  // Pure mode 0/1/2
                screen.setPure(std::atoi(tok + 5), engine);
                std::vector<float> pb(4800 * 2);
                for (int i = 0; i < 5; ++i) engine.render(pb.data(), 4800);
            } else if (!std::strcmp(tok, "REC") || !std::strcmp(tok, "RECMODE")) {  // RECORD (RECMODE: right click)
                screen.pressRecord(tok[3] != 0, engine);
            } else if (!std::strncmp(tok, "CLICK:", 6) || !std::strncmp(tok, "RCLICK:", 7) ||
                       !std::strncmp(tok, "DRAG:", 5)) {
                // mouse in logical units: CLICK:x,y  RCLICK:x,y  DRAG:x0,y0,x1,y1 (press, move in steps, release)
                float v[4] = {};
                const char* q = std::strchr(tok, ':') + 1;
                for (int i = 0; i < 4 && q; ++i) {
                    v[i] = (float)std::atof(q);
                    q = std::strchr(q, ',');
                    if (q) ++q;
                }
                screen.click(v[0], v[1], tok[0] == 'R', engine);
                if (tok[0] == 'D') {
                    for (int i = 1; i <= 8; ++i)
                        screen.onMouseMove(v[0] + (v[2] - v[0]) * i / 8, v[1] + (v[3] - v[1]) * i / 8, true, engine);
                    screen.onMouseUp(v[2], v[3], engine);
                } else {
                    screen.onMouseUp(v[0], v[1], engine);
                }
            } else if (!std::strncmp(tok, "OP:", 3)) {  // Pure EDIT OP. screen 0..2
                screen.setEditOpScreen(std::atoi(tok + 3));
            } else if (!std::strncmp(tok, "T:", 2)) {  // theme index
                setCurrentTheme(std::atoi(tok + 2));
            } else if (!std::strncmp(tok, "P:", 2)) {
                const char* pn = tok + 2;
                Panel p = sameNoCase(pn, "DISKOP")    ? Panel::DiskOp
                          : sameNoCase(pn, "SAMPLER") ? Panel::Sampler
                          : sameNoCase(pn, "EDITOP")  ? Panel::EditOp
                          : sameNoCase(pn, "SETUP")   ? Panel::Setup
                          : sameNoCase(pn, "POSED")   ? Panel::PosEd
                          : sameNoCase(pn, "INSTR")   ? Panel::Instrument
                                                      : Panel::Help;
                screen.openPanel(p);
            } else {
                unsigned vk = tok[1] ? 0 : (unsigned)std::towupper((unsigned char)tok[0]);
                for (const Named& n : names)
                    if (sameNoCase(tok, n.name)) vk = n.vk;
                if (vk == 0xFFFF) engine.stop();
                else if (vk) screen.onKey(vk, shift, ctrl, alt, false, false, engine);
            }
            drawFrame();  // refresh the screen's view of the engine between keys
        }
    }
    if (!gfxOk) return 1;
    drawFrame();
    return gfx.savePng(std::filesystem::path(argv[2]).wstring().c_str()) ? 0 : 1;
}
