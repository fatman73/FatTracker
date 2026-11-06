// _wcsicmp / _wcsnicmp outside Windows: ASCII case folding, what the MSVC functions do in the "C" locale.
// On Windows this header adds nothing.
#pragma once
#ifndef _WIN32
#include <cstddef>

inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wchar_t ca = a[i], cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca += 32;
        if (cb >= L'A' && cb <= L'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return _wcsnicmp(a, b, (size_t)-1); }
#endif
