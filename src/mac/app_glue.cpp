#include "mac/app_glue.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "app/chip_route.h"
#include "app/shot.h"
#include "audio/engine.h"
#include "core/song.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/play_screen.h"
#include "ui/text.h"
#include "ui/theme.h"

namespace {
Engine* g_engine = nullptr;
Gfx g_gfx;
Text g_text;
Font g_font;
PlayScreen g_screen;
bool g_dirty = true;
std::wstring g_path;  // file of the current song ("" = untitled)
std::wstring g_replayDir;

std::wstring wide(const char* utf8) { return std::filesystem::path(utf8).wstring(); }

// Loads a song (chip or module) and shows it, like loadModuleFile on Windows (without the Pure floppy
// wait, the TFMX sample-file hint and the drive sound: they come with the shared app logic).
bool loadModuleFile(const std::wstring& path) {
    g_engine->stop();
    std::string error;
    if (!loadSongFile(*g_engine, path, g_replayDir, &error)) {
        static char msg[128];
        std::snprintf(msg, sizeof msg, "%s", error.c_str());
        g_screen.setStatus(msg);
        return false;
    }
    g_screen.songChanged();
    if (g_engine->tfmx()) {
        static char msg[128];
        const int songs = g_engine->chipSubsongs();
        if (songs > 1) std::snprintf(msg, sizeof msg, "%s - %d SONGS (POS) - PLAYBACK ONLY", g_engine->tfmxFormat(), songs);
        else std::snprintf(msg, sizeof msg, "%s - PLAYBACK ONLY", g_engine->tfmxFormat());
        g_screen.setStatus(msg);
        g_path.clear();
    } else {
        g_screen.setStatus("ALL RIGHT");
        g_path = path;
    }
    g_screen.setCurrentPath(path);
    return true;
}
}  // namespace

int appMainStart(int argc, char** argv) {
    g_engine = new Engine();
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(4)));
    g_text.init();
    g_font.setText(&g_text);
    // --shot from the build folder: SC68 replays next to the binary
    std::error_code ec;
    g_replayDir = (std::filesystem::absolute(argv[0], ec).parent_path() / "Replay").wstring();
    return runShot(argc, argv, g_gfx, g_text, g_font, g_screen, *g_engine, loadModuleFile);
}

void appWindowReady(const char* iniPath, const char* replayDir) {
    g_replayDir = wide(replayDir);
    if (!g_engine->startAudio()) g_screen.setStatus(g_engine->audioStatus());
    g_screen.loadSettings(wide(iniPath).c_str(), *g_engine);
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(g_screen.defaultChannels())));
    g_screen.songChanged();
    g_dirty = true;
}

bool appOpenFile(const char* utf8Path, bool play) {
    const bool ok = loadModuleFile(wide(utf8Path));
    if (ok && play) g_engine->play();
    g_dirty = true;
    return ok;
}

void appPollAudio() {
    g_engine->pollAudio();
    g_dirty = true;
}

void appSetDirty() { g_dirty = true; }

bool appFrameDue() { return g_dirty || g_screen.animating(); }

bool appDrawFrame() {
    g_dirty = false;
    const Theme& t = currentTheme();
    if (!g_gfx.beginFrame(t.background)) return false;
    g_screen.draw(g_gfx, g_text, g_font, t, *g_engine);
    g_gfx.endFrame();
    return true;
}

void appResize(uint32_t w, uint32_t h) {
    g_gfx.resize(w, h);
    g_dirty = true;
}

CGImageRef appCopyFrame() { return g_gfx.copyImage(); }

void appQuit(const char* iniPath) {
    g_screen.saveSettings(wide(iniPath).c_str());
    g_engine->stop();
    delete g_engine;
    g_engine = nullptr;
}
