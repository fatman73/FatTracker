// Windows implementation of platform/os.h: the Win32 calls the UI used directly before.
#include "platform/os.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::wstring osJoin(const std::wstring& dir, const std::wstring& name) { return dir + L"\\" + name; }

std::wstring osParentDir(const std::wstring& dir) {
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos || dir.size() <= 3) return L"";  // "C:\" is a root
    std::wstring p = dir.substr(0, slash);
    if (p.size() == 2) p += L"\\";  // "C:" -> "C:\"
    return p;
}

std::wstring osCurrentDir() {
    wchar_t buf[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, buf);
    return buf;
}

std::wstring osHomeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const std::wstring exe = buf;
    return exe.substr(0, exe.find_last_of(L'\\'));
}

void osDrives(std::vector<OsDirItem>& out) {
    const DWORD mask = GetLogicalDrives();
    for (int d = 0; d < 26; ++d)
        if (mask & (1u << d)) out.push_back({std::wstring(1, (wchar_t)(L'A' + d)) + L":\\", true, 0});
}

std::wstring osDriveDir(const std::wstring& driveEntryName) { return driveEntryName.substr(0, 2); }  // "C:"

bool osListDir(const std::wstring& dir, std::vector<OsDirItem>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back({n, isDir, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

bool osFileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool osRenameFile(const std::wstring& from, const std::wstring& to, bool replace) {
    return MoveFileExW(from.c_str(), to.c_str(), replace ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
}

bool osDeleteFile(const std::wstring& path) { return DeleteFileW(path.c_str()) != 0; }

const char* osDeleteDoneText() { return "FILE DELETED"; }

const char* osReadFile(const std::wstring& pathW, std::vector<uint8_t>& out) {
    const wchar_t* path = pathW.c_str();
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "ERROR: CANNOT OPEN FILE";
    LARGE_INTEGER sz;
    const char* err = nullptr;
    if (!GetFileSizeEx(h, &sz))
        err = "ERROR: CANNOT READ FILE";
    else if (sz.QuadPart > 256ll * 1024 * 1024)
        err = "ERROR: FILE TOO BIG";
    if (!err) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        if (!out.empty() && (!ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) || got != out.size()))
            err = "ERROR: CANNOT READ FILE";
    }
    CloseHandle(h);
    return err;
}

bool osWriteFileAtomic(const std::wstring& pathW, const std::vector<uint8_t>& data) {
    const wchar_t* path = pathW.c_str();
    std::wstring tmp = std::wstring(path) + L".fattmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) &&
                                     written == data.size());
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

void osLocalTime(std::tm& out) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    out = {};
    out.tm_sec = st.wSecond;
    out.tm_min = st.wMinute;
    out.tm_hour = st.wHour;
    out.tm_mday = st.wDay;
    out.tm_mon = st.wMonth - 1;
    out.tm_year = st.wYear - 1900;
    out.tm_wday = st.wDayOfWeek;
}

int osIniInt(const wchar_t* ini, const wchar_t* section, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(section, key, def, ini);
}

std::wstring osIniString(const wchar_t* ini, const wchar_t* section, const wchar_t* key) {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(section, key, L"", buf, MAX_PATH, ini);
    return buf;
}

void osIniWrite(const wchar_t* ini, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), ini);
}
