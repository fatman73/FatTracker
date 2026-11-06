// Where a saved window comes back: whole, inside the work area of the monitor nearest to where it was
// (a monitor may be gone since), at least minW x minH and at most the work area. Plain values, no Win32 types,
// so it is testable and shared by the platforms.
#pragma once
#include <algorithm>

struct WinRect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

inline WinRect fitWindowRect(const WinRect& saved, const WinRect& work, int minW, int minH) {
    const int workW = work.right - work.left, workH = work.bottom - work.top;
    const int w = std::clamp(saved.right - saved.left, std::min(minW, workW), workW);
    const int h = std::clamp(saved.bottom - saved.top, std::min(minH, workH), workH);
    const int x = std::clamp(saved.left, work.left, work.right - w);
    const int y = std::clamp(saved.top, work.top, work.bottom - h);
    return {x, y, x + w, y + h};
}
