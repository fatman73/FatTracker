// FatTracker entry point: window, message loop, file loading, input routing.
#include <memory>
#include <vector>

#include <functional>

#include "app/document.h"
#include "audio/engine.h"
#include "formats/formats.h"
#include "formats/sample_io.h"
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
PlayScreen g_screen;
HWND g_hwnd = nullptr;
bool g_dirty = true;
bool g_fullscreen = false;
WINDOWPLACEMENT g_placement = {sizeof(WINDOWPLACEMENT)};
constexpr UINT_PTR kTimerAudio = 1, kTimerClock = 2;

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

bool loadModuleFile(const wchar_t* path) {
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path, data)) {
        g_screen.setStatus(err);
        return false;
    }
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
        return false;
    }
    const bool truncated = (r.value().loadFlags & LOAD_TRUNCATED) != 0;
    g_engine->setSong(std::make_unique<Song>(static_cast<Song&&>(r.value())));
    g_screen.songChanged();
    g_screen.setStatus(truncated ? "LOADED: FILE TRUNCATED" : "ALL RIGHT");
    g_path = path;
    g_screen.setCurrentPath(path);
    updateTitle();
    return true;
}

// Runs `then` right away, or after asking to save unsaved changes.
void confirmDiscard(HWND hwnd, std::function<void()> then);

bool saveAsDialog(HWND hwnd);

bool saveTo(const std::wstring& path, SaveKind kind) {
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    SaveResult r = saveSong(*g_engine, path, kind);
    g_screen.setStatus(r.message);
    if (r.ok && kind == SaveKind::Xm) {
        g_path = path;
        g_screen.editor().setClean();
    }
    if (r.ok && !r.warnings.empty()) {
        static char text[96];
        wsprintfA(text, "MOD SAVED. %d LOSSES, E.G.: %s", (int)r.warnings.size(), r.warnings[0]);
        g_screen.ask(text, {"OK"}, nullptr);
    }
    updateTitle();
    g_dirty = true;
    return r.ok;
}

bool saveCurrent(HWND hwnd) {
    if (!g_path.empty() && kindFromPath(g_path) == SaveKind::Xm) return saveTo(g_path, SaveKind::Xm);
    return saveAsDialog(hwnd);
}

bool saveAsDialog(HWND hwnd) {
    wchar_t file[MAX_PATH] = {};
    std::wstring def = g_path.empty() ? L"untitled.xm" : withExtension(g_path, L".xm");
    lstrcpynW(file, def.c_str(), MAX_PATH);
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"FastTracker 2 module (*.xm)\0*.xm\0ProTracker module (*.mod)\0*.mod\0WAV audio (*.wav)\0*.wav\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"xm";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
    const SaveKind kind = ofn.nFilterIndex == 2 ? SaveKind::Mod : ofn.nFilterIndex == 3 ? SaveKind::Wav : SaveKind::Xm;
    const wchar_t* ext = kind == SaveKind::Mod ? L".mod" : kind == SaveKind::Wav ? L".wav" : L".xm";
    std::wstring path = file;
    if (kindFromPath(path) != kind) path = withExtension(path, ext);
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
                     if (a == 0 && !saveCurrent(hwnd)) return;
                     then();
                 });
    g_dirty = true;
}

void openFileDialog(HWND hwnd) {
    confirmDiscard(hwnd, [hwnd]() {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {sizeof ofn};
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Modules (*.mod;*.xm;*.stk)\0*.mod;*.xm;*.stk;mod.*\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) loadModuleFile(file);
        g_dirty = true;
    });
}

void loadAnyFile(const std::wstring& path) {
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path.c_str(), data)) {
        g_screen.setStatus(err);
        return;
    }
    auto smp = loadSampleFile(data.data(), data.size());
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
    const std::wstring path = g_screen.actionPath();
    if (a == UiAction::LoadPath) loadAnyFile(path);
    if (a == UiAction::SaveXmPath) saveTo(path, SaveKind::Xm);
    if (a == UiAction::SaveModPath) saveTo(path, SaveKind::Mod);
    if (a == UiAction::SaveWavPath) saveTo(path, SaveKind::Wav);
    if (a == UiAction::SaveSamplePath) {
        std::vector<uint8_t> bytes;
        g_engine->lock();
        const Song* s = g_engine->song();
        const int idx = g_screen.currentSample();
        if (s && idx < (int)s->samples.size() && s->samples[idx].length) bytes = saveSampleWav(s->samples[idx]);
        g_engine->unlock();
        if (bytes.empty()) g_screen.setStatus("ERROR: THE SAMPLE IS EMPTY");
        else g_screen.setStatus(writeFileAtomic(path.c_str(), bytes) ? "SAMPLE SAVED (WAV)" : "ERROR: CANNOT WRITE FILE");
    }
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
        case WM_DPICHANGED: {
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
            if (wp == VK_F4) break;  // Alt+F4 closes
            [[fallthrough]];
        case WM_KEYDOWN: {
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
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            float lx, ly;
            deviceToLogical(g_gfx.viewport(), (float)pt.x, (float)pt.y, lx, ly);
            handle(g_screen.onWheel(lx, ly, GET_WHEEL_DELTA_WPARAM(wp), *g_engine), hwnd);
            return 0;
        }
        case WM_CHAR:
            handle(g_screen.onChar((wchar_t)wp, *g_engine), hwnd);
            return 0;
        case WM_RBUTTONDOWN:
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
            confirmDiscard(hwnd, [hwnd]() { DestroyWindow(hwnd); });
            return 0;
        case WM_DESTROY:
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
                if (!wcsncmp(tok, L"T:", 2)) {  // theme index
                    setCurrentTheme(_wtoi(tok + 2));
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"P:", 2)) {
                    const wchar_t* pn = tok + 2;
                    Panel p = !lstrcmpiW(pn, L"DISKOP") ? Panel::DiskOp : !lstrcmpiW(pn, L"SAMPLER") ? Panel::Sampler
                            : !lstrcmpiW(pn, L"EDITOP") ? Panel::EditOp : !lstrcmpiW(pn, L"SETUP") ? Panel::Setup : Panel::Help;
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
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"FatTrackerWnd";
    RegisterClassExW(&wc);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    int cw = screenW >= 2560 ? 1920 : 1600, ch = screenW >= 2560 ? 1080 : 900;
    RECT r = {0, 0, cw, ch};
    UINT dpi = GetDpiForSystem();
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, dpi);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"FatTracker", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                inst, nullptr);
    g_hwnd = hwnd;
    if (!hwnd || !g_gfx.init(hwnd)) {
        MessageBoxW(nullptr, L"Direct2D initialization failed.", L"FatTracker", MB_ICONERROR);
        return 1;
    }
    if (!g_engine->startAudio()) g_screen.setStatus(g_engine->audioStatus());
    g_screen.loadSettings(iniPath().c_str(), *g_engine);
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(g_screen.defaultChannels())));
    g_screen.songChanged();
    SetTimer(hwnd, kTimerAudio, 2000, nullptr);
    SetTimer(hwnd, kTimerClock, 1000, nullptr);
    {  // FatTracker.exe <module>  (also used by "Open with")
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2) loadModuleFile(argv[1]);
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
