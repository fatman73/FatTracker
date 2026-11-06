#include "mac/app_glue.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/chip_route.h"
#include "app/shot.h"
#include "audio/engine.h"
#include "core/song.h"
#include "formats/sample_io.h"
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

bool g_openRequest = false;    // the open panel should be shown (after the unsaved-changes question)
std::string g_lastTitle = "\x01";  // forces the first title update
bool g_lastEdited = false;

// Runs `then` right away, or after asking to save unsaved changes (main.cpp confirmDiscard; SAVE needs the
// save code of M6, so it reports that and keeps the song).
void confirmDiscard(std::function<void()> then) {
    if (!g_screen.dirty()) {
        then();
        return;
    }
    g_screen.ask("THE SONG HAS UNSAVED CHANGES", {"SAVE", "DISCARD", "CANCEL"}, [then](int a) {
        if (a == 2) return;
        if (a == 0) {
            g_screen.setStatus("SAVING IS NOT ON THE MAC YET");
            return;
        }
        then();
    });
    g_dirty = true;
}

// A dropped or DISK OP. file: a sample goes into the current sample slot, anything else loads as a song.
void loadAnyFile(const std::wstring& path) {
    std::vector<uint8_t> data;
    if (const char* err = readWholeFile(path, data)) {
        g_screen.setStatus(err);
        return;
    }
    const bool sampleSig = data.size() >= 12 && ((!std::memcmp(data.data(), "RIFF", 4) && !std::memcmp(data.data() + 8, "WAVE", 4)) ||
                                                  (!std::memcmp(data.data(), "FORM", 4) && (!std::memcmp(data.data() + 8, "8SVX", 4) ||
                                                                                            !std::memcmp(data.data() + 8, "AIFF", 4) ||
                                                                                            !std::memcmp(data.data() + 8, "AIFC", 4))));
    auto smp = loadSampleFile(data.data(), data.size());
    if (sampleSig && !smp.ok()) {
        g_screen.setStatus(smp.error());
        return;
    }
    if (smp.ok()) {
        const size_t slash = path.find_last_of(L"\\/");
        g_screen.sampleLoaded(*g_engine, static_cast<Sample&&>(smp.value()), path.substr(slash + 1));
        return;
    }
    confirmDiscard([path]() { loadModuleFile(path); });
}

// What a UI event asked for (main.cpp handle(); the dialogs, saving and recording come with M6).
void handle(UiAction a) {
    if (a == UiAction::OpenFile) appRequestOpen();
    if (a == UiAction::SaveFile || a == UiAction::ExportWav || a == UiAction::SaveXmPath ||
        a == UiAction::SaveModPath || a == UiAction::SaveWavPath || a == UiAction::SaveSamplePath)
        g_screen.setStatus("SAVING IS NOT ON THE MAC YET");
    if (a == UiAction::LoadPath) loadAnyFile(g_screen.actionPath());
    g_screen.takeDirRead();  // the floppy drive sound comes with M6
    if (g_screen.takeRecordRequest()) g_screen.setStatus("RECORDING IS NOT ON THE MAC YET");
    if (a != UiAction::None) g_dirty = true;
}

void toLogical(float px, float py, float& lx, float& ly) { deviceToLogical(g_gfx.viewport(), px, py, lx, ly); }
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

void appWindowReady(const char* iniPath, const char* replayDir, bool audio) {
    g_replayDir = wide(replayDir);
    if (audio && !g_engine->startAudio()) g_screen.setStatus(g_engine->audioStatus());
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

void appKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat) {
    if (ctrl && !alt && vk == 'O') {  // Ctrl+O (Control or Command): the open panel, like main.cpp
        if (!repeat) appRequestOpen();
        return;
    }
    handle(g_screen.onKey(vk, shift, ctrl, alt, extended, repeat, *g_engine));
}

void appKeyUp(unsigned vk) { handle(g_screen.onKeyUp(vk, *g_engine)); }

void appChar(uint32_t c) {
    if (c > 0xFFFF) return;
    handle(g_screen.onChar((wchar_t)c, *g_engine));
}

void appMouseDown(float px, float py, bool right) {
    float lx, ly;
    toLogical(px, py, lx, ly);
    handle(g_screen.onMouse(lx, ly, right, *g_engine));
}

void appMouseUp(float px, float py) {
    float lx, ly;
    toLogical(px, py, lx, ly);
    handle(g_screen.onMouseUp(lx, ly, *g_engine));
}

void appMouseMove(float px, float py, bool leftDown) {
    float lx, ly;
    toLogical(px, py, lx, ly);
    if (g_screen.onMouseMove(lx, ly, leftDown, *g_engine)) g_dirty = true;
}

void appWheel(float px, float py, int delta) {
    float lx, ly;
    toLogical(px, py, lx, ly);
    handle(g_screen.onWheel(lx, ly, delta, *g_engine));
}

void appDropFile(const char* utf8Path) {
    loadAnyFile(wide(utf8Path));
    g_dirty = true;
}

void appRequestOpen() {
    confirmDiscard([]() { g_openRequest = true; });
    g_dirty = true;
}

bool appTakeOpenRequest(char* startDir, int cap) {
    if (!g_openRequest) return false;
    g_openRequest = false;
    const std::wstring& d = g_screen.browseDir();
    std::string u8;
    if (!d.empty() && d != L"::drives") u8 = std::filesystem::path(d).string();
    std::snprintf(startDir, (size_t)cap, "%s", u8.c_str());
    return true;
}

void appOpenChosen(const char* utf8Path) {
    loadModuleFile(wide(utf8Path));
    g_screen.openPanel(Panel::Main);
    g_dirty = true;
}

bool appTitle(char* name, int cap, bool& edited) {
    std::string n = g_path.empty() ? std::string("untitled") : std::filesystem::path(g_path).filename().string();
    edited = g_screen.dirty();
    if (n == g_lastTitle && edited == g_lastEdited) return false;
    g_lastTitle = n;
    g_lastEdited = edited;
    std::snprintf(name, (size_t)cap, "%s", n.c_str());
    return true;
}

float appScale() { return g_gfx.viewport().scale; }

bool appSavePng(const char* utf8Path) {
    appDrawFrame();
    return g_gfx.savePng(wide(utf8Path).c_str());
}
