// Main screen: ProTracker-style layout (counters, buttons, scopes, info bar, pattern) that
// stretches to fill the whole window, with a modern finish, plus the pattern editor.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "audio/engine.h"
#include "core/edit.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/pattern_view.h"
#include "ui/demo_fx.h"
#include "ui/pixel_canvas.h"
#include <memory>
#include "ui/text.h"

enum class UiAction {
    None,
    Redraw,
    OpenFile,     // Ctrl+O: system open dialog
    SaveFile,     // Ctrl+S
    LoadPath,     // load actionPath() (module or sample)
    SaveXmPath,   // save to actionPath()
    SaveModPath,
    SaveWavPath,
    SaveSamplePath,
    ExportWav,    // MOD2WAV in Pure mode: save the song as WAV
    Quit
};

enum class Panel { Main, DiskOp, EditOp, Setup, Sampler, Help };

struct DirEntry {
    std::wstring name;
    bool dir;
    uint64_t size;
};

// Modal question with up to four answers (drawn over the pattern area).
struct Dialog {
    bool open = false;
    char text[96] = {};
    const char* buttons[4] = {};
    int count = 0;
    std::function<void(int)> onAnswer;  // index of the clicked button
};

class PlayScreen {
public:
    void draw(Gfx& g, Text& tx, Font& f, const Theme& t, Engine& e);
    void setStatus(const char* msg);
    void clearStatusIf(const char* prefix);  // resets to ALL RIGHT if the status starts with prefix
    UiAction onKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat, Engine& e);
    UiAction onKeyUp(unsigned vk, Engine& e);
    UiAction onMouse(float lx, float ly, bool right, Engine& e);
    UiAction onMouseUp(float lx, float ly, Engine& e);
    bool onMouseMove(float lx, float ly, bool leftDown, Engine& e);  // true when a redraw is needed
    UiAction onWheel(float lx, float ly, int delta, Engine& e);
    UiAction onChar(wchar_t c, Engine& e);
    const std::wstring& actionPath() const { return actionPath_; }
    void setCurrentPath(const std::wstring& p);  // file of the song, used by DISK OP.
    const std::wstring& browseDir() const { return dir_; }
    void sampleLoaded(Engine& e, Sample&& s, const std::wstring& fileName);
    Panel panel() const { return panel_; }
    void openPanel(Panel p);
    int currentSample() const { return sample_; }
    int curSampleIndex(const Song& s, int note = 49) const;  // global sample for the SAMPLE counter
    void clampView(const Song& s);
    void applyMetronome(Engine& e);
    bool animating() const { return animating_; }
    void songChanged();
    void setViewMode(int m);  // 4, 8, 16, 32 columns or 0 = as many as the song has channels
    int viewCount(int numChannels) const;
    // editing state shared with the app shell
    Editor& editor() { return editor_; }
    bool dirty() const { return editor_.dirty(); }
    void ask(const char* text, std::initializer_list<const char*> buttons, std::function<void(int)> onAnswer);

private:
    struct Layout {
        float W, H;
        RectF counters, buttons, logo, scopes, scopeGrid, clock, info, pattern, side;
    };
    Layout layout(const Viewport& vp) const;
    RectF counterRow(int i) const;
    RectF buttonRect(int i) const;
    RectF viewButton(int i) const;
    RectF sideButton(int i) const;
    RectF sideArrow(int i) const;  // 0/1 pattern -/+, 2/3 bpm -/+
    RectF dialogButton(int i) const;
    int hitId(float x, float y) const;
    PatternCtx ctx(const Song* s, const Telemetry& tel) const;
    void toggleMute(int ch, Engine& e);
    void solo(int ch, Engine& e);
    void counterStep(int row, int dir, bool big, Engine& e);
    UiAction pressButton(int b, Engine& e);  // main button grid (B_PLAY ...)
    void bpmStep(int d, Engine& e);
    // Pure mode screen (pure_screen.cpp)
    void pureDraw(Gfx& g, Text& tx, const Theme& t, Engine& e);
    UiAction pureMouse(float x, float y, bool right, Engine& e);
    void pureGeometry(float& ox, float& oy, float& sx, float& sy) const;  // device pixels
    void pureToAmiga(float lx, float ly, int& ax, int& ay) const;          // logical -> Amiga pixel
    int pureFirstChannel(int numChannels);
    std::unique_ptr<PixelCanvas> canvas_;
    DemoFx demo_;  // effects in the logo box (click = next)
    double demoTime_ = -1;
    int pureFirst_ = 0;
    float vpScale_ = 1.f;
    static constexpr int kPureExitHover = 7000;
    void drawDialog(Gfx& g, Text& tx, const Theme& t);
    // play_edit.cpp
    UiAction editKey(unsigned vk, bool shift, bool ctrl, bool alt, bool repeat, Engine& e);
    void sideAction(int i, Engine& e);
    void moveCursor(int rows, int cols, Engine& e);
    void ensureCursorVisible(const Song& s);
    int editRow() const;      // row the editor writes to (playing row while recording)
    int editPattern() const;
    Block currentBlock(bool wholeTrackIfNone) const;
    void advance(Engine& e);  // after an entry: move down by the edit skip

    Layout L_{};
    int viewMode_ = 8;
    int firstChannel_ = 0;
    int viewRow_ = 0;
    int viewPattern_ = 0;
    int viewOrder_ = 0;
    int sample_ = 0;
    int hover_ = -1;
    char status_[64] = "DROP A MODULE HERE OR CLICK DISK OP.";
    bool animating_ = false;
    Telemetry last_{};
    PatternView pattern_;
    Font* font_ = nullptr;
    float scopeBuf_[256];
    // editor
    Editor editor_;
    Clip clip_;
    bool editMode_ = false;
    bool recordMode_ = false;
    bool scopePattern_ = false;  // side panel operations act on the whole pattern (else track)
    int curCh_ = 0, curCol_ = 0;
    int octave_ = 4;
    int editSkip_ = 1;
    bool blockOn_ = false;
    int blockRow_ = 0, blockCh_ = 0;  // anchor of the selection
    unsigned jamKey_ = 0;
    Dialog dialog_;

    // ---- panels (disk_op.cpp, sampler.cpp, edit_op.cpp, help.cpp)
    Panel panel_ = Panel::Main;
    RectF panelArea() const;    // top band (DISK OP., EDIT OP., SETUP)
    RectF bottomArea() const;   // pattern + side (SAMPLER, HELP)
    std::wstring actionPath_;
    // DISK OP.
    std::wstring dir_;
    std::wstring fileName_ = L"untitled";
    std::vector<DirEntry> entries_;
    int diskSel_ = 0, diskScroll_ = 0;
    int diskFilter_ = 0;  // 0 modules, 1 samples, 2 all
    void diskRefresh();
    void diskDraw(Gfx& g, Text& tx, const Theme& t);
    UiAction diskMouse(float x, float y, bool right, Engine& e);
    UiAction diskKey(unsigned vk, bool ctrl, Engine& e);
    UiAction diskActivate(int index, Engine& e);
    int diskRows() const;
    RectF diskButton(int i) const;
    RectF diskList() const;
    RectF diskNameField() const;
    // SAMPLER
    uint32_t viewStart_ = 0, viewEnd_ = 0;  // visible range of the waveform
    uint32_t selA_ = 0, selB_ = 0;
    int drag_ = 0;  // 0 none, 1 selecting, 2 loop start, 3 loop end
    Sample sampleClip_;
    Sample sampleUndo_;
    int sampleUndoIndex_ = -1;
    void samplerDraw(Gfx& g, Text& tx, const Theme& t, Engine& e);
    UiAction samplerMouse(float x, float y, bool right, Engine& e);
    UiAction samplerKey(unsigned vk, bool shift, bool ctrl, Engine& e);
    void samplerOp(int op, Engine& e);
    RectF samplerWave() const;
    RectF samplerButton(int i) const;
    uint32_t xToSample(float x) const;
    float sampleToX(uint32_t i) const;
    // EDIT OP.
    int quantize_ = 1;
    bool metronome_ = false;
    int metroEvery_ = 4;
    int transposeScope_ = 0;  // 0 track, 1 pattern, 2 song, 3 block
    bool transposeOnlySample_ = false;
    int exchangeTo_ = 2;
    void editOpDraw(Gfx& g, Text& tx, const Theme& t);
    UiAction editOpMouse(float x, float y, bool right, Engine& e);
    RectF editOpCell(int row, int col) const;
    int editOpHit(float x, float y) const;
    // HELP
    int helpScroll_ = 0;
    void helpDraw(Gfx& g, Text& tx, const Theme& t);
    // SETUP (setup.cpp)
    void setupDraw(Gfx& g, Text& tx, const Theme& t);
    UiAction setupMouse(float x, float y, bool right, Engine& e);
    RectF setupRect(int id) const;
    int interp_ = 2, separationPct_ = 100, gainPct_ = 100, defaultChannels_ = 4;
    int pure_ = 0, drivePct_ = 70, themeBeforePure_ = 0;  // Pure mode: 0 off, 1 A500, 2 A1200
    bool dirRead_ = false;
    int selColor_ = 0;
    int patternRowsCache_ = 64;

public:
    void applyMixing(Engine& e);
    void loadSettings(const wchar_t* ini, Engine& e);
    void saveSettings(const wchar_t* ini) const;
    int defaultChannels() const { return defaultChannels_; }
    void setPure(int mode, Engine& e);  // switches the classic theme too
    void setDemoEffect(int fx) { demo_.setEffect(fx); }  // logo box effect (dev screenshots)
    void setDemoTime(double s) { demoTime_ = s; }       // fixed demo time (dev screenshots), < 0 = clock
    bool takeDirRead() { const bool r = dirRead_; dirRead_ = false; return r; }  // DISK OP. listed a folder
};
