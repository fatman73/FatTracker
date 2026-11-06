#include "app/document.h"

#include <cwctype>

#include "platform/os.h"

#include "core/render.h"
#include "formats/wav.h"
#include "formats/writers.h"

// The file I/O is the system's (platform/os.h): the Win32 calls on Windows, POSIX on macOS.
const char* readFileW(const wchar_t* path, std::vector<uint8_t>& out) { return osReadFile(path, out); }

bool writeFileAtomic(const wchar_t* path, const std::vector<uint8_t>& data) { return osWriteFileAtomic(path, data); }

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
