// The C++ side of the macOS app (engine, screen, drawing), called from the AppKit code in main_mac.mm.
// Kept apart because AppKit headers (QuickDraw's `struct Pattern`) clash with the song model's names.
#pragma once
#include <cstdint>

typedef struct CGImage* CGImageRef;

int appMainStart(int argc, char** argv);        // engine, fonts; runs --shot (returns its exit code) or -1
void appWindowReady(const char* iniPath, const char* replayDir, bool audio = true);  // audio, settings, empty song
bool appOpenFile(const char* utf8Path, bool play);
void appPollAudio();                            // every 2 s
void appSetDirty();
bool appFrameDue();                             // something changed, or the screen animates
bool appDrawFrame();                            // draws the frame (false: nothing could be drawn)
void appResize(uint32_t pixelW, uint32_t pixelH);
CGImageRef appCopyFrame();                      // the last frame (caller releases it)
void appQuit(const char* iniPath);

// input; positions in device pixels from the view's top-left corner (like the Win32 client coordinates)
void appKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat);
void appKeyUp(unsigned vk);
void appChar(uint32_t c);
void appMouseDown(float px, float py, bool right);
void appMouseUp(float px, float py);
void appMouseMove(float px, float py, bool leftDown);
void appWheel(float px, float py, int delta);  // +-120 per notch, positive = up (WM_MOUSEWHEEL)
void appDropFile(const char* utf8Path);        // a module (asks about unsaved changes) or a sample
void appRequestOpen();                         // Cmd+O: asks about unsaved changes, then wants the open panel
bool appTakeOpenRequest(char* startDir, int cap);  // true once when the open panel should be shown
void appOpenChosen(const char* utf8Path);      // the file picked in the open panel
bool appTitle(char* name, int cap, bool& edited);  // window title parts; true when they changed
float appScale();                              // device pixels per logical unit (1920x1080 layout)
bool appSavePng(const char* utf8Path);         // draws the current frame and saves it (--input-test)
