// The shared app logic (src/app/app_logic.cpp) driven the way the window drives it, with a fake AppShell that
// answers the file dialogs: opening, Ctrl+S, Save As, the MOD export question, DISK OP. saves with the
// OVERWRITE question, SAVE SAMPLE, DELETE, dropped samples and modules, the unsaved-changes question
// (SAVE / DISCARD / CANCEL), the Pure mode floppy wait, chip songs and the window title.
// Files are written only to <source>/build/apptest_tmp.
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "test.h"

#include "app/app_logic.h"
#include "app/document.h"
#include "audio/capture.h"
#include "audio/engine.h"
#include "core/song.h"
#include "formats/formats.h"
#include "formats/sample_io.h"
#include "ui/font.h"
#include "ui/gfx.h"
#include "ui/play_screen.h"
#include "ui/text.h"
#include "ui/theme.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#endif

namespace fs = std::filesystem;

// DISK OP. button order in disk_op.cpp
enum { D_MODULES, D_SAMPLES, D_ALL, D_PARENT, D_HOME, D_DRIVES, D_LOAD, D_SAVEXM, D_EXPMOD, D_EXPWAV, D_SAVESMP,
       D_RENAME, D_DELETE, D_EXIT };

struct PlayScreenProbe {
    static UiAction press(PlayScreen& s, int button, Engine& e) {
        const RectF r = s.diskButton(button);
        return s.click(r.x + r.w / 2, r.y + r.h / 2, false, e);
    }
    static bool select(PlayScreen& s, const std::wstring& name) {
        for (size_t i = 0; i < s.entries_.size(); ++i)
            if (s.entries_[i].name == name) {
                s.diskSel_ = (int)i;
                return true;
            }
        return false;
    }
    static void setName(PlayScreen& s, const std::wstring& n) { s.fileName_ = n; }
};

namespace {
struct FakeShell : AppShell {
    std::wstring title;
    bool titleDirty = false, busy = false, timer = false;
    int openCalls = 0, saveCalls = 0, saveFilter = 0;
    std::wstring openAnswer, saveAnswer, saveSuggested;  // "" = the user cancels
    SaveKind saveKind = SaveKind::Xm;
    void redraw() override {}
    void setTitle(const std::wstring& n, bool d) override { title = n, titleDirty = d; }
    void setBusy(bool b) override { busy = b; }
    void startFloppyTimer(double) override { timer = true; }
    void stopFloppyTimer() override { timer = false; }
    void openDialog(const std::wstring&, std::function<void(const std::wstring&)> done) override {
        ++openCalls;
        if (!openAnswer.empty()) done(openAnswer);
    }
    void saveDialog(const std::wstring& suggested, int filter, std::function<void(const std::wstring&, SaveKind)> done) override {
        ++saveCalls;
        saveSuggested = suggested;
        saveFilter = filter;
        if (!saveAnswer.empty()) done(saveAnswer, saveKind);
    }
};

struct App {
    Engine engine;
    PlayScreen screen;
    AudioCapture capture;
    FakeShell shell;
    Gfx gfx;
    Text text;
    Font font;
    bool drawn = false;
    App() {
#ifdef _WIN32
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        engine.setSong(std::make_unique<Song>(makeEmptySong(4)));
        text.init();
        font.setText(&text);
        app::init(engine, screen, capture, shell, L"");
        // one frame establishes the layout the clicks hit
        if (gfx.initOffscreen(1920, 1080) && gfx.beginFrame(currentTheme().background)) {
            screen.draw(gfx, text, font, currentTheme(), engine);
            gfx.endFrame();
            drawn = true;
        }
    }
    void frame() {
        if (gfx.beginFrame(currentTheme().background)) {
            screen.draw(gfx, text, font, currentTheme(), engine);
            gfx.endFrame();
        }
    }
    void press(int button) {
        app::handle(PlayScreenProbe::press(screen, button, engine));
        frame();
    }
    bool status(const char* prefix) const { return std::strncmp(screen.status(), prefix, std::strlen(prefix)) == 0; }
    bool asks(const char* prefix) const {
        const char* q = screen.question();
        return q && std::strncmp(q, prefix, std::strlen(prefix)) == 0;
    }
};

App& theApp() {
    static App a;
    return a;
}

fs::path tmpDir() {
    static const fs::path d = fs::path(FT_SOURCE_DIR) / "build" / "apptest_tmp";
    static bool made = [] {
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d, ec);
        return true;
    }();
    (void)made;
    return d;
}

std::wstring tmp(const wchar_t* name) { return (tmpDir() / name).wstring(); }

// A small module written by the program itself: one sample, a note. More than 4 channels: an XM whose sample has
// a ping-pong loop (something a MOD cannot keep).
std::wstring makeModFile(const wchar_t* name, int channels = 4) {
    Song s = makeEmptySong(channels);
    if (s.samples.size() < 31) s.samples.resize(31);
    Sample& smp = s.samples[0];
    smp.data8.resize(2000);
    for (size_t i = 0; i < smp.data8.size(); ++i) smp.data8[i] = (int8_t)((i * 8) & 0x7F) - 64;
    smp.length = (uint32_t)smp.data8.size();
    if (channels > 4) {
        smp.loop = LoopType::PingPong;
        smp.loopStart = 0;
        smp.loopLength = smp.length;
    }
    std::snprintf(smp.name, sizeof smp.name, "SAW");
    s.patterns[0].at(0, 0).note = 49;
    s.patterns[0].at(0, 0).instr = 1;
    const std::wstring path = tmp(name);
    Engine e;
    e.setSong(std::make_unique<Song>(static_cast<Song&&>(s)));
    const SaveResult r = saveSong(e, path, channels > 4 ? SaveKind::Xm : SaveKind::Mod);
    CHECK(r.ok);
    return path;
}

std::wstring makeWavFile(const wchar_t* name) {
    Sample s;
    s.data8.resize(4000);
    for (size_t i = 0; i < s.data8.size(); ++i) s.data8[i] = (int8_t)((i & 31) * 4 - 64);
    s.length = (uint32_t)s.data8.size();
    const std::wstring path = tmp(name);
    CHECK(writeFileAtomic(path.c_str(), saveSampleWav(s)));
    return path;
}

void fresh(App& a, const std::wstring& module) {
    app::cancelPendingLoad();
    a.screen.setPure(0, a.engine);
    a.screen.editor().setClean();
    a.shell = FakeShell{};
    a.shell.openAnswer = module;
    app::handle(UiAction::OpenFile);
    a.screen.openPanel(Panel::Main);
    a.frame();
}

uintmax_t fileSize(const std::wstring& p) {
    std::error_code ec;
    const uintmax_t n = fs::file_size(p, ec);
    return ec ? 0 : n;
}
}  // namespace

TEST(app_open_dialog_loads_module) {
    App& a = theApp();
    CHECK(a.drawn);
    const std::wstring mod = makeModFile(L"open.mod");
    fresh(a, mod);
    CHECK_EQ(a.shell.openCalls, 1);
    CHECK(a.status("ALL RIGHT"));
    CHECK(a.shell.title == L"open.mod");
    CHECK(!a.shell.titleDirty);
    CHECK(app::songPath() == mod);
    a.shell.openAnswer.clear();  // cancelled dialog: nothing changes
    app::handle(UiAction::OpenFile);
    CHECK(app::songPath() == mod);
}

TEST(app_title_star_follows_unsaved_changes) {
    App& a = theApp();
    fresh(a, makeModFile(L"title.mod"));
    a.screen.editor().markDirty();
    app::updateTitle();
    CHECK(a.shell.titleDirty);
    a.screen.editor().setClean();
    app::updateTitle();
    CHECK(!a.shell.titleDirty);
}

TEST(app_ctrl_s_save_as_first_then_in_place) {
    App& a = theApp();
    fresh(a, makeModFile(L"song.mod"));
    a.screen.editor().markDirty();
    const std::wstring xm = tmp(L"song_saved.xm");
    a.shell.saveAnswer = tmp(L"song_saved.xm");  // the dialog adds .xm (lpstrDefExt)
    a.shell.saveKind = SaveKind::Xm;
    app::handle(UiAction::SaveFile);  // a .mod song: Ctrl+S asks where (XM)
    CHECK_EQ(a.shell.saveCalls, 1);
    CHECK(a.shell.saveSuggested == tmp(L"song.xm"));
    CHECK_EQ(a.shell.saveFilter, 1);
    CHECK(fileSize(xm) > 0);
    CHECK(!a.screen.dirty());
    CHECK(a.shell.title == L"song_saved.xm" && !a.shell.titleDirty);
    CHECK(app::songPath() == xm);
    // again: saved over the same file, no dialog
    fs::remove(xm);
    a.screen.editor().markDirty();
    app::handle(UiAction::SaveFile);
    CHECK_EQ(a.shell.saveCalls, 1);
    CHECK(fileSize(xm) > 0);
    CHECK(!a.screen.dirty());
}

TEST(app_export_wav_from_save_as) {
    App& a = theApp();
    fresh(a, makeModFile(L"exp.mod"));
    a.screen.editor().markDirty();
    a.shell.saveAnswer = tmp(L"exp_render.xm");
    a.shell.saveKind = SaveKind::Wav;
    app::handle(UiAction::ExportWav);
    CHECK_EQ(a.shell.saveFilter, 3);
    CHECK(fileSize(tmp(L"exp_render.wav")) > 44);
    CHECK(a.screen.dirty());  // an export never makes the song clean
    CHECK(app::songPath() == tmp(L"exp.mod"));
}

TEST(app_diskop_mod_export_question) {
    App& a = theApp();
    fresh(a, makeModFile(L"wide.xm", 12));  // 12 channels: MOD cannot keep them
    a.screen.openPanel(Panel::DiskOp);
    a.frame();
    PlayScreenProbe::setName(a.screen, L"wide_out");
    const std::wstring out = tmp(L"wide_out.mod");
    a.press(D_EXPMOD);
    CHECK(a.asks("MOD CANNOT KEEP"));
    a.screen.answer(1);  // CANCEL
    CHECK(!fs::exists(out));
    a.press(D_EXPMOD);
    CHECK(a.asks("MOD CANNOT KEEP"));
    a.screen.answer(0);  // EXPORT
    CHECK(fileSize(out) > 0);
}

TEST(app_diskop_save_overwrite_question) {
    App& a = theApp();
    fresh(a, makeModFile(L"ow.mod"));
    a.screen.openPanel(Panel::DiskOp);
    a.frame();
    PlayScreenProbe::setName(a.screen, L"ow_target");
    const std::wstring target = tmp(L"ow_target.xm");
    {
        std::ofstream(fs::path(target), std::ios::binary) << "OLD";
    }
    a.press(D_SAVEXM);
    CHECK(a.asks("FILE EXISTS. OVERWRITE?"));
    a.screen.answer(1);  // CANCEL: the old file stays
    CHECK_EQ((int)fileSize(target), 3);
    a.press(D_SAVEXM);
    CHECK(a.asks("FILE EXISTS. OVERWRITE?"));
    a.screen.answer(0);  // OVERWRITE
    CHECK(fileSize(target) > 3);
    // a new name saves without asking
    PlayScreenProbe::setName(a.screen, L"ow_new");
    a.press(D_SAVEXM);
    CHECK(a.screen.question() == nullptr);
    CHECK(fileSize(tmp(L"ow_new.xm")) > 0);
}

TEST(app_diskop_save_sample) {
    App& a = theApp();
    fresh(a, makeModFile(L"smp.mod"));
    a.screen.openPanel(Panel::DiskOp);
    a.frame();
    PlayScreenProbe::setName(a.screen, L"smp_out");
    a.press(D_SAVESMP);
    CHECK(a.status("SAMPLE SAVED (WAV)"));
    std::vector<uint8_t> bytes;
    CHECK(readFileW(tmp(L"smp_out.wav").c_str(), bytes) == nullptr);
    auto back = loadSampleFile(bytes.data(), bytes.size());
    CHECK(back.ok() && back.value().length == 2000);
    a.press(D_SAVESMP);  // exists now: asks first
    CHECK(a.asks("FILE EXISTS. OVERWRITE?"));
    a.screen.answer(1);
}

TEST(app_diskop_delete) {
    App& a = theApp();
    fresh(a, makeModFile(L"del.mod"));
    const std::wstring victim = tmp(L"do_usuniecia.txt");
    {
        std::ofstream(fs::path(victim)) << "x";
    }
    a.screen.openPanel(Panel::DiskOp);
    a.frame();
    a.press(D_ALL);  // lists every file of the song's folder
    CHECK(PlayScreenProbe::select(a.screen, L"do_usuniecia.txt"));
    a.press(D_DELETE);
    CHECK(a.screen.question() != nullptr);
    a.screen.answer(1);  // CANCEL
    CHECK(fs::exists(victim));
    CHECK(PlayScreenProbe::select(a.screen, L"do_usuniecia.txt"));
    a.press(D_DELETE);
    a.screen.answer(0);  // DELETE
    CHECK(a.status("FILE DELETED"));
    CHECK(!fs::exists(victim));
}

TEST(app_dropped_sample_goes_to_the_slot) {
    App& a = theApp();
    const std::wstring mod = makeModFile(L"drop.mod");
    fresh(a, mod);
    app::loadAnyFile(makeWavFile(L"probka.wav"));
    CHECK(a.screen.question() == nullptr);  // a sample never asks
    a.engine.lock();
    const Song* s = a.engine.song();
    const int idx = s ? a.screen.curSampleIndex(*s) : -1;
    const uint32_t len = idx >= 0 ? s->samples[idx].length : 0;
    a.engine.unlock();
    CHECK_EQ((int)len, 4000);
    CHECK(app::songPath() == mod);  // still the same song
}

TEST(app_dropped_module_asks_about_unsaved_changes) {
    App& a = theApp();
    const std::wstring first = makeModFile(L"first.mod");
    const std::wstring second = makeModFile(L"second.mod");
    fresh(a, first);
    a.screen.editor().markDirty();
    app::loadAnyFile(second);
    CHECK(a.asks("THE SONG HAS UNSAVED CHANGES"));
    a.screen.answer(2);  // CANCEL
    CHECK(app::songPath() == first);
    app::loadAnyFile(second);
    a.screen.answer(1);  // DISCARD
    CHECK(app::songPath() == second);
    CHECK(!a.screen.dirty());
}

TEST(app_close_save_discard_cancel) {
    App& a = theApp();
    int closed = 0;
    auto close = [&closed]() { ++closed; };
    // CANCEL keeps the window, DISCARD closes
    fresh(a, makeModFile(L"close.mod"));
    a.screen.editor().markDirty();
    app::confirmDiscard(close);
    a.screen.answer(2);
    CHECK_EQ(closed, 0);
    app::confirmDiscard(close);
    a.screen.answer(1);
    CHECK_EQ(closed, 1);
    // SAVE, the Save As dialog cancelled: stays open
    a.shell.saveAnswer.clear();
    app::confirmDiscard(close);
    a.screen.answer(0);
    CHECK_EQ(a.shell.saveCalls, 1);
    CHECK_EQ(closed, 1);
    // SAVE as MOD chosen in the dialog: exported, but the song is still unsaved, so it stays open
    a.shell.saveAnswer = tmp(L"close_as.xm");
    a.shell.saveKind = SaveKind::Mod;
    app::confirmDiscard(close);
    a.screen.answer(0);
    CHECK(fileSize(tmp(L"close_as.mod")) > 0);
    CHECK_EQ(closed, 1);
    // SAVE as XM: saved, then closes
    a.shell.saveAnswer = tmp(L"close_as.xm");
    a.shell.saveKind = SaveKind::Xm;
    app::confirmDiscard(close);
    a.screen.answer(0);
    CHECK(fileSize(tmp(L"close_as.xm")) > 0);
    CHECK_EQ(closed, 2);
    // an XM song saves in place (no dialog) and closes
    a.screen.editor().markDirty();
    const int dialogs = a.shell.saveCalls;
    app::confirmDiscard(close);
    a.screen.answer(0);
    CHECK_EQ(a.shell.saveCalls, dialogs);
    CHECK_EQ(closed, 3);
    // nothing unsaved: closes at once
    app::confirmDiscard(close);
    CHECK(a.screen.question() == nullptr);
    CHECK_EQ(closed, 4);
}

TEST(app_pure_floppy_wait) {
    App& a = theApp();
    const std::wstring before = makeModFile(L"pure_before.mod");
    const std::wstring song = makeModFile(L"pure_song.xm", 8);
    fresh(a, before);
    a.screen.setPure(1, a.engine);  // AMIGA 500
    CHECK(app::loadModuleFile(song));
    CHECK(app::loadPending());
    CHECK(a.shell.busy && a.shell.timer);
    CHECK(a.status("LOADING..."));
    CHECK(app::songPath() == before);  // not there yet
    app::finishPendingLoad();         // a key or click skips the wait
    CHECK(!app::loadPending());
    CHECK(!a.shell.busy && !a.shell.timer);
    CHECK(app::songPath() == song);
    CHECK(a.status("ALL RIGHT"));
    // the timer finishing does the same
    CHECK(app::loadModuleFile(before));
    app::floppyTimer();
    CHECK(app::songPath() == before);
    // closing during the read drops the half-read song
    CHECK(app::loadModuleFile(song));
    app::cancelPendingLoad();
    CHECK(!app::loadPending() && !a.shell.busy && !a.shell.timer);
    CHECK(app::songPath() == before);
    a.screen.setPure(0, a.engine);
}

TEST(app_chip_songs_tfmx_pair_and_sc68) {
    const fs::path mods = fs::path(FT_SOURCE_DIR) / "modules";
    const fs::path tfmx = mods / "Chris Huelsbeck" / "TFMX";
    const fs::path sc68 = mods / "AtariST" / "Count Zero - Awesome.sc68";
    if (!fs::exists(tfmx / "mdat.turrican 2 level 0-intro") || !fs::exists(sc68)) {
        std::printf("SKIP app_chip_songs_tfmx_pair_and_sc68 (modules folder not found)\n");
        return;
    }
    App& a = theApp();
    for (const char* pick : {"mdat.turrican 2 level 0-intro", "smpl.turrican 2 level 0-intro"}) {
        fresh(a, (tfmx / pick).wstring());
        CHECK(a.status("TFMX"));
        CHECK(std::strstr(a.screen.status(), "PLAYBACK ONLY") != nullptr);
        CHECK(app::songPath().empty());  // nothing to save over
        CHECK(a.shell.title == L"untitled");
    }
    app::handle(UiAction::SaveFile);  // playback only: says so, writes nothing
    CHECK(a.status("PLAYBACK ONLY - NOTHING TO SAVE"));
    fresh(a, sc68.wstring());
    CHECK(a.status("SC68"));
    a.engine.stop();
}
