// DISK OP.: ProTracker-style file browser in the top band.
#include <algorithm>
#include <cstdio>

#include "ui/play_screen.h"
#include "ui/widgets.h"

namespace {
enum {
    D_MODULES, D_SAMPLES, D_ALL, D_PARENT, D_HOME, D_DRIVES,
    D_LOAD, D_SAVEXM, D_EXPMOD, D_EXPWAV, D_SAVESMP, D_RENAME, D_DELETE, D_EXIT, D_COUNT
};
const char* const kDiskButtons[D_COUNT] = {"MODULES", "SAMPLES", "ALL FILES", "PARENT",     "HOME",
                                          "DRIVES",  "LOAD",    "SAVE XM",   "EXPORT MOD", "EXPORT WAV",
                                          "SAVE SAMPLE", "RENAME", "DELETE", "EXIT"};
constexpr float kRowH = 26.f;

bool hasExt(const std::wstring& n, std::initializer_list<const wchar_t*> exts) {
    const size_t dot = n.find_last_of(L'.');
    std::wstring e = dot == std::wstring::npos ? L"" : n.substr(dot);
    for (auto& c : e) c = (wchar_t)towlower(c);
    std::wstring low = n;
    for (auto& c : low) c = (wchar_t)towlower(c);
    for (const wchar_t* x : exts) {
        if (e == x) return true;
        // Amiga style prefixes: mod.name, mdat.name
        const std::wstring pre = std::wstring(x + 1) + L".";
        if (low.compare(0, pre.size(), pre) == 0) return true;
    }
    return false;
}

std::string narrow(const std::wstring& w, size_t max) {
    std::string s;
    for (wchar_t c : w) {
        if (s.size() >= max) break;
        s += (c >= 32 && c < 127) ? (char)c : '?';
    }
    return s;
}
}  // namespace

RectF PlayScreen::panelArea() const { return {L_.counters.x, L_.counters.y, L_.scopes.x - 6 - L_.counters.x, L_.counters.h}; }

RectF PlayScreen::bottomArea() const {
    return {L_.pattern.x, L_.pattern.y, L_.side.x + L_.side.w - L_.pattern.x, L_.pattern.h};
}

// New file name for RENAME: the typed name keeps the file's kind - its extension, or the Amiga
// prefix ("mod.", "mdat.", "smpl.") - unless the typed name already ends in a known extension.
std::wstring renameTarget(const std::wstring& from, std::wstring typed) {
    while (!typed.empty() && (typed.back() == L'.' || typed.back() == L' ')) typed.pop_back();
    while (!typed.empty() && typed.front() == L' ') typed.erase(typed.begin());
    if (typed.empty()) return typed;
    static const wchar_t* const kPrefixes[] = {L"mod.", L"mdat.", L"smpl."};
    for (const wchar_t* p : kPrefixes) {
        const size_t n = wcslen(p);
        if (from.size() > n && !_wcsnicmp(from.c_str(), p, n))
            return _wcsnicmp(typed.c_str(), p, n) ? from.substr(0, n) + typed : typed;
    }
    static const wchar_t* const kExts[] = {L".mod", L".xm", L".s3m", L".it", L".mtm", L".669", L".med", L".mmd0",
                                           L".mmd1", L".mmd2", L".mmd3", L".dbm", L".digi", L".symmod", L".wav",
                                           L".iff", L".8svx", L".aiff", L".mdat", L".smpl", L".tfx", L".sam"};
    for (const wchar_t* x : kExts) {
        const size_t n = wcslen(x);
        if (typed.size() > n && !_wcsicmp(typed.c_str() + typed.size() - n, x)) return typed;
    }
    const size_t dot = from.find_last_of(L'.');
    return dot == std::wstring::npos ? typed : typed + from.substr(dot);
}

RectF PlayScreen::diskButton(int i) const {
    const RectF a = panelArea();
    const float w = 170, h = 36;
    return {a.x + 10 + (i % 2) * (w + 6), a.y + 10 + (i / 2) * (h + 6), w, h};
}

RectF PlayScreen::diskNameField() const {
    const RectF a = panelArea();
    const float x = a.x + 370;
    return {x + 80, a.y + a.h - 44, a.w - 380 - 90, 34};
}

RectF PlayScreen::diskList() const {
    const RectF a = panelArea();
    return {a.x + 370, a.y + 50, a.w - 380, a.h - 50 - 54};
}

int PlayScreen::diskRows() const { return (int)((diskList().h - 8) / kRowH); }

void PlayScreen::setCurrentPath(const std::wstring& p) {
    const size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        dir_ = p.substr(0, slash);
        std::wstring name = p.substr(slash + 1);
        const size_t dot = name.find_last_of(L'.');
        fileName_ = dot == std::wstring::npos ? name : name.substr(0, dot);
    }
}

void PlayScreen::diskRefresh() {
    dirRead_ = true;
    entries_.clear();
    if (dir_.empty()) {
        wchar_t buf[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, buf);
        dir_ = buf;
    }
    if (dir_ == L"::drives") {
        const DWORD mask = GetLogicalDrives();
        for (int d = 0; d < 26; ++d)
            if (mask & (1u << d)) entries_.push_back({std::wstring(1, (wchar_t)(L'A' + d)) + L":\\", true, 0});
    } else {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir_ + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
                const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (!isDir) {
                    const bool mod = hasExt(n, {L".mod", L".xm", L".stk", L".s3m", L".it", L".mdat", L".mmd0",
                                                L".mmd1", L".mmd2", L".mmd3", L".med", L".symmod", L".dbm",
                                                L".digi", L".tfx"}) ||
                                     !_wcsnicmp(n.c_str(), L"mdat.", 5) || !_wcsnicmp(n.c_str(), L"mod.", 4);
                    const bool smp = hasExt(n, {L".wav", L".iff", L".8svx", L".aiff"});
                    if (diskFilter_ == 0 && !mod) continue;
                    if (diskFilter_ == 1 && !smp) continue;
                }
                entries_.push_back({n, isDir, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(entries_.begin(), entries_.end(), [](const DirEntry& a, const DirEntry& b) {
            if (a.dir != b.dir) return a.dir;
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
    }
    diskSel_ = std::min(diskSel_, (int)entries_.size() - 1);
    if (diskSel_ < 0) diskSel_ = 0;
    diskScroll_ = 0;
}

void PlayScreen::diskDraw(Gfx& g, Text& tx, const Theme& t) {
    const RectF a = panelArea();
    Ui u{g, tx, t};
    g.panel(a, t, Gfx::PanelState::Normal);
    for (int i = 0; i < D_COUNT; ++i) {
        Gfx::PanelState st = hover_ == 600 + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal;
        if (i == diskFilter_) st = Gfx::PanelState::Active;
        drawButton(u, diskButton(i), kDiskButtons[i], st, 15.f);
    }
    // path
    RectF path = {a.x + 370, a.y + 10, a.w - 380, 32};
    g.well(path, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, path, narrow(dir_ == L"::drives" ? L"DRIVES" : dir_, 90).c_str(), 15.f, t.digits, Align::Left,
            FontRole::Mono);
    // list
    const RectF list = diskList();
    g.well(list, t, t.patternBg);
    const int rows = diskRows();
    if (diskSel_ < diskScroll_) diskScroll_ = diskSel_;
    if (diskSel_ >= diskScroll_ + rows) diskScroll_ = diskSel_ - rows + 1;
    g.pushClip(list);
    for (int r = 0; r < rows; ++r) {
        const int i = diskScroll_ + r;
        if (i >= (int)entries_.size()) break;
        const DirEntry& d = entries_[i];
        RectF row = {list.x + 4, list.y + 4 + r * kRowH, list.w - 8, kRowH};
        if (i == diskSel_) g.fillRoundRect(row, 3.f, withAlpha(t.rowBar, 0.9f));
        const Color c = i == diskSel_ ? t.rowBarText : (d.dir ? t.patternFx : t.patternText);
        std::string name = narrow(d.name, 70);
        if (d.dir) name = "[" + name + "]";
        tx.draw(g, row, name.c_str(), 15.f, c, Align::Left, FontRole::Mono);
        if (!d.dir) {
            char sz[24];
            std::snprintf(sz, sizeof sz, "%llu", (unsigned long long)d.size);
            tx.draw(g, row, sz, 15.f, c, Align::Right, FontRole::Mono);
        }
    }
    if (entries_.empty()) tx.draw(g, list, "NO MATCHING FILES", 16.f, t.rowNumber, Align::Center);
    g.popClip();
    // file name for saving
    tx.draw(g, {a.x + 370, a.y + a.h - 44, 80, 34}, "NAME", 15.f, t.label, Align::Left);
    const RectF nf = diskNameField();
    g.well(nf, t, mixColor(t.panelLight, t.panel, 0.25f));
    std::string fname = narrow(fileName_, 60) + "_";
    tx.draw(g, nf, fname.c_str(), 16.f, t.digits, Align::Left, FontRole::Mono);
}

UiAction PlayScreen::diskActivate(int index, Engine&) {
    if (index < 0 || index >= (int)entries_.size()) return UiAction::None;
    const DirEntry d = entries_[index];
    if (d.dir) {
        dir_ = dir_ == L"::drives" ? d.name.substr(0, 2) : dir_ + L"\\" + d.name;
        if (dir_.size() == 2) dir_ += L"";  // "C:"
        diskRefresh();
        return UiAction::Redraw;
    }
    actionPath_ = dir_ + L"\\" + d.name;
    return UiAction::LoadPath;
}

UiAction PlayScreen::diskMouse(float x, float y, bool, Engine& e) {
    for (int i = 0; i < D_COUNT; ++i) {
        if (!diskButton(i).contains(x, y)) continue;
        auto target = [&](const wchar_t* ext) { return dir_ + L"\\" + fileName_ + ext; };
        switch (i) {
            case D_MODULES:
            case D_SAMPLES:
            case D_ALL:
                diskFilter_ = i;
                diskRefresh();
                return UiAction::Redraw;
            case D_PARENT: {
                const size_t slash = dir_.find_last_of(L"\\/");
                if (dir_ == L"::drives") return UiAction::None;
                if (slash == std::wstring::npos || dir_.size() <= 3)
                    dir_ = L"::drives";
                else
                    dir_ = dir_.substr(0, slash);
                if (dir_.size() == 2) dir_ += L"\\";
                diskRefresh();
                return UiAction::Redraw;
            }
            case D_HOME: {
                wchar_t buf[MAX_PATH];
                GetModuleFileNameW(nullptr, buf, MAX_PATH);
                dir_ = buf;
                dir_ = dir_.substr(0, dir_.find_last_of(L'\\'));
                diskRefresh();
                return UiAction::Redraw;
            }
            case D_DRIVES:
                dir_ = L"::drives";
                diskRefresh();
                return UiAction::Redraw;
            case D_LOAD:  // file requester, opened in the folder being browsed
                return UiAction::OpenFile;
            case D_SAVEXM:
                actionPath_ = target(L".xm");
                return UiAction::SaveXmPath;
            case D_EXPMOD:
                actionPath_ = target(L".mod");
                return UiAction::SaveModPath;
            case D_EXPWAV:
                actionPath_ = target(L".wav");
                return UiAction::SaveWavPath;
            case D_SAVESMP:
                actionPath_ = target(L".wav");
                return UiAction::SaveSamplePath;
            case D_RENAME: {  // the selected file gets the name typed in the NAME field
                if (diskSel_ < 0 || diskSel_ >= (int)entries_.size() || entries_[diskSel_].dir || dir_ == L"::drives")
                    return setStatus("SELECT A FILE FIRST"), UiAction::Redraw;
                const std::wstring from = entries_[diskSel_].name;
                const std::wstring to = renameTarget(from, fileName_);
                if (to.empty()) return setStatus("TYPE THE NEW NAME IN THE NAME FIELD"), UiAction::Redraw;
                const std::wstring src = dir_ + L"\\" + from, dst = dir_ + L"\\" + to;
                if (to == from) return setStatus("THE FILE ALREADY HAS THIS NAME"), UiAction::Redraw;
                auto doRename = [this, src, dst](bool replace) {
                    const bool ok = MoveFileExW(src.c_str(), dst.c_str(), replace ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
                    setStatus(ok ? "FILE RENAMED" : "ERROR: CANNOT RENAME FILE");
                    diskRefresh();
                };
                // another file of that name: ask, naming it, with CANCEL first (Enter keeps the file)
                if (_wcsicmp(to.c_str(), from.c_str()) != 0 && GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    char text[96];
                    std::snprintf(text, sizeof text, "REPLACE THE EXISTING %s ?", narrow(to, 40).c_str());
                    ask(text, {"CANCEL", "REPLACE"}, [doRename](int a) {
                        if (a == 1) doRename(true);
                    });
                } else {
                    doRename(false);  // a new name, or only its case changes
                }
                return UiAction::Redraw;
            }
            case D_DELETE: {
                if (diskSel_ < 0 || diskSel_ >= (int)entries_.size() || entries_[diskSel_].dir) return UiAction::None;
                const std::wstring path = dir_ + L"\\" + entries_[diskSel_].name;
                char text[96];
                std::snprintf(text, sizeof text, "DELETE %s ?", narrow(entries_[diskSel_].name, 40).c_str());
                ask(text, {"DELETE", "CANCEL"}, [this, path](int a) {
                    if (a != 0) return;
                    setStatus(DeleteFileW(path.c_str()) ? "FILE DELETED" : "ERROR: CANNOT DELETE FILE");
                    diskRefresh();
                });
                return UiAction::Redraw;
            }
            case D_EXIT:
                panel_ = Panel::Main;
                return UiAction::Redraw;
        }
    }
    const RectF list = diskList();
    if (list.contains(x, y)) {
        const int i = diskScroll_ + (int)((y - list.y - 4) / kRowH);
        if (i >= 0 && i < (int)entries_.size()) {
            if (i == diskSel_) return diskActivate(i, e);  // second click opens
            diskSel_ = i;
            if (!entries_[i].dir) {
                const std::wstring& n = entries_[i].name;
                const size_t dot = n.find_last_of(L'.');
                fileName_ = dot == std::wstring::npos ? n : n.substr(0, dot);
            }
        }
        return UiAction::Redraw;
    }
    return UiAction::None;
}

UiAction PlayScreen::diskKey(unsigned vk, bool, Engine& e) {
    const int n = (int)entries_.size();
    switch (vk) {
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT: {
            const int step = vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : vk == VK_PRIOR ? -diskRows() : diskRows();
            diskSel_ = n ? std::clamp(diskSel_ + step, 0, n - 1) : 0;
            if (n && !entries_[diskSel_].dir) {  // the NAME field follows the selection, like a click
                const std::wstring& nm = entries_[diskSel_].name;
                const size_t dot = nm.find_last_of(L'.');
                fileName_ = dot == std::wstring::npos ? nm : nm.substr(0, dot);
            }
            return UiAction::Redraw;
        }
        case VK_RETURN:
            return diskActivate(diskSel_, e);
        case VK_BACK:
            if (!fileName_.empty()) fileName_.pop_back();
            return UiAction::Redraw;
        case VK_ESCAPE:
            panel_ = Panel::Main;
            return UiAction::Redraw;
    }
    return UiAction::None;
}

UiAction PlayScreen::onChar(wchar_t c, Engine&) {
    if (nameEdit_) {  // song / sample name being typed
        if (c >= 32 && c < 127) line_.insert((char)c);
        return UiAction::Redraw;
    }
    if (panel_ != Panel::DiskOp || dialog_.open) return UiAction::None;
    const wchar_t* bad = L"\\/:*?\"<>|";
    if (c < 32 || wcschr(bad, c) || fileName_.size() >= 60) return UiAction::None;
    fileName_ += c;
    return UiAction::Redraw;
}
