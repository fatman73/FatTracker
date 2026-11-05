#include "ui/play_screen.h"

#include <cstdio>
#include <cstring>

#include "ui/widgets.h"

namespace {
// Layout (logical 1920x1080 canvas)
constexpr float kTopH = 360.f;
constexpr float kInfoY = 360.f, kInfoH = 80.f;
constexpr float kPatY = 440.f;
constexpr float kCounterW = 560.f, kCounterH = 36.f;
constexpr float kButtonsX = 560.f, kButtonsW = 620.f;
constexpr float kRightX = 1180.f;
constexpr float kSidePanelW = 260.f;

struct Btn {
    const char* label;
    int id;
};
enum { B_PLAY, B_STOP, B_PATTERN, B_CLEAR, B_EDIT, B_EDITOP, B_RECORD, B_DISKOP, B_PLST, B_SETUP, B_SAMPLER, B_HELP };
const Btn kButtons[12] = {{"PLAY", B_PLAY},       {"STOP", B_STOP},     {"PATTERN", B_PATTERN},
                          {"CLEAR", B_CLEAR},     {"EDIT", B_EDIT},     {"EDIT OP.", B_EDITOP},
                          {"RECORD", B_RECORD},   {"DISK OP.", B_DISKOP}, {"PLST", B_PLST},
                          {"SETUP", B_SETUP},     {"SAMPLER", B_SAMPLER}, {"HELP", B_HELP}};
constexpr int kBtnCols = 3, kBtnRows = 4;
constexpr float kBtnH = 60.f;

RectF buttonRect(int i) {
    const float w = kButtonsW / kBtnCols;
    return {kButtonsX + (i % kBtnCols) * w, (float)(i / kBtnCols) * kBtnH, w, kBtnH};
}

const char* const kSideButtons[] = {"INSERT", "CUT",     "DELETE",  "COPY",    "CLEAR",   "PASTE",
                                    "OCT.UP", "OCT.DN",  "NOTE UP", "NOTE DN", "SCR.UP",  "SCR.DN",
                                    "ROT.UP", "ROT.DN",  "FLIP",    "EXCHG"};

RectF patternArea(int viewMode) {
    return {0.f, kPatY, viewMode >= 16 ? 1920.f : 1920.f - kSidePanelW, 1080.f - kPatY};
}
}  // namespace

void PlayScreen::setStatus(const char* msg) {
    std::snprintf(status_, sizeof status_, "%s", msg ? msg : "ALL RIGHT");
}

void PlayScreen::songChanged() {
    viewRow_ = 0;
    viewPattern_ = 0;
    firstChannel_ = 0;
}

PatternCtx PlayScreen::ctx(const Song* s, const Telemetry& tel) const {
    PatternCtx c{};
    c.song = s;
    c.tel = &tel;
    c.viewMode = viewMode_;
    c.firstChannel = firstChannel_;
    if (tel.playing) {
        c.pattern = tel.pattern;
        c.row = tel.row;
    } else {
        c.pattern = viewPattern_;
        c.row = viewRow_;
    }
    return c;
}

void PlayScreen::draw(Gfx& g, Font& f, const Theme& t, Engine& e) {
    Telemetry tel;
    e.snapshot(tel);
    last_ = tel;
    const Song* s = e.song();
    if (tel.playing) {
        viewPattern_ = tel.pattern;
        viewRow_ = tel.row;
    }
    const PatternCtx pc = ctx(s, tel);
    char buf[96];

    // ---------- top-left counters ----------
    const Sample* smp = s && !s->samples.empty() ? &s->samples[0] : nullptr;
    struct Ctr {
        const char* label;
        char value[16];
    } ctrs[10];
    auto setc = [&](int i, const char* l, const char* fmt, unsigned v) {
        ctrs[i].label = l;
        std::snprintf(ctrs[i].value, sizeof ctrs[i].value, fmt, v);
    };
    setc(0, "POS", "%04u", tel.order);
    setc(1, "PATTERN", "%04u", (unsigned)pc.pattern);
    setc(2, "LENGTH", "%04u", s ? (unsigned)s->order.size() : 0u);
    setc(3, "FINETUNE", "%u", smp ? (unsigned)(smp->finetune / 16 & 15) : 0u);
    setc(4, "SAMPLE", "%04u", 1u);
    setc(5, "VOLUME", "%04X", smp ? smp->volume : 0u);
    setc(6, "LENGTH", "%05X", smp ? smp->length : 0u);
    setc(7, "REPEAT", "%05X", smp ? smp->loopStart : 0u);
    setc(8, "REPLEN", "%05X", smp ? (smp->loop == LoopType::None ? 2u : smp->loopLength) : 0u);
    setc(9, "CHANNELS", "%04u", s ? s->numChannels : 4u);
    for (int i = 0; i < 10; ++i)
        drawCounter(g, f, t, {0.f, i * kCounterH, kCounterW, kCounterH}, ctrs[i].label, ctrs[i].value);

    // ---------- buttons ----------
    for (int i = 0; i < kBtnCols * kBtnRows; ++i) {
        bool pressed = (kButtons[i].id == B_PLAY && tel.playing);
        drawButton(g, f, t, buttonRect(i), kButtons[i].label, pressed, 22.f);
    }
    // logo panel
    RectF logo = {kButtonsX, kBtnRows * kBtnH, kButtonsW, kTopH - kBtnRows * kBtnH};
    g.bevel(logo, t, false);
    g.fillRect({logo.x + 10, logo.y + 10, logo.w - 20, logo.h - 20}, t.patternBg);
    f.drawText(g, logo.x + 34, logo.y + 26, 44.f, "FATTRACKER", t.patternText);
    f.drawText(g, logo.x + 36, logo.y + 82, 14.f, "32 CHANNELS  16 BIT  V0.1", t.rowNumber);

    // ---------- scopes ----------
    g.bevel({kRightX, 0.f, 1920.f - kRightX, kTopH}, t, false);
    const int nScopes = pc.viewMode < (s ? s->numChannels : 4) ? pc.viewMode : (s ? s->numChannels : 4);
    const int first = computePatternLayout(viewMode_, s ? s->numChannels : 4, firstChannel_, 1.f).firstChannel;
    const int cols = nScopes <= 4 ? nScopes : (nScopes <= 8 ? 4 : 8);
    const int rows = (nScopes + cols - 1) / cols;
    const float sx0 = kRightX + 16.f, sy0 = 14.f, sw = 1920.f - kRightX - 32.f, sh = 276.f;
    const float cw = sw / cols, chh = sh / rows;
    RectF scopeRects[32];
    for (int i = 0; i < nScopes; ++i) {
        scopeRects[i] = {sx0 + (i % cols) * cw + 5.f, sy0 + (i / cols) * chh + 5.f, cw - 10.f, chh - 10.f};
        drawScopeBg(g, t, scopeRects[i]);
    }
    // clock line
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const char* const mon[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const unsigned playSec = tel.rate ? (unsigned)(tel.playFrames / tel.rate) : 0u;
    std::snprintf(buf, sizeof buf, "DATE %02u-%s-%02u  TIME %02u:%02u:%02u  PLAY %02u:%02u:%02u", st.wDay,
                  mon[(st.wMonth + 11) % 12], st.wYear % 100, st.wHour, st.wMinute, st.wSecond, playSec / 3600,
                  playSec / 60 % 60, playSec % 60);
    RectF clock = {kRightX + 10.f, 300.f, 1920.f - kRightX - 20.f, 50.f};
    g.bevel(clock, t, true);
    drawText(g, f, clock, buf, 14.f, t.digits, Align::Center);

    // ---------- info bar ----------
    RectF r1 = {0.f, kInfoY, 1920.f, kInfoH / 2};
    RectF r2 = {0.f, kInfoY + kInfoH / 2, 1920.f, kInfoH / 2};
    g.bevel(r1, t, false);
    g.bevel(r2, t, false);
    char name[21] = {};
    if (s) std::memcpy(name, s->name, 20);
    for (char& ch : name)
        if (ch && (ch < 32 || ch > 126)) ch = ' ';
    std::snprintf(buf, sizeof buf, "SONGNAME: %-20s", name[0] ? name : "untitled");
    f.drawText(g, 16.f, r1.y + 11.f, 18.f, "SONGNAME:", t.label);
    f.drawText(g, 16.f + 10 * 18.f, r1.y + 11.f, 18.f, name[0] ? name : "untitled", t.digits);
    std::snprintf(buf, sizeof buf, "TEMPO %02u  BPM %03u", tel.speed, tel.bpm);
    f.drawText(g, 1180.f, r1.y + 11.f, 18.f, buf, t.label);
    char sname[23] = {};
    if (smp) std::memcpy(sname, smp->name, 22);
    for (char& ch : sname)
        if (ch && (ch < 32 || ch > 126)) ch = ' ';
    f.drawText(g, 16.f, r2.y + 11.f, 18.f, "SAMPLENAME:", t.label);
    f.drawText(g, 16.f + 12 * 18.f, r2.y + 11.f, 18.f, sname, t.digits);
    f.drawText(g, 820.f, r2.y + 11.f, 18.f, "STATUS:", t.label);
    f.drawText(g, 820.f + 8 * 18.f, r2.y + 11.f, 18.f, status_,
               std::strncmp(status_, "ERROR", 5) == 0 ? t.cursor : t.digits);
    size_t bytes = 0;
    if (s)
        for (const Sample& sm : s->samples) bytes += (size_t)sm.length * (sm.is16 ? 2 : 1);
    std::snprintf(buf, sizeof buf, "SAMPLES %6uK", (unsigned)(bytes / 1024));
    f.drawText(g, 1600.f, r2.y + 11.f, 18.f, buf, t.label);

    // ---------- pattern ----------
    RectF pa = patternArea(viewMode_);
    pattern_.drawBack(g, f, t, pa, pc);
    if (viewMode_ < 16) {
        RectF side = {1920.f - kSidePanelW, kPatY, kSidePanelW, 1080.f - kPatY};
        g.bevel(side, t, false);
        std::snprintf(buf, sizeof buf, "\x03%02u\x04  \x03%03u\x04", (unsigned)pc.pattern, tel.bpm);
        drawText(g, f, {side.x, side.y + 6, side.w, 36}, buf, 20.f, t.digits, Align::Center);
        drawButton(g, f, t, {side.x + 8, side.y + 48, side.w - 16, 44}, "TRACK", false, 18.f);
        for (int i = 0; i < 16; ++i) {
            RectF br = {side.x + 8 + (i % 2) * (side.w - 16) / 2, side.y + 100 + (i / 2) * 50.f, (side.w - 16) / 2,
                        46};
            drawButton(g, f, t, br, kSideButtons[i], false, 15.f);
        }
    }

    // ---------- glowing layer: scope lines + VU meters ----------
    g.beginGlow();
    for (int i = 0; i < nScopes; ++i) {
        e.scope(first + i, scopeBuf_, 256);
        drawScopeLine(g, t, scopeRects[i], scopeBuf_, 256);
    }
    pattern_.drawMeters(g, t, pa, pc);
    g.endGlow(6.f, t.glow);

    pattern_.drawText(g, f, t, pa, pc);

    bool anyVu = false;
    for (int c = 0; c < 32; ++c) anyVu = anyVu || tel.ch[c].vu > 0.002f;
    animating_ = tel.playing || anyVu;
}

void PlayScreen::toggleMute(int ch, Engine& e) { e.setMute(ch, !last_.ch[ch].muted); }

void PlayScreen::solo(int ch, Engine& e) {
    const int n = e.song() ? e.song()->numChannels : 0;
    bool alreadySolo = !last_.ch[ch].muted;
    for (int c = 0; c < n; ++c)
        if (c != ch && !last_.ch[c].muted) alreadySolo = false;
    for (int c = 0; c < n; ++c) e.setMute(c, alreadySolo ? false : c != ch);
}

void PlayScreen::setViewMode(int m) { viewMode_ = (m == 4 || m == 16) ? m : 8; }

bool PlayScreen::onKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, Engine& e) {
    const Song* s = e.song();
    const int nch = s ? s->numChannels : 4;
    if ((vk == VK_MENU && extended) || vk == VK_F5) {
        e.play();
        setStatus("ALL RIGHT");
        return true;
    }
    if (vk == VK_CONTROL && extended) {
        e.playPattern((uint16_t)viewPattern_);
        return true;
    }
    if (alt) return false;
    switch (vk) {
        case VK_SPACE:
            e.stop();
            return true;
        case VK_F11:
            viewMode_ = viewMode_ == 4 ? 8 : (viewMode_ == 8 ? 16 : 4);
            return true;
        case VK_TAB:
            firstChannel_ += shift ? -1 : 1;
            firstChannel_ = computePatternLayout(viewMode_, nch, firstChannel_, 1.f).firstChannel;
            return true;
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT: {
            if (last_.playing || !s || s->patterns.empty()) return false;
            int step = (vk == VK_UP || vk == VK_DOWN) ? 1 : 16;
            if (vk == VK_UP || vk == VK_PRIOR) step = -step;
            const int rows = s->patterns[viewPattern_ < (int)s->patterns.size() ? viewPattern_ : 0].rows;
            viewRow_ = (viewRow_ + step + rows * 16) % rows;
            return true;
        }
        case VK_LEFT:
        case VK_RIGHT:
            if (last_.playing || !s) return false;
            viewPattern_ += vk == VK_RIGHT ? 1 : -1;
            if (viewPattern_ < 0) viewPattern_ = (int)s->patterns.size() - 1;
            if (viewPattern_ >= (int)s->patterns.size()) viewPattern_ = 0;
            viewRow_ = 0;
            return true;
    }
    if (ctrl && vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) {
        int ch = computePatternLayout(viewMode_, nch, firstChannel_, 1.f).firstChannel + (int)(vk - VK_NUMPAD1);
        if (ch < nch) toggleMute(ch, e);
        return true;
    }
    return false;
}

bool PlayScreen::onMouse(float x, float y, bool right, Engine& e) {
    for (int i = 0; i < kBtnCols * kBtnRows; ++i) {
        if (!buttonRect(i).contains(x, y)) continue;
        switch (kButtons[i].id) {
            case B_PLAY:
                e.play();
                setStatus("ALL RIGHT");
                break;
            case B_STOP:
                e.stop();
                break;
            case B_PATTERN:
                e.playPattern((uint16_t)viewPattern_);
                break;
            default:
                setStatus("AVAILABLE IN STAGE 2");
                break;
        }
        return true;
    }
    const Telemetry tel = last_;
    const int ch = pattern_.headerHit(patternArea(viewMode_), ctx(e.song(), tel), x, y);
    if (ch >= 0) {
        if (right)
            solo(ch, e);
        else
            toggleMute(ch, e);
        return true;
    }
    return false;
}
