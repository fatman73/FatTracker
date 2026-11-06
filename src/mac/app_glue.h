// The C++ side of the macOS app (engine, screen, drawing), called from the AppKit code in main_mac.mm.
// Kept apart because AppKit headers (QuickDraw's `struct Pattern`) clash with the song model's names.
#pragma once
#include <cstdint>

typedef struct CGImage* CGImageRef;

int appMainStart(int argc, char** argv);        // engine, fonts; runs --shot (returns its exit code) or -1
void appWindowReady(const char* iniPath, const char* replayDir);  // audio, settings, empty song
bool appOpenFile(const char* utf8Path, bool play);
void appPollAudio();                            // every 2 s
void appSetDirty();
bool appFrameDue();                             // something changed, or the screen animates
bool appDrawFrame();                            // draws the frame (false: nothing could be drawn)
void appResize(uint32_t pixelW, uint32_t pixelH);
CGImageRef appCopyFrame();                      // the last frame (caller releases it)
void appQuit(const char* iniPath);
