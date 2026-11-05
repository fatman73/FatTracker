// FatTracker entry point: window, message loop, file loading, input routing.
#include <algorithm>
#include <memory>
#include <vector>

#include <functional>

#include "app/document.h"
#include "formats/writers.h"
#include "audio/engine.h"
#include "formats/formats.h"
#include "audio/capture.h"
#include "core/sample_edit.h"
#include "core/tfmx_player.h"
#include "formats/sample_io.h"
#include "ui/cursor.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/play_screen.h"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

namespace {
Gfx g_gfx;
Text g_text;
Font g_font;
Engine* g_engine = nullptr;
AudioCapture g_capture;  // sampler SAMPLE button: recording from the audio input
AmigaCursors g_cursors;
bool g_busy = false;
PlayScreen g_screen;
HWND g_hwnd = nullptr;
bool g_dirty = true;
bool g_fullscreen = false;
WINDOWPLACEMENT g_placement = {sizeof(WINDOWPLACEMENT)};
constexpr UINT_PTR kTimerAudio = 1, kTimerClock = 2, kTimerFloppy = 3;

void toggleFullscreen(HWND hwnd) {
    DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi = {sizeof mi};
        if (GetWindowPlacement(hwnd, &g_placement) &&
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_fullscreen = true;
        }
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_placement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
    g_dirty = true;
}

// Returns false when nothing could be drawn (minimized window, lost device).
bool drawFrame() {
    const Theme& t = currentTheme();
    if (!g_gfx.beginFrame(t.background)) return false;
    g_screen.draw(g_gfx, g_text, g_font, t, *g_engine);
    g_gfx.endFrame();
    return true;
}

std::wstring g_path;  // file of the current song ("" = untitled)

std::wstring iniPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    return p.substr(0, p.find_last_of(L'\\') + 1) + L"FatTracker.ini";
}

void updateTitle() {
    if (!g_hwnd) return;
    const wchar_t* name = g_path.empty() ? L"untitled" : g_path.c_str();
    for (const wchar_t* p = name; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    wchar_t title[300];
    wsprintfW(title, L"FatTracker \x2014 %s%s", name, g_screen.dirty() ? L" *" : L"");
    SetWindowTextW(g_hwnd, title);
}

std::unique_ptr<Song> g_pending;  // Pure mode: loaded song waiting for the floppy
std::wstring g_pendingPath;
bool g_pendingTruncated = false;
// Pure mode: a TFMX pair waiting for the floppy (it has its own player, so it is kept as files)
bool g_pendingTfmx = false;
std::vector<uint8_t> g_pendingMdat, g_pendingSmpl;
char g_pendingName[21] = {};
bool loadPending() { return g_pending || g_pendingTfmx; }
bool applyTfmx(const std::vector<uint8_t>& mdat, const std::vector<uint8_t>& smpl, const char* name,
               const std::wstring& mdatPath);
void applyLoaded(std::unique_ptr<Song> song, const std::wstring& path, bool truncated);
void finishPendingLoad();

// Size of a file on disk (0 if unknown) - drives the floppy write sound.
uint32_t fileSize(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    return GetFileAttributesExW(path, GetFileExInfoStandard, &a) ? a.nFileSizeLow : 0;
}

// Pure mode floppy sounds supplied by the user: sounds/floppy_*.wav next to the exe or in the working folder.
void loadFloppySounds() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring exeDir = std::wstring(exe).substr(0, std::wstring(exe).find_last_of(L'\\') + 1);
    const std::wstring dirs[2] = {exeDir + L"sounds\\", L"sounds\\"};
    static Sample snd[3];
    const Sample* use[3] = {};
    const wchar_t* names[3] = {L"floppy_motor.wav", L"floppy_step.wav", L"floppy_insert.wav"};
    for (int i = 0; i < 3; ++i)
        for (const std::wstring& d : dirs) {
            std::vector<uint8_t> data;
            if (readFileW((d + names[i]).c_str(), data)) continue;
            auto r = loadSampleFile(data.data(), data.size());
            if (!r.ok()) continue;
            snd[i] = static_cast<Sample&&>(r.value());
            use[i] = &snd[i];
            break;
        }
    g_engine->setFloppySounds(use[0], use[1], use[2]);  // copies (trimmed) what it uses
    for (Sample& s : snd) s = Sample{};
}

// TFMX songs are two files: mdat.<name> (song) and smpl.<name> (samples), or .mdat/.smpl, .tfx/.sam.
// Either one may be chosen; returns false when this is not TFMX at all.
bool loadTfmxPair(const std::wstring& path, std::vector<uint8_t>& data) {
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
    std::wstring mdatPath = path, smplPath;
    auto swapPrefix = [&](const wchar_t* from, const wchar_t* to) {
        return dir + to + file.substr(wcslen(from));
    };
    auto swapExt = [&](const std::wstring& p, size_t extLen, const wchar_t* to) { return p.substr(0, p.size() - extLen) + to; };
    auto hasSuffix = [&](const wchar_t* s) {
        const size_t n = wcslen(s);
        return file.size() > n && _wcsicmp(file.c_str() + file.size() - n, s) == 0;
    };
    if (!_wcsnicmp(file.c_str(), L"smpl.", 5)) mdatPath = swapPrefix(L"smpl.", L"mdat.");
    else if (hasSuffix(L".smpl")) mdatPath = swapExt(path, 5, L".mdat");
    else if (hasSuffix(L".sam")) mdatPath = swapExt(path, 4, L".tfx");
    if (mdatPath != path && readFileW(mdatPath.c_str(), data)) return false;
    if (!TfmxPlayer::detect(data.data(), data.size())) return false;
    const std::wstring mfile = mdatPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
    if (!_wcsnicmp(mfile.c_str(), L"mdat.", 5)) smplPath = dir + L"smpl." + mfile.substr(5);
    else if (mfile.size() > 5 && !_wcsicmp(mfile.c_str() + mfile.size() - 5, L".mdat")) smplPath = swapExt(mdatPath, 5, L".smpl");
    else if (mfile.size() > 4 && !_wcsicmp(mfile.c_str() + mfile.size() - 4, L".tfx")) smplPath = swapExt(mdatPath, 4, L".sam");
    std::vector<uint8_t> smpl;
    if (smplPath.empty() || readFileW(smplPath.c_str(), smpl)) {
        g_screen.setStatus("TFMX: SMPL FILE NOT FOUND NEXT TO MDAT");
        return true;
    }
    char name[21] = {};
    const std::wstring shown = !_wcsnicmp(mfile.c_str(), L"mdat.", 5) ? mfile.substr(5) : mfile;
    for (size_t i = 0; i < 20 && i < shown.size(); ++i) name[i] = shown[i] < 127 ? (char)towupper(shown[i]) : '_';
    // Pure mode: the drive reads both files first; the music waits for PLAY
    const double wait = g_engine->floppyRead((uint32_t)(data.size() + smpl.size()));
    if (wait > 0.05) {
        g_pendingTfmx = true;
        g_pendingMdat = data;
        g_pendingSmpl = static_cast<std::vector<uint8_t>&&>(smpl);
        std::memcpy(g_pendingName, name, sizeof g_pendingName);
        g_pendingPath = mdatPath;
        g_busy = true;
        SetCursor(g_cursors.busy());
        g_screen.setStatus("LOADING...");
        SetTimer(g_hwnd, kTimerFloppy, (UINT)(wait * 1000), nullptr);
        return true;
    }
    applyTfmx(data, smpl, name, mdatPath);
    return true;
}

bool applyTfmx(const std::vector<uint8_t>& mdat, const std::vector<uint8_t>& smpl, const char* name,
               const std::wstring& mdatPath) {
    if (!g_engine->loadTfmx(mdat.data(), mdat.size(), smpl.data(), smpl.size(), name, false)) {
        g_screen.setStatus("TFMX: CANNOT PLAY THIS FILE");
        return false;
    }
    g_screen.songChanged();
    static char msg[64];
    wsprintfA(msg, "%s - PLAYBACK ONLY", g_engine->tfmxFormat());
    g_screen.setStatus(msg);
    g_path.clear();  // nothing to save over
    g_screen.setCurrentPath(mdatPath);
    updateTitle();
    return true;
}

// The sampler's SAMPLE button: first press records, second press puts the recording in the slot.
void finishRecording() {
    const uint32_t rate = g_capture.rate();
    const bool lost = g_capture.lost(), full = g_capture.full();
    std::vector<float> rec = g_capture.take();
    g_screen.setRecording(false);
    if (rec.size() < rate / 50) {
        g_screen.setStatus("NOTHING RECORDED");
        return;
    }
    const double secs = (double)rec.size() / rate;
    g_screen.sampleLoaded(*g_engine, sampleFromRecording(rec.data(), rec.size(), rate), L"RECORDING");
    static char msg[48];
    wsprintfA(msg, lost ? "INPUT LOST - KEPT %d.%d S" : full ? "60 S LIMIT - RECORDED %d.%d S" : "RECORDED %d.%d S",
              (int)secs, (int)(secs * 10) % 10);
    g_screen.setStatus(msg);
}

void toggleRecording() {
    if (g_screen.recording()) return finishRecording();  // also right after the time limit stopped it
    if (!g_capture.start()) {
        g_screen.setStatus("NO AUDIO INPUT TO RECORD FROM");
        return;
    }
    g_screen.setRecording(true);
    g_screen.setStatus("RECORDING... PRESS SAMPLE TO STOP");
}

bool loadModuleFile(const wchar_t* path) {
    finishPendingLoad();  // a load in progress lands first
    g_engine->stop();     // a file was chosen: the music that plays now stops
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path, data)) {
        g_screen.setStatus(err);
        return false;
    }
    if (loadTfmxPair(path, data)) return true;
    auto r = loadModule(data.data(), data.size());
    if (!r.ok()) {
        static char msg[96];
        const wchar_t* name = path;
        for (const wchar_t* p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/') name = p + 1;
        if (!_wcsnicmp(name, L"smpl.", 5))
            wsprintfA(msg, "THIS IS A TFMX SAMPLE FILE - LOAD THE MDAT FILE");
        else
            wsprintfA(msg, "ERROR: %s", r.error());
        g_screen.setStatus(msg);
        g_engine->floppyDir();  // the drive still looked at it
        return false;
    }
    const bool truncated = (r.value().loadFlags & LOAD_TRUNCATED) != 0;
    auto song = std::make_unique<Song>(static_cast<Song&&>(r.value()));
    // Pure mode: the song arrives when the simulated floppy has read it (a key or click skips the wait)
    const double wait = g_engine->floppyRead((uint32_t)data.size());
    if (wait > 0.05) {
        g_pending = static_cast<std::unique_ptr<Song>&&>(song);
        g_pendingPath = path;
        g_pendingTruncated = truncated;
        g_busy = true;
        SetCursor(g_cursors.busy());
        g_screen.setStatus("LOADING...");
        SetTimer(g_hwnd, kTimerFloppy, (UINT)(wait * 1000), nullptr);
        return true;
    }
    applyLoaded(static_cast<std::unique_ptr<Song>&&>(song), path, truncated);
    return true;
}

void applyLoaded(std::unique_ptr<Song> song, const std::wstring& path, bool truncated) {
    g_engine->setSong(static_cast<std::unique_ptr<Song>&&>(song));
    g_screen.songChanged();
    g_screen.setStatus(truncated ? "LOADED: FILE TRUNCATED" : "ALL RIGHT");
    g_path = path;
    g_screen.setCurrentPath(path);
    updateTitle();
}

bool g_dropChar = false;  // the key that skipped a load must not also type

void finishPendingLoad() {
    KillTimer(g_hwnd, kTimerFloppy);
    if (!loadPending()) return;
    g_engine->floppyCancel();
    g_busy = false;
    SetCursor(g_cursors.arrow());
    if (g_pendingTfmx) {
        g_pendingTfmx = false;
        applyTfmx(g_pendingMdat, g_pendingSmpl, g_pendingName, g_pendingPath);
        g_pendingMdat.clear();
        g_pendingSmpl.clear();
    } else {
        applyLoaded(static_cast<std::unique_ptr<Song>&&>(g_pending), g_pendingPath, g_pendingTruncated);
        g_pending.reset();
    }
    g_dirty = true;
}

// Runs `then` right away, or after asking to save unsaved changes.
void confirmDiscard(HWND hwnd, std::function<void()> then);

bool saveAsDialog(HWND hwnd, DWORD filter = 1);  // 1 XM, 2 MOD, 3 WAV

bool saveTo(const std::wstring& path, SaveKind kind) {
    if (g_engine->tfmx()) {  // the TFMX display song is not the music: nothing to save
        g_screen.setStatus("TFMX: PLAYBACK ONLY - NOTHING TO SAVE");
        g_dirty = true;
        return false;
    }
    g_busy = true;
    SetCursor(g_cursors.busy());
    SaveResult r = saveSong(*g_engine, path, kind);
    if (r.ok) g_engine->floppyWrite(fileSize(path.c_str()));
    g_busy = false;
    SetCursor(g_cursors.arrow());
    g_screen.setStatus(r.message);
    if (r.ok && kind == SaveKind::Xm) {
        g_path = path;
        g_screen.editor().setClean();
    }
    updateTitle();
    g_dirty = true;
    return r.ok;
}

// MOD export first lists what the format cannot keep and asks; other kinds save directly.
void exportChecked(const std::wstring& path, SaveKind kind) {
    if (g_engine->tfmx()) {  // playback only: saveTo explains
        saveTo(path, kind);
        return;
    }
    if (kind == SaveKind::Mod) {
        g_engine->lock();
        const ModExport m = exportMod(*g_engine->song());
        g_engine->unlock();
        if (!m.warnings.empty()) {
            static char text[160];
            wsprintfA(text, "MOD CANNOT KEEP %d FEATURE(S), E.G.: %s", (int)m.warnings.size(), m.warnings[0]);
            g_screen.ask(text, {"EXPORT", "CANCEL"}, [path](int a) {
                if (a == 0) saveTo(path, SaveKind::Mod);
            });
            g_dirty = true;
            return;
        }
    }
    saveTo(path, kind);
}

// DISK OP. saves: ask before replacing an existing file.
void saveFromDiskOp(const std::wstring& path, SaveKind kind) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return exportChecked(path, kind);
    g_screen.ask("FILE EXISTS. OVERWRITE?", {"OVERWRITE", "CANCEL"}, [path, kind](int a) {
        if (a == 0) exportChecked(path, kind);
    });
    g_dirty = true;
}

bool saveCurrent(HWND hwnd) {
    if (!g_path.empty() && kindFromPath(g_path) == SaveKind::Xm) return saveTo(g_path, SaveKind::Xm);
    return saveAsDialog(hwnd);
}

bool saveAsDialog(HWND hwnd, DWORD filter) {
    if (g_engine->tfmx()) return saveTo(L"", SaveKind::Xm);  // says why nothing is saved
    wchar_t file[MAX_PATH] = {};
    std::wstring def = g_path.empty() ? L"untitled.xm" : withExtension(g_path, L".xm");
    lstrcpynW(file, def.c_str(), MAX_PATH);
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"FastTracker 2 module (*.xm)\0*.xm\0ProTracker module (*.mod)\0*.mod\0WAV audio (*.wav)\0*.wav\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"xm";
    ofn.nFilterIndex = filter;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
    const SaveKind kind = ofn.nFilterIndex == 2 ? SaveKind::Mod : ofn.nFilterIndex == 3 ? SaveKind::Wav : SaveKind::Xm;
    const wchar_t* ext = kind == SaveKind::Mod ? L".mod" : kind == SaveKind::Wav ? L".wav" : L".xm";
    std::wstring path = file;
    if (kindFromPath(path) != kind) path = withExtension(path, ext);
    if (kind != SaveKind::Xm) {  // exports never make the song clean
        exportChecked(path, kind);
        return false;
    }
    return saveTo(path, kind);
}

void confirmDiscard(HWND hwnd, std::function<void()> then) {
    if (!g_screen.dirty()) {
        then();
        return;
    }
    g_screen.ask("THE SONG HAS UNSAVED CHANGES", {"SAVE", "DISCARD", "CANCEL"},
                 [hwnd, then](int a) {
                     if (a == 2) return;
                     if (a == 0 && (!saveCurrent(hwnd) || g_screen.dirty())) return;
                     then();
                 });
    g_dirty = true;
}

void openFileDialog(HWND hwnd) {
    confirmDiscard(hwnd, [hwnd]() {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {sizeof ofn};
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Modules\0*.mod;*.xm;*.s3m;*.it;*.mtm;*.669;*.stk;mod.*;*.mmd0;*.mmd1;*.mmd2;*.mmd3;*.med;*.dbm;*.digi;*.symmod;mdat.*;*.mdat;*.tfx\0"
                          L"All files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        wchar_t dir[MAX_PATH];
        GetFullPathNameW(g_screen.browseDir().c_str(), MAX_PATH, dir, nullptr);
        if (!g_screen.browseDir().empty() && g_screen.browseDir() != L"::drives") ofn.lpstrInitialDir = dir;
        if (GetOpenFileNameW(&ofn)) {
            loadModuleFile(file);
            g_screen.openPanel(Panel::Main);
        }
        g_dirty = true;
    });
}

void loadAnyFile(const std::wstring& path) {
    finishPendingLoad();
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path.c_str(), data)) {
        g_screen.setStatus(err);
        return;
    }
    const bool sampleSig = data.size() >= 12 && ((!memcmp(data.data(), "RIFF", 4) && !memcmp(data.data() + 8, "WAVE", 4)) ||
                                                  (!memcmp(data.data(), "FORM", 4) && !memcmp(data.data() + 8, "8SVX", 4)));
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
    confirmDiscard(g_hwnd, [path]() { loadModuleFile(path.c_str()); });
}

void handle(UiAction a, HWND hwnd) {
    if (a == UiAction::OpenFile) openFileDialog(hwnd);
    if (a == UiAction::SaveFile) saveCurrent(hwnd);
    if (a == UiAction::ExportWav) saveAsDialog(hwnd, 3);
    const std::wstring path = g_screen.actionPath();
    if (a == UiAction::LoadPath) loadAnyFile(path);
    if (a == UiAction::SaveXmPath) saveFromDiskOp(path, SaveKind::Xm);
    if (a == UiAction::SaveModPath) saveFromDiskOp(path, SaveKind::Mod);
    if (a == UiAction::SaveWavPath) saveFromDiskOp(path, SaveKind::Wav);
    if (a == UiAction::SaveSamplePath) {
        auto save = [path]() {
            std::vector<uint8_t> bytes;
            g_engine->lock();
            const Song* s = g_engine->song();
            const int idx = s ? g_screen.curSampleIndex(*s) : -1;
            if (idx >= 0 && s->samples[idx].length) bytes = saveSampleWav(s->samples[idx]);
            g_engine->unlock();
            if (bytes.empty()) g_screen.setStatus("ERROR: THE SAMPLE IS EMPTY");
            else if (!writeFileAtomic(path.c_str(), bytes)) g_screen.setStatus("ERROR: CANNOT WRITE FILE");
            else {
                g_screen.setStatus("SAMPLE SAVED (WAV)");
                g_engine->floppyWrite((uint32_t)bytes.size());
            }
            g_dirty = true;
        };
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) save();
        else g_screen.ask("FILE EXISTS. OVERWRITE?", {"OVERWRITE", "CANCEL"}, [save](int r) { if (r == 0) save(); });
    }
    if (g_screen.takeDirRead()) g_engine->floppyDir();
    if (g_screen.takeRecordRequest()) toggleRecording();
    if (a != UiAction::None) {
        g_dirty = true;
        updateTitle();
    }
}
void mouse(HWND hwnd, LPARAM lp, bool right) {
    float lx, ly;
    deviceToLogical(g_gfx.viewport(), (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), lx, ly);
    if (!right) SetCapture(hwnd);
    handle(g_screen.onMouse(lx, ly, right, *g_engine), hwnd);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            g_gfx.resize(LOWORD(lp), HIWORD(lp));
            g_dirty = true;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(g_busy ? g_cursors.busy() : g_cursors.arrow());
                return TRUE;
            }
            break;
        case WM_DPICHANGED: {
            g_cursors.build(HIWORD(wp));
            const RECT* r = (const RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN && (lp & (1 << 29))) {
                toggleFullscreen(hwnd);
                return 0;
            }
            [[fallthrough]];  // Alt+F4 copies the pattern like in ProTracker; close with the window button
        case WM_KEYDOWN: {
            g_dropChar = false;
            if (loadPending()) {  // any key skips the simulated floppy read (and types nothing)
                finishPendingLoad();
                g_dropChar = true;
                return 0;
            }
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            const bool alt = (lp & (1 << 29)) != 0, ext = (lp & (1 << 24)) != 0;
            const bool repeat = (lp & (1 << 30)) != 0;  // keyboard auto-repeat
            if (ctrl && !alt && wp == 'O') {
                if (!repeat) openFileDialog(hwnd);
                return 0;
            }
            handle(g_screen.onKey((unsigned)wp, shift, ctrl, alt, ext, repeat, *g_engine), hwnd);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            handle(g_screen.onKeyUp((unsigned)wp, *g_engine), hwnd);
            break;
        case WM_SYSCHAR:
            return 0;  // no menu beep on Alt combinations
        case WM_LBUTTONDOWN:
            if (loadPending()) {
                finishPendingLoad();
                return 0;
            }
            mouse(hwnd, lp, false);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            float lx, ly;
            deviceToLogical(g_gfx.viewport(), (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), lx, ly);
            handle(g_screen.onMouseUp(lx, ly, *g_engine), hwnd);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (loadPending()) {
                finishPendingLoad();
                return 0;
            }
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            float lx, ly;
            deviceToLogical(g_gfx.viewport(), (float)pt.x, (float)pt.y, lx, ly);
            handle(g_screen.onWheel(lx, ly, GET_WHEEL_DELTA_WPARAM(wp), *g_engine), hwnd);
            return 0;
        }
        case WM_CHAR:
            if (g_dropChar) {
                g_dropChar = false;
                return 0;
            }
            handle(g_screen.onChar((wchar_t)wp, *g_engine), hwnd);
            return 0;
        case WM_RBUTTONDOWN:
            if (loadPending()) {
                finishPendingLoad();
                return 0;
            }
            mouse(hwnd, lp, true);
            return 0;
        case WM_MOUSEMOVE: {
            float lx, ly;
            deviceToLogical(g_gfx.viewport(), (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), lx, ly);
            if (g_screen.onMouseMove(lx, ly, (wp & MK_LBUTTON) != 0, *g_engine)) g_dirty = true;
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t file[MAX_PATH];
            if (DragQueryFileW(drop, 0, file, MAX_PATH)) {
                std::wstring path = file;
                loadAnyFile(path);
            }
            DragFinish(drop);
            g_dirty = true;
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerFloppy) finishPendingLoad();
            if (g_screen.recording()) {  // recording: show time and level, stop at the limit
                if (!g_capture.running()) {
                    finishRecording();
                } else {
                    static char recMsg[64];
                    const int lvl = (int)(g_capture.level() * 20);
                    char bar[21] = {};
                    for (int i = 0; i < 20; ++i) bar[i] = i < lvl ? '#' : '.';
                    wsprintfA(recMsg, "RECORDING %d S  %s", (int)g_capture.seconds(), bar);
                    g_screen.setStatus(recMsg);
                }
            }
            if (wp == kTimerAudio) {
                g_engine->pollAudio();
                if (const char* st = g_engine->audioStatus())
                    g_screen.setStatus(st);
                else
                    g_screen.clearStatusIf("ERROR: NO AUDIO");
            }
            g_dirty = true;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            g_dirty = true;
            return 0;
        case WM_CLOSE:
            if (loadPending()) {  // closing during a simulated load: the half-read song is dropped
                KillTimer(hwnd, kTimerFloppy);
                g_pending.reset();
                g_pendingTfmx = false;
                g_busy = false;
            }
            confirmDiscard(hwnd, [hwnd]() { DestroyWindow(hwnd); });
            return 0;
        case WM_DESTROY:
            g_capture.stop();
            g_screen.saveSettings(iniPath().c_str());
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Dev tool: FatTracker.exe --shot <file.png> [width height [module [seconds [view]]]]
// renders one frame offscreen (optionally after playing a module silently).
int screenshotMode() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = -1;
    if (argv && argc >= 3 && lstrcmpW(argv[1], L"--cursor") == 0) {  // dev: cursor artwork preview
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        result = AmigaCursors::savePng(argv[2], 64) ? 0 : 1;
    }
    if (argv && argc >= 3 && lstrcmpW(argv[1], L"--shot") == 0) {
        uint32_t w = argc >= 5 ? (uint32_t)_wtoi(argv[3]) : 1920;
        uint32_t h = argc >= 5 ? (uint32_t)_wtoi(argv[4]) : 1080;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        result = 1;
        const bool gfxOk = w && h && g_gfx.initOffscreen(w, h);  // one device for the whole run
        if (argc >= 6 && loadModuleFile(argv[5])) {
            g_engine->play();
            const int seconds10 = argc >= 7 ? (int)(_wtof(argv[6]) * 10) : 15;
            std::vector<float> buf(4800 * 2);
            for (int i = 0; i < seconds10; ++i) g_engine->render(buf.data(), 4800);
        }
        if (argc >= 8) g_screen.setViewMode(_wtoi(argv[7]));
        if (argc >= 9) {  // simulated key presses, e.g. "SPACE DOWN Z X S+DOWN"
            drawFrame();  // establishes the layout
            struct Named {
                const wchar_t* name;
                unsigned vk;
            } names[] = {{L"SPACE", VK_SPACE}, {L"UP", VK_UP},       {L"DOWN", VK_DOWN},   {L"LEFT", VK_LEFT},
                         {L"RIGHT", VK_RIGHT}, {L"TAB", VK_TAB},     {L"DEL", VK_DELETE},  {L"INS", VK_INSERT},
                         {L"BACK", VK_BACK},   {L"ESC", VK_ESCAPE},  {L"F1", VK_F1},       {L"F2", VK_F2},
                         {L"F3", VK_F3},       {L"F4", VK_F4},       {L"F5", VK_F5},       {L"STOP", 0xFFFF}};
            wchar_t* ctx = nullptr;
            for (wchar_t* tok = wcstok_s(argv[8], L" ", &ctx); tok; tok = wcstok_s(nullptr, L" ", &ctx)) {
                bool shift = false, ctrl = false, alt = false;
                for (;;) {
                    if (!wcsncmp(tok, L"S+", 2)) shift = true;
                    else if (!wcsncmp(tok, L"C+", 2)) ctrl = true;
                    else if (!wcsncmp(tok, L"A+", 2)) alt = true;
                    else break;
                    tok += 2;
                }
                if (!wcsncmp(tok, L"NAME:", 5)) {  // start editing the song (1) or sample (2) name
                    g_screen.startNameEdit(_wtoi(tok + 5), *g_engine);
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"TXT:", 4)) {  // type characters ('_' = space)
                    for (const wchar_t* p = tok + 4; *p; ++p) g_screen.onChar(*p == L'_' ? L' ' : *p, *g_engine);
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"FXT:", 4)) {  // logo box demo time in seconds
                    g_screen.setDemoTime(_wtof(tok + 4));
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"FX:", 3)) {  // logo box demo effect
                    g_screen.setDemoEffect(_wtoi(tok + 3));
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"PURE:", 5)) {  // Pure mode 0/1/2
                    g_screen.setPure(_wtoi(tok + 5), *g_engine);
                    std::vector<float> pb(4800 * 2);
                    for (int i = 0; i < 5; ++i) g_engine->render(pb.data(), 4800);
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"OP:", 3)) {  // Pure EDIT OP. screen 0..2
                    g_screen.setEditOpScreen(_wtoi(tok + 3));
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"T:", 2)) {  // theme index
                    setCurrentTheme(_wtoi(tok + 2));
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"P:", 2)) {
                    const wchar_t* pn = tok + 2;
                    Panel p = !lstrcmpiW(pn, L"DISKOP") ? Panel::DiskOp : !lstrcmpiW(pn, L"SAMPLER") ? Panel::Sampler
                            : !lstrcmpiW(pn, L"EDITOP") ? Panel::EditOp : !lstrcmpiW(pn, L"SETUP") ? Panel::Setup
                            : !lstrcmpiW(pn, L"POSED") ? Panel::PosEd : Panel::Help;
                    g_screen.openPanel(p);
                    drawFrame();
                    continue;
                }
                unsigned vk = tok[1] ? 0 : (unsigned)towupper(tok[0]);
                for (const Named& n : names)
                    if (!lstrcmpiW(tok, n.name)) vk = n.vk;
                if (vk == 0xFFFF) g_engine->stop();
                else if (vk) g_screen.onKey(vk, shift, ctrl, alt, false, false, *g_engine);
                drawFrame();  // refresh the screen's view of the engine between keys
            }
        }
        if (gfxOk) {
            drawFrame();
            result = g_gfx.savePng(argv[2]) ? 0 : 1;
        }
    }
    if (argv) LocalFree(argv);
    return result;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_engine = new Engine();
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(4)));
    g_text.init();
    g_font.setText(&g_text);
    if (int shot = screenshotMode(); shot >= 0) return shot;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = {sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = nullptr;  // Amiga cursor set in WM_SETCURSOR
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"FatTrackerWnd";
    RegisterClassExW(&wc);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    int cw = screenW >= 2560 ? 1920 : 1600, ch = screenW >= 2560 ? 1080 : 900;
    RECT r = {0, 0, cw, ch};
    UINT dpi = GetDpiForSystem();
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, dpi);
    // keep the whole window inside the work area (the taskbar must not cover its bottom)
    RECT wa = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int maxW = wa.right - wa.left, maxH = wa.bottom - wa.top;
    const int ww = std::min((int)(r.right - r.left), maxW), wh = std::min((int)(r.bottom - r.top), maxH);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"FatTracker", WS_OVERLAPPEDWINDOW,
                                wa.left + (maxW - ww) / 2, wa.top + (maxH - wh) / 2, ww, wh, nullptr, nullptr,
                                inst, nullptr);
    g_hwnd = hwnd;
    if (hwnd) g_cursors.build(GetDpiForWindow(hwnd));
    if (!hwnd || !g_gfx.init(hwnd)) {
        MessageBoxW(nullptr, L"Direct2D initialization failed.", L"FatTracker", MB_ICONERROR);
        return 1;
    }
    if (!g_engine->startAudio()) g_screen.setStatus(g_engine->audioStatus());
    loadFloppySounds();
    g_screen.loadSettings(iniPath().c_str(), *g_engine);
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(g_screen.defaultChannels())));
    g_screen.songChanged();
    SetTimer(hwnd, kTimerAudio, 2000, nullptr);
    SetTimer(hwnd, kTimerClock, 1000, nullptr);
    {  // FatTracker.exe <module>  (also used by "Open with")
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // a module named on the command line starts at once, unless Pure mode is still "reading" it
        if (argv && argc >= 2 && loadModuleFile(argv[1]) && !loadPending()) g_engine->play();
        if (argv) LocalFree(argv);
    }
    ShowWindow(hwnd, show);

    MSG m;
    for (;;) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                delete g_engine;
                return (int)m.wParam;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (g_dirty || g_screen.animating()) {
            g_dirty = false;
            // Present() waits for vsync and paces the loop. When nothing can be drawn
            // (minimized, device lost) there is no vsync, so sleep instead of spinning.
            if (IsIconic(hwnd) || !drawFrame())
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
        } else {
            WaitMessage();
        }
    }
}
