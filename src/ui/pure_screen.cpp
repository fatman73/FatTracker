// Pure mode screen: a ProTracker style Amiga lowres screen (320x256), drawn pixel by pixel
// and shown with an integer scale. Shares all editing state with the normal screen.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/pixel_canvas.h"
#include "ui/play_screen.h"
#include "ui/pattern_layout.h"
#include "ui/pure_layout.h"
#include "ui/widgets.h"

namespace {
constexpr uint32_t kBlack = argb(0x000000);
constexpr uint32_t kFace = argb(0xABABAB), kLight = argb(0xEEEEEE), kDark = argb(0x555555);
constexpr uint32_t kInk = argb(0xEEEEEE), kInkShadow = argb(0x222222);
constexpr uint32_t kBarFace = argb(0xBBBBBB), kBarInk = argb(0x111111);
constexpr uint32_t kBlue = argb(0x5A78FF), kCursor = argb(0xEE2222);
constexpr uint32_t kScopeLine = argb(0x22DD22), kVu = argb(0x22CC33), kVuDark = argb(0x118822);
constexpr uint32_t kSep = argb(0x777777);
constexpr int kPureButtons[12] = {0, 1, -2, 2, 3, -3, 4, -4, -5, 6, -6, -7};  // B_* of the normal screen, <0 special
const char kHexDigits[] = "0123456789ABCDEF";

void box(PixelCanvas& c, const pure::Box& b, bool sunken = false) { c.bevel(b.x, b.y, b.w, b.h, kFace, kLight, kDark, sunken); }

void label(PixelCanvas& c, const pure::Box& b, const char* s, bool center = true) {
    const int w = (int)std::strlen(s) * 8 - 1;
    const int x = center ? b.x + (b.w - w) / 2 : b.x + 3;
    c.textShadow(x, b.y + (b.h - 7) / 2, s, kInk, kInkShadow);
}

void rightText(PixelCanvas& c, const pure::Box& b, const char* s) {
    c.textShadow(b.x + b.w - 2 - (int)std::strlen(s) * 8, b.y + 2, s, kInk, kInkShadow);
}

// 8 character ProTracker cell text: note, instrument, effect, parameter ("C#201A0F").
void ptCell(const Cell& cell, bool ptOctaves, char out[9]) {
    char t[11];
    formatCell(cell, ptOctaves, t);
    out[0] = t[0];
    out[1] = t[1];
    out[2] = t[2];
    out[3] = kHexDigits[cell.instr >> 4];
    out[4] = kHexDigits[cell.instr & 15];
    out[5] = cell.fx < 16 ? kHexDigits[cell.fx] : (char)('G' + cell.fx - 16);
    out[6] = kHexDigits[cell.param >> 4];
    out[7] = kHexDigits[cell.param & 15];
    out[8] = 0;
}
}  // namespace

void PlayScreen::pureGeometry(float& ox, float& oy, float& scale) const {
    const float dw = L_.W * vpScale_, dh = L_.H * vpScale_;
    scale = (float)std::max(1, (int)std::min(dw / PixelCanvas::W, dh / PixelCanvas::H));
    ox = (float)(int)((dw - PixelCanvas::W * scale) / 2);
    oy = (float)(int)((dh - PixelCanvas::H * scale) / 2);
}

RectF PlayScreen::pureExitRect() const {
    float ox, oy, sc;
    pureGeometry(ox, oy, sc);
    const float right = (ox + PixelCanvas::W * sc) / vpScale_;  // logical x where the Amiga screen ends
    const float w = std::min(260.f, std::max(120.f, L_.W - right - 40.f));
    if (L_.W - right >= w + 20.f) return {right + (L_.W - right - w) / 2, L_.H - 90.f, w, 50.f};
    return {L_.W - w - 10.f, L_.H - 60.f, w, 50.f};  // no margin: over the screen corner
}

int PlayScreen::pureFirstChannel(int numChannels) {
    const int maxFirst = std::max(0, numChannels - 4);
    if (curCh_ < pureFirst_) pureFirst_ = curCh_;
    if (curCh_ > pureFirst_ + 3) pureFirst_ = curCh_ - 3;
    pureFirst_ = std::clamp(pureFirst_, 0, maxFirst);
    return pureFirst_;
}

void PlayScreen::pureDraw(Gfx& g, Text& tx, const Theme& t, Engine& e) {
    if (!canvas_) canvas_ = std::make_unique<PixelCanvas>();
    PixelCanvas& c = *canvas_;
    panel_ = Panel::Main;  // the classic screen has no modern panels
    const Song* s = e.song();
    const Telemetry& tel = last_;
    const int nch = s ? s->numChannels : 4;
    const int first = pureFirstChannel(nch);
    const int sIdx = s ? curSampleIndex(*s) : -1;
    const Sample* smp = sIdx >= 0 ? &s->samples[sIdx] : nullptr;
    char buf[64];
    c.clear(kBlack);

    // ---- counters
    static const char* const kLabels[9] = {"POS", "PATTERN", "LENGTH", "FINETUNE", "SAMPLE",
                                           "VOLUME", "LENGTH", "REPEAT", "REPLEN"};
    const int pat = s && viewPattern_ < (int)s->patterns.size() ? viewPattern_ : 0;
    for (int r = 0; r < pure::kCounterRows; ++r) {
        box(c, pure::counterLabel(r));
        label(c, pure::counterLabel(r), kLabels[r]);
        box(c, pure::counterValue(r));
        switch (r) {
            case 0: std::snprintf(buf, sizeof buf, "%04d", viewOrder_); break;
            case 1: std::snprintf(buf, sizeof buf, "%04d", s && !s->order.empty() ? s->order[std::min(viewOrder_, (int)s->order.size() - 1)] : 0); break;
            case 2: std::snprintf(buf, sizeof buf, "%04d", s ? (int)s->order.size() : 0); break;
            case 3: std::snprintf(buf, sizeof buf, smp && smp->finetune / 16 ? "%+d" : "%d", smp ? smp->finetune / 16 : 0); break;
            case 4: std::snprintf(buf, sizeof buf, "%04X", sample_ + 1); break;
            case 5: std::snprintf(buf, sizeof buf, "%04X", smp ? smp->volume : 0); break;
            case 6: std::snprintf(buf, sizeof buf, "%04X", smp ? smp->length : 0); break;
            case 7: std::snprintf(buf, sizeof buf, "%04X", smp ? smp->loopStart : 0); break;
            default: std::snprintf(buf, sizeof buf, "%04X", smp ? (smp->loop == LoopType::None ? 2 : smp->loopLength) : 0); break;
        }
        rightText(c, pure::counterValue(r), buf);
        for (int d = 0; d < 2; ++d) {
            const pure::Box a = pure::counterArrow(r, d);
            box(c, a);
            c.textShadow(a.x + 2, a.y + 2, d ? "\x02" : "\x01", kInk, kInkShadow);
        }
    }
    for (int i = 0; i < 2; ++i) {
        box(c, pure::posButton(i));
        label(c, pure::posButton(i), i ? "D" : "I");
    }

    // ---- buttons
    for (int i = 0; i < 12; ++i) {
        const int b = kPureButtons[i];
        const bool on = (b == 0 && tel.playing && !recordMode_) || (b == 4 && editMode_) || (b == 6 && recordMode_);
        box(c, pure::gridButton(i), on);
        label(c, pure::gridButton(i), pure::buttonLabel(i));
    }
    for (int i = 0; i < 5; ++i) {
        const bool muted = i < 4 && first + i < 32 && tel.ch[first + i].muted;
        box(c, pure::sideButton(i), muted);
        const char d[2] = {i < 4 ? (char)('1' + i) : 'A', 0};
        label(c, pure::sideButton(i), d);
    }

    // ---- quadrascope
    box(c, pure::scopeTitle());
    label(c, pure::scopeTitle(), "QUADRASCOPE");
    c.bevel(120, 5 * pure::kRowH, 200, 3 * pure::kRowH, kFace, kLight, kDark);
    for (int i = 0; i < 4; ++i) {
        const pure::Box sb = pure::scope(i);
        const int ch = first + i;
        c.text(sb.x - 7, sb.y + 2, i == 0 ? "1" : i == 1 ? "2" : i == 2 ? "3" : "4", kInkShadow, true);
        c.fill(sb.x, sb.y, sb.w, sb.h, kBlack);
        if (ch >= nch || tel.ch[ch].muted) continue;
        e.scope(ch, scopeBuf_, sb.w);
        const int mid = sb.y + sb.h / 2;
        int py = mid;
        for (int x = 0; x < sb.w; ++x) {
            const int y = std::clamp(mid - (int)(scopeBuf_[x] * (sb.h / 2 - 1) * 1.6f), sb.y + 1, sb.y + sb.h - 2);
            c.line(sb.x + x - (x ? 1 : 0), x ? py : y, sb.x + x, y, kScopeLine);
            py = y;
        }
    }
    box(c, pure::creditLine());
    c.textShadow(pure::creditLine().x + 4, pure::creditLine().y + 2, "FATTRACKER", kInk, kInkShadow);
    rightText(c, pure::creditLine(), pure_ == 2 ? "AMIGA 1200" : "AMIGA 500");

    // ---- song / sample names
    box(c, pure::songRow());
    char name[23];
    std::snprintf(name, sizeof name, "%-20.20s", s ? s->name : "");
    for (char& ch : name)
        if (ch == ' ') ch = '_';
    const unsigned secs = tel.rate ? (unsigned)(tel.playFrames / tel.rate) : 0;
    std::snprintf(buf, sizeof buf, "SONGNAME: %s %02u:%02u", name, (secs / 60) % 100, secs % 60);
    label(c, pure::songRow(), buf, false);
    box(c, pure::sampleRow());
    std::snprintf(name, sizeof name, "%-22.22s", smp ? smp->name : "");
    for (char& ch : name)
        if (ch == ' ') ch = '_';
    std::snprintf(buf, sizeof buf, "SAMPLENAME: %s", name);
    label(c, pure::sampleRow(), buf, false);
    box(c, pure::loadButton());
    label(c, pure::loadButton(), "LOAD");

    // ---- info line: edit skip, tempo, status, tune size, timing
    const pure::Box info = pure::infoArea();
    box(c, info);
    c.bevel(4, info.y + 3, 20, 11, kFace, kLight, kDark, true);
    std::snprintf(buf, sizeof buf, "%2d", editSkip_);
    c.textShadow(6, info.y + 5, buf, kInk, kInkShadow);
    std::snprintf(buf, sizeof buf, "%3d", tel.playing ? tel.bpm : (s ? s->initialBpm : 125));
    c.textShadow(42, info.y + 1, buf, kInk, kInkShadow);
    c.textShadow(pure::tempoArrow(0).x, pure::tempoArrow(0).y, "\x01", kInk, kInkShadow, true);
    c.textShadow(pure::tempoArrow(0).x + 8, pure::tempoArrow(0).y, "TEMPO", kInk, kInkShadow, true);
    c.textShadow(pure::tempoArrow(1).x, pure::tempoArrow(1).y, "\x02", kInk, kInkShadow, true);
    char st[16];
    std::snprintf(st, sizeof st, "%.10s", status_);
    std::snprintf(buf, sizeof buf, "STATUS:%s", st);
    c.textShadow(92, info.y + 5, buf, kInk, kInkShadow, true);
    size_t bytes = 0;
    if (s)
        for (const Sample& sm : s->samples) bytes += (size_t)sm.length * (sm.is16 ? 2 : 1);
    std::snprintf(buf, sizeof buf, "TUNE %6u", (unsigned)(bytes % 1000000));
    c.textShadow(232, info.y + 1, buf, kInk, kInkShadow, true);
    c.textShadow(232, info.y + 9, "TIMING  CIA", kInk, kInkShadow, true);

    // ---- pattern
    c.fill(0, pure::kPatternY, 320, 256 - pure::kPatternY, kBlack);
    c.fill(24, pure::kPatternY, 2, 256 - pure::kPatternY, kSep);
    for (int i = 0; i < 4; ++i) c.fill(pure::track(i).x + pure::kTrackW, pure::kPatternY, 3, 256 - pure::kPatternY, kSep);
    const bool ptOct = s && s->quirks == Quirks::PT23;
    const int rows = s && !s->patterns.empty() ? s->patterns[pat].rows : 64;
    const int cur = std::min(viewRow_, rows - 1);
    for (int rel = -pure::kRowsAbove; rel <= 9; ++rel) {
        const int row = cur + rel;
        if (row < 0 || row >= rows) continue;
        const bool centre = rel == 0;
        const int y = rel < 0 ? pure::kBarY + rel * pure::kPatternRowH
                              : (centre ? pure::kBarY : pure::kBarY + pure::kBarH + (rel - 1) * pure::kPatternRowH);
        if (y + 6 > 256) break;
        std::snprintf(buf, sizeof buf, row < 100 ? "%02d" : "%3d", row);
        if (centre) {
            c.bevel(0, y, 24, pure::kBarH, kBarFace, kLight, kDark);
            c.text(row < 100 ? 4 : 0, y + 2, buf, kBarInk);
        } else {
            c.text(row < 100 ? 4 : 0, y, buf, kBlue, true);
        }
        for (int i = 0; i < 4; ++i) {
            const int ch = first + i;
            const pure::Box tb = pure::track(i);
            if (centre) c.bevel(tb.x, y, tb.w, pure::kBarH, kBarFace, kLight, kDark);
            if (!s || ch >= nch || s->patterns.empty()) continue;
            char cell[9];
            ptCell(s->patterns[pat].at(row, ch), ptOct, cell);
            if (centre) c.text(tb.x + 3, y + 2, cell, kBarInk);
            else c.text(tb.x + 3, y, cell, kBlue, true);
        }
    }
    // edit cursor (red frame on the centre row)
    if (curCh_ >= first && curCh_ < first + 4 && curCol_ != kColVolHi && curCol_ != kColVolLo) {
        const pure::Box tb = pure::track(curCh_ - first);
        const int chars = curCol_ == kColNote ? 3 : 1;
        const int at = curCol_ == kColNote ? 0 : curCol_ <= kColInstrLo ? 2 + curCol_ : curCol_;
        c.frame(tb.x + 2 + at * 8, pure::kBarY, chars * 8 + 1, pure::kBarH, editMode_ ? kCursor : argb(0x666666));
    }
    // VU meters: green striped bars standing on the centre row
    for (int i = 0; i < 4; ++i) {
        const int ch = first + i;
        if (ch >= nch || tel.ch[ch].muted) continue;
        const int h = std::min(48, (int)(tel.ch[ch].vu * 1.6f * 48));
        const int x = pure::track(i).x + 26;
        for (int k = 0; k < h; ++k) c.fill(x, pure::kBarY - 1 - k, 14, 1, (k & 1) ? kVuDark : kVu);
    }

    // ---- show it
    g.fillRect({0, 0, L_.W, L_.H}, Color{0, 0, 0, 1});
    float ox, oy, sc;
    pureGeometry(ox, oy, sc);
    g.drawPixels(c.pixels(), PixelCanvas::W, PixelCanvas::H, ox, oy, sc);

    // ---- outside the Amiga screen: the A500 LEDs and a way back
    Ui u{g, tx, t};
    const RectF ex = pureExitRect();
    const float lx = ex.x, ly = ex.y - 120;
    auto led = [&](float y, const char* name, Color on, bool lit) {
        g.fillRoundRect({lx, y, 46, 12}, 3.f, lit ? on : Color{on.r * 0.25f, on.g * 0.25f, on.b * 0.25f, 1});
        tx.draw(g, {lx + 56, y - 6, 200, 24}, name, 14.f, Color{0.6f, 0.6f, 0.6f, 1}, Align::Left);
    };
    led(ly, "POWER", Color{0.2f, 1.f, 0.3f, 1}, true);
    led(ly + 34, "LED FILTER", Color{0.2f, 1.f, 0.3f, 1}, tel.led);
    led(ly + 68, "DRIVE", Color{1.f, 0.6f, 0.15f, 1}, tel.drive);
    drawButton(u, ex, "EXIT PURE MODE", hover_ == kPureExitHover ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 16.f);
    drawDialog(g, tx, t);
}

UiAction PlayScreen::pureMouse(float lx, float ly, bool right, Engine& e) {
    if (pureExitRect().contains(lx, ly)) {
        setPure(0, e);
        return UiAction::Redraw;
    }
    float ox, oy, sc;
    pureGeometry(ox, oy, sc);
    const int ax = (int)((lx * vpScale_ - ox) / sc), ay = (int)((ly * vpScale_ - oy) / sc);
    const Song* s = e.song();
    const int nch = s ? s->numChannels : 4;
    const int first = pureFirstChannel(nch);
    const int h = pure::hitTest(ax, ay);
    if (h >= pure::kArrow && h < pure::kArrow + 18) {
        const int row = (h - pure::kArrow) / 2;
        if (row == 6) {
            setStatus("SAMPLER ONLY");
            return UiAction::Redraw;
        }
        counterStep(row, (h - pure::kArrow) % 2 == 0 ? 1 : -1, right, e);
        return UiAction::Redraw;
    }
    if ((h == pure::kPosIns || h == pure::kPosDel) && s && !last_.playing) {
        const int pos = std::min(viewOrder_, (int)s->order.size() - 1);
        e.edit([&](Song& song) {
            if (h == pure::kPosIns) editor_.insertOrder(song, pos, song.order[pos]);
            else editor_.deleteOrder(song, pos);
        });
        if (viewOrder_ >= (int)e.song()->order.size()) viewOrder_ = (int)e.song()->order.size() - 1;
        return UiAction::Redraw;
    }
    if (h >= pure::kButton && h < pure::kButton + 12) {
        const int b = kPureButtons[h - pure::kButton];
        if (b >= 0) return pressButton(b, e);
        if (b == -2) return UiAction::ExportWav;
        if (b == -6) return UiAction::OpenFile;  // DISK OP.: the file requester
        setStatus("NOT IN PURE");
        return UiAction::Redraw;
    }
    if (h >= pure::kSide && h < pure::kSide + 5) {
        const int i = h - pure::kSide;
        if (i < 4 && first + i < nch) toggleMute(first + i, e);
        if (i == 4)
            for (int ch = 0; ch < nch; ++ch)
                if (last_.ch[ch].muted) e.setMute(ch, false);
        return UiAction::Redraw;
    }
    if (h >= pure::kScope && h < pure::kScope + 4 && first + (h - pure::kScope) < nch) {
        if (right) solo(first + h - pure::kScope, e);
        else toggleMute(first + h - pure::kScope, e);
        return UiAction::Redraw;
    }
    if (h == pure::kLoad) return UiAction::OpenFile;
    if (h == pure::kTempoUp || h == pure::kTempoDown) {
        bpmStep((h == pure::kTempoUp ? 1 : -1) * (right ? 10 : 1), e);
        return UiAction::Redraw;
    }
    if (h >= pure::kTrack && h < pure::kTrack + 4 && s) {
        const int i = h - pure::kTrack;
        if (first + i >= nch) return UiAction::None;
        curCh_ = first + i;
        const int col = pure::columnAt(i, ax);
        if (col >= 0) curCol_ = col;
        int rel = 0;
        if (pure::rowAt(ay, rel) && rel && !last_.playing) moveCursor(rel, 0, e);
        return UiAction::Redraw;
    }
    return UiAction::None;
}
