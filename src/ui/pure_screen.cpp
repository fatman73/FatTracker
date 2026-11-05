// Pure mode screen: a ProTracker style Amiga lowres screen (320x256), drawn pixel by pixel
// and stretched over the window with sharp pixels. Shares all editing state with the normal screen.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui/pixel_canvas.h"
#include "ui/play_screen.h"
#include "ui/pattern_layout.h"
#include "ui/pure_layout.h"
#include "ui/widgets.h"

namespace {
// ProTracker 2.3 palette: grey panels with light / dark bevels, embossed captions, black values,
// blue pattern text, yellow quadrascope, red-yellow-green VU meters.
constexpr uint32_t kBlack = argb(0x000000);
constexpr uint32_t kFace = argb(0x888888), kLight = argb(0xBBBBBB), kDark = argb(0x555555);
constexpr uint32_t kLabelInk = argb(0xCCCCCC), kLabelShadow = argb(0x333333);
constexpr uint32_t kValueInk = argb(0x000000);
constexpr uint32_t kBarFace = argb(0xBBBBBB), kBarLight = argb(0xEEEEEE), kBarDark = argb(0x666666);
constexpr uint32_t kBlue = argb(0x4A66FF), kCursor = argb(0xEE2222), kScope = argb(0xFFDD00);
constexpr int kVuMax = 48;
constexpr int kPureButtons[12] = {0, 1, -2, 2, 3, -3, 4, -4, -5, 6, -6, -7};  // B_* of the normal screen, <0 special
const char kHexDigits[] = "0123456789ABCDEF";

void box(PixelCanvas& c, const pure::Box& b, bool sunken = false) { c.bevel(b.x, b.y, b.w, b.h, kFace, kLight, kDark, sunken); }

void label(PixelCanvas& c, const pure::Box& b, const char* s) {  // embossed, centred
    const int w = (int)std::strlen(s) * 8 - 1;
    c.textShadow(b.x + (b.w - w) / 2, b.y + (b.h - 5) / 2, s, kLabelInk, kLabelShadow, kSmall);
}

void value(PixelCanvas& c, const pure::Box& b, const char* s) {  // black, right aligned
    c.text(pure::valueX(b, (int)std::strlen(s)), b.y + (b.h - 6) / 2 + 1, s, kValueInk, kMedium);
}

// VU colour k pixels above the bar: green at the bottom, yellow, orange, red at the top.
uint32_t vuColor(int k) {
    struct Stop {
        float at;
        int r, g, b;
    };
    static const Stop kStops[] = {{0.f, 0x10, 0xB8, 0x10}, {0.5f, 0xF0, 0xF0, 0x00}, {0.78f, 0xFF, 0x90, 0x00}, {1.f, 0xFF, 0x20, 0x00}};
    const float f = (float)k / (kVuMax - 1);
    int i = 0;
    while (i < 2 && f > kStops[i + 1].at) ++i;
    const Stop& a = kStops[i];
    const Stop& b = kStops[i + 1];
    const float u = (f - a.at) / (b.at - a.at);
    auto mix = [&](int x, int y) { return (uint32_t)(x + (y - x) * u + 0.5f); };
    return argb((mix(a.r, b.r) << 16) | (mix(a.g, b.g) << 8) | mix(a.b, b.b));
}

// Character of the 8 character cell text ("C#201A0F") under the cursor column.
int cursorChar(int col) { return col == kColNote ? 0 : col <= kColInstrLo ? 2 + col : col; }

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

// The Amiga screen covers the whole window (device pixels).
void PlayScreen::pureGeometry(float& ox, float& oy, float& sx, float& sy) const {
    ox = oy = 0;
    sx = L_.W * vpScale_ / PixelCanvas::W;
    sy = L_.H * vpScale_ / PixelCanvas::H;
}

void PlayScreen::pureToAmiga(float lx, float ly, int& ax, int& ay) const {
    float ox, oy, sx, sy;
    pureGeometry(ox, oy, sx, sy);
    ax = (int)((lx * vpScale_ - ox) / sx);
    ay = (int)((ly * vpScale_ - oy) / sy);
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

    // ---- counters: embossed labels, black values
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
            case 6: std::snprintf(buf, sizeof buf, "%05X", smp ? smp->length & 0xFFFFF : 0); break;
            case 7: std::snprintf(buf, sizeof buf, "%05X", smp ? smp->loopStart & 0xFFFFF : 0); break;
            default: std::snprintf(buf, sizeof buf, "%05X", smp ? (smp->loop == LoopType::None ? 2 : smp->loopLength & 0xFFFFF) : 0); break;
        }
        value(c, pure::counterValue(r), buf);
        for (int d = 0; d < 2; ++d) {
            const pure::Box a = pure::counterArrow(r, d);
            box(c, a);
            c.textShadow(a.x + 3, a.y + 3, d ? "\x02" : "\x01", kLabelInk, kLabelShadow, kSmall);
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

    // ---- quadrascope: yellow dots on black
    box(c, pure::scopeTitle());
    label(c, pure::scopeTitle(), "QUADRASCOPE");
    c.bevel(120, 5 * pure::kRowH, 200, 3 * pure::kRowH, kFace, kLight, kDark);
    for (int i = 0; i < 4; ++i) {
        const pure::Box sb = pure::scope(i);
        const int ch = first + i;
        const char num[2] = {(char)('1' + i), 0};
        c.textShadow(sb.x - 7, sb.y + 1, num, kLabelInk, kLabelShadow, kSmall);
        c.fill(sb.x, sb.y, sb.w, sb.h, kBlack);
        if (ch >= nch || tel.ch[ch].muted) continue;
        e.scope(ch, scopeBuf_, sb.w);
        const int mid = sb.y + sb.h / 2;
        for (int x = 0; x < sb.w; ++x)
            c.set(sb.x + x, std::clamp(mid - (int)(scopeBuf_[x] * (sb.h / 2 - 1) * 1.6f), sb.y + 1, sb.y + sb.h - 2),
                  kScope);
    }
    // machine line: name and the power / drive LEDs of the A500 case, then EXIT
    const pure::Box cl = pure::creditLine();
    c.bevel(cl.x, cl.y, cl.w, cl.h, kFace, kFace, kFace);
    c.textShadow(cl.x + 4, cl.y + 3, pure_ == 2 ? "A1200" : "A500", kLabelInk, kLabelShadow, kSmall);
    const uint32_t power = tel.led ? argb(0x33FF44) : argb(0x1C7A24);  // dims with the filter, like the A500
    const uint32_t drive = tel.drive ? argb(0xFFA020) : argb(0x5A3A10);
    c.bevel(cl.x + 52, cl.y + 3, 14, 5, power, power, kDark, true);
    c.bevel(cl.x + 72, cl.y + 3, 14, 5, drive, drive, kDark, true);
    c.textShadow(cl.x + 92, cl.y + 3, "DF0", kLabelInk, kLabelShadow, kSmall);
    box(c, pure::exitButton(), hover_ == kPureExitHover);
    label(c, pure::exitButton(), "EXIT PURE");

    // ---- song / sample names: embossed captions, black names
    const pure::Box sr = pure::songRow();
    box(c, sr);
    c.textShadow(sr.x + 18, sr.y + 3, "SONGNAME:", kLabelInk, kLabelShadow, kSmall);
    char name[23];
    std::snprintf(name, sizeof name, "%-20.20s", s ? s->name : "");
    for (char& ch : name)
        if (ch == ' ') ch = '_';
    c.text(sr.x + 106, sr.y + 3, name, kValueInk, kMedium);
    const unsigned secs = tel.rate ? (unsigned)(tel.playFrames / tel.rate) : 0;
    std::snprintf(buf, sizeof buf, "%02u:%02u", (secs / 60) % 100, secs % 60);
    value(c, sr, buf);
    const pure::Box smr = pure::sampleRow();
    box(c, smr);
    c.textShadow(smr.x + 10, smr.y + 3, "SAMPLENAME:", kLabelInk, kLabelShadow, kSmall);
    std::snprintf(name, sizeof name, "%-22.22s", smp ? smp->name : "");
    for (char& ch : name)
        if (ch == ' ') ch = '_';
    c.text(smr.x + 106, smr.y + 3, name, kValueInk, kMedium);
    box(c, pure::loadButton());
    label(c, pure::loadButton(), "LOAD");

    // ---- info line: edit skip, tempo, status, tune size, timing
    const pure::Box info = pure::infoArea();
    box(c, info);
    c.bevel(8, info.y + 5, 22, 11, kFace, kDark, kLight);
    std::snprintf(buf, sizeof buf, "%2d", editSkip_);
    c.text(11, info.y + 8, buf, kValueInk, kMedium);
    std::snprintf(buf, sizeof buf, "%3d", tel.playing ? tel.bpm : (s ? s->initialBpm : 125));
    c.text(36, info.y + 2, buf, kValueInk, kMedium);
    c.textShadow(pure::tempoArrow(0).x, pure::tempoArrow(0).y + 1, "\x01", kLabelInk, kLabelShadow, kSmall);
    c.textShadow(pure::tempoArrow(0).x + 8, pure::tempoArrow(0).y + 1, "TEMPO", kLabelInk, kLabelShadow, kSmall);
    c.textShadow(pure::tempoArrow(1).x, pure::tempoArrow(1).y + 1, "\x02", kLabelInk, kLabelShadow, kSmall);
    c.textShadow(90, info.y + 7, "STATUS:", kLabelInk, kLabelShadow, kSmall);
    std::snprintf(buf, sizeof buf, "%.15s", status_);
    c.text(148, info.y + 7, buf, kValueInk, kMedium);
    size_t bytes = 0;
    if (s)
        for (const Sample& sm : s->samples) bytes += (size_t)sm.length * (sm.is16 ? 2 : 1);
    c.textShadow(234, info.y + 2, "TUNE", kLabelInk, kLabelShadow, kSmall);
    std::snprintf(buf, sizeof buf, "%6u", (unsigned)(bytes % 1000000));
    c.text(270, info.y + 2, buf, kValueInk, kMedium);
    c.textShadow(234, info.y + 10, "TIMING", kLabelInk, kLabelShadow, kSmall);
    c.text(294, info.y + 10, "CIA", kValueInk, kMedium);

    // ---- pattern: grey frame, black columns, small blue text, tall black text on the playing row
    c.fill(0, pure::kPatternY, 320, 256 - pure::kPatternY, kFace);
    const pure::Box rn = pure::rowNumbers();
    constexpr int kBottomFrame = 3;  // grey frame under the pattern, like ProTracker
    c.fill(rn.x, rn.y, rn.w, rn.h - kBottomFrame, kBlack);
    for (int i = 0; i < 4; ++i)
        c.fill(pure::track(i).x, pure::kPatternY, pure::kTrackW, 256 - pure::kPatternY - kBottomFrame, kBlack);
    c.fill(0, 255, 320, 1, kDark);
    const bool ptOct = s && s->quirks == Quirks::PT23;
    const int rows = s && !s->patterns.empty() ? s->patterns[pat].rows : 64;
    const int cur = std::min(viewRow_, rows - 1);
    for (int rel = -pure::kRowsAbove; rel <= pure::kRowsAbove; ++rel) {
        const int row = cur + rel;
        if (row < 0 || row >= rows) continue;
        const bool centre = rel == 0;
        const int y = rel < 0 ? pure::kBarY + rel * pure::kPatternRowH + 1
                              : (centre ? pure::kBarY : pure::kBarY + pure::kBarH + (rel - 1) * pure::kPatternRowH + 1);
        std::snprintf(buf, sizeof buf, row < 100 ? "%02d" : "%3d", row);
        const int nx = rn.x + (row < 100 ? 4 : 0);
        if (centre) {
            c.bevel(rn.x - 2, y, rn.w + 4, pure::kBarH, kBarFace, kBarLight, kBarDark);
            c.text(nx, y + 3, buf, kValueInk, kTall);
        } else {
            c.text(nx, y, buf, kBlue, kSmall);
        }
        for (int i = 0; i < 4; ++i) {
            const int ch = first + i;
            const pure::Box tb = pure::track(i);
            if (centre) c.bevel(tb.x - 2, y, tb.w + 4, pure::kBarH, kBarFace, kBarLight, kBarDark);
            if (!s || ch >= nch || s->patterns.empty()) continue;
            char cell[9];
            ptCell(s->patterns[pat].at(row, ch), ptOct, cell);
            if (centre) c.text(tb.x + 2, y + 3, cell, kValueInk, kTall);
            else c.text(tb.x + 2, y, cell, kBlue, kSmall);
        }
    }
    // cursor: a red one-character frame on the playing row, on the element that is edited
    // (note, sample hi / lo, effect, parameter hi / lo), always red like ProTracker
    if (curCh_ >= first && curCh_ < first + 4 && curCol_ != kColVolHi && curCol_ != kColVolLo) {
        const pure::Box tb = pure::track(curCh_ - first);
        c.frame(tb.x + 1 + cursorChar(curCol_) * 8, pure::kBarY + 1, 9, pure::kBarH - 2, kCursor);
    }
    // VU meters standing on the playing row: one gradient, red at the top, yellow, green at the bottom
    for (int i = 0; i < 4; ++i) {
        const int ch = first + i;
        if (ch >= nch || tel.ch[ch].muted) continue;
        const int h = std::min(kVuMax, (int)(tel.ch[ch].vu * 1.6f * kVuMax));
        const int x = pure::track(i).x + 25;
        for (int k = 0; k < h; ++k) c.fill(x, pure::kBarY - 1 - k, 10, 1, vuColor(k));
    }

    // ---- show it over the whole window
    float ox, oy, sx, sy;
    pureGeometry(ox, oy, sx, sy);
    g.drawPixels(c.pixels(), PixelCanvas::W, PixelCanvas::H, ox, oy, PixelCanvas::W * sx, PixelCanvas::H * sy);
    drawDialog(g, tx, t);
    animating_ = true;  // scopes, VU meters, clock and LEDs move all the time
}

UiAction PlayScreen::pureMouse(float lx, float ly, bool right, Engine& e) {
    int ax, ay;
    pureToAmiga(lx, ly, ax, ay);
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
    if (h == pure::kExit) {
        setPure(0, e);
        return UiAction::Redraw;
    }
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
