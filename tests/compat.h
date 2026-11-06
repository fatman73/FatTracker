// Shims for the MSVC CRT calls the tests use (_wfopen_s, fopen_s, _wcsnicmp, _strnicmp) on other compilers.
// On Windows this header is empty.
#pragma once
#ifndef _WIN32
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cwctype>
#include <string>

#include "platform/wcompat.h"  // _wcsnicmp, _wcsicmp

// std::filesystem::path::c_str() is a char* (UTF-8) outside Windows
inline int _wfopen_s(FILE** f, const char* path, const wchar_t* mode) {
    std::string m;
    for (const wchar_t* p = mode; *p; ++p) m += (char)*p;
    *f = std::fopen(path, m.c_str());
    return *f ? 0 : errno;
}
inline int fopen_s(FILE** f, const char* path, const char* mode) {
    *f = std::fopen(path, mode);
    return *f ? 0 : errno;
}
inline int _strnicmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (unsigned char)ca < (unsigned char)cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}
#endif
