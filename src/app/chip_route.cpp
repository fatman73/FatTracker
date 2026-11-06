#include "app/chip_route.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>

#include "audio/engine.h"
#include "core/amiga_synth.h"
#include "core/ay_player.h"
#include "core/gme_player.h"
#include "core/hippel_player.h"
#include "core/hvl_player.h"
#include "core/pt3_player.h"
#include "core/sc68_player.h"
#include "core/sid_player.h"
#include "core/sndh_player.h"
#include "core/tfmx_player.h"
#include "core/song.h"
#include "core/zx_trackers.h"
#include "formats/formats.h"

namespace {
#ifdef _WIN32
const wchar_t* const kHere = L".\\";
#else
const wchar_t* const kHere = L"./";
#endif

wchar_t lowerAscii(wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c; }

// ASCII case folding, like _wcsnicmp / _wcsicmp in the "C" locale: 0 when equal
int cmpNoCase(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const wchar_t ca = lowerAscii(a[i]), cb = lowerAscii(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}
int cmpNoCase(const wchar_t* a, const wchar_t* b) { return cmpNoCase(a, b, (size_t)-1); }

std::string upperStatus(const char* prefix, const std::string& text) {
    char msg[100];
    std::snprintf(msg, sizeof msg, "%s%s", prefix, text.c_str());
    for (char* c = msg; *c; ++c) *c = (unsigned char)*c < 127 ? (char)toupper((unsigned char)*c) : '_';
    return msg;
}
}  // namespace

const char* readWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (!std::filesystem::is_regular_file(p, ec) || ec) return "ERROR: CANNOT OPEN FILE";
    const uintmax_t size = std::filesystem::file_size(p, ec);
    if (ec) return "ERROR: CANNOT READ FILE";
    if (size > 256ull * 1024 * 1024) return "ERROR: FILE TOO BIG";
    std::ifstream in(p, std::ios::binary);
    if (!in) return "ERROR: CANNOT OPEN FILE";
    out.resize((size_t)size);
    if (size && !in.read(reinterpret_cast<char*>(out.data()), (std::streamsize)size)) return "ERROR: CANNOT READ FILE";
    return nullptr;
}

AmigaFileResolver songFolderFiles(const std::wstring& songPath) {
    const size_t slash = songPath.find_last_of(L"\\/");
    return folderFileResolver(slash == std::wstring::npos ? L"." : songPath.substr(0, slash + 1));
}

AmigaFileResolver replayFiles(const std::wstring& songPath, const std::wstring& exeReplayDir) {
    const size_t slash = songPath.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? kHere : songPath.substr(0, slash + 1);
    std::vector<AmigaFileResolver> in = {folderFileResolver(dir), folderFileResolver(dir + L"Replay")};
    if (!exeReplayDir.empty()) in.push_back(folderFileResolver(exeReplayDir));
    return [in](const std::string& name, std::vector<uint8_t>& out) {
        if (!isPlainFileName(name)) return false;
        for (const AmigaFileResolver& r : in)
            if (r(name, out)) return true;
        return false;
    };
}

// TFMX songs are two files: mdat.<name> (song) and smpl.<name> (samples), or .mdat/.smpl, .tfx/.sam.
// Either one may be chosen. Mirrors loadTfmxPair in src/app/main.cpp (Windows).
ChipRoute routeChipFile(const std::wstring& path, std::vector<uint8_t>& data, const std::wstring& exeReplayDir) {
    ChipRoute r;
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
    std::wstring mdatPath = path, smplPath;
    auto swapPrefix = [&](const wchar_t* from, const wchar_t* to) { return dir + to + file.substr(wcslen(from)); };
    auto swapExt = [&](const std::wstring& p, size_t extLen, const wchar_t* to) { return p.substr(0, p.size() - extLen) + to; };
    auto hasSuffix = [&](const wchar_t* s) {
        const size_t n = wcslen(s);
        return file.size() > n && cmpNoCase(file.c_str() + file.size() - n, s) == 0;
    };
    if (!cmpNoCase(file.c_str(), L"smpl.", 5)) mdatPath = swapPrefix(L"smpl.", L"mdat.");
    else if (hasSuffix(L".smpl")) mdatPath = swapExt(path, 5, L".mdat");
    else if (hasSuffix(L".sam")) mdatPath = swapExt(path, 4, L".tfx");
    if (mdatPath != path && readWholeFile(mdatPath, data)) return r;  // NotChip
    // Future Composer / Jochen Hippel: one file with song and samples, played like TFMX
    // Hippel's probes search the whole file: they count only for FC / Hippel names or a tag at the start,
    // and the file must really load, else the module loaders get it (a MOD may name a sample "TFMX")
    auto named = [&](std::initializer_list<const wchar_t*> exts) {
        for (const wchar_t* x : exts) {
            const size_t n = wcslen(x);
            if (file.size() > n && (cmpNoCase(file.c_str() + file.size() - n, x) == 0 ||
                                    (!cmpNoCase(file.c_str(), x + 1, n - 1) && file[n - 1] == L'.')))
                return true;
        }
        return false;
    };
    const bool hippelName = named({L".fc", L".fc13", L".fc14", L".smod", L".hip", L".hipc", L".hip7", L".sog", L".mcmd", L".coso"});
    const bool hippelTag = data.size() >= 4 && (!std::memcmp(data.data(), "FC14", 4) || !std::memcmp(data.data(), "SMOD", 4) ||
                                                !std::memcmp(data.data(), "COSO", 4) || !std::memcmp(data.data(), "MCMD", 4));
    // a one-file chip song that really loads; each format in turn, the first that loads wins
    bool hippel = false;
    Chip chip = Chip::Tfmx;
    if (!TfmxPlayer::detect(data.data(), data.size()) && mdatPath == path) {
        const uint8_t* d = data.data();
        const size_t n = data.size();
        auto pick = [&](Chip c, bool loads) {
            if (!hippel && loads) hippel = true, chip = c;
        };
        if (SidPlayer::detect(d, n)) {  // C64 PSID / RSID
            SidPlayer trial;
            pick(Chip::Sid, trial.load(d, n));
        }
        if (!hippel && HvlPlayer::detect(d, n)) {  // Amiga AHX ("THX") / HivelyTracker ("HVL")
            HvlPlayer trial;
            pick(Chip::Hvl, trial.load(d, n));
        }
        if (!hippel && SndhPlayer::detect(d, n)) {  // Atari ST SNDH (its 68000 player; ICE! packed too)
            SndhPlayer trial;
            pick(Chip::Sndh, trial.load(d, n));
        }
        if (!hippel && (Sc68Player::detect(d, n) || Sc68Player::detectYmst(d, n))) {  // Atari ST SC68 / YMST
            Sc68Player trial;
            std::string err;
            if (!trial.load(d, n, replayFiles(path, exeReplayDir), -1, &err)) {  // e.g. its external replay is missing
                r.kind = ChipRoute::Error;
                r.error = upperStatus("", err);
                r.driveLooked = true;
                return r;
            }
            pick(Chip::Sc68, true);
        }
        if (!hippel && Pt3Player::detect(d, n)) {  // ZX Spectrum Pro Tracker 3 / Vortex Tracker II
            Pt3Player trial;
            pick(Chip::Pt3, trial.load(d, n));
        }
        if (!hippel && GmePlayer::detect(d, n)) {  // SPC, VGM, SAP, AY, NSF, GBS, ...
            GmePlayer trial;
            pick(Chip::Gme, trial.load(d, n));
        }
        if (!hippel && AmigaSynthPlayer::detect(d, n))  // JamCracker, Delta Music, SidMon, SoundMon, Mugician, Whittaker, Sonic Arranger, Art Of Noise
            pick(Chip::Synth, AmigaSynthPlayer::open(d, n) != nullptr);
        if (!hippel && AmigaSynthPlayer::needsFiles(d, n)) {  // IFF SMUS: its instruments are files next to it
            std::string err;
            if (!AmigaSynthPlayer::openWithFiles(d, n, songFolderFiles(path), &err)) {
                r.kind = ChipRoute::Error;
                r.error = upperStatus("IFF SMUS: ", err);
                r.driveLooked = true;  // the drive still looked at it
                return r;
            }
            pick(Chip::SynthFiles, true);
        }
        if (!hippel && AyPlayer::detect(d, n)) {  // YM / VTX / PSG (LHA packed YM too)
            AyPlayer trial;
            pick(Chip::Ay, trial.load(d, n));
        }
        if (!hippel && (hippelName || hippelTag) && HippelPlayer::detect(d, n)) {  // FC / Jochen Hippel
            HippelPlayer trial;
            pick(Chip::Hippel, trial.load(d, n));
        }
        if (!hippel && ZxTrackerPlayer::detect(d, n)) {  // ZX STC / Sound Tracker 1.1 / PT2 / ASC / SQT (no tags)
            ZxTrackerPlayer trial;
            pick(Chip::ZxTracker, trial.load(d, n));
        }
    }
    if (!hippel && !TfmxPlayer::detect(data.data(), data.size())) return r;  // NotChip
    const std::wstring mfile = mdatPath.substr(slash == std::wstring::npos ? 0 : slash + 1);
    if (!cmpNoCase(mfile.c_str(), L"mdat.", 5)) smplPath = dir + L"smpl." + mfile.substr(5);
    else if (mfile.size() > 5 && !cmpNoCase(mfile.c_str() + mfile.size() - 5, L".mdat")) smplPath = swapExt(mdatPath, 5, L".smpl");
    else if (mfile.size() > 4 && !cmpNoCase(mfile.c_str() + mfile.size() - 4, L".tfx")) smplPath = swapExt(mdatPath, 4, L".sam");
    if (!hippel && (smplPath.empty() || readWholeFile(smplPath, r.smpl))) {
        r.kind = ChipRoute::Error;
        r.error = "TFMX: SMPL FILE NOT FOUND NEXT TO MDAT";
        r.smpl.clear();
        return r;
    }
    const std::wstring shown = !cmpNoCase(mfile.c_str(), L"mdat.", 5) ? mfile.substr(5) : mfile;
    for (size_t i = 0; i < 20 && i < shown.size(); ++i) r.name[i] = shown[i] < 127 ? (char)towupper(shown[i]) : '_';
    r.kind = ChipRoute::Ok;
    r.chip = chip;
    r.mdatPath = mdatPath;
    return r;
}

bool loadRoutedChip(Engine& engine, const ChipRoute& route, const std::vector<uint8_t>& mdat,
                    const std::wstring& exeReplayDir, bool play) {
    const uint8_t* d = mdat.data();
    const size_t n = mdat.size();
    const char* name = route.name;
    switch (route.chip) {  // decided by routeChipFile's trial loads
        case Chip::Sid: return engine.loadSid(d, n, "", play);
        case Chip::Pt3: return engine.loadPt3(d, n, name, play);
        case Chip::ZxTracker: return engine.loadZxTracker(d, n, name, play);
        case Chip::Gme: return engine.loadGme(d, n, name, play);
        case Chip::Synth: return engine.loadAmigaSynth(d, n, name, play);
        case Chip::SynthFiles: return engine.loadAmigaSynth(d, n, songFolderFiles(route.mdatPath), name, play);
        case Chip::Ay: return engine.loadAy(d, n, name, play);
        case Chip::Sndh: return engine.loadSndh(d, n, name, play);
        case Chip::Hvl: return engine.loadHvl(d, n, name, play);
        case Chip::Sc68: return engine.loadSc68(d, n, replayFiles(route.mdatPath, exeReplayDir), name, play);
        case Chip::Hippel: return engine.loadHippel(d, n, name, play);
        case Chip::Tfmx: return engine.loadTfmx(d, n, route.smpl.data(), route.smpl.size(), name, play);
    }
    return false;
}

bool loadSongFile(Engine& engine, const std::wstring& path, const std::wstring& exeReplayDir, std::string* error) {
    std::vector<uint8_t> data;
    if (const char* err = readWholeFile(path, data)) {
        if (error) *error = err;
        return false;
    }
    const ChipRoute route = routeChipFile(path, data, exeReplayDir);
    if (route.kind == ChipRoute::Error) {
        if (error) *error = route.error;
        return false;
    }
    if (route.kind == ChipRoute::Ok) {
        if (loadRoutedChip(engine, route, data, exeReplayDir, false)) return true;
        if (error) *error = "CANNOT PLAY THIS FILE";
        return false;
    }
    auto r = loadModule(data.data(), data.size());
    if (!r.ok()) {
        if (error) *error = std::string("ERROR: ") + r.error();
        return false;
    }
    engine.setSong(std::make_unique<Song>(static_cast<Song&&>(r.value())));
    return true;
}
