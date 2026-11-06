// macOS / POSIX implementation of platform/os.h.
#include "platform/os.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
std::string utf8(const std::wstring& w) { return fs::path(w).string(); }
std::wstring wide(const std::string& s) { return fs::path(s).wstring(); }

bool hiddenFile(const std::string& path, const std::string& name) {
    if (!name.empty() && name[0] == '.') return true;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_flags & UF_HIDDEN);
}

// ---- INI: [section] / key=value lines; sections and keys compare without case like GetPrivateProfile*
std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r')) --b;
    return s.substr(a, b - a);
}
bool sameNoCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}
std::vector<std::wstring> readLines(const wchar_t* ini) {
    std::vector<std::wstring> lines;
    std::ifstream in(fs::path(ini), std::ios::binary);
    if (!in) return lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(wide(line));
    return lines;
}
// Index of the line holding `key` in `section` (-1), and of the section header (-1 when missing).
int findKey(const std::vector<std::wstring>& lines, const wchar_t* section, const wchar_t* key, int* sectionLine) {
    bool in = false;
    *sectionLine = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        const std::wstring t = trim(lines[i]);
        if (!t.empty() && t[0] == L'[') {
            const size_t close = t.find(L']');
            in = close != std::wstring::npos && sameNoCase(trim(t.substr(1, close - 1)), section);
            if (in) *sectionLine = i;
            continue;
        }
        if (!in) continue;
        const size_t eq = t.find(L'=');
        if (eq != std::wstring::npos && sameNoCase(trim(t.substr(0, eq)), key)) return i;
    }
    return -1;
}
}  // namespace

std::wstring osJoin(const std::wstring& dir, const std::wstring& name) {
    if (!dir.empty() && dir.back() == L'/') return dir + name;
    return dir + L"/" + name;
}

std::wstring osParentDir(const std::wstring& dir) {
    if (dir.empty() || dir == L"/") return L"";  // the root: show the drive list
    std::wstring d = dir;
    while (d.size() > 1 && d.back() == L'/') d.pop_back();
    const size_t slash = d.find_last_of(L'/');
    if (slash == std::wstring::npos) return L"";
    return slash == 0 ? L"/" : d.substr(0, slash);
}

std::wstring osCurrentDir() {
    std::error_code ec;
    const fs::path p = fs::current_path(ec);
    return ec ? osHomeDir() : p.wstring();
}

std::wstring osHomeDir() {
    const char* h = std::getenv("HOME");
    return wide(h && *h ? h : "/");
}

void osDrives(std::vector<OsDirItem>& out) {
    out.push_back({L"/", true, 0});
    out.push_back({osHomeDir(), true, 0});
    std::error_code ec;
    for (fs::directory_iterator it("/Volumes", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (fs::is_symlink(it->symlink_status(e2))) continue;  // the start-up disk is a link to "/"
        if (it->is_directory(e2)) out.push_back({it->path().wstring(), true, 0});
    }
}

std::wstring osDriveDir(const std::wstring& driveEntryName) { return driveEntryName; }

bool osListDir(const std::wstring& dir, std::vector<OsDirItem>& out) {
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return false;
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name == "." || name == ".." || hiddenFile(it->path().string(), name)) continue;
        std::error_code e2;
        const bool isDir = it->is_directory(e2);
        const uint64_t size = isDir ? 0 : (uint64_t)it->file_size(e2);
        out.push_back({wide(name), isDir, e2 ? 0 : size});
    }
    return true;
}

bool osFileExists(const std::wstring& path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

bool osRenameFile(const std::wstring& from, const std::wstring& to, bool replace) {
    // MoveFileEx without REPLACE_EXISTING fails on another file of that name; a change of case only
    // (the same file on a case-insensitive volume) goes through like on Windows
    std::error_code ec;
    if (!replace && osFileExists(to) && !fs::equivalent(fs::path(from), fs::path(to), ec)) return false;
    return std::rename(utf8(from).c_str(), utf8(to).c_str()) == 0;
}

bool osDeleteFile(const std::wstring& path) { return ::unlink(utf8(path).c_str()) == 0; }

void osLocalTime(std::tm& out) {
    const std::time_t now = std::time(nullptr);
    localtime_r(&now, &out);
}

int osIniInt(const wchar_t* ini, const wchar_t* section, const wchar_t* key, int def) {
    const std::wstring v = osIniString(ini, section, key);
    if (v.empty()) return def;
    // like GetPrivateProfileInt: the leading number, 0 when there is none
    return (int)std::wcstol(v.c_str(), nullptr, 10);
}

std::wstring osIniString(const wchar_t* ini, const wchar_t* section, const wchar_t* key) {
    const std::vector<std::wstring> lines = readLines(ini);
    int sec;
    const int i = findKey(lines, section, key, &sec);
    if (i < 0) return L"";
    const std::wstring& l = lines[i];
    return trim(l.substr(l.find(L'=') + 1));
}

void osIniWrite(const wchar_t* ini, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    std::vector<std::wstring> lines = readLines(ini);
    int sec;
    const int i = findKey(lines, section, key, &sec);
    const std::wstring entry = std::wstring(key) + L"=" + value;
    if (i >= 0) {
        lines[i] = entry;
    } else if (sec >= 0) {
        int at = sec + 1;  // after the section's last entry
        while (at < (int)lines.size() && !(trim(lines[at]).size() && trim(lines[at])[0] == L'[')) ++at;
        while (at > sec + 1 && trim(lines[at - 1]).empty()) --at;
        lines.insert(lines.begin() + at, entry);
    } else {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back(L"");
        lines.push_back(L"[" + std::wstring(section) + L"]");
        lines.push_back(entry);
    }
    std::error_code ec;
    const fs::path p(ini), tmp = fs::path(ini).concat(".tmp");
    fs::create_directories(p.parent_path(), ec);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        for (const std::wstring& l : lines) out << utf8(l) << '\n';
        if (!out) return;
    }
    fs::rename(tmp, p, ec);
}
