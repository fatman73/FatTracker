#include "ui/play_screen.h"

#include <cstdio>
#include <cstring>

#include "ui/widgets.h"

namespace {
constexpr float M = 8.f;          // outer margin
constexpr float G = 6.f;          // gap between panels
constexpr float kTopH = 352.f;    // counters / buttons / scopes band
constexpr float kInfoH = 80.f;
constexpr float kCountersW = 560.f;
constexpr float kButtonsW = 620.f;
constexpr float kSideW = 260.f;

enum { B_PLAY, B_STOP, B_PATTERN, B_CLEAR, B_EDIT, B_EDITOP, B_RECORD, B_DISKOP, B_PLST, B_SETUP, B_SAMPLER, B_HELP };
const char* const kButtons[12] = {"PLAY",   "STOP",     "PATTERN", "CLEAR", "EDIT",    "EDIT OP.",
                                  "RECORD", "DISK OP.", "PLST",    "SETUP", "SAMPLER", "HELP"};
const char* const kSideButtons[] = {"INSERT", "CUT",     "DELETE", "COPY",    "CLEAR",  "PASTE",
                                    "OCT UP", "OCT DN",  "NOTE UP", "NOTE DN", "SCR UP", "SCR DN",
                                    "ROT UP", "ROT DN",  "FLIP",    "EXCHG"};
const char* const kCounterLabels[10] = {"POS",    "PATTERN", "LENGTH", "FINETUNE", "SAMPLE",
                                        "VOLUME", "LENGTH",  "REPEAT", "REPLEN",   "CHANNELS"};
const bool kCounterEditable[10] = {true, true, false, true, true, true, false, true, true, true};
constexpr int kViewModes[4] = {4, 8, 16, 32};

// hit ids
constexpr int H_BUTTON = 0, H_ARROW = 100, H_VIEW = 200;
}  // namespace

PlayScreen::Layout PlayScreen::layout(const Viewport& vp) const {
    Layout l{};
    l.W = vp.w;
    l.H = vp.h;
    l.counters = {M, M, kCountersW, kTopH};
    l.buttons = {M + kCountersW + G, M, kButtonsW, 4 * 58.f - G};
    l.logo = {l.buttons.x, M + 4 * 58.f, kButtonsW, kTopH - 4 * 58.f};
    l.scopes = {l.buttons.x + kButtonsW + G, M, l.W - (l.buttons.x + kButtonsW + G) - M, kTopH};
    l.clock = {l.scopes.x + 10, l.scopes.y + l.scopes.h - 50, l.scopes.w - 20, 40};
    l.scopeGrid = {l.scopes.x + 10, l.scopes.y + 10, l.scopes.w - 20, l.scopes.h - 70};
    l.info = {M, M + kTopH + G, l.W - 2 * M, kInfoH};
    const float py = l.info.y + kInfoH + G;
    // the side panel is always visible; the pattern takes the rest of the width
    l.pattern = {M, py, l.W - 2 * M - kSideW - G, l.H - py - M};
    l.side = {l.W - M - kSideW, py, kSideW, l.H - py - M};
    return l;
}

RectF PlayScreen::counterRow(int i) const {
    const float h = (L_.counters.h - 12) / 10.f;
    return {L_.counters.x + 10, L_.counters.y + 6 + i * h, L_.counters.w - 16, h};
}

RectF PlayScreen::buttonRect(int i) const {
    const float w = (kButtonsW - 2 * G) / 3.f;
    return {L_.buttons.x + (i % 3) * (w + G), L_.buttons.y + (i / 3) * 58.f, w, 52.f};
}

RectF PlayScreen::viewButton(int i) const {
    return {L_.info.x + L_.info.w - 10 - (4 - i) * 58.f, L_.info.y + 5, 52.f, 30.f};
}

int PlayScreen::hitId(float x, float y) const {
    for (int i = 0; i < 12; ++i)
        if (buttonRect(i).contains(x, y)) return H_BUTTON + i;
    for (int r = 0; r < 10; ++r)
        for (int k = 0; k < 2; ++k)
            if (counterArrow(counterRow(r), k).contains(x, y)) return H_ARROW + r * 2 + k;
    for (int i = 0; i < 4; ++i)
        if (viewButton(i).contains(x, y)) return H_VIEW + i;
    return -1;
}

void PlayScreen::setStatus(const char* msg) {
    std::snprintf(status_, sizeof status_, "%s", msg ? msg : "ALL RIGHT");
}

void PlayScreen::clearStatusIf(const char* prefix) {
    if (std::strncmp(status_, prefix, std::strlen(prefix)) == 0) setStatus("ALL RIGHT");
}

void PlayScreen::songChanged() {
    viewRow_ = viewPattern_ = viewOrder_ = sample_ = firstChannel_ = 0;
}

void PlayScreen::setViewMode(int m) { viewMode_ = (m == 4 || m == 16 || m == 32) ? m : 8; }

PatternCtx PlayScreen::ctx(const Song* s, const Telemetry& tel) const {
    PatternCtx c{};
    c.song = s;
    c.tel = &tel;
    c.viewMode = viewMode_;
    c.firstChannel = firstChannel_;
    c.pattern = tel.playing ? tel.pattern : viewPattern_;
    c.row = tel.playing ? tel.row : viewRow_;
    return c;
}

void PlayScreen::draw(Gfx& g, Text& tx, Font& f, const Theme& t, Engine& e) {
    font_ = &f;
    L_ = layout(g.viewport());
    Ui u{g, tx, t};
    Telemetry tel;
    e.snapshot(tel);
    last_ = tel;
    const Song* s = e.song();
    if (tel.playing) {
        viewPattern_ = tel.pattern;
        viewRow_ = tel.row;
        viewOrder_ = tel.order;
    }
    if (s && sample_ >= (int)s->samples.size()) sample_ = 0;
    const PatternCtx pc = ctx(s, tel);
    char buf[128];

    // ---------- counters ----------
    g.panel(L_.counters, t, Gfx::PanelState::Normal);
    const Sample* smp = s && sample_ < (int)s->samples.size() ? &s->samples[sample_] : nullptr;
    unsigned vals[10] = {(unsigned)viewOrder_,
                         (unsigned)pc.pattern,
                         s ? (unsigned)s->order.size() : 0u,
                         0u,
                         (unsigned)sample_ + 1,
                         smp ? smp->volume : 0u,
                         smp ? smp->length : 0u,
                         smp ? smp->loopStart : 0u,
                         smp ? (smp->loop == LoopType::None ? 2u : smp->loopLength) : 0u,
                         s ? s->numChannels : 4u};
    for (int i = 0; i < 10; ++i) {
        char v[16];
        if (i == 3) {
            const int ft = smp ? smp->finetune / 16 : 0;
            std::snprintf(v, sizeof v, "%+d", ft);
        } else if (i >= 6 && i <= 8) {
            std::snprintf(v, sizeof v, "%05X", vals[i]);
        } else if (i == 5) {
            std::snprintf(v, sizeof v, "%02X", vals[i]);
        } else {
            std::snprintf(v, sizeof v, "%04u", vals[i]);
        }
        int hov = (hover_ >= H_ARROW && hover_ < H_ARROW + 20 && (hover_ - H_ARROW) / 2 == i) ? (hover_ - H_ARROW) % 2
                                                                                              : -1;
        drawCounter(u, counterRow(i), kCounterLabels[i], v, kCounterEditable[i], hov);
    }

    // ---------- buttons ----------
    for (int i = 0; i < 12; ++i) {
        Gfx::PanelState st = Gfx::PanelState::Normal;
        if (hover_ == H_BUTTON + i) st = Gfx::PanelState::Hover;
        if ((i == B_PLAY && tel.playing) || (i == B_STOP && !tel.playing && false)) st = Gfx::PanelState::Active;
        drawButton(u, buttonRect(i), kButtons[i], st, 19.f);
    }
    g.well(L_.logo, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, {L_.logo.x, L_.logo.y + 8, L_.logo.w, 64}, "FATTRACKER", 54.f, t.digits, Align::Center);

    // ---------- scopes ----------
    g.panel(L_.scopes, t, Gfx::PanelState::Normal);
    const int nch = s ? s->numChannels : 4;
    // one scope per song channel (up to 32), independent of the pattern view
    const int nScopes = nch;
    const int first = 0;
    const int cols = nScopes <= 4 ? nScopes : (nScopes <= 8 ? 4 : 8);
    const int rows = (nScopes + cols - 1) / cols;
    const float cw = L_.scopeGrid.w / cols, chh = L_.scopeGrid.h / rows;
    RectF scopeRects[32];
    for (int i = 0; i < nScopes; ++i) {
        scopeRects[i] = {L_.scopeGrid.x + (i % cols) * cw + 3, L_.scopeGrid.y + (i / cols) * chh + 3, cw - 6, chh - 6};
        drawScopeBg(g, t, scopeRects[i]);
        std::snprintf(buf, sizeof buf, "%d", first + i + 1);
        tx.draw(g, {scopeRects[i].x + 2, scopeRects[i].y + 2, 40, 18}, buf, 11.f, withAlpha(t.scopeLine, 0.55f),
                Align::Left, FontRole::Mono);
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const char* const mon[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const unsigned playSec = tel.rate ? (unsigned)(tel.playFrames / tel.rate) : 0u;
    g.well(L_.clock, t, mixColor(t.panelLight, t.panel, 0.25f));
    std::snprintf(buf, sizeof buf, "DATE %02u-%s-%02u     TIME %02u:%02u:%02u     PLAY %02u:%02u", st.wDay,
                  mon[(st.wMonth + 11) % 12], st.wYear % 100, st.wHour, st.wMinute, st.wSecond, playSec / 60,
                  playSec % 60);
    tx.draw(g, L_.clock, buf, 16.f, t.digits, Align::Center, FontRole::Mono);

    // ---------- info bar ----------
    g.panel(L_.info, t, Gfx::PanelState::Normal);
    const float rowH = (kInfoH - 10) / 2;
    const float y1 = L_.info.y + 5, y2 = y1 + rowH;
    char name[23] = {};
    if (s) std::memcpy(name, s->name, 20);
    for (char& ch : name)
        if (ch && (ch < 32 || ch > 126)) ch = ' ';
    tx.draw(g, {L_.info.x + 6, y1, 140, rowH}, "SONGNAME", 15.f, t.label, Align::Left);
    RectF nameField = {L_.info.x + 150, y1 + 3, 380, rowH - 6};
    g.well(nameField, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, nameField, name[0] ? name : "untitled", 16.f, t.digits, Align::Left, FontRole::Mono);
    std::snprintf(buf, sizeof buf, "TEMPO %02u   BPM %03u", tel.speed, tel.bpm);
    tx.draw(g, {L_.info.x + 560, y1, 300, rowH}, buf, 16.f, t.label, Align::Left, FontRole::Mono);
    tx.draw(g, {viewButton(0).x - 90, y1, 84, rowH}, "VIEW", 15.f, t.label, Align::Right);
    for (int i = 0; i < 4; ++i) {
        Gfx::PanelState vs = viewMode_ == kViewModes[i] ? Gfx::PanelState::Active
                             : hover_ == H_VIEW + i       ? Gfx::PanelState::Hover
                                                          : Gfx::PanelState::Normal;
        std::snprintf(buf, sizeof buf, "%d", kViewModes[i]);
        drawButton(u, viewButton(i), buf, vs, 16.f);
    }
    char sname[23] = {};
    if (smp) std::memcpy(sname, smp->name, 22);
    for (char& ch : sname)
        if (ch && (ch < 32 || ch > 126)) ch = ' ';
    tx.draw(g, {L_.info.x + 6, y2, 140, rowH}, "SAMPLENAME", 15.f, t.label, Align::Left);
    RectF snField = {L_.info.x + 150, y2 + 3, 380, rowH - 6};
    g.well(snField, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, snField, sname, 16.f, t.digits, Align::Left, FontRole::Mono);
    tx.draw(g, {L_.info.x + 560, y2, 90, rowH}, "STATUS", 15.f, t.label, Align::Left);
    RectF stField = {L_.info.x + 650, y2 + 3, L_.info.w - 650 - 220, rowH - 6};
    g.well(stField, t, mixColor(t.panelLight, t.panel, 0.25f));
    const bool err = std::strncmp(status_, "ERROR", 5) == 0;
    tx.draw(g, stField, status_, 16.f, err ? t.cursor : t.digits, Align::Left, FontRole::Mono);
    size_t bytes = 0;
    if (s)
        for (const Sample& sm : s->samples) bytes += (size_t)sm.length * (sm.is16 ? 2 : 1);
    std::snprintf(buf, sizeof buf, "SAMPLES %uK", (unsigned)(bytes / 1024));
    tx.draw(g, {L_.info.x + L_.info.w - 214, y2, 208, rowH}, buf, 15.f, t.label, Align::Right, FontRole::Mono);

    // ---------- pattern + side panel ----------
    pattern_.drawBack(g, tx, f, t, L_.pattern, pc);
    {
        g.panel(L_.side, t, Gfx::PanelState::Normal);
        std::snprintf(buf, sizeof buf, "\x03 %02u \x04     \x03 %03u \x04", (unsigned)pc.pattern, tel.bpm);
        RectF hdr = {L_.side.x + 8, L_.side.y + 8, L_.side.w - 16, 34};
        g.well(hdr, t, mixColor(t.panelLight, t.panel, 0.25f));
        tx.draw(g, hdr, buf, 16.f, t.digits, Align::Center, FontRole::Mono);
        const float bw = (L_.side.w - 16 - G) / 2;
        for (int i = 0; i < 16; ++i) {
            RectF br = {L_.side.x + 8 + (i % 2) * (bw + G), L_.side.y + 52 + (i / 2) * 46.f, bw, 40};
            drawButton(u, br, kSideButtons[i], Gfx::PanelState::Normal, 14.f);
        }
    }

    // ---------- glowing layer: logo, scope lines, VU meters ----------
    g.beginGlow();
    for (int i = 0; i < nScopes; ++i) {
        e.scope(first + i, scopeBuf_, 256);
        drawScopeLine(g, t, scopeRects[i], scopeBuf_, 256);
    }
    pattern_.drawMeters(g, f, t, L_.pattern, pc);
    g.endGlow(7.f, t.glow);
    tx.draw(g, {L_.logo.x, L_.logo.y + 72, L_.logo.w, 30}, "32 CHANNELS   \xB7   16 BIT   \xB7   V0.2", 14.f,
            t.panelDark, Align::Center);

    pattern_.drawText(g, f, t, L_.pattern, pc);

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

void PlayScreen::counterStep(int row, int dir, bool big, Engine& e) {
    const Song* s = e.song();
    if (!s) return;
    const bool stopped = !last_.playing;
    switch (row) {
        case 0:  // POS
            if (!stopped || s->order.empty()) return;
            viewOrder_ = (viewOrder_ + dir + (int)s->order.size()) % (int)s->order.size();
            viewPattern_ = s->order[viewOrder_];
            viewRow_ = 0;
            return;
        case 1:  // PATTERN
            if (!stopped || s->patterns.empty()) return;
            viewPattern_ = (viewPattern_ + dir + (int)s->patterns.size()) % (int)s->patterns.size();
            viewRow_ = 0;
            return;
        case 4:  // SAMPLE
            if (s->samples.empty()) return;
            sample_ = (sample_ + dir * (big ? 16 : 1) + (int)s->samples.size() * 16) % (int)s->samples.size();
            return;
        case 9: {  // CHANNELS
            const int n = s->numChannels + dir * (big ? 4 : 1);
            e.edit([n](Song& song) { setChannelCount(song, n); });
            firstChannel_ = computePatternLayout(viewMode_, e.song()->numChannels, firstChannel_, 1.f).firstChannel;
            char m[48];
            std::snprintf(m, sizeof m, "CHANNELS: %d", e.song()->numChannels);
            setStatus(m);
            return;
        }
        default:
            break;
    }
    if (sample_ >= (int)s->samples.size()) return;
    const int idx = sample_;
    e.edit([row, dir, big, idx](Song& song) {
        Sample& sm = song.samples[idx];
        switch (row) {
            case 3: {  // FINETUNE -8..7
                int ft = sm.finetune / 16 + dir;
                ft = ft < -8 ? -8 : (ft > 7 ? 7 : ft);
                sm.finetune = (int8_t)(ft * 16);
                break;
            }
            case 5: {  // VOLUME 0..64
                int v = sm.volume + dir * (big ? 16 : 1);
                sm.volume = (uint8_t)(v < 0 ? 0 : (v > 64 ? 64 : v));
                break;
            }
            case 7: {  // REPEAT (loop start)
                if (sm.length < 4) break;
                long v = (long)sm.loopStart + dir * (big ? 256 : 2);
                v = v < 0 ? 0 : (v > (long)sm.length - 2 ? (long)sm.length - 2 : v);
                sm.loopStart = (uint32_t)v;
                if (sm.loop != LoopType::None && sm.loopStart + sm.loopLength > sm.length)
                    sm.loopLength = sm.length - sm.loopStart;
                break;
            }
            case 8: {  // REPLEN (2 = no loop, as in ProTracker)
                long cur = sm.loop == LoopType::None ? 2 : (long)sm.loopLength;
                long v = cur + dir * (big ? 256 : 2);
                long maxLen = (long)sm.length - (long)sm.loopStart;
                v = v < 2 ? 2 : (v > maxLen ? maxLen : v);
                if (v <= 2) {
                    sm.loop = LoopType::None;
                    sm.loopLength = 0;
                } else {
                    if (sm.loop == LoopType::None) sm.loop = LoopType::Forward;
                    sm.loopLength = (uint32_t)v;
                }
                break;
            }
        }
    });
}

UiAction PlayScreen::onKey(unsigned vk, bool shift, bool ctrl, bool alt, bool extended, bool repeat, Engine& e) {
    const Song* s = e.song();
    const int nch = s ? s->numChannels : 4;
    if ((vk == VK_MENU && extended) || vk == VK_F5) {
        if (repeat) return UiAction::None;
        e.play((uint16_t)viewOrder_);
        return UiAction::Redraw;
    }
    if (vk == VK_CONTROL && extended) {
        if (repeat) return UiAction::None;
        e.playPattern((uint16_t)viewPattern_);
        return UiAction::Redraw;
    }
    if (alt) return UiAction::None;
    switch (vk) {
        case VK_SPACE:
            e.stop();
            return UiAction::Redraw;
        case VK_F11:
            if (repeat) return UiAction::None;
            viewMode_ = viewMode_ == 4 ? 8 : (viewMode_ == 8 ? 16 : (viewMode_ == 16 ? 32 : 4));
            return UiAction::Redraw;
        case VK_TAB:
            firstChannel_ += shift ? -1 : 1;
            firstChannel_ = computePatternLayout(viewMode_, nch, firstChannel_, 1.f).firstChannel;
            return UiAction::Redraw;
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT: {
            if (last_.playing || !s || s->patterns.empty()) return UiAction::None;
            int step = (vk == VK_UP || vk == VK_DOWN) ? 1 : 16;
            if (vk == VK_UP || vk == VK_PRIOR) step = -step;
            const int rows = s->patterns[viewPattern_ < (int)s->patterns.size() ? viewPattern_ : 0].rows;
            viewRow_ = (viewRow_ + step + rows * 16) % rows;
            return UiAction::Redraw;
        }
        case VK_LEFT:
        case VK_RIGHT:
            counterStep(1, vk == VK_RIGHT ? 1 : -1, false, e);
            return UiAction::Redraw;
    }
    if (ctrl && vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9 && !repeat) {
        int ch = computePatternLayout(viewMode_, nch, firstChannel_, 1.f).firstChannel + (int)(vk - VK_NUMPAD1);
        if (ch < nch) toggleMute(ch, e);
        return UiAction::Redraw;
    }
    return UiAction::None;
}

bool PlayScreen::onMouseMove(float x, float y) {
    const int h = hitId(x, y);
    if (h == hover_) return false;
    hover_ = h;
    return true;
}

UiAction PlayScreen::onMouse(float x, float y, bool right, Engine& e) {
    const int h = hitId(x, y);
    if (h >= H_BUTTON && h < H_BUTTON + 12) {
        switch (h - H_BUTTON) {
            case B_PLAY:
                e.play((uint16_t)viewOrder_);
                break;
            case B_STOP:
                e.stop();
                break;
            case B_PATTERN:
                e.playPattern((uint16_t)viewPattern_);
                break;
            case B_DISKOP:
                return UiAction::OpenFile;
            default: {
                char m[64];
                std::snprintf(m, sizeof m, "%s: COMING IN STAGE 2", kButtons[h - H_BUTTON]);
                setStatus(m);
                break;
            }
        }
        return UiAction::Redraw;
    }
    if (h >= H_ARROW && h < H_ARROW + 20) {
        const int row = (h - H_ARROW) / 2;
        if (!kCounterEditable[row]) {
            setStatus("EDIT SAMPLE LENGTH IN THE SAMPLER (STAGE 2)");
            return UiAction::Redraw;
        }
        counterStep(row, (h - H_ARROW) % 2 == 0 ? 1 : -1, right, e);
        return UiAction::Redraw;
    }
    if (h >= H_VIEW && h < H_VIEW + 4) {
        viewMode_ = kViewModes[h - H_VIEW];
        firstChannel_ = computePatternLayout(viewMode_, e.song() ? e.song()->numChannels : 4, firstChannel_, 1.f)
                            .firstChannel;
        return UiAction::Redraw;
    }
    if (font_) {
        const int ch = pattern_.headerHit(*font_, L_.pattern, ctx(e.song(), last_), x, y);
        if (ch >= 0) {
            if (right)
                solo(ch, e);
            else
                toggleMute(ch, e);
            return UiAction::Redraw;
        }
    }
    return UiAction::None;
}
