// FatTracker entry point: window, message loop, fullscreen toggle.
#include "ui/gfx.h"

#include <shellapi.h>

namespace {
Gfx g_gfx;
bool g_dirty = true;
bool g_fullscreen = false;
WINDOWPLACEMENT g_placement = {sizeof(WINDOWPLACEMENT)};

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
    g_gfx.bevel({760, 440, 400, 200}, t, false);
    g_gfx.endFrame();
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
            break;
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
}  // namespace

// Dev tool: FatTracker.exe --shot <file.png> [width height] renders one frame offscreen.
static int screenshotMode() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = -1;
    if (argv && argc >= 3 && lstrcmpW(argv[1], L"--shot") == 0) {
        uint32_t w = argc >= 5 ? (uint32_t)_wtoi(argv[3]) : 1920;
        uint32_t h = argc >= 5 ? (uint32_t)_wtoi(argv[4]) : 1080;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        result = 1;
        if (w && h && g_gfx.initOffscreen(w, h)) {
            drawFrame();
            result = g_gfx.savePng(argv[2]) ? 0 : 1;
        }
    }
    if (argv) LocalFree(argv);
    return result;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    if (int shot = screenshotMode(); shot >= 0) return shot;
    WNDCLASSEXW wc = {sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FatTrackerWnd";
    RegisterClassExW(&wc);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    int cw = screenW >= 2560 ? 1920 : 1600, ch = screenW >= 2560 ? 1080 : 900;
    RECT r = {0, 0, cw, ch};
    UINT dpi = GetDpiForSystem();
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"FatTracker", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd || !g_gfx.init(hwnd)) {
        MessageBoxW(nullptr, L"Direct2D initialization failed.", L"FatTracker", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, show);

    MSG m;
    for (;;) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) return (int)m.wParam;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (g_dirty) {
            g_dirty = false;
            drawFrame();
        } else {
            WaitMessage();
        }
    }
}
