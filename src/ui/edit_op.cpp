// EDIT OP. (top band) and HELP (pattern area).
#include <algorithm>
#include <cstdio>

#include "ui/credits.h"
#include "ui/play_screen.h"
#include "ui/widgets.h"

namespace {
constexpr int kHitEditOp = 800;
// left column: value rows with arrows
enum { R_SKIP, R_OCTAVE, R_QUANT, R_METRO, R_PATLEN, R_RECMODE, R_ROWS };
const char* const kRowLabels[R_ROWS] = {"EDIT SKIP", "OCTAVE", "REC QUANTIZE", "METRONOME", "PATTERN LENGTH",
                                        "REC MODE"};
// right side buttons
enum {
    B_NEWPAT, B_CLONEPAT, B_TR_TRACK, B_TR_PATTERN, B_TR_SONG, B_TR_BLOCK, B_TR_ONLY, B_TR_M12, B_TR_M1, B_TR_P1,
    B_TR_P12, B_EX_PREV, B_EX_NEXT, B_EX_REPLACE, B_EX_SWAP, B_EXIT, B_COUNT
};
const int kQuant[] = {1, 2, 4, 8, 16};
}  // namespace

RectF PlayScreen::editOpCell(int row, int col) const {
    const RectF a = panelArea();
    // col 0: label, 1: down arrow, 2: value, 3: up arrow (rows) ; col >= 10: buttons
    const float y = a.y + 14 + row * 48.f;
    switch (col) {
        case 0: return {a.x + 14, y, 220, 40};
        case 1: return {a.x + 240, y + 2, 40, 36};
        case 2: return {a.x + 286, y + 2, 110, 36};
        case 3: return {a.x + 402, y + 2, 40, 36};
    }
    return {};
}

static RectF opButton(RectF a, int i) {
    switch (i) {
        case B_NEWPAT: return {a.x + 14, a.y + 14 + (int)R_ROWS * 48.f, 210, 40};
        case B_CLONEPAT: return {a.x + 232, a.y + 14 + (int)R_ROWS * 48.f, 210, 40};
        case B_TR_TRACK: case B_TR_PATTERN: case B_TR_SONG: case B_TR_BLOCK:
            return {a.x + 600 + (i - B_TR_TRACK) * 140.f, a.y + 56, 132, 40};
        case B_TR_ONLY: return {a.x + 600, a.y + 104, 412, 40};
        case B_TR_M12: case B_TR_M1: case B_TR_P1: case B_TR_P12:
            return {a.x + 600 + (i - B_TR_M12) * 104.f, a.y + 152, 98, 40};
        case B_EX_PREV: return {a.x + 820, a.y + 250, 40, 40};
        case B_EX_NEXT: return {a.x + 972, a.y + 250, 40, 40};
        case B_EX_REPLACE: return {a.x + 600, a.y + 298, 202, 40};
        case B_EX_SWAP: return {a.x + 810, a.y + 298, 202, 40};
        case B_EXIT: return {a.x + a.w - 130, a.y + a.h - 54, 116, 40};
    }
    return {};
}

void PlayScreen::editOpDraw(Gfx& g, Text& tx, const Theme& t) {
    const RectF a = panelArea();
    Ui u{g, tx, t};
    g.panel(a, t, Gfx::PanelState::Normal);
    char v[48];
    for (int r = 0; r < R_ROWS; ++r) {
        tx.draw(g, editOpCell(r, 0), kRowLabels[r], 16.f, t.label, Align::Left);
        switch (r) {
            case R_SKIP: std::snprintf(v, sizeof v, "%d", editSkip_); break;
            case R_OCTAVE: std::snprintf(v, sizeof v, "%d", octave_); break;
            case R_QUANT: std::snprintf(v, sizeof v, quantize_ == 1 ? "OFF" : "%d ROWS", quantize_); break;
            case R_METRO: std::snprintf(v, sizeof v, metronome_ ? "EVERY %d" : "OFF", metroEvery_); break;
            case R_PATLEN: std::snprintf(v, sizeof v, "%d", last_.numChannels ? patternRowsCache_ : 64); break;
            case R_RECMODE: std::snprintf(v, sizeof v, recordStep_ ? "STEP" : "LIVE"); break;
        }
        g.well(editOpCell(r, 2), t, mixColor(t.panelLight, t.panel, 0.25f));
        tx.draw(g, editOpCell(r, 2), v, 16.f, t.digits, Align::Center, FontRole::Mono);
        for (int k = 0; k < 2; ++k) {
            const int id = kHitEditOp + 50 + r * 2 + k;
            drawButton(u, editOpCell(r, k == 0 ? 1 : 3), k == 0 ? "\x03" : "\x04",
                       hover_ == id ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 12.f);
        }
    }
    tx.draw(g, {a.x + 600, a.y + 14, 412, 36}, "TRANSPOSE", 17.f, t.label, Align::Left);
    tx.draw(g, {a.x + 600, a.y + 204, 412, 40}, "EXCHANGE SAMPLE NUMBERS", 17.f, t.label, Align::Left);
    std::snprintf(v, sizeof v, "%02X  \x04  %02X", sample_ + 1, exchangeTo_);
    RectF ex = {a.x + 600, a.y + 250, 212, 40};
    g.well(ex, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, ex, v, 17.f, t.digits, Align::Center, FontRole::Mono);
    std::snprintf(v, sizeof v, "%02X", exchangeTo_);
    RectF exv = {a.x + 866, a.y + 250, 100, 40};
    g.well(exv, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, exv, v, 17.f, t.digits, Align::Center, FontRole::Mono);
    const char* labels[B_COUNT] = {"NEW PATTERN", "CLONE PATTERN", "TRACK", "PATTERN", "SONG", "BLOCK",
                                   transposeOnlySample_ ? "ONLY CURRENT SAMPLE: ON" : "ONLY CURRENT SAMPLE: OFF",
                                   "-12", "-1", "+1", "+12", "\x03", "\x04", "REPLACE", "SWAP", "EXIT"};
    for (int i = 0; i < B_COUNT; ++i) {
        Gfx::PanelState st = hover_ == kHitEditOp + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal;
        if (i >= B_TR_TRACK && i <= B_TR_BLOCK && transposeScope_ == i - B_TR_TRACK) st = Gfx::PanelState::Active;
        if (i == B_TR_ONLY && transposeOnlySample_) st = Gfx::PanelState::Active;
        drawButton(u, opButton(a, i), labels[i], st, i == B_EX_PREV || i == B_EX_NEXT ? 12.f : 15.f);
    }
}

UiAction PlayScreen::editOpMouse(float x, float y, bool right, Engine& e) {
    const RectF a = panelArea();
    const Song* s = e.song();
    if (!s) return UiAction::None;
    for (int r = 0; r < R_ROWS; ++r)
        for (int k = 0; k < 2; ++k) {
            if (!editOpCell(r, k == 0 ? 1 : 3).contains(x, y)) continue;
            opStep(r, k == 0 ? -1 : 1, right, e);
            return UiAction::Redraw;
        }
    if (editOpCell(R_METRO, 2).contains(x, y)) {
        metronome_ = !metronome_;
        applyMetronome(e);
        return UiAction::Redraw;
    }
    if (editOpCell(R_RECMODE, 2).contains(x, y)) {
        toggleRecMode(e);
        return UiAction::Redraw;
    }
    for (int i = 0; i < B_COUNT; ++i) {
        if (!opButton(a, i).contains(x, y)) continue;
        switch (i) {
            case B_NEWPAT:
            case B_CLONEPAT:
                opNewPattern(i == B_CLONEPAT, e);
                break;
            case B_TR_TRACK: case B_TR_PATTERN: case B_TR_SONG: case B_TR_BLOCK:
                transposeScope_ = i - B_TR_TRACK;
                break;
            case B_TR_ONLY:
                transposeOnlySample_ = !transposeOnlySample_;
                break;
            case B_TR_M12: case B_TR_M1: case B_TR_P1: case B_TR_P12:
                opTranspose(i == B_TR_M12 ? -12 : i == B_TR_M1 ? -1 : i == B_TR_P1 ? 1 : 12, e);
                break;
            case B_EX_PREV: exchangeTo_ = exchangeTo_ > 1 ? exchangeTo_ - 1 : 255; break;
            case B_EX_NEXT: exchangeTo_ = exchangeTo_ < 255 ? exchangeTo_ + 1 : 1; break;
            case B_EX_REPLACE:
            case B_EX_SWAP:
                opExchange(i == B_EX_SWAP, e);
                break;
            case B_EXIT: panel_ = Panel::Main; break;
        }
        return UiAction::Redraw;
    }
    return UiAction::None;
}

// ---- EDIT OP. actions shared by the normal panel and the Pure screen
void PlayScreen::opStep(int row, int d, bool big, Engine& e) {
    const Song* s = e.song();
    switch (row) {
        case R_SKIP: editSkip_ = std::clamp(editSkip_ + d, 0, 16); break;
        case R_OCTAVE: octave_ = std::clamp(octave_ + d, 0, 6); break;
        case R_QUANT: {
            int i = 0;
            while (i < 4 && kQuant[i] != quantize_) ++i;
            quantize_ = kQuant[std::clamp(i + d, 0, 4)];
            break;
        }
        case R_METRO:
            if (!metronome_ && d > 0) metronome_ = true;
            else if (metronome_ && metroEvery_ + d < 1) metronome_ = false;
            else metroEvery_ = std::clamp(metroEvery_ + d, 1, 16);
            applyMetronome(e);
            break;
        case R_PATLEN: {
            if (!s) break;
            const int pat = editPattern();
            const int rows = s->patterns[pat].rows + d * (big ? 16 : 1);
            e.edit([&](Song& song) { editor_.setPatternRows(song, pat, rows); });
            break;
        }
        case R_RECMODE: toggleRecMode(e); break;  // two values: either arrow switches
    }
}

void PlayScreen::opNewPattern(bool clone, Engine& e) {
    if (!e.song()) return;
    const int pat = editPattern();
    int p = -1;
    e.edit([&](Song& song) {
        p = editor_.newPattern(song, song.patterns[pat].rows);
        if (p >= 0 && clone) {
            Clip c = editor_.copy(song, Block{pat, 0, 255, 0, kMaxChannels - 1});
            editor_.paste(song, p, 0, 0, c);
        }
    });
    if (p < 0) {
        setStatus("ERROR: 256 PATTERNS MAXIMUM");
    } else {
        char m[40];
        std::snprintf(m, sizeof m, "PATTERN %02d %s", p, clone ? "CLONED" : "CREATED");
        setStatus(m);
        if (!last_.playing) viewPattern_ = p;
    }
}

void PlayScreen::opTranspose(int semis, Engine& e) {
    if (!e.song()) return;
    const int pat = editPattern();
    const int only = transposeOnlySample_ ? sample_ + 1 : 0;
    e.edit([&](Song& song) {
        if (transposeScope_ == 2) editor_.transposeSong(song, semis, only);
        else if (transposeScope_ == 3 && blockOn_) editor_.transpose(song, currentBlock(false), semis, only);
        else if (transposeScope_ == 1) editor_.transpose(song, Block{pat, 0, 255, 0, kMaxChannels - 1}, semis, only);
        else editor_.transpose(song, Block{pat, 0, 255, curCh_, curCh_}, semis, only);
    });
    setStatus("TRANSPOSED");
}

void PlayScreen::opExchange(bool swap, Engine& e) {
    e.edit([&](Song& song) { editor_.exchangeInstrument(song, sample_ + 1, exchangeTo_, swap); });
    setStatus(swap ? "SAMPLE NUMBERS SWAPPED" : "SAMPLE NUMBERS REPLACED");
}

int PlayScreen::editOpHit(float x, float y) const {
    for (int r = 0; r < R_ROWS; ++r)
        for (int k = 0; k < 2; ++k)
            if (editOpCell(r, k == 0 ? 1 : 3).contains(x, y)) return kHitEditOp + 50 + r * 2 + k;
    for (int i = 0; i < B_COUNT; ++i)
        if (opButton(panelArea(), i).contains(x, y)) return kHitEditOp + i;
    return -1;
}

void PlayScreen::applyMetronome(Engine& e) { e.setMetronome(metronome_, metroEvery_); }

RectF PlayScreen::helpTab() const {
    const RectF a = bottomArea();
    return {a.x + a.w - 8 - 16 - 150, a.y + 8 + 10, 150, 36};
}

void PlayScreen::helpDraw(Gfx& g, Text& tx, const Theme& t) {
    const RectF a = bottomArea();
    g.panel(a, t, Gfx::PanelState::Normal);
    RectF w = {a.x + 8, a.y + 8, a.w - 16, a.h - 16};
    g.well(w, t, t.patternBg);
    Ui u{g, tx, t};
    drawButton(u, helpTab(), credits_ ? "HELP" : "CREDITS",
               hover_ == kHelpTabHit ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 15.f);
    const float lh = 23.f;
    if (credits_) {  // two columns of the credits table, scrolled by lines
        tx.draw(g, {w.x + 16, w.y + 8, w.w / 2, 36}, "CREDITS AND LICENCES", 17.f, t.patternFx, Align::Left);
        const float top = w.y + 54;
        const int perCol = std::max(1, (int)((w.y + w.h - 8 - top) / lh));
        const float colW = w.w / 2 - 28;
        const auto ls = credits::lines(std::max(20, (int)(colW / (15.f * tx.monoAdvance()))));
        const int n = (int)ls.size();
        creditsScroll_ = std::clamp(creditsScroll_, 0, std::max(0, n - 2 * perCol));
        for (int i = 0; i < 2 * perCol && creditsScroll_ + i < n; ++i) {
            const credits::Line& l = ls[creditsScroll_ + i];
            tx.draw(g, {w.x + 16 + (i / perCol) * (w.w / 2), top + (i % perCol) * lh, colW, lh}, l.text.c_str(), 15.f,
                    l.heading ? t.patternFx : t.patternText, Align::Left, FontRole::Mono);
        }
        if (n > 2 * perCol) {
            char m[48];
            std::snprintf(m, sizeof m, "WHEEL / ARROWS: SCROLL  %d%%", creditsScroll_ * 100 / std::max(1, n - 2 * perCol));
            tx.draw(g, {w.x + w.w / 2, w.y + 8 + 10, w.w / 2 - 190, 36}, m, 14.f, t.patternText, Align::Right);
        }
        return;
    }
#ifdef __APPLE__
    // the Mac keys (src/mac/main_mac.mm): Command works as Ctrl, Option as Alt, plus laptop stand-ins for the
    // keys a MacBook lacks (right Ctrl, Insert, numeric keypad)
    static const char* const lines[] = {
        "PLAYBACK",                          "  F5 / RIGHT OPTION   PLAY SONG",
        "  RIGHT CMD / CTRL    PLAY PATTERN", "  SPACE               STOP / EDIT MODE ON-OFF",
        "  RECORD button       RECORD (RIGHT CLICK: STEP / LIVE)", "",
        "NAVIGATION",                        "  ARROWS              MOVE CURSOR",
        "  TAB / SHIFT+TAB     NEXT / PREVIOUS TRACK", "  FN+ARROWS           PGUP / PGDN / HOME / END",
        "  CMD+LEFT/RIGHT      SONG POSITION",  "  F11                 VIEW 4 / 8 / 16 / 32",
        "  F1 / F2             OCTAVE DOWN / UP (FN+F-KEYS ON A MACBOOK)", "",
        "NOTE ENTRY (EDIT MODE)",            "  Z S X D C V G B H N J M   LOWER OCTAVE",
        "  Q 2 W 3 E R 5 T 6 Y 7 U   UPPER OCTAVE", "  1                   KEY OFF",
        "  0-9 A-F             DIGITS (EFFECTS 0-9 A-Z)", "  FN+DEL / SHIFT      CLEAR FIELD / CELL",
        "  OPT+DEL / DEL       INSERT / DELETE ROW (SHIFT: ALL TRACKS)", "",
        "BLOCKS",                            "  SHIFT+ARROWS        SELECT",
        "  CMD+C / X / V       COPY / CUT / PASTE", "  CMD+A               SELECT TRACK",
        "  SHIFT+F3 F4 F5      CUT / COPY / PASTE TRACK", "  OPT+F3 F4 F5        CUT / COPY / PASTE PATTERN",
        "  CMD+Z / CMD+Y       UNDO / REDO", "",
        "FILES",                             "  CMD+O               OPEN MODULE",
        "  CMD+S               SAVE (XM)",   "  DISK OP.            BROWSE, SAVE, EXPORT MOD / WAV",
        "  DRAG & DROP         LOAD MODULE OR SAMPLE", "",
        "INSTRUMENT (XM)",                   "  CMD+I               INSTRUMENT EDITOR (OR SAMPLER: INSTRUMENT)",
        "  CLICK ENVELOPE      ADD / DRAG POINT (RIGHT CLICK: DELETE)",
        "  CLICK / DRAG KEYS   MAP SAMPLE TO KEYS (RIGHT CLICK: NONE)", "",
        "OTHER",                             "  CLICK TRACK HEADER  MUTE (RIGHT / CTRL+CLICK: SOLO)",
        "  CMD+1 ... 9         MUTE TRACK 1-9 OF THE VIEW",
        "  OPT+RETURN          FULL SCREEN", "  ESC                 CLOSE PANEL"};
#else
    static const char* const lines[] = {
        "PLAYBACK",                          "  F5 / RIGHT ALT      PLAY SONG",
        "  RIGHT CTRL          PLAY PATTERN", "  SPACE               STOP / EDIT MODE ON-OFF",
        "  RECORD button       RECORD (RIGHT CLICK: STEP / LIVE)", "",
        "NAVIGATION",                        "  ARROWS              MOVE CURSOR",
        "  TAB / SHIFT+TAB     NEXT / PREVIOUS TRACK", "  PGUP / PGDN / HOME / END",
        "  CTRL+LEFT/RIGHT     SONG POSITION",  "  F11                 VIEW 4 / 8 / 16 / 32",
        "  F1 / F2             OCTAVE DOWN / UP", "",
        "NOTE ENTRY (EDIT MODE)",            "  Z S X D C V G B H N J M   LOWER OCTAVE",
        "  Q 2 W 3 E R 5 T 6 Y 7 U   UPPER OCTAVE", "  1                   KEY OFF",
        "  0-9 A-F             DIGITS (EFFECTS 0-9 A-Z)", "  DEL / SHIFT+DEL     CLEAR FIELD / CELL",
        "  INS / BACKSPACE     INSERT / DELETE ROW (SHIFT: ALL TRACKS)", "",
        "BLOCKS",                            "  SHIFT+ARROWS        SELECT",
        "  CTRL+C / X / V      COPY / CUT / PASTE", "  CTRL+A              SELECT TRACK",
        "  SHIFT+F3 F4 F5      CUT / COPY / PASTE TRACK", "  ALT+F3 F4 F5        CUT / COPY / PASTE PATTERN",
        "  CTRL+Z / CTRL+Y     UNDO / REDO", "",
        "FILES",                             "  CTRL+O              OPEN MODULE",
        "  CTRL+S              SAVE (XM)",   "  DISK OP.            BROWSE, SAVE, EXPORT MOD / WAV",
        "  DRAG & DROP         LOAD MODULE OR SAMPLE", "",
        "INSTRUMENT (XM)",                   "  CTRL+I              INSTRUMENT EDITOR (OR SAMPLER: INSTRUMENT)",
        "  CLICK ENVELOPE      ADD / DRAG POINT (RIGHT CLICK: DELETE)",
        "  CLICK / DRAG KEYS   MAP SAMPLE TO KEYS (RIGHT CLICK: NONE)", "",
        "OTHER",                             "  CLICK TRACK HEADER  MUTE (RIGHT CLICK: SOLO)",
        "  ALT+ENTER           FULL SCREEN", "  ESC                 CLOSE PANEL"};
#endif
    const int n = (int)(sizeof lines / sizeof lines[0]);
    const int perCol = (int)((w.h - 16) / lh);
    for (int i = 0; i < n; ++i) {
        const int col = i / perCol, row = i % perCol;
        const bool head = lines[i][0] && lines[i][0] != ' ';
        tx.draw(g, {w.x + 16 + col * (w.w / 2), w.y + 8 + row * lh, w.w / 2 - 20, lh}, lines[i], 15.f,
                head ? t.patternFx : t.patternText, Align::Left, FontRole::Mono);
    }
}
