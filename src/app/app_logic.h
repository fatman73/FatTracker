// The application's file and song logic, shared by Windows (src/app/main.cpp) and macOS (src/mac/app_glue.cpp):
// loading songs (modules, chip songs through chip_route, samples; Pure mode's floppy wait), saving and
// exporting, the unsaved-changes question, recording from the audio input, the drive sounds and the UI
// actions. Everything that belongs to the window system goes through AppShell.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "app/document.h"

class AudioCapture;
class Engine;
class PlayScreen;
enum class UiAction;

// What the app logic needs from the window system.
struct AppShell {
    virtual ~AppShell() = default;
    virtual void redraw() = 0;                                         // something changed: draw a frame
    virtual void setTitle(const std::wstring& fileName, bool dirty) = 0;  // "untitled" or the file name
    virtual void setBusy(bool busy) = 0;                               // busy cursor while the floppy reads
    virtual void startFloppyTimer(double seconds) = 0;                 // then call app::floppyTimer()
    virtual void stopFloppyTimer() = 0;
    // File dialogs. `done` gets the chosen file and is not called when the user cancels; it may run after
    // the call returns (a sheet on the Mac).
    virtual void openDialog(const std::wstring& startDir, std::function<void(const std::wstring&)> done) = 0;
    // `suggested`: full path or file name; `filter` 1 XM, 2 MOD, 3 WAV (preselected); `done` gets the path
    // and the type chosen in the dialog.
    virtual void saveDialog(const std::wstring& suggested, int filter,
                            std::function<void(const std::wstring&, SaveKind)> done) = 0;
};

namespace app {
// `replayDir`: the program's own Replay folder for SC68 / YMST external replays ("" = none).
void init(Engine& engine, PlayScreen& screen, AudioCapture& capture, AppShell& shell, const std::wstring& replayDir);
// Pure mode drive sounds supplied by the user: floppy_motor/step/insert.wav from the first folder that has
// each (folders end with a separator).
void loadFloppySounds(const std::vector<std::wstring>& dirs);

bool loadModuleFile(const std::wstring& path);  // a song; true also when it waits for the simulated floppy
void loadAnyFile(const std::wstring& path);      // dropped / DISK OP. file: a sample or a song (asks first)
bool loadPending();                              // Pure mode: a song waits for the floppy
void finishPendingLoad();                        // the floppy is done (timer), or a key / click skipped it
void floppyTimer();
void cancelPendingLoad();                        // closing during a load: the half-read song is dropped

void confirmDiscard(std::function<void()> then);  // `then` now, or after the SAVE / DISCARD / CANCEL question
void openFileDialog();
void saveCurrent(std::function<void(bool saved)> done = {});
void saveAsDialog(int filter = 1, std::function<void(bool saved)> done = {});  // 1 XM, 2 MOD, 3 WAV

void handle(UiAction a);   // what a UI event asked for
void updateTitle();
void timerTick(bool pollAudio);  // every timer: the recording's status; every 2 s also the audio device
const std::wstring& songPath();  // file of the current song ("" = untitled or playback only)
}  // namespace app
