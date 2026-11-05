#include "ui/play_screen.h"

#include <algorithm>
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
const char* const kSideButtons[16] = {"TRACK",   "INSERT",  "DELETE", "CUT",    "COPY",   "PASTE",
                                      "CLEAR",   "OCT UP",  "OCT DN", "NOTE UP", "NOTE DN", "ROT UP",
                                      "ROT DN",  "FLIP",    "EXCHG",  "UNDO"};
const char* const kCounterLabels[10] = {"POS",    "PATTERN", "LENGTH", "FINETUNE", "SAMPLE",
                                        "VOLUME", "LENGTH",  "REPEAT", "REPLEN",   "CHANNELS"};
const bool kCounterEditable[10] = {true, true, true, true, true, true, false, true, true, true};
constexpr int kViewModes[4] = {4, 8, 16, 32};

// hit ids
constexpr int H_BUTTON = 0, H_ARROW = 100, H_VIEW = 200, H_SIDE = 300, H_SIDEARROW = 400, H_DIALOG = 500;
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

RectF PlayScreen::sideButton(int i) const {
    const float bw = (L_.side.w - 16 - G) / 2;
    return {L_.side.x + 8 + (i % 2) * (bw + G), L_.side.y + 52 + (i / 2) * 46.f, bw, 40};
}

RectF PlayScreen::sideArrow(int i) const {
    const float w = (L_.side.w - 16) / 2;
    const float x = L_.side.x + 8 + (i / 2) * w;
    return {x + (i % 2 ? w - 34 : 0), L_.side.y + 8, 34, 34};
}

RectF PlayScreen::dialogButton(int i) const {
    const float w = 150, gap = 12, total = dialog_.count * w + (dialog_.count - 1) * gap;
    const float x0 = L_.pattern.x + (L_.pattern.w - total) / 2;
    const float y = L_.pattern.y + L_.pattern.h / 2 + 10;
    return {x0 + i * (w + gap), y, w, 44};
}

int PlayScreen::hitId(float x, float y) const {
    if (dialog_.open) {
        for (int i = 0; i < dialog_.count; ++i)
            if (dialogButton(i).contains(x, y)) return H_DIALOG + i;
        return -1;
    }
    if (panel_ == Panel::DiskOp) {
        for (int i = 0; i < 13; ++i)
            if (diskButton(i).contains(x, y)) return 600 + i;
    } else if (panel_ == Panel::EditOp) {
        const int eh = editOpHit(x, y);
        if (eh >= 0) return eh;
    } else if (panel_ == Panel::Setup) {
        for (int id = 0; id < 100; ++id)
            if (setupRect(id).w > 0 && setupRect(id).contains(x, y)) return 900 + id;
    }
    if (panel_ == Panel::Sampler)
        for (int i = 0; i < 20; ++i)
            if (samplerButton(i).contains(x, y)) return 700 + i;
    const bool topMain = panel_ != Panel::DiskOp && panel_ != Panel::EditOp && panel_ != Panel::Setup;
    const bool bottomPattern = panel_ != Panel::Sampler && panel_ != Panel::Help;
    if (topMain)
    for (int i = 0; i < 12; ++i)
        if (buttonRect(i).contains(x, y)) return H_BUTTON + i;
    if (topMain)
    for (int r = 0; r < 10; ++r)
        for (int k = 0; k < 2; ++k)
            if (counterArrow(counterRow(r), k).contains(x, y)) return H_ARROW + r * 2 + k;
    for (int i = 0; i < 4; ++i)
        if (viewButton(i).contains(x, y)) return H_VIEW + i;
    if (bottomPattern) {
        for (int i = 0; i < 16; ++i)
            if (sideButton(i).contains(x, y)) return H_SIDE + i;
        for (int i = 0; i < 4; ++i)
            if (sideArrow(i).contains(x, y)) return H_SIDEARROW + i;
    }
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
    curCh_ = curCol_ = 0;
    blockOn_ = false;
    recordMode_ = false;
    editor_.reset();
    editor_.setClean();
    sampleUndoIndex_ = -1;
    sampleUndo_ = Sample{};
}

int PlayScreen::curSampleIndex(const Song& s, int note) const {
    if (!s.instruments.empty()) {
        if (sample_ >= (int)s.instruments.size()) return -1;
        const uint16_t g = s.instruments[sample_].sampleMap[(note < 1 || note > 96 ? 49 : note) - 1];
        return g < s.samples.size() ? (int)g : -1;
    }
    return sample_ < (int)s.samples.size() ? sample_ : -1;
}

void PlayScreen::clampView(const Song& s) {
    const int ns = s.instruments.empty() ? (int)s.samples.size() : (int)s.instruments.size();
    if (sample_ >= ns) sample_ = 0;
    if (viewOrder_ >= (int)s.order.size()) viewOrder_ = 0;
    if (viewPattern_ >= (int)s.patterns.size()) viewPattern_ = 0;
    if (!s.patterns.empty() && viewRow_ >= s.patterns[viewPattern_].rows) viewRow_ = 0;
    if (curCh_ >= s.numChannels) curCh_ = s.numChannels - 1;
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
    c.cursorCh = curCh_;
    c.cursorCol = curCol_;
    c.edit = editMode_;
    if (blockOn_) {
        const Block b = currentBlock(false);
        c.blockOn = true;
        c.bRow0 = b.row0;
        c.bRow1 = b.row1;
        c.bCh0 = b.ch0;
        c.bCh1 = b.ch1;
    }
    return c;
}

void PlayScreen::drawDialog(Gfx& g, Text& tx, const Theme& t) {
    if (!dialog_.open) return;
    g.fillRect(L_.pattern, withAlpha({0, 0, 0, 1}, 0.45f));
    const float w = 720, h = 170;
    RectF box = {L_.pattern.x + (L_.pattern.w - w) / 2, L_.pattern.y + L_.pattern.h / 2 - 100, w, h};
    g.panel(box, t, Gfx::PanelState::Normal);
    tx.draw(g, {box.x, box.y + 14, box.w, 50}, dialog_.text, 20.f, t.label, Align::Center);
    Ui u{g, tx, t};
    for (int i = 0; i < dialog_.count; ++i)
        drawButton(u, dialogButton(i), dialog_.buttons[i],
                   hover_ == H_DIALOG + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 17.f);
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
        if (!recordMode_ || true) viewRow_ = tel.row;
        viewOrder_ = tel.order;
    }
    if (s) clampView(*s);
    if (s && curCh_ >= s->numChannels) curCh_ = s->numChannels - 1;
    const PatternCtx pc = ctx(s, tel);
    char buf[128];
    if (s && pc.pattern < (int)s->patterns.size()) patternRowsCache_ = s->patterns[pc.pattern].rows;
    const bool topMain = panel_ != Panel::DiskOp && panel_ != Panel::EditOp && panel_ != Panel::Setup;
    const bool bottomPattern = panel_ != Panel::Sampler && panel_ != Panel::Help;

    // ---------- counters ----------
    const int sIdx = s ? curSampleIndex(*s) : -1;
    const Sample* smp = sIdx >= 0 ? &s->samples[sIdx] : nullptr;
    if (panel_ == Panel::DiskOp) diskDraw(g, tx, t);
    if (panel_ == Panel::EditOp) editOpDraw(g, tx, t);
    if (panel_ == Panel::Setup) setupDraw(g, tx, t);
    if (topMain) {
    g.panel(L_.counters, t, Gfx::PanelState::Normal);
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
        if ((i == B_PLAY && tel.playing && !recordMode_) || (i == B_EDIT && editMode_) ||
            (i == B_RECORD && recordMode_))
            st = Gfx::PanelState::Active;
        drawButton(u, buttonRect(i), kButtons[i], st, 19.f);
    }
    g.well(L_.logo, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, {L_.logo.x, L_.logo.y + 8, L_.logo.w, 64}, "FATTRACKER", 54.f, t.digits, Align::Center);
    }  // topMain

    // ---------- scopes ----------
    g.panel(L_.scopes, t, Gfx::PanelState::Normal);
    const int nch = s ? s->numChannels : 4;
    // one scope per song channel (up to 32), independent of the pattern view
    const int nScopes = nch;
    const int cols = nScopes <= 4 ? nScopes : (nScopes <= 8 ? 4 : 8);
    const int rows = (nScopes + cols - 1) / cols;
    const float cw = L_.scopeGrid.w / cols, chh = L_.scopeGrid.h / rows;
    RectF scopeRects[32];
    for (int i = 0; i < nScopes; ++i) {
        scopeRects[i] = {L_.scopeGrid.x + (i % cols) * cw + 3, L_.scopeGrid.y + (i / cols) * chh + 3, cw - 6, chh - 6};
        drawScopeBg(g, t, scopeRects[i]);
        std::snprintf(buf, sizeof buf, "%d", i + 1);
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
    std::snprintf(buf, sizeof buf, "TEMPO %02u  BPM %03u  OCT %d  SKIP %d%s", tel.speed, tel.bpm, octave_, editSkip_,
                  editor_.dirty() ? "  *" : "");
    tx.draw(g, {L_.info.x + 560, y1, 560, rowH}, buf, 16.f, t.label, Align::Left, FontRole::Mono);
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

    // ---------- pattern + side panel (or SAMPLER / HELP) ----------
    if (panel_ == Panel::Sampler) samplerDraw(g, tx, t, e);
    if (panel_ == Panel::Help) helpDraw(g, tx, t);
    if (bottomPattern) {
    pattern_.drawBack(g, tx, f, t, L_.pattern, pc);
    pattern_.drawCursor(g, f, t, L_.pattern, pc);
    g.panel(L_.side, t, Gfx::PanelState::Normal);
    for (int i = 0; i < 2; ++i) {
        const float w = (L_.side.w - 16) / 2;
        RectF hdr = {L_.side.x + 8 + i * w + (i ? 3.f : 0.f), L_.side.y + 8, w - 3, 34};
        g.well(hdr, t, mixColor(t.panelLight, t.panel, 0.25f));
        std::snprintf(buf, sizeof buf, i == 0 ? "%02u" : "%03u", i == 0 ? (unsigned)pc.pattern : (unsigned)tel.bpm);
        tx.draw(g, hdr, buf, 16.f, t.digits, Align::Center, FontRole::Mono);
    }
    for (int i = 0; i < 4; ++i) {
        RectF a = sideArrow(i);
        g.panel(a, t, hover_ == H_SIDEARROW + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal);
        tx.draw(g, a, i % 2 ? "\x04" : "\x03", 12.f, t.label, Align::Center);
    }
    for (int i = 0; i < 16; ++i) {
        Gfx::PanelState sst = hover_ == H_SIDE + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal;
        const char* label = i == 0 ? (scopePattern_ ? "PATTERN" : "TRACK") : kSideButtons[i];
        if (i == 0) sst = Gfx::PanelState::Active;
        drawButton(u, sideButton(i), label, sst, 14.f);
    }
    }  // bottomPattern

    // ---------- glowing layer: scope lines, VU meters ----------
    g.beginGlow();
    for (int i = 0; i < nScopes; ++i) {
        e.scope(i, scopeBuf_, 256);
        drawScopeLine(g, t, scopeRects[i], scopeBuf_, 256);
    }
    if (bottomPattern) pattern_.drawMeters(g, f, t, L_.pattern, pc);
    g.endGlow(7.f, t.glow);
    if (topMain) tx.draw(g, {L_.logo.x, L_.logo.y + 72, L_.logo.w, 30}, "32 CHANNELS   \xB7   16 BIT   \xB7   V0.3", 14.f,
            t.panelDark, Align::Center);

    if (bottomPattern) pattern_.drawText(g, f, t, L_.pattern, pc);
    drawDialog(g, tx, t);

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
        case 1: {  // PATTERN at the current position (ProTracker); one past the last creates a new pattern
            if (!stopped || s->order.empty()) return;
            const int pos = viewOrder_ < (int)s->order.size() ? viewOrder_ : 0;
            int p = s->order[pos] + dir * (big ? 10 : 1);
            if (p < 0) p = 0;
            e.edit([&](Song& song) {
                if (p >= (int)song.patterns.size()) p = editor_.newPattern(song, 64);
                if (p >= 0) editor_.setOrder(song, pos, (uint8_t)p);
            });
            viewPattern_ = e.song()->order[pos];
            viewRow_ = 0;
            return;
        }
        case 2: {  // LENGTH: add / remove positions at the end
            if (!stopped) return;
            e.edit([&](Song& song) {
                if (dir > 0)
                    editor_.insertOrder(song, (int)song.order.size(), song.order.back());
                else
                    editor_.deleteOrder(song, (int)song.order.size() - 1);
            });
            if (viewOrder_ >= (int)e.song()->order.size()) viewOrder_ = (int)e.song()->order.size() - 1;
            return;
        }
        case 4: {  // SAMPLE (instrument number in songs that use XM instruments)
            const int n = s->instruments.empty() ? (int)s->samples.size() : (int)s->instruments.size();
            if (n == 0) return;
            sample_ = (sample_ + dir * (big ? 16 : 1) + n * 16) % n;
            return;
        }
        case 9: {  // CHANNELS
            const int n = s->numChannels + dir * (big ? 4 : 1);
            e.edit([&](Song& song) { editor_.setChannels(song, n); });
            ensureCursorVisible(*e.song());
            char m[48];
            std::snprintf(m, sizeof m, "CHANNELS: %d", e.song()->numChannels);
            setStatus(m);
            return;
        }
        default:
            break;
    }
    const int idx = curSampleIndex(*s);
    if (idx < 0) return;
    editor_.markDirty();
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
    if (dialog_.open) {
        if (vk == VK_ESCAPE || vk == VK_RETURN) {
            const int answer = vk == VK_RETURN ? 0 : dialog_.count - 1;
            Dialog d = dialog_;
            dialog_.open = false;
            if (d.onAnswer) d.onAnswer(answer);
        }
        return UiAction::Redraw;
    }
    const bool typing = panel_ == Panel::DiskOp;
    if ((vk == VK_MENU && extended && !ctrl && !typing) || (vk == VK_F5 && !shift && !ctrl && !alt)) {
        if (repeat) return UiAction::None;
        recordMode_ = false;
        e.play((uint16_t)viewOrder_);
        return UiAction::Redraw;
    }
    if (vk == VK_CONTROL && extended && !alt && !typing) {
        if (repeat) return UiAction::None;
        recordMode_ = false;
        e.playPattern((uint16_t)viewPattern_);
        return UiAction::Redraw;
    }
    // ---- panels get the keyboard first
    if (panel_ == Panel::DiskOp) return vk == VK_SPACE ? UiAction::None : diskKey(vk, ctrl, e);
    if (panel_ == Panel::Sampler) {
        const UiAction r = samplerKey(vk, shift, ctrl, e);
        if (r != UiAction::None) return r;
        if (vk != VK_SPACE) return UiAction::None;
    }
    if (panel_ == Panel::Help && vk != VK_ESCAPE && vk != VK_SPACE) return UiAction::None;
    if (vk == VK_ESCAPE && panel_ != Panel::Main) {
        panel_ = Panel::Main;
        return UiAction::Redraw;
    }
    if (vk == VK_SPACE && !alt && !ctrl) {  // ProTracker: space stops and toggles edit mode
        if (repeat) return UiAction::None;
        if (last_.playing) {
            e.stop();
            recordMode_ = false;
        } else {
            editMode_ = !editMode_;
            setStatus(editMode_ ? "EDIT MODE" : "ALL RIGHT");
        }
        return UiAction::Redraw;
    }
    if (ctrl && !alt && (vk == 'S')) return repeat ? UiAction::None : UiAction::SaveFile;
    if (ctrl && !alt && (vk == VK_LEFT || vk == VK_RIGHT)) {
        counterStep(0, vk == VK_RIGHT ? 1 : -1, false, e);
        return UiAction::Redraw;
    }
    if (vk == VK_F11 && !repeat) {
        viewMode_ = viewMode_ == 4 ? 8 : (viewMode_ == 8 ? 16 : (viewMode_ == 16 ? 32 : 4));
        if (s) ensureCursorVisible(*s);
        return UiAction::Redraw;
    }
    if (ctrl && vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9 && !repeat) {
        int ch = computePatternLayout(viewMode_, nch, firstChannel_, 1.f).firstChannel + (int)(vk - VK_NUMPAD1);
        if (ch < nch) toggleMute(ch, e);
        return UiAction::Redraw;
    }
    return editKey(vk, shift, ctrl, alt, repeat, e);
}

void PlayScreen::openPanel(Panel p) {
    panel_ = panel_ == p ? Panel::Main : p;
    if (panel_ == Panel::DiskOp) diskRefresh();
    if (panel_ == Panel::Sampler) viewStart_ = viewEnd_ = selA_ = selB_ = 0;
}

bool PlayScreen::onMouseMove(float x, float y, bool leftDown, Engine& e) {
    bool redraw = false;
    if (drag_ && leftDown && panel_ == Panel::Sampler) {
        const uint32_t pos = xToSample(x);
        if (drag_ == 1) {
            selB_ = pos;
        } else if (e.song() && curSampleIndex(*e.song()) >= 0) {
            const int idx = curSampleIndex(*e.song());
            const int which = drag_;
            e.edit([&](Song& song) {
                Sample& sm = song.samples[idx];
                const uint32_t end = sm.loopStart + sm.loopLength;
                if (which == 2 && pos + 2 <= end) {
                    sm.loopLength = end - pos;
                    sm.loopStart = pos;
                }
                if (which == 3 && pos >= sm.loopStart + 2 && pos <= sm.length) sm.loopLength = pos - sm.loopStart;
            });
            editor_.markDirty();
        }
        redraw = true;
    }
    const int h = hitId(x, y);
    if (h != hover_) {
        hover_ = h;
        redraw = true;
    }
    return redraw;
}

UiAction PlayScreen::onMouseUp(float, float, Engine&) {
    if (!drag_) return UiAction::None;
    drag_ = 0;
    return UiAction::Redraw;
}

UiAction PlayScreen::onWheel(float x, float y, int delta, Engine& e) {
    const int notches = delta / 120 != 0 ? delta / 120 : (delta > 0 ? 1 : -1);
    if (panel_ == Panel::Sampler && samplerWave().contains(x, y) && viewEnd_ > viewStart_) {
        const uint32_t center = xToSample(x);
        const double f = notches > 0 ? 0.7 : 1.0 / 0.7;
        const int si = e.song() ? curSampleIndex(*e.song()) : -1;
        const double len = si >= 0 ? e.song()->samples[si].length : 0;
        double a = center - (center - (double)viewStart_) * f, b = center + ((double)viewEnd_ - center) * f;
        if (b - a < 16) b = a + 16;
        a = a < 0 ? 0 : a;
        b = b > len ? len : b;
        viewStart_ = (uint32_t)a;
        viewEnd_ = (uint32_t)b;
        return UiAction::Redraw;
    }
    if (panel_ == Panel::DiskOp && diskList().contains(x, y)) {
        diskScroll_ = std::max(0, std::min((int)entries_.size() - diskRows(), diskScroll_ - notches * 3));
        diskSel_ = std::max(diskScroll_, std::min(diskSel_, diskScroll_ + diskRows() - 1));
        return UiAction::Redraw;
    }
    if (panel_ != Panel::Sampler && panel_ != Panel::Help && L_.pattern.contains(x, y) && !last_.playing) {
        moveCursor(-notches, 0, e);
        return UiAction::Redraw;
    }
    return UiAction::None;
}
UiAction PlayScreen::onMouse(float x, float y, bool right, Engine& e) {
    const int h = hitId(x, y);
    if (dialog_.open) {
        if (h >= H_DIALOG && h < H_DIALOG + dialog_.count) {
            Dialog d = dialog_;
            dialog_.open = false;
            if (d.onAnswer) d.onAnswer(h - H_DIALOG);
        }
        return UiAction::Redraw;
    }
    if (panel_ == Panel::DiskOp && panelArea().contains(x, y)) return diskMouse(x, y, right, e);
    if (panel_ == Panel::EditOp && panelArea().contains(x, y)) return editOpMouse(x, y, right, e);
    if (panel_ == Panel::Setup && panelArea().contains(x, y)) return setupMouse(x, y, right, e);
    if (panel_ == Panel::Sampler && bottomArea().contains(x, y)) return samplerMouse(x, y, right, e);
    if (panel_ == Panel::Help && bottomArea().contains(x, y)) {
        panel_ = Panel::Main;
        return UiAction::Redraw;
    }
    if (h >= H_BUTTON && h < H_BUTTON + 12) {
        switch (h - H_BUTTON) {
            case B_PLAY:
                recordMode_ = false;
                e.play((uint16_t)viewOrder_);
                break;
            case B_STOP:
                recordMode_ = false;
                e.stop();
                break;
            case B_PATTERN:
                recordMode_ = false;
                e.playPattern((uint16_t)viewPattern_);
                break;
            case B_EDIT:
                editMode_ = !editMode_;
                setStatus(editMode_ ? "EDIT MODE" : "ALL RIGHT");
                break;
            case B_RECORD:
                recordMode_ = !recordMode_;
                if (recordMode_) {
                    editMode_ = true;
                    e.playPattern((uint16_t)viewPattern_);
                    setStatus("RECORDING");
                } else {
                    e.stop();
                    setStatus("ALL RIGHT");
                }
                break;
            case B_CLEAR:
                ask("CLEAR WHAT?", {"SONG", "SAMPLES", "ALL", "CANCEL"}, [this, &e](int a) {
                    if (a == 3) return;
                    e.stop();
                    if (a == 0 || a == 2) e.edit([&](Song& song) { editor_.clearSong(song); });
                    if (a != 0) editor_.markDirty();
                    if (a == 1 || a == 2) e.editStructure([](Song& song) {
                            for (Sample& sm : song.samples) sm = Sample{};
                        });
                    viewRow_ = viewPattern_ = viewOrder_ = 0;
                    setStatus(a == 0 ? "SONG CLEARED" : a == 1 ? "SAMPLES CLEARED" : "ALL CLEARED");
                });
                break;
            case B_DISKOP:
                openPanel(Panel::DiskOp);
                break;
            case B_EDITOP:
                openPanel(Panel::EditOp);
                break;
            case B_SETUP:
                openPanel(Panel::Setup);
                break;
            case B_SAMPLER:
                openPanel(Panel::Sampler);
                break;
            case B_HELP:
                openPanel(Panel::Help);
                break;
            default: {
                char m[64];
                std::snprintf(m, sizeof m, "PLST: LOAD SAMPLES IN DISK OP. (SAMPLES FILTER)");
                setStatus(m);
                break;
            }
        }
        return UiAction::Redraw;
    }
    if (h >= H_ARROW && h < H_ARROW + 20) {
        const int row = (h - H_ARROW) / 2;
        if (!kCounterEditable[row]) {
            setStatus("EDIT THE SAMPLE LENGTH IN THE SAMPLER");
            return UiAction::Redraw;
        }
        counterStep(row, (h - H_ARROW) % 2 == 0 ? 1 : -1, right, e);
        return UiAction::Redraw;
    }
    if (h >= H_VIEW && h < H_VIEW + 4) {
        viewMode_ = kViewModes[h - H_VIEW];
        if (e.song()) ensureCursorVisible(*e.song());
        return UiAction::Redraw;
    }
    if (h >= H_SIDE && h < H_SIDE + 16) {
        sideAction(h - H_SIDE, e);
        return UiAction::Redraw;
    }
    if (h >= H_SIDEARROW && h < H_SIDEARROW + 4) {
        const int i = h - H_SIDEARROW;
        if (i < 2) {
            if (e.song() && !last_.playing) {
                const int n = (int)e.song()->patterns.size();
                viewPattern_ = ((viewPattern_ + (i ? 1 : -1)) % n + n) % n;
                viewRow_ = 0;
            }
        } else {
            const int d = (i == 3 ? 1 : -1) * (right ? 10 : 1);
            editor_.markDirty();
            e.edit([d](Song& song) {
                int b = song.initialBpm + d;
                song.initialBpm = (uint8_t)(b < 32 ? 32 : (b > 255 ? 255 : b));
            });
        }
        return UiAction::Redraw;
    }
    if (font_) {
        const PatternCtx pc = ctx(e.song(), last_);
        const int ch = pattern_.headerHit(*font_, L_.pattern, pc, x, y);
        if (ch >= 0) {
            if (right)
                solo(ch, e);
            else
                toggleMute(ch, e);
            return UiAction::Redraw;
        }
        int row, cch, col;
        if (pattern_.cellHit(*font_, L_.pattern, pc, x, y, row, cch, col)) {
            curCh_ = cch;
            curCol_ = col;
            blockOn_ = false;
            if (!last_.playing && e.song()) {
                const Song& s = *e.song();
                const int rows = s.patterns[viewPattern_ < (int)s.patterns.size() ? viewPattern_ : 0].rows;
                if (row >= 0 && row < rows) viewRow_ = row;
            }
            return UiAction::Redraw;
        }
    }
    return UiAction::None;
}
