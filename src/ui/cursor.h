// Amiga-style Windows cursors built from cursor_art at the window's DPI.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class AmigaCursors {
public:
    ~AmigaCursors() { release(); }
    void build(UINT dpi);  // (re)creates both cursors for the given DPI
    HCURSOR arrow() const { return arrow_ ? arrow_ : LoadCursorW(nullptr, IDC_ARROW); }
    HCURSOR busy() const { return busy_ ? busy_ : LoadCursorW(nullptr, IDC_WAIT); }
    static bool savePng(const wchar_t* path, int size);  // dev: preview of the artwork

private:
    void release();
    HCURSOR arrow_ = nullptr, busy_ = nullptr;
};
