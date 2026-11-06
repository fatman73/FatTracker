// Dev screenshots (--shot): see shot.cpp.
#pragma once
#include <functional>
#include <string>

class Engine;
class Font;
class Gfx;
class PlayScreen;
class Text;

// Returns -1 when argv is not a --shot command line, else the process exit code (0 = PNG saved).
int runShot(int argc, char** argv, Gfx& gfx, Text& text, Font& font, PlayScreen& screen, Engine& engine,
            const std::function<bool(const std::wstring&)>& loadModuleFile);
