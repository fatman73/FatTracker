#include "app/document.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/render.h"
#include "formats/wav.h"
#include "formats/writers.h"

const char* readFileW(const wchar_t* path, std::vector<uint8_t>& out) {
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

bool writeFileAtomic(const wchar_t* path, const std::vector<uint8_t>& data) {
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

SaveKind kindFromPath(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return SaveKind::Xm;
    std::wstring ext = path.substr(dot);
    for (wchar_t& c : ext) c = (wchar_t)towlower(c);
    if (ext == L".mod") return SaveKind::Mod;
    if (ext == L".wav") return SaveKind::Wav;
    return SaveKind::Xm;
}

std::wstring withExtension(const std::wstring& path, const wchar_t* ext) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) return path.substr(0, dot) + ext;
    return path + ext;
}

SaveResult saveSong(Engine& e, const std::wstring& path, SaveKind kind) {
    SaveResult r;
    if (!e.song()) {
        r.message = "ERROR: NOTHING TO SAVE";
        return r;
    }
    std::vector<uint8_t> bytes;
    if (kind == SaveKind::Wav) {
        e.lock();
        Song copy = *e.song();  // render from a snapshot so playback/editing stay responsive
        e.unlock();
        std::vector<float> pcm;
        const size_t frames = renderSong(copy, 48000, Interp::Cubic, true, 1200.f, pcm);
        bytes = writeWav(pcm.data(), frames, 48000, false);
    } else {
        e.lock();
        if (kind == SaveKind::Xm) {
            bytes = saveXm(*e.song());
        } else {
            ModExport m = exportMod(*e.song());
            bytes = static_cast<std::vector<uint8_t>&&>(m.data);
            r.warnings = m.warnings;
        }
        e.unlock();
    }
    if (!writeFileAtomic(path.c_str(), bytes)) {
        r.message = "ERROR: CANNOT WRITE FILE";
        return r;
    }
    r.ok = true;
    r.message = kind == SaveKind::Xm ? "SONG SAVED (XM)" : kind == SaveKind::Mod ? "SONG EXPORTED (MOD)" : "WAV EXPORTED";
    return r;
}
