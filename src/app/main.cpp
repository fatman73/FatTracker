// FatTracker entry point: window, message loop, file loading, input routing.
#include <memory>
#include <vector>

#include "audio/engine.h"
#include "formats/formats.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/play_screen.h"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

namespace {
Gfx g_gfx;
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

void drawFrame() {
    const Theme& t = themeHybrid();
    if (!g_gfx.beginFrame(t.background)) return;
    g_screen.draw(g_gfx, g_font, t, *g_engine);
    g_gfx.endFrame();
}

// Reads a whole file (max 256 MB). Returns an error string or nullptr.
const char* readFileW(const wchar_t* path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "ERROR: CANNOT OPEN FILE";
    LARGE_INTEGER sz;
    const char* err = nullptr;
    if (!GetFileSizeEx(h, &sz)) err = "ERROR: CANNOT READ FILE";
    else if (sz.QuadPart > 256ll * 1024 * 1024) err = "ERROR: FILE TOO BIG";
    if (!err) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        if (!out.empty() && (!ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) || got != out.size()))
            err = "ERROR: CANNOT READ FILE";
    }
    CloseHandle(h);
    return err;
}

bool loadModuleFile(const wchar_t* path) {
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path, data)) {
        g_screen.setStatus(err);
        return false;
    }
    auto r = loadModule(data.data(), data.size());
    if (!r.ok()) {
        static char msg[64];
        wsprintfA(msg, "ERROR: %s", r.error());
        g_screen.setStatus(msg);
        return false;
    }
    const bool truncated = (r.value().loadFlags & LOAD_TRUNCATED) != 0;
    g_engine->setSong(std::make_unique<Song>(static_cast<Song&&>(r.value())));
    g_screen.songChanged();
    g_screen.setStatus(truncated ? "LOADED: FILE TRUNCATED" : "ALL RIGHT");
    if (g_hwnd) {
        const wchar_t* name = path;
        for (const wchar_t* p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/') name = p + 1;
        wchar_t title[300];
        wsprintfW(title, L"FatTracker \x2014 %s", name);
        SetWindowTextW(g_hwnd, title);
    }
    return true;
}

void openFileDialog(HWND hwnd) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {sizeof ofn};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Modules (*.mod;*.xm;*.stk)\0*.mod;*.xm;*.stk;mod.*\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) loadModuleFile(file);
    g_dirty = true;
}

void mouse(LPARAM lp, bool right) {
    float lx, ly;
    deviceToLogical(g_gfx.viewport(), (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), lx, ly);
    if (g_screen.onMouse(lx, ly, right, *g_engine)) g_dirty = true;
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
            if (ctrl && !alt && wp == 'O') {
                openFileDialog(hwnd);
                return 0;
            }
            if (g_screen.onKey((unsigned)wp, shift, ctrl, alt, ext, *g_engine)) g_dirty = true;
            return 0;
        }
        case WM_SYSCHAR:
            return 0;  // no menu beep on Alt combinations
        case WM_LBUTTONDOWN:
            mouse(lp, false);
            return 0;
        case WM_RBUTTONDOWN:
            mouse(lp, true);
            return 0;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t file[MAX_PATH];
            if (DragQueryFileW(drop, 0, file, MAX_PATH)) loadModuleFile(file);
            DragFinish(drop);
            g_dirty = true;
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerAudio) {
                g_engine->pollAudio();
                if (const char* st = g_engine->audioStatus()) g_screen.setStatus(st);
            }
            g_dirty = true;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            g_dirty = true;
            return 0;
        case WM_DESTROY:
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
        if (argc >= 6 && loadModuleFile(argv[5])) {
            g_engine->play();
            const int seconds10 = argc >= 7 ? (int)(_wtof(argv[6]) * 10) : 15;
            std::vector<float> buf(4800 * 2);
            for (int i = 0; i < seconds10; ++i) g_engine->render(buf.data(), 4800);
        }
        if (argc >= 8) g_screen.setViewMode(_wtoi(argv[7]));
        if (w && h && g_gfx.initOffscreen(w, h)) {
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
    SetTimer(hwnd, kTimerAudio, 2000, nullptr);
    SetTimer(hwnd, kTimerClock, 1000, nullptr);
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
            drawFrame();  // Present() waits for vsync, pacing the loop
        } else {
            WaitMessage();
        }
    }
}
