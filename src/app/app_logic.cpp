// Moved from src/app/main.cpp without changes in behaviour; the Win32 calls became AppShell calls and the
// chip-song routing (loadTfmxPair) is chip_route's routeChipFile.
#include "app/app_logic.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <system_error>

#include "app/chip_route.h"
#include "audio/capture.h"
#include "audio/engine.h"
#include "core/sample_edit.h"
#include "formats/formats.h"
#include "formats/sample_io.h"
#include "formats/writers.h"
#include "platform/os.h"
#include "platform/wcompat.h"
#include "ui/play_screen.h"

namespace {
Engine* g_engine = nullptr;
PlayScreen* g_screen = nullptr;
AudioCapture* g_capture = nullptr;
AppShell* g_shell = nullptr;
std::wstring g_replayDir;
std::wstring g_path;  // file of the current song ("" = untitled)

std::unique_ptr<Song> g_pending;  // Pure mode: loaded song waiting for the floppy
std::wstring g_pendingPath;
bool g_pendingTruncated = false;
// Pure mode: a chip song waiting for the floppy (it has its own player, so it is kept as files)
bool g_pendingTfmx = false;
std::vector<uint8_t> g_pendingMdat;
ChipRoute g_pendingRoute;

// Size of a file on disk (0 if unknown) - drives the floppy write sound.
uint32_t fileSize(const std::wstring& path) {
    std::error_code ec;
    const uintmax_t n = std::filesystem::file_size(std::filesystem::path(path), ec);
    return ec ? 0 : (uint32_t)n;
}

void applyLoaded(std::unique_ptr<Song> song, const std::wstring& path, bool truncated) {
    g_engine->setSong(static_cast<std::unique_ptr<Song>&&>(song));
    g_screen->songChanged();
    g_screen->setStatus(truncated ? "LOADED: FILE TRUNCATED" : "ALL RIGHT");
    g_path = path;
    g_screen->setCurrentPath(path);
    app::updateTitle();
}

bool applyChip(const std::vector<uint8_t>& mdat, const ChipRoute& route) {
    if (!loadRoutedChip(*g_engine, route, mdat, g_replayDir, false)) {
        g_screen->setStatus("CANNOT PLAY THIS FILE");
        return false;
    }
    g_screen->songChanged();
    char msg[128];
    const int songs = g_engine->chipSubsongs();  // more than one: POS steps through them
    if (songs > 1) std::snprintf(msg, sizeof msg, "%s - %d SONGS (POS) - PLAYBACK ONLY", g_engine->tfmxFormat(), songs);
    else std::snprintf(msg, sizeof msg, "%s - PLAYBACK ONLY", g_engine->tfmxFormat());
    g_screen->setStatus(msg);
    g_path.clear();  // nothing to save over
    g_screen->setCurrentPath(route.mdatPath);
    app::updateTitle();
    return true;
}

// Chip songs (TFMX pair, FC / Hippel, SID, SNDH, GME ...): false when this is not one at all.
bool loadChip(const std::wstring& path, std::vector<uint8_t>& data) {
    ChipRoute route = routeChipFile(path, data, g_replayDir);
    if (route.kind == ChipRoute::NotChip) return false;
    if (route.kind == ChipRoute::Error) {
        g_screen->setStatus(route.error.c_str());
        if (route.driveLooked) g_engine->floppyDir();  // the drive still looked at it
        return true;
    }
    // Pure mode: the drive reads both files first; the music waits for PLAY
    const double wait = g_engine->floppyRead((uint32_t)(data.size() + route.smpl.size()));
    if (wait > 0.05) {
        g_pendingTfmx = true;
        g_pendingMdat = data;
        g_pendingRoute = static_cast<ChipRoute&&>(route);
        g_pendingPath = g_pendingRoute.mdatPath;
        g_shell->setBusy(true);
        g_screen->setStatus("LOADING...");
        g_shell->startFloppyTimer(wait);
        return true;
    }
    applyChip(data, route);
    return true;
}

bool saveTo(const std::wstring& path, SaveKind kind) {
    if (g_engine->tfmx()) {  // the TFMX display song is not the music: nothing to save
        g_screen->setStatus("PLAYBACK ONLY - NOTHING TO SAVE");
        g_shell->redraw();
        return false;
    }
    g_shell->setBusy(true);
    SaveResult r = saveSong(*g_engine, path, kind);
    if (r.ok) g_engine->floppyWrite(fileSize(path));
    g_shell->setBusy(false);
    g_screen->setStatus(r.message);
    if (r.ok && kind == SaveKind::Xm) {
        g_path = path;
        g_screen->editor().setClean();
    }
    app::updateTitle();
    g_shell->redraw();
    return r.ok;
}

void exportChecked(const std::wstring& path, SaveKind kind) {
    if (g_engine->tfmx()) {  // playback only: saveTo explains
        saveTo(path, kind);
        return;
    }
    if (kind == SaveKind::Mod) {
        g_engine->lock();
        const ModExport m = exportMod(*g_engine->song());
        g_engine->unlock();
        if (!m.warnings.empty()) {
            char text[160];
            std::snprintf(text, sizeof text, "MOD CANNOT KEEP %d FEATURE(S), E.G.: %s", (int)m.warnings.size(),
                          m.warnings[0]);
            g_screen->ask(text, {"EXPORT", "CANCEL"}, [path](int a) {
                if (a == 0) saveTo(path, SaveKind::Mod);
            });
            g_shell->redraw();
            return;
        }
    }
    saveTo(path, kind);
}

// DISK OP. saves: ask before replacing an existing file.
void saveFromDiskOp(const std::wstring& path, SaveKind kind) {
    if (!osFileExists(path)) return exportChecked(path, kind);
    g_screen->ask("FILE EXISTS. OVERWRITE?", {"OVERWRITE", "CANCEL"}, [path, kind](int a) {
        if (a == 0) exportChecked(path, kind);
    });
    g_shell->redraw();
}

// The sampler's SAMPLE button: first press records, second press puts the recording in the slot.
void finishRecording() {
    const uint32_t rate = g_capture->rate();
    const bool lost = g_capture->lost(), full = g_capture->full();
    std::vector<float> rec = g_capture->take();
    g_screen->setRecording(false);
    if (rec.size() < rate / 50) {
        g_screen->setStatus("NOTHING RECORDED");
        return;
    }
    const double secs = (double)rec.size() / rate;
    g_screen->sampleLoaded(*g_engine, sampleFromRecording(rec.data(), rec.size(), rate), L"RECORDING");
    char msg[48];
    std::snprintf(msg, sizeof msg,
                  lost ? "INPUT LOST - KEPT %d.%d S" : full ? "60 S LIMIT - RECORDED %d.%d S" : "RECORDED %d.%d S",
                  (int)secs, (int)(secs * 10) % 10);
    g_screen->setStatus(msg);
}

void toggleRecording() {
    if (g_screen->recording()) return finishRecording();  // also right after the time limit stopped it
    if (!g_capture->start()) {
        g_screen->setStatus(g_capture->error() ? g_capture->error() : "NO AUDIO INPUT TO RECORD FROM");
        return;
    }
    g_screen->setRecording(true);
    g_screen->setStatus("RECORDING... PRESS SAMPLE TO STOP");
}
}  // namespace

namespace app {

void init(Engine& engine, PlayScreen& screen, AudioCapture& capture, AppShell& shell, const std::wstring& replayDir) {
    g_engine = &engine;
    g_screen = &screen;
    g_capture = &capture;
    g_shell = &shell;
    g_replayDir = replayDir;
}

void loadFloppySounds(const std::vector<std::wstring>& dirs) {
    static Sample snd[3];
    const Sample* use[3] = {};
    const wchar_t* names[3] = {L"floppy_motor.wav", L"floppy_step.wav", L"floppy_insert.wav"};
    for (int i = 0; i < 3; ++i)
        for (const std::wstring& d : dirs) {
            std::vector<uint8_t> data;
            if (readFileW((d + names[i]).c_str(), data)) continue;
            auto r = loadSampleFile(data.data(), data.size());
            if (!r.ok()) continue;
            snd[i] = static_cast<Sample&&>(r.value());
            use[i] = &snd[i];
            break;
        }
    g_engine->setFloppySounds(use[0], use[1], use[2]);  // copies (trimmed) what it uses
    for (Sample& s : snd) s = Sample{};
}

bool loadModuleFile(const std::wstring& path) {
    finishPendingLoad();  // a load in progress lands first
    g_engine->stop();     // a file was chosen: the music that plays now stops
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path.c_str(), data)) {
        g_screen->setStatus(err);
        return false;
    }
    if (loadChip(path, data)) return true;
    auto r = loadModule(data.data(), data.size());
    if (!r.ok()) {
        char msg[96];
        const size_t slash = path.find_last_of(L"\\/");
        const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
        if (!_wcsnicmp(name.c_str(), L"smpl.", 5))
            std::snprintf(msg, sizeof msg, "THIS IS A TFMX SAMPLE FILE - LOAD THE MDAT FILE");
        else
            std::snprintf(msg, sizeof msg, "ERROR: %s", r.error());
        g_screen->setStatus(msg);
        g_engine->floppyDir();  // the drive still looked at it
        return false;
    }
    const bool truncated = (r.value().loadFlags & LOAD_TRUNCATED) != 0;
    auto song = std::make_unique<Song>(static_cast<Song&&>(r.value()));
    // Pure mode: the song arrives when the simulated floppy has read it (a key or click skips the wait)
    const double wait = g_engine->floppyRead((uint32_t)data.size());
    if (wait > 0.05) {
        g_pending = static_cast<std::unique_ptr<Song>&&>(song);
        g_pendingPath = path;
        g_pendingTruncated = truncated;
        g_shell->setBusy(true);
        g_screen->setStatus("LOADING...");
        g_shell->startFloppyTimer(wait);
        return true;
    }
    applyLoaded(static_cast<std::unique_ptr<Song>&&>(song), path, truncated);
    return true;
}

bool loadPending() { return g_pending || g_pendingTfmx; }

void finishPendingLoad() {
    g_shell->stopFloppyTimer();
    if (!loadPending()) return;
    g_engine->floppyCancel();
    g_shell->setBusy(false);
    if (g_pendingTfmx) {
        g_pendingTfmx = false;
        applyChip(g_pendingMdat, g_pendingRoute);
        g_pendingMdat.clear();
        g_pendingRoute = ChipRoute{};
    } else {
        applyLoaded(static_cast<std::unique_ptr<Song>&&>(g_pending), g_pendingPath, g_pendingTruncated);
        g_pending.reset();
    }
    g_shell->redraw();
}

void floppyTimer() { finishPendingLoad(); }

void cancelPendingLoad() {
    if (!loadPending()) return;
    g_shell->stopFloppyTimer();
    g_pending.reset();
    g_pendingTfmx = false;
    g_pendingMdat.clear();
    g_shell->setBusy(false);
}

void loadAnyFile(const std::wstring& path) {
    finishPendingLoad();
    std::vector<uint8_t> data;
    if (const char* err = readFileW(path.c_str(), data)) {
        g_screen->setStatus(err);
        return;
    }
    const bool sampleSig = data.size() >= 12 && ((!std::memcmp(data.data(), "RIFF", 4) && !std::memcmp(data.data() + 8, "WAVE", 4)) ||
                                                  (!std::memcmp(data.data(), "FORM", 4) && (!std::memcmp(data.data() + 8, "8SVX", 4) ||
                                                                                            !std::memcmp(data.data() + 8, "AIFF", 4) ||
                                                                                            !std::memcmp(data.data() + 8, "AIFC", 4))));
    auto smp = loadSampleFile(data.data(), data.size());
    if (sampleSig && !smp.ok()) {
        g_screen->setStatus(smp.error());
        return;
    }
    if (smp.ok()) {
        const size_t slash = path.find_last_of(L"\\/");
        g_screen->sampleLoaded(*g_engine, static_cast<Sample&&>(smp.value()), path.substr(slash + 1));
        return;
    }
    confirmDiscard([path]() { loadModuleFile(path); });
}

void confirmDiscard(std::function<void()> then) {
    if (!g_screen->dirty()) {
        then();
        return;
    }
    g_screen->ask("THE SONG HAS UNSAVED CHANGES", {"SAVE", "DISCARD", "CANCEL"}, [then](int a) {
        if (a == 2) return;
        if (a == 0) {
            saveCurrent([then](bool saved) {
                if (saved && !g_screen->dirty()) then();
            });
            return;
        }
        then();
    });
    g_shell->redraw();
}

void openFileDialog() {
    confirmDiscard([]() {
        const std::wstring& dir = g_screen->browseDir();
        g_shell->openDialog(dir == L"::drives" ? std::wstring() : dir, [](const std::wstring& file) {
            loadModuleFile(file);
            g_screen->openPanel(Panel::Main);
            g_shell->redraw();
        });
        g_shell->redraw();
    });
}

void saveCurrent(std::function<void(bool saved)> done) {
    if (!g_path.empty() && kindFromPath(g_path) == SaveKind::Xm) {
        const bool ok = saveTo(g_path, SaveKind::Xm);
        if (done) done(ok);
        return;
    }
    saveAsDialog(1, done);
}

void saveAsDialog(int filter, std::function<void(bool saved)> done) {
    if (g_engine->tfmx()) {  // says why nothing is saved
        const bool ok = saveTo(L"", SaveKind::Xm);
        if (done) done(ok);
        return;
    }
    const std::wstring def = g_path.empty() ? L"untitled.xm" : withExtension(g_path, L".xm");
    g_shell->saveDialog(def, filter, [done](const std::wstring& chosen, SaveKind kind) {
        const wchar_t* ext = kind == SaveKind::Mod ? L".mod" : kind == SaveKind::Wav ? L".wav" : L".xm";
        std::wstring path = chosen;
        if (kindFromPath(path) != kind) path = withExtension(path, ext);
        if (kind != SaveKind::Xm) {  // exports never make the song clean
            exportChecked(path, kind);
            if (done) done(false);
            return;
        }
        const bool ok = saveTo(path, kind);
        if (done) done(ok);
    });
}

void handle(UiAction a) {
    if (a == UiAction::OpenFile) openFileDialog();
    if (a == UiAction::SaveFile) saveCurrent();
    if (a == UiAction::ExportWav) saveAsDialog(3);
    const std::wstring path = g_screen->actionPath();
    if (a == UiAction::LoadPath) loadAnyFile(path);
    if (a == UiAction::SaveXmPath) saveFromDiskOp(path, SaveKind::Xm);
    if (a == UiAction::SaveModPath) saveFromDiskOp(path, SaveKind::Mod);
    if (a == UiAction::SaveWavPath) saveFromDiskOp(path, SaveKind::Wav);
    if (a == UiAction::SaveSamplePath) {
        auto save = [path]() {
            std::vector<uint8_t> bytes;
            g_engine->lock();
            const Song* s = g_engine->song();
            const int idx = s ? g_screen->curSampleIndex(*s) : -1;
            if (idx >= 0 && s->samples[idx].length) bytes = saveSampleWav(s->samples[idx]);
            g_engine->unlock();
            if (bytes.empty()) g_screen->setStatus("ERROR: THE SAMPLE IS EMPTY");
            else if (!writeFileAtomic(path.c_str(), bytes)) g_screen->setStatus("ERROR: CANNOT WRITE FILE");
            else {
                g_screen->setStatus("SAMPLE SAVED (WAV)");
                g_engine->floppyWrite((uint32_t)bytes.size());
            }
            g_shell->redraw();
        };
        if (!osFileExists(path)) save();
        else g_screen->ask("FILE EXISTS. OVERWRITE?", {"OVERWRITE", "CANCEL"}, [save](int r) { if (r == 0) save(); });
    }
    if (g_screen->takeDirRead()) g_engine->floppyDir();
    if (g_screen->takeRecordRequest()) toggleRecording();
    if (a != UiAction::None) {
        g_shell->redraw();
        updateTitle();
    }
}

void updateTitle() {
    const size_t slash = g_path.find_last_of(L"\\/");
    g_shell->setTitle(g_path.empty() ? L"untitled" : g_path.substr(slash == std::wstring::npos ? 0 : slash + 1),
                      g_screen->dirty());
}

void timerTick(bool pollAudio) {
    if (g_screen->recording()) {  // recording: show time and level, stop at the limit
        if (!g_capture->running()) {
            finishRecording();
        } else {
            char recMsg[64];
            const int lvl = (int)(g_capture->level() * 20);
            char bar[21] = {};
            for (int i = 0; i < 20; ++i) bar[i] = i < lvl ? '#' : '.';
            std::snprintf(recMsg, sizeof recMsg, "RECORDING %d S  %s", (int)g_capture->seconds(), bar);
            g_screen->setStatus(recMsg);
        }
    }
    if (pollAudio) {
        g_engine->pollAudio();
        if (const char* st = g_engine->audioStatus())
            g_screen->setStatus(st);
        else
            g_screen->clearStatusIf("ERROR: NO AUDIO");
    }
    g_shell->redraw();
}

const std::wstring& songPath() { return g_path; }

}  // namespace app
