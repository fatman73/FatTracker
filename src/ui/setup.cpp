// SETUP: themes, palette editor, mixing options; settings persistence (FatTracker.ini).
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>

#include "platform/os.h"
#include "ui/play_screen.h"
#include "ui/widgets.h"

namespace {
constexpr int kHitSetup = 900;
enum : int {
    ID_THEME = 0,      // + theme index (0..themeCount())
    ID_SWATCH = 10,    // + color index
    ID_RGB = 40,       // + channel*2 + (0 down, 1 up), channel 0..2
    ID_RGBBAR = 50,    // + channel
    ID_INTERP = 60,    // + 0..2
    ID_VALUE = 70,     // + row*2 + dir (rows: 0 separation, 1 volume, 2 default channels, 3 drive volume)
    ID_PURE = 80,      // + 0 off, 1 A500, 2 A1200
    ID_EXIT = 90
};
const char* const kInterp[3] = {"NONE", "LINEAR", "CUBIC"};
const char* const kPure[3] = {"OFF", "A500", "A1200"};
constexpr int kValueRows = 4;
}  // namespace

RectF PlayScreen::setupRect(int id) const {
    const RectF a = panelArea();
    if (id >= ID_THEME && id <= ID_THEME + 4) return {a.x + 14, a.y + 44 + (id - ID_THEME) * 46.f, 190, 40};
    if (id >= ID_SWATCH && id < ID_SWATCH + kThemeColors) {
        const int i = id - ID_SWATCH;
        return {a.x + 222 + (i % 5) * 120.f, a.y + 44 + (i / 5) * 34.f, 114, 30};
    }
    const float px = a.x + 222, py = a.y + 226;
    if (id >= ID_RGB && id < ID_RGB + 6) {
        const int ch = (id - ID_RGB) / 2, dir = (id - ID_RGB) % 2;
        return {dir ? px + 28 + 36 + 440 + 6 : px + 28, py + ch * 38.f, 32, 32};
    }
    if (id >= ID_RGBBAR && id < ID_RGBBAR + 3) return {px + 28 + 38, py + (id - ID_RGBBAR) * 38.f, 436, 32};
    const float ax = a.x + 840, aw = a.w - 840 - 14;
    if (id >= ID_INTERP && id < ID_INTERP + 3) return {ax + (id - ID_INTERP) * (aw / 3), a.y + 40, aw / 3 - 6, 34};
    if (id >= ID_VALUE && id < ID_VALUE + kValueRows * 2) {
        const int row = (id - ID_VALUE) / 2, dir = (id - ID_VALUE) % 2;
        return {dir ? ax + aw - 36 : ax + aw - 36 - 6 - 90 - 6 - 36, a.y + 86 + row * 42.f, 36, 34};
    }
    if (id >= ID_PURE && id < ID_PURE + 3) {
        const float bx = ax + 128, bw = (aw - 128) / 3;
        return {bx + (id - ID_PURE) * bw, a.y + 86 + kValueRows * 42.f, bw - 6, 34};
    }
    if (id == ID_EXIT) return {a.x + 14, a.y + a.h - 54, 190, 40};
    return {};
}

void PlayScreen::setupDraw(Gfx& g, Text& tx, const Theme& t) {
    const RectF a = panelArea();
    Ui u{g, tx, t};
    g.panel(a, t, Gfx::PanelState::Normal);
    auto hov = [&](int id) { return hover_ == kHitSetup + id; };
    tx.draw(g, {a.x + 14, a.y + 8, 190, 32}, "THEME", 16.f, t.label, Align::Left);
    for (int i = 0; i <= themeCount(); ++i) {
        Gfx::PanelState st = currentThemeIndex() == i ? Gfx::PanelState::Active
                             : hov(ID_THEME + i)      ? Gfx::PanelState::Hover
                                                      : Gfx::PanelState::Normal;
        drawButton(u, setupRect(ID_THEME + i), themeName(i), st, 15.f);
    }
    drawButton(u, setupRect(ID_EXIT), "EXIT", hov(ID_EXIT) ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 15.f);

    tx.draw(g, {a.x + 222, a.y + 8, 600, 32}, "PALETTE (EDITS THE CUSTOM THEME)", 16.f, t.label, Align::Left);
    Theme shown = currentTheme();
    for (int i = 0; i < kThemeColors; ++i) {
        RectF r = setupRect(ID_SWATCH + i);
        const Color c = themeColor(shown, i);
        g.fillRoundRect(r, 4.f, c);
        g.strokeRect(r, i == selColor_ ? t.cursor : withAlpha(t.panelDark, 0.8f), i == selColor_ ? 3.f : 1.f);
        const float lum = 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
        tx.draw(g, r, themeColorName(i), 11.f, lum > 0.55f ? Color{0, 0, 0, 1} : Color{1, 1, 1, 1}, Align::Center);
    }
    const Color sel = themeColor(shown, selColor_);
    const float comps[3] = {sel.r, sel.g, sel.b};
    const char* const names[3] = {"R", "G", "B"};
    const Color bars[3] = {rgb(0xE04040), rgb(0x40C060), rgb(0x4070E0)};
    for (int ch = 0; ch < 3; ++ch) {
        RectF bar = setupRect(ID_RGBBAR + ch);
        tx.draw(g, {bar.x - 66, bar.y, 28, bar.h}, names[ch], 16.f, t.label, Align::Center);
        g.well(bar, t, t.patternBg);
        g.fillRoundRect({bar.x + 3, bar.y + 3, (bar.w - 6) * comps[ch], bar.h - 6}, 3.f, bars[ch]);
        char v[8];
        std::snprintf(v, sizeof v, "%d", (int)(comps[ch] * 255 + 0.5f));
        tx.draw(g, {bar.x + bar.w + 44, bar.y, 60, bar.h}, v, 15.f, t.digits, Align::Left, FontRole::Mono);
        for (int d = 0; d < 2; ++d)
            drawButton(u, setupRect(ID_RGB + ch * 2 + d), d ? "\x04" : "\x03",
                       hov(ID_RGB + ch * 2 + d) ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 12.f);
    }

    const float ax = a.x + 840, aw = a.w - 840 - 14;
    tx.draw(g, {ax, a.y + 8, aw, 32}, "INTERPOLATION", 16.f, t.label, Align::Left);
    for (int i = 0; i < 3; ++i)
        drawButton(u, setupRect(ID_INTERP + i), kInterp[i],
                   interp_ == i ? Gfx::PanelState::Active : hov(ID_INTERP + i) ? Gfx::PanelState::Hover : Gfx::PanelState::Normal,
                   14.f);
    const char* labels[kValueRows] = {"STEREO SEPARATION", "MASTER VOLUME", "NEW SONG CH.", "DRIVE VOLUME"};
    for (int row = 0; row < kValueRows; ++row) {
        RectF dn = setupRect(ID_VALUE + row * 2), up = setupRect(ID_VALUE + row * 2 + 1);
        tx.draw(g, {ax, dn.y, dn.x - ax - 6, dn.h}, labels[row], 14.f, t.label, Align::Left);
        RectF val = {dn.x + dn.w + 6, dn.y, up.x - dn.x - dn.w - 12, dn.h};
        g.well(val, t, mixColor(t.panelLight, t.panel, 0.25f));
        char v[16];
        if (row == 0) std::snprintf(v, sizeof v, "%d%%", separationPct_);
        if (row == 1) std::snprintf(v, sizeof v, "%d%%", gainPct_);
        if (row == 2) std::snprintf(v, sizeof v, "%d", defaultChannels_);
        if (row == 3) std::snprintf(v, sizeof v, "%d%%", drivePct_);
        tx.draw(g, val, v, 15.f, t.digits, Align::Center, FontRole::Mono);
        drawButton(u, dn, "\x03", hov(ID_VALUE + row * 2) ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 12.f);
        drawButton(u, up, "\x04", hov(ID_VALUE + row * 2 + 1) ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 12.f);
    }
    const RectF p0 = setupRect(ID_PURE);
    tx.draw(g, {ax, p0.y, 124, p0.h}, "PURE MODE", 14.f, t.label, Align::Left);
    for (int i = 0; i < 3; ++i)
        drawButton(u, setupRect(ID_PURE + i), kPure[i],
                   pure_ == i ? Gfx::PanelState::Active : hov(ID_PURE + i) ? Gfx::PanelState::Hover : Gfx::PanelState::Normal,
                   14.f);
}

void PlayScreen::setPure(int mode, Engine& e) {
    mode = std::clamp(mode, 0, 2);
    if (mode && !pure_) {
        themeBeforePure_ = currentThemeIndex();
        setCurrentTheme(1);  // CLASSIC PT 2.3
    } else if (!mode && pure_) {
        setCurrentTheme(themeBeforePure_);
    }
    pure_ = mode;
    credits_ = false;
    // Pure has its own EDIT OP., POS ED. and SAMPLER; the other panels exist only on the normal screen
    if (mode ? panel_ != Panel::EditOp && panel_ != Panel::Sampler && panel_ != Panel::PosEd : panel_ == Panel::PosEd)
        panel_ = Panel::Main;
    drag_ = 0;
    e.setPure(mode);
    setStatus(mode == 0 ? "PURE MODE OFF" : mode == 1 ? "AMIGA 500" : "AMIGA 1200");  // Pure's STATUS shows 10 characters
}

void PlayScreen::applyMixing(Engine& e) {
    e.setMixing(interp_, separationPct_ / 100.f, gainPct_ / 100.f);
    e.setDriveVolume(drivePct_ / 100.f);
}

UiAction PlayScreen::setupMouse(float x, float y, bool right, Engine& e) {
    auto hit = [&](int id) { return setupRect(id).contains(x, y); };
    for (int i = 0; i <= themeCount(); ++i)
        if (hit(ID_THEME + i)) {
            if (i == themeCount() && currentThemeIndex() != i) customTheme() = currentTheme();
            setCurrentTheme(i);
            return UiAction::Redraw;
        }
    if (hit(ID_EXIT)) {
        panel_ = Panel::Main;
        return UiAction::Redraw;
    }
    for (int i = 0; i < kThemeColors; ++i)
        if (hit(ID_SWATCH + i)) {
            selColor_ = i;
            return UiAction::Redraw;
        }
    auto editColor = [&](auto&& fn) {
        if (currentThemeIndex() != themeCount()) {
            customTheme() = currentTheme();
            setCurrentTheme(themeCount());
        }
        fn(themeColor(customTheme(), selColor_));
    };
    for (int ch = 0; ch < 3; ++ch) {
        for (int d = 0; d < 2; ++d)
            if (hit(ID_RGB + ch * 2 + d)) {
                const float step = (right ? 16.f : 1.f) / 255.f * (d ? 1 : -1);
                editColor([&](Color& c) {
                    float* p = ch == 0 ? &c.r : ch == 1 ? &c.g : &c.b;
                    *p = std::clamp(*p + step, 0.f, 1.f);
                });
                return UiAction::Redraw;
            }
        if (hit(ID_RGBBAR + ch)) {
            const RectF bar = setupRect(ID_RGBBAR + ch);
            const float v = std::clamp((x - bar.x - 3) / (bar.w - 6), 0.f, 1.f);
            editColor([&](Color& c) { (ch == 0 ? c.r : ch == 1 ? c.g : c.b) = v; });
            return UiAction::Redraw;
        }
    }
    for (int i = 0; i < 3; ++i)
        if (hit(ID_INTERP + i)) {
            interp_ = i;
            applyMixing(e);
            return UiAction::Redraw;
        }
    for (int i = 0; i < 3; ++i)
        if (hit(ID_PURE + i)) {
            if (i != pure_) setPure(i, e);
            return UiAction::Redraw;
        }
    for (int row = 0; row < kValueRows; ++row)
        for (int d = 0; d < 2; ++d)
            if (hit(ID_VALUE + row * 2 + d)) {
                const int s = (d ? 1 : -1) * (right ? 10 : (row == 2 ? 1 : 5));
                if (row == 0) separationPct_ = std::clamp(separationPct_ + s, 0, 100);
                if (row == 1) gainPct_ = std::clamp(gainPct_ + s, 10, 400);
                if (row == 2) defaultChannels_ = std::clamp(defaultChannels_ + (d ? 1 : -1) * (right ? 4 : 1), 4, 32);
                if (row == 3) drivePct_ = std::clamp(drivePct_ + s, 0, 100);
                applyMixing(e);
                return UiAction::Redraw;
            }
    return UiAction::None;
}

// ---------------------------------------------------------------- settings file

namespace {
int readInt(const wchar_t* ini, const wchar_t* key, int def) { return osIniInt(ini, L"FatTracker", key, def); }
void writeInt(const wchar_t* ini, const wchar_t* key, int v) { osIniWrite(ini, L"FatTracker", key, std::to_wstring(v)); }
uint32_t pack(Color c) {
    auto q = [](float v) { return (uint32_t)(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}
}  // namespace

void PlayScreen::loadSettings(const wchar_t* ini, Engine& e) {
    for (int i = 0; i < kThemeColors; ++i) {
        const std::wstring key = L"Color" + std::to_wstring(i);
        const std::wstring val = osIniString(ini, L"CustomTheme", key.c_str()).substr(0, 15);
        if (!val.empty()) themeColor(customTheme(), i) = rgb((uint32_t)wcstoul(val.c_str(), nullptr, 16));
    }
    setCurrentTheme(readInt(ini, L"Theme", 0));
    interp_ = std::clamp(readInt(ini, L"Interpolation", 2), 0, 2);
    separationPct_ = std::clamp(readInt(ini, L"StereoSeparation", 100), 0, 100);
    gainPct_ = std::clamp(readInt(ini, L"MasterVolume", 100), 10, 400);
    defaultChannels_ = std::clamp(readInt(ini, L"NewSongChannels", 4), 4, 32);
    drivePct_ = std::clamp(readInt(ini, L"DriveVolume", 70), 0, 100);
    setViewMode(readInt(ini, L"View", 8));
    octave_ = std::clamp(readInt(ini, L"Octave", 4), 0, 6);
    editSkip_ = std::clamp(readInt(ini, L"EditSkip", 1), 0, 16);
    recordStep_ = readInt(ini, L"RecordLive", 0) == 0;  // STEP unless LIVE was chosen
    quantize_ = std::clamp(readInt(ini, L"RecordQuantize", 1), 1, 16);
    metronome_ = readInt(ini, L"Metronome", 0) != 0;
    metroEvery_ = std::clamp(readInt(ini, L"MetronomeRows", 4), 1, 16);
    const std::wstring dir = osIniString(ini, L"FatTracker", L"Directory");
    if (!dir.empty()) dir_ = dir;
    applyMixing(e);
    applyMetronome(e);
    const int pure = std::clamp(readInt(ini, L"PureMode", 0), 0, 2);
    if (pure) {
        pure_ = pure;  // the saved theme already is the Pure one
        themeBeforePure_ = std::clamp(readInt(ini, L"ThemeBeforePure", 0), 0, themeCount());
        e.setPure(pure);
    }
}

void PlayScreen::saveSettings(const wchar_t* ini) const {
    writeInt(ini, L"Theme", currentThemeIndex());
    writeInt(ini, L"Interpolation", interp_);
    writeInt(ini, L"StereoSeparation", separationPct_);
    writeInt(ini, L"MasterVolume", gainPct_);
    writeInt(ini, L"NewSongChannels", defaultChannels_);
    writeInt(ini, L"DriveVolume", drivePct_);
    writeInt(ini, L"PureMode", pure_);
    writeInt(ini, L"ThemeBeforePure", themeBeforePure_);
    writeInt(ini, L"View", viewMode_);
    writeInt(ini, L"Octave", octave_);
    writeInt(ini, L"EditSkip", editSkip_);
    writeInt(ini, L"RecordLive", recordStep_ ? 0 : 1);
    writeInt(ini, L"RecordQuantize", quantize_);
    writeInt(ini, L"Metronome", metronome_ ? 1 : 0);
    writeInt(ini, L"MetronomeRows", metroEvery_);
    if (!dir_.empty() && dir_ != L"::drives") osIniWrite(ini, L"FatTracker", L"Directory", dir_);
    Theme& c = customTheme();
    for (int i = 0; i < kThemeColors; ++i) {
        const std::wstring key = L"Color" + std::to_wstring(i);
        wchar_t val[16];
        std::swprintf(val, 16, L"%06X", (unsigned)pack(themeColor(c, i)));
        osIniWrite(ini, L"CustomTheme", key.c_str(), val);
    }
}
