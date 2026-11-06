// FatTracker entry point (Windows): window, message loop, input routing. The file and song logic is shared
// with macOS in src/app/app_logic.cpp; WinShell gives it the Win32 dialogs, timer, cursor and title.
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <functional>
#include <memory>
#include <vector>

#include "app/app_logic.h"
#include "audio/capture.h"
#include "audio/engine.h"
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

std::wstring iniPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    return p.substr(0, p.find_last_of(L'\\') + 1) + L"FatTracker.ini";
}

std::wstring exeDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::wstring(exe).substr(0, std::wstring(exe).find_last_of(L'\\') + 1);
}

// The window system for the shared app logic (src/app/app_logic.cpp): the Win32 calls main.cpp made itself before.
struct WinShell : AppShell {
    void redraw() override { g_dirty = true; }
    void setTitle(const std::wstring& name, bool dirty) override {
        if (!g_hwnd) return;
        wchar_t title[300];
        wsprintfW(title, L"FatTracker \x2014 %s%s", name.c_str(), dirty ? L" *" : L"");
        SetWindowTextW(g_hwnd, title);
    }
    void setBusy(bool busy) override {
        g_busy = busy;
        SetCursor(busy ? g_cursors.busy() : g_cursors.arrow());
    }
    void startFloppyTimer(double seconds) override { SetTimer(g_hwnd, kTimerFloppy, (UINT)(seconds * 1000), nullptr); }
    void stopFloppyTimer() override { KillTimer(g_hwnd, kTimerFloppy); }
    void openDialog(const std::wstring& startDir, std::function<void(const std::wstring&)> done) override {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = {sizeof ofn};
        ofn.hwndOwner = g_hwnd;
        ofn.lpstrFilter = L"Modules\0*.mod;*.xm;*.s3m;*.it;*.mtm;*.669;*.fc;*.fc13;*.fc14;*.smod;fc.*;smod.*;*.hip;*.hipc;*.hip7;hip.*;hipc.*;hip7.*;*.sog;sog.*;*.mcmd;mcmd.*;*.coso;coso.*;*.sid;*.psid;*.rsid;*.sndh;sndh.*;*.ahx;*.thx;*.hvl;ahx.*;thx.*;hvl.*;*.ym;*.vtx;*.psg;*.jam;jam.*;*.spc;*.vgm;*.vgz;*.gym;*.sap;*.ay;*.nsf;*.nsfe;*.gbs;*.hes;*.kss;*.dm;*.dm1;*.dm2;dm.*;dm1.*;dm2.*;*.sid1;*.sid2;*.sd1;*.sd2;*.smn;sid.*;sid1.*;sid2.*;smn.*;*.bp;*.bp2;*.bp3;*.sndmon;bp.*;bp2.*;bp3.*;sndmon.*;*.dmu;*.dmu2;*.mug;*.mug2;dmu.*;dmu2.*;mug.*;mug2.*;*.pt3;pt3.*;*.dw;*.dwold;dw.*;dwold.*;*.smus;smus.*;*.pt2;*.stc;*.st11;*.st13;*.asc;*.sqt;*.sc68;*.ymst;*.sa;*.sonic;sa.*;sonic.*;*.aon;*.aon4;*.aon8;aon.*;aon4.*;aon8.*;*.stk;mod.*;*.mmd0;*.mmd1;*.mmd2;*.mmd3;*.med;*.dbm;*.digi;*.symmod;mdat.*;*.mdat;*.tfx\0"
                          L"All files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        wchar_t dir[MAX_PATH];
        GetFullPathNameW(startDir.c_str(), MAX_PATH, dir, nullptr);
        if (!startDir.empty()) ofn.lpstrInitialDir = dir;
        if (GetOpenFileNameW(&ofn)) done(file);
    }
    void saveDialog(const std::wstring& suggested, int filter,
                    std::function<void(const std::wstring&, SaveKind)> done) override {
        wchar_t file[MAX_PATH] = {};
        lstrcpynW(file, suggested.c_str(), MAX_PATH);
        OPENFILENAMEW ofn = {sizeof ofn};
        ofn.hwndOwner = g_hwnd;
        ofn.lpstrFilter = L"FastTracker 2 module (*.xm)\0*.xm\0ProTracker module (*.mod)\0*.mod\0WAV audio (*.wav)\0*.wav\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"xm";
        ofn.nFilterIndex = (DWORD)filter;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) return;
        done(file, ofn.nFilterIndex == 2 ? SaveKind::Mod : ofn.nFilterIndex == 3 ? SaveKind::Wav : SaveKind::Xm);
    }
};
WinShell g_shell;

bool g_dropChar = false;  // the key that skipped a load must not also type

void mouse(HWND hwnd, LPARAM lp, bool right) {
    float lx, ly;
    deviceToLogical(g_gfx.viewport(), (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), lx, ly);
    if (!right) SetCapture(hwnd);
    app::handle(g_screen.onMouse(lx, ly, right, *g_engine));
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
            if (app::loadPending()) {  // any key skips the simulated floppy read (and types nothing)
                app::finishPendingLoad();
                g_dropChar = true;
                return 0;
            }
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            const bool alt = (lp & (1 << 29)) != 0, ext = (lp & (1 << 24)) != 0;
            const bool repeat = (lp & (1 << 30)) != 0;  // keyboard auto-repeat
            if (ctrl && !alt && wp == 'O') {
                if (!repeat) app::openFileDialog();
                return 0;
            }
            app::handle(g_screen.onKey((unsigned)wp, shift, ctrl, alt, ext, repeat, *g_engine));
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            app::handle(g_screen.onKeyUp((unsigned)wp, *g_engine));
            break;
        case WM_SYSCHAR:
            return 0;  // no menu beep on Alt combinations
        case WM_LBUTTONDOWN:
            if (app::loadPending()) {
                app::finishPendingLoad();
                return 0;
            }
            mouse(hwnd, lp, false);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            float lx, ly;
            deviceToLogical(g_gfx.viewport(), (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), lx, ly);
            app::handle(g_screen.onMouseUp(lx, ly, *g_engine));
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (app::loadPending()) {
                app::finishPendingLoad();
                return 0;
            }
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            float lx, ly;
            deviceToLogical(g_gfx.viewport(), (float)pt.x, (float)pt.y, lx, ly);
            app::handle(g_screen.onWheel(lx, ly, GET_WHEEL_DELTA_WPARAM(wp), *g_engine));
            return 0;
        }
        case WM_CHAR:
            if (g_dropChar) {
                g_dropChar = false;
                return 0;
            }
            app::handle(g_screen.onChar((wchar_t)wp, *g_engine));
            return 0;
        case WM_RBUTTONDOWN:
            if (app::loadPending()) {
                app::finishPendingLoad();
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
                app::loadAnyFile(path);
            }
            DragFinish(drop);
            g_dirty = true;
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerFloppy) app::floppyTimer();
            app::timerTick(wp == kTimerAudio);  // recording status; the audio device every 2 s
            g_dirty = true;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            g_dirty = true;
            return 0;
        case WM_CLOSE:
            app::cancelPendingLoad();  // closing during a simulated load: the half-read song is dropped
            app::confirmDiscard([hwnd]() { DestroyWindow(hwnd); });
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
        if (argc >= 6 && app::loadModuleFile(argv[5])) {
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
                if (!lstrcmpW(tok, L"REC") || !lstrcmpW(tok, L"RECMODE")) {  // RECORD button (RECMODE: right click)
                    g_screen.pressRecord(tok[3] != 0, *g_engine);
                    drawFrame();
                    continue;
                }
                if (!wcsncmp(tok, L"CLICK:", 6) || !wcsncmp(tok, L"RCLICK:", 7) || !wcsncmp(tok, L"DRAG:", 5)) {
                    // mouse in logical units: CLICK:x,y  RCLICK:x,y  DRAG:x0,y0,x1,y1 (press, move in steps, release)
                    float v[4] = {};
                    const wchar_t* q = wcschr(tok, L':') + 1;
                    for (int i = 0; i < 4 && q; ++i) {
                        v[i] = (float)_wtof(q);
                        q = wcschr(q, L',');
                        if (q) ++q;
                    }
                    g_screen.click(v[0], v[1], tok[0] == L'R', *g_engine);
                    if (tok[0] == L'D') {
                        for (int i = 1; i <= 8; ++i)
                            g_screen.onMouseMove(v[0] + (v[2] - v[0]) * i / 8, v[1] + (v[3] - v[1]) * i / 8, true, *g_engine);
                        g_screen.onMouseUp(v[2], v[3], *g_engine);
                    } else {
                        g_screen.onMouseUp(v[0], v[1], *g_engine);
                    }
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
                            : !lstrcmpiW(pn, L"POSED") ? Panel::PosEd : !lstrcmpiW(pn, L"INSTR") ? Panel::Instrument
                            : Panel::Help;
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
    app::init(*g_engine, g_screen, g_capture, g_shell, exeDir() + L"Replay");
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
    app::loadFloppySounds({exeDir() + L"sounds\\", L"sounds\\"});
    g_screen.loadSettings(iniPath().c_str(), *g_engine);
    g_engine->setSong(std::make_unique<Song>(makeEmptySong(g_screen.defaultChannels())));
    g_screen.songChanged();
    SetTimer(hwnd, kTimerAudio, 2000, nullptr);
    SetTimer(hwnd, kTimerClock, 1000, nullptr);
    {  // FatTracker.exe <module>  (also used by "Open with")
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        // a module named on the command line starts at once, unless Pure mode is still "reading" it
        if (argv && argc >= 2 && app::loadModuleFile(argv[1]) && !app::loadPending()) g_engine->play();
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
