// Reading and writing files for the app (Windows): modules, XM/MOD/WAV saving.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "audio/engine.h"

const char* readFileW(const wchar_t* path, std::vector<uint8_t>& out);  // nullptr = ok, else error
bool writeFileAtomic(const wchar_t* path, const std::vector<uint8_t>& data);

enum class SaveKind { Xm, Mod, Wav };
SaveKind kindFromPath(const std::wstring& path);
std::wstring withExtension(const std::wstring& path, const wchar_t* ext);  // ext like L".xm"

struct SaveResult {
    bool ok = false;
    const char* message = "";
    std::vector<const char*> warnings;
};
SaveResult saveSong(Engine& e, const std::wstring& path, SaveKind kind);
