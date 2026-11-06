#include "mac/app_glue.h"

#include <filesystem>
#include <memory>
#include <string>

#include "app/app_logic.h"
#include "app/shot.h"
#include "audio/capture.h"
#include "audio/engine.h"
#include "core/song.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/play_screen.h"
#include "ui/text.h"
#include "ui/theme.h"

namespace {
Engine* g_engine = nullptr;
AudioCapture* g_capture = nullptr;
Gfx g_gfx;
Text g_text;
Font g_font;
PlayScreen g_screen;
MacHost g_host;
bool g_dirty = true;
bool g_busy = false;

std::wstring wide(const char* utf8) { return std::filesystem::path(utf8).wstring(); }
std::string utf8(const std::wstring& w) { return std::filesystem::path(w).string(); }

// AppShell for the shared app logic: AppKit through MacHost (empty during --shot: no window)
struct MacShell : AppShell {
    void redraw() override { g_dirty = true; }
    void setTitle(const std::wstring& name, bool dirty) override {
        if (g_host.setTitle) g_host.setTitle(utf8(name).c_str(), dirty);
    }
    void setBusy(bool busy) override {
        g_busy = busy;
        if (g_host.setBusy) g_host.setBusy(busy);
    }
    void startFloppyTimer(double seconds) override {
        if (g_host.startFloppyTimer) g_host.startFloppyTimer(seconds);
    }
    void stopFloppyTimer() override {
        if (g_host.stopFloppyTimer) g_host.stopFloppyTimer();
    }
    void openDialog(const std::wstring& startDir, std::function<void(const std::wstring&)> done) override {
        if (!g_host.openDialog) return;
        g_host.openDialog(utf8(startDir).c_str(), [done](const char* path) {
            done(wide(path));
            g_dirty = true;
        });
    }
    void saveDialog(const std::wstring& suggested, int filter,
                    std::function<void(const std::wstring&, SaveKind)> done) override {
        if (!g_host.saveDialog) return;
        const std::filesystem::path p(suggested);
        g_host.saveDialog(p.has_parent_path() ? p.parent_path().string().c_str() : "", p.filename().string().c_str(),
                          filter, [done](const char* path, int f) {
                              done(wide(path), f == 2 ? SaveKind::Mod : f == 3 ? SaveKind::Wav : SaveKind::Xm);
                              g_dirty = true;
                          });
    }
};
MacShell g_shell;

void toLogical(float px, float py, float& lx, float& ly) { deviceToLogical(g_gfx.viewport(), px, py, lx, ly); }

// a key, click or wheel during Pure mode's simulated floppy read only ends the wait (main.cpp)
bool skipPendingLoad() {
    if (!app::loadPending()) return false;
    app::finishPendingLoad();
    g_dirty = true;
    return true;
}
}  // namespace

int appMainStart(int argc, char** argv) {
    g_engine = new Engine();
    g_capture = new AudioCapture();
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(4)));
    g_text.init();
    g_font.setText(&g_text);
    // --shot from the build folder: SC68 replays next to the binary
    std::error_code ec;
    const std::wstring replay = (std::filesystem::absolute(argv[0], ec).parent_path() / "Replay").wstring();
    app::init(*g_engine, g_screen, *g_capture, g_shell, replay);
    return runShot(argc, argv, g_gfx, g_text, g_font, g_screen, *g_engine, app::loadModuleFile);
}

void appSetHost(const MacHost& host) { g_host = host; }

void appWindowReady(const char* iniPath, const char* replayDir, const std::vector<std::string>& soundDirs, bool audio) {
    app::init(*g_engine, g_screen, *g_capture, g_shell, wide(replayDir));
    if (audio && !g_engine->startAudio()) g_screen.setStatus(g_engine->audioStatus());
    std::vector<std::wstring> dirs;
    for (const std::string& d : soundDirs) dirs.push_back(wide(d.c_str()));
    app::loadFloppySounds(dirs);
    g_screen.loadSettings(wide(iniPath).c_str(), *g_engine);
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(g_screen.defaultChannels())));
    g_screen.songChanged();
    app::updateTitle();
    g_dirty = true;
}

bool appOpenFile(const char* utf8Path, bool play) {
    const std::wstring path = wide(utf8Path);
    app::confirmDiscard([path, play]() {  // a file opened from the Finder while a song has unsaved changes
        if (app::loadModuleFile(path) && play && !app::loadPending()) g_engine->play();
    });
    g_dirty = true;
    return true;
}

void appPollAudio() { app::timerTick(true); }

void appClockTick() { app::timerTick(false); }

void appFloppyTimer() {
    app::floppyTimer();
    app::timerTick(false);
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
    g_capture->stop();
    g_screen.saveSettings(wide(iniPath).c_str());
    g_engine->stop();
    delete g_capture;
    g_capture = nullptr;
    delete g_engine;
    g_engine = nullptr;
}

bool appKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat) {
    if (skipPendingLoad()) return true;  // any key skips the simulated floppy read (and types nothing)
    if (ctrl && !alt && vk == 'O') {     // Ctrl+O (Control or Command): the open panel, like main.cpp
        if (!repeat) app::openFileDialog();
        return false;
    }
    app::handle(g_screen.onKey(vk, shift, ctrl, alt, extended, repeat, *g_engine));
    return false;
}

void appKeyUp(unsigned vk) { app::handle(g_screen.onKeyUp(vk, *g_engine)); }

void appChar(uint32_t c) {
    if (c > 0xFFFF) return;
    app::handle(g_screen.onChar((wchar_t)c, *g_engine));
}

bool appMouseDown(float px, float py, bool right) {
    if (skipPendingLoad()) return true;
    float lx, ly;
    toLogical(px, py, lx, ly);
    app::handle(g_screen.onMouse(lx, ly, right, *g_engine));
    return false;
}

void appMouseUp(float px, float py) {
    float lx, ly;
    toLogical(px, py, lx, ly);
    app::handle(g_screen.onMouseUp(lx, ly, *g_engine));
}

void appMouseMove(float px, float py, bool leftDown) {
    float lx, ly;
    toLogical(px, py, lx, ly);
    if (g_screen.onMouseMove(lx, ly, leftDown, *g_engine)) g_dirty = true;
}

bool appWheel(float px, float py, int delta) {
    if (skipPendingLoad()) return true;
    float lx, ly;
    toLogical(px, py, lx, ly);
    app::handle(g_screen.onWheel(lx, ly, delta, *g_engine));
    return false;
}

void appDropFile(const char* utf8Path) {
    app::loadAnyFile(wide(utf8Path));
    g_dirty = true;
}

void appRequestOpen() {
    app::openFileDialog();
    g_dirty = true;
}

void appConfirmClose(std::function<void()> then) {
    app::cancelPendingLoad();  // closing during a simulated load: the half-read song is dropped
    app::confirmDiscard(then);
    g_dirty = true;
}

bool appHasUnsavedChanges() { return g_screen.dirty(); }

bool appBusy() { return g_busy; }

float appScale() { return g_gfx.viewport().scale; }

bool appSavePng(const char* utf8Path) {
    appDrawFrame();
    return g_gfx.savePng(wide(utf8Path).c_str());
}
