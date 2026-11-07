// The C++ side of the macOS app (engine, screen, drawing, the shared app logic), called from the AppKit code
// in main_mac.mm. Kept apart because AppKit headers (QuickDraw's `struct Pattern`) clash with the song
// model's names. MacHost carries the AppKit side of AppShell (src/app/app_logic.h) the other way.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

typedef struct CGImage* CGImageRef;
typedef struct CGColorSpace* CGColorSpaceRef;

// AppKit services the app logic asks for (paths are UTF-8). Dialog callbacks are not called on cancel.
struct MacHost {
    std::function<void(const char* fileName, bool dirty)> setTitle;
    std::function<void(bool busy)> setBusy;
    std::function<void(double seconds)> startFloppyTimer;  // then appFloppyTimer()
    std::function<void()> stopFloppyTimer;
    std::function<void(const char* startDir, std::function<void(const char* path)> done)> openDialog;
    // filter 1 XM, 2 MOD, 3 WAV; `done` gets the path and the chosen filter
    std::function<void(const char* dir, const char* name, int filter, std::function<void(const char* path, int filter)> done)>
        saveDialog;
};

int appMainStart(int argc, char** argv);  // engine, fonts; runs --shot (returns its exit code) or -1
void appSetHost(const MacHost& host);
// audio (unless `audio` is false), settings, drive sounds (first folder that has each file), empty song
void appWindowReady(const char* iniPath, const char* replayDir, const std::vector<std::string>& soundDirs,
                    bool audio = true);
bool appOpenFile(const char* utf8Path, bool play);  // command line / Finder: load, then play unless it waits
void appPollAudio();                               // every 2 s
void appClockTick();                               // every second (clock, recording status)
void appFloppyTimer();                             // the simulated floppy finished reading
void appSetDirty();
bool appFrameDue();                                // something changed, or the screen animates
bool appDrawFrame();                               // draws the frame (false: nothing could be drawn)
void appResize(uint32_t pixelW, uint32_t pixelH);
void appSetColorSpace(CGColorSpaceRef space);    // the window's: frames need no conversion on the way to the screen
CGImageRef appCopyFrame();                         // the last frame (caller releases it)
// After a logo-only frame: the redrawn part in bitmap pixels and a copy of it (else false / nullptr)
bool appFramePartial(int* x, int* y, int* w, int* h);
CGImageRef appCopyPartialFrame();
void appQuit(const char* iniPath);

// input; positions in device pixels from the view's top-left corner (like the Win32 client coordinates).
// appKey / appMouseDown / appWheel return true when the event only skipped a simulated floppy load.
bool appKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat);
void appKeyUp(unsigned vk);
void appChar(uint32_t c);
bool appMouseDown(float px, float py, bool right);
void appMouseUp(float px, float py);
void appMouseMove(float px, float py, bool leftDown);
bool appWheel(float px, float py, int delta);  // +-120 per notch, positive = up (WM_MOUSEWHEEL)
void appDropFile(const char* utf8Path);        // a module (asks about unsaved changes) or a sample
void appRequestOpen();                         // File > Open: unsaved-changes question, then the open panel
// Closing the window / quitting: drops a half-read load, asks about unsaved changes, then runs `then`.
void appConfirmClose(std::function<void()> then);
bool appHasUnsavedChanges();
bool appBusy();                                // the busy cursor is shown
float appScale();                              // device pixels per logical unit (1920x1080 layout)
// --input-test: a logo-only frame against a full frame at the same demo time; differing pixels (-1: not possible)
int appCheckPartialFrame();
void appSetDemoTime(double seconds);  // --input-test: the logo demo at a fixed time (-1: the clock again)
bool appSavePng(const char* utf8Path);         // draws the current frame and saves it (--input-test)
