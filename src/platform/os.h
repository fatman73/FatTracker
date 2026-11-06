// The operating-system services the UI needs (DISK OP. folders and files, settings file, clock),
// one implementation per system: os_win.cpp (the calls the UI made before) and os_posix.cpp (macOS).
#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct OsDirItem {
    std::wstring name;
    bool dir;
    uint64_t size;
};

// `dir` + separator + `name`
std::wstring osJoin(const std::wstring& dir, const std::wstring& name);
// The folder above `dir`; "" when `dir` is a root (the caller then shows the drive list).
std::wstring osParentDir(const std::wstring& dir);
std::wstring osCurrentDir();
// DISK OP. HOME: Windows the program's folder, macOS the user's home folder.
std::wstring osHomeDir();
// DISK OP. DRIVES: one folder entry per drive / volume; osDriveDir gives the folder to open for an entry.
void osDrives(std::vector<OsDirItem>& out);
std::wstring osDriveDir(const std::wstring& driveEntryName);
// Entries of `dir` without ".", ".." and hidden ones (unsorted). False when it cannot be read.
bool osListDir(const std::wstring& dir, std::vector<OsDirItem>& out);
bool osFileExists(const std::wstring& path);
bool osRenameFile(const std::wstring& from, const std::wstring& to, bool replace);
bool osDeleteFile(const std::wstring& path);
void osLocalTime(std::tm& out);

// Settings file (Windows INI semantics: [section] key=value).
int osIniInt(const wchar_t* ini, const wchar_t* section, const wchar_t* key, int def);
std::wstring osIniString(const wchar_t* ini, const wchar_t* section, const wchar_t* key);
void osIniWrite(const wchar_t* ini, const wchar_t* section, const wchar_t* key, const std::wstring& value);
