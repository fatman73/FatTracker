// SAMPLER: waveform editor in the pattern area.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/keymap.h"
#include "core/sample_edit.h"
#include "ui/play_screen.h"
#include "ui/widgets.h"

namespace {
enum {
    S_PLAY, S_PLAYSEL, S_STOP, S_CUT, S_COPY, S_PASTE, S_CROP, S_SILENCE, S_REVERSE, S_NORMALIZE,
    S_FADEIN, S_FADEOUT, S_VOLDN, S_VOLUP, S_BITS, S_LOOP, S_SHOWALL, S_ZOOMSEL, S_UNDO, S_EXIT, S_COUNT
};
const char* const kSamplerButtons[S_COUNT] = {"PLAY",   "PLAY SEL", "STOP",     "CUT",     "COPY",
                                             "PASTE",  "CROP",     "SILENCE",  "REVERSE", "NORMALIZE",
                                             "FADE IN", "FADE OUT", "VOL -",   "VOL +",   "8/16 BIT",
                                             "LOOP",   "SHOW ALL", "ZOOM SEL", "UNDO",    "EXIT"};
constexpr int kHitSampler = 700;
}  // namespace

RectF PlayScreen::samplerWave() const {
    const RectF a = bottomArea();
    return {a.x + 8, a.y + 44, a.w - 16, a.h - 44 - 2 * 46 - 12};
}

RectF PlayScreen::samplerButton(int i) const {
    const RectF a = bottomArea();
    const float w = (a.w - 16 - 9 * 6) / 10;
    return {a.x + 8 + (i % 10) * (w + 6), a.y + a.h - 2 * 46 - 4 + (i / 10) * 46, w, 40};
}

float PlayScreen::sampleToX(uint32_t i) const {
    const RectF w = samplerWave();
    const double span = viewEnd_ > viewStart_ ? (double)(viewEnd_ - viewStart_) : 1.0;
    return w.x + (float)(((double)i - viewStart_) / span * w.w);
}

uint32_t PlayScreen::xToSample(float x) const {
    const RectF w = samplerWave();
    double t = (x - w.x) / w.w;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return viewStart_ + (uint32_t)std::lround(t * (viewEnd_ - viewStart_));
}

void PlayScreen::samplerDraw(Gfx& g, Text& tx, const Theme& t, Engine& e) {
    const RectF a = bottomArea();
    Ui u{g, tx, t};
    g.panel(a, t, Gfx::PanelState::Normal);
    const Song* s = e.song();
    const int si = s ? curSampleIndex(*s) : -1;
    const Sample* sm = si >= 0 ? &s->samples[si] : nullptr;
    const uint32_t len = sm ? sm->length : 0;
    if (viewEnd_ > len || viewEnd_ <= viewStart_) {
        viewStart_ = 0;
        viewEnd_ = len;
    }
    selA_ = std::min(selA_, len);
    selB_ = std::min(selB_, len);
    char buf[160], name[23] = {};
    if (sm) std::memcpy(name, sm->name, 22);
    for (char& c : name)
        if (c && (c < 32 || c > 126)) c = ' ';
    const uint32_t s0 = std::min(selA_, selB_), s1 = std::max(selA_, selB_);
    std::snprintf(buf, sizeof buf, "SAMPLE %02X  %-22s  LENGTH %u  %s  LOOP %s %u-%u  SEL %u-%u", sample_ + 1, name,
                  len, sm && sm->is16 ? "16 BIT" : "8 BIT",
                  !sm || sm->loop == LoopType::None ? "OFF" : (sm->loop == LoopType::Forward ? "FWD" : "PINGPONG"),
                  sm ? sm->loopStart : 0u, sm ? sm->loopStart + sm->loopLength : 0u, s0, s1);
    RectF info = {a.x + 8, a.y + 6, a.w - 16, 32};
    g.well(info, t, mixColor(t.panelLight, t.panel, 0.25f));
    tx.draw(g, info, buf, 15.f, t.digits, Align::Left, FontRole::Mono);

    const RectF w = samplerWave();
    g.well(w, t, t.patternBg);
    g.pushClip(w);
    const float cy = w.y + w.h / 2, amp = w.h * 0.46f;
    if (sm && len) {
        if (s1 > s0) {
            const float x0 = std::max(w.x, sampleToX(s0)), x1 = std::min(w.x + w.w, sampleToX(s1));
            g.fillRect({x0, w.y, x1 - x0, w.h}, withAlpha(t.patternSample, 0.25f));
        }
        g.line(w.x, cy, w.x + w.w, cy, withAlpha(t.patternText, 0.3f), 1.f);
        const int cols = (int)w.w;
        const double span = (double)(viewEnd_ - viewStart_) / cols;
        auto val = [&](uint32_t i) { return sm->is16 ? sm->data16[i] / 32768.f : sm->data8[i] / 128.f; };
        float prevY = cy;
        for (int c = 0; c < cols; ++c) {
            uint32_t i0 = viewStart_ + (uint32_t)(c * span);
            uint32_t i1 = viewStart_ + (uint32_t)((c + 1) * span);
            if (i1 <= i0) i1 = i0 + 1;
            if (i0 >= len) break;
            i1 = std::min(i1, len);
            float mn = 1, mx = -1;
            for (uint32_t i = i0; i < i1; ++i) {
                const float v = val(i);
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            const float x = w.x + c + 0.5f;
            if (span <= 1.0) {
                const float y = cy - val(i0) * amp;
                if (c) g.line(x - 1, prevY, x, y, t.scopeLine, 1.5f);
                prevY = y;
            } else {
                g.line(x, cy - mx * amp, x, cy - mn * amp + 0.5f, t.scopeLine, 1.f);
            }
        }
        if (sm->loop != LoopType::None) {
            for (int k = 0; k < 2; ++k) {
                const float x = sampleToX(k == 0 ? sm->loopStart : sm->loopStart + sm->loopLength);
                g.line(x, w.y, x, w.y + w.h, t.patternFx, 2.f);
                g.fillRect({x - 7, w.y, 14, 14}, t.patternFx);
                tx.draw(g, {x - 7, w.y, 14, 14}, k == 0 ? "S" : "E", 10.f, t.patternBg, Align::Center);
            }
        }
    } else {
        tx.draw(g, w, "EMPTY SAMPLE - LOAD ONE IN DISK OP. (SAMPLES)", 18.f, t.rowNumber, Align::Center);
    }
    g.popClip();
    for (int i = 0; i < S_COUNT; ++i) {
        Gfx::PanelState st = hover_ == kHitSampler + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal;
        drawButton(u, samplerButton(i), kSamplerButtons[i], st, 14.f);
    }
}

void PlayScreen::samplerOp(int op, Engine& e) {
    const Song* s = e.song();
    const int idx = s ? curSampleIndex(*s) : -1;
    if (idx < 0) return;
    const uint32_t len = s->samples[idx].length;
    uint32_t a = std::min(selA_, selB_), b = std::max(selA_, selB_);
    const bool hasSel = b > a;
    if (!hasSel) a = 0, b = len;
    auto modify = [&](auto&& fn, const char* msg) {
        e.editStructure([&](Song& song) {
            sampleUndo_ = song.samples[idx];
            sampleUndoIndex_ = idx;
            fn(song.samples[idx]);
            sampleFixLoop(song.samples[idx]);
        });
        editor_.markDirty();
        setStatus(msg);
    };
    switch (op) {
        case S_PLAY:
            e.previewNote(idx, 49, 0);
            break;
        case S_PLAYSEL:
            e.previewNote(idx, 49, a);
            break;
        case S_STOP:
            e.previewStop();
            break;
        case S_CUT:
            if (!hasSel) return setStatus("SELECT A RANGE FIRST");
            modify([&](Sample& sm) { sampleClip_ = sampleCut(sm, a, b); }, "CUT");
            selB_ = selA_ = a;
            break;
        case S_COPY:
            e.lock();
            sampleClip_ = sampleCopy(e.song()->samples[idx], a, b);
            e.unlock();
            setStatus("COPIED");
            break;
        case S_PASTE:
            if (!sampleClip_.length) return setStatus("CLIPBOARD IS EMPTY");
            modify([&](Sample& sm) { samplePaste(sm, std::min(selA_, selB_), sampleClip_); }, "PASTED");
            break;
        case S_CROP:
            if (!hasSel) return setStatus("SELECT A RANGE FIRST");
            modify([&](Sample& sm) { sampleCrop(sm, a, b); }, "CROPPED");
            selA_ = 0, selB_ = b - a;
            break;
        case S_SILENCE:
            modify([&](Sample& sm) { sampleSilence(sm, a, b); }, "SILENCED");
            break;
        case S_REVERSE:
            modify([&](Sample& sm) { sampleReverse(sm, a, b); }, "REVERSED");
            break;
        case S_NORMALIZE:
            modify([&](Sample& sm) { sampleNormalize(sm, a, b); }, "NORMALIZED");
            break;
        case S_FADEIN:
            modify([&](Sample& sm) { sampleVolume(sm, a, b, 0.f, 100.f); }, "FADED IN");
            break;
        case S_FADEOUT:
            modify([&](Sample& sm) { sampleVolume(sm, a, b, 100.f, 0.f); }, "FADED OUT");
            break;
        case S_VOLDN:
        case S_VOLUP: {
            const float f = op == S_VOLDN ? 80.f : 125.f;
            modify([&](Sample& sm) { sampleVolume(sm, a, b, f, f); }, op == S_VOLDN ? "QUIETER" : "LOUDER");
            break;
        }
        case S_BITS:
            modify([&](Sample& sm) { sm.is16 ? sampleTo8(sm) : sampleTo16(sm); }, "BIT DEPTH CHANGED");
            break;
        case S_LOOP:
            modify(
                [&](Sample& sm) {
                    if (hasSel) {
                        sm.loop = sm.loop == LoopType::Forward ? LoopType::PingPong : LoopType::Forward;
                        sm.loopStart = a;
                        sm.loopLength = b - a;
                    } else {
                        sm.loop = sm.loop == LoopType::None      ? LoopType::Forward
                                  : sm.loop == LoopType::Forward ? LoopType::PingPong
                                                                 : LoopType::None;
                        if (sm.loop != LoopType::None && sm.loopLength < 2) {
                            sm.loopStart = 0;
                            sm.loopLength = sm.length;
                        }
                    }
                },
                "LOOP CHANGED");
            break;
        case S_SHOWALL:
            viewStart_ = 0, viewEnd_ = len;
            break;
        case S_ZOOMSEL:
            if (hasSel) viewStart_ = a, viewEnd_ = b;
            break;
        case S_UNDO:
            if (sampleUndoIndex_ != idx) return setStatus("NOTHING TO UNDO");
            e.editStructure([&](Song& song) { std::swap(song.samples[idx], sampleUndo_); });
            editor_.markDirty();
            setStatus("UNDO");
            break;
        case S_EXIT:
            panel_ = Panel::Main;
            break;
    }
}

UiAction PlayScreen::samplerMouse(float x, float y, bool right, Engine& e) {
    for (int i = 0; i < S_COUNT; ++i)
        if (samplerButton(i).contains(x, y)) {
            samplerOp(i, e);
            return UiAction::Redraw;
        }
    const RectF w = samplerWave();
    if (!w.contains(x, y)) return UiAction::None;
    const Song* s = e.song();
    const int si = s ? curSampleIndex(*s) : -1;
    if (si < 0) return UiAction::None;
    const Sample& sm = s->samples[si];
    if (sm.loop != LoopType::None && y < w.y + 20) {
        if (std::fabs(x - sampleToX(sm.loopStart)) < 9) drag_ = 2;
        else if (std::fabs(x - sampleToX(sm.loopStart + sm.loopLength)) < 9) drag_ = 3;
        if (drag_) return UiAction::Redraw;
    }
    if (right) {  // right click: play from here
        e.previewNote(si, 49, xToSample(x));
        return UiAction::Redraw;
    }
    drag_ = 1;
    selA_ = selB_ = xToSample(x);
    return UiAction::Redraw;
}

UiAction PlayScreen::samplerKey(unsigned vk, bool shift, bool ctrl, Engine& e) {
    if (vk == VK_ESCAPE) {
        panel_ = Panel::Main;
        return UiAction::Redraw;
    }
    if (ctrl) {
        const int op = vk == 'X' ? S_CUT : vk == 'C' ? S_COPY : vk == 'V' ? S_PASTE : vk == 'Z' ? S_UNDO : -1;
        if (op >= 0) {
            samplerOp(op, e);
            return UiAction::Redraw;
        }
        if (vk == 'A' && e.song() && curSampleIndex(*e.song()) >= 0) {
            selA_ = 0;
            selB_ = e.song()->samples[curSampleIndex(*e.song())].length;
            return UiAction::Redraw;
        }
    }
    if (vk == VK_DELETE) {
        samplerOp(S_CUT, e);
        return UiAction::Redraw;
    }
    (void)shift;
    const int note = noteForKey(vk, octave_);
    if (note > 0) {
        if (e.song()) e.previewNote(curSampleIndex(*e.song(), note), note, 0);
        jamKey_ = vk;
        return UiAction::Redraw;
    }
    return UiAction::None;
}

void PlayScreen::sampleLoaded(Engine& e, Sample&& s, const std::wstring& fileName) {
    const size_t dot = fileName.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? fileName : fileName.substr(0, dot);
    int n = 0;
    for (wchar_t c : stem) {
        if (n >= 22) break;
        s.name[n++] = (c >= 32 && c < 127) ? (char)c : '_';
    }
    s.name[n] = 0;
    const Song* cur = e.song();
    if (!cur) return;
    // the slot under the SAMPLE counter; an XM instrument without a sample gets a new one mapped to all notes
    int idx = curSampleIndex(*cur);
    const bool inst = !cur->instruments.empty();
    if (idx < 0) idx = inst ? (int)cur->samples.size() : sample_;
    const int ins = sample_;
    const bool hadOld = idx < (int)cur->samples.size();
    e.editStructure([&](Song& song) {
        if (idx >= (int)song.samples.size()) song.samples.resize(idx + 1);
        if (inst && ins < (int)song.instruments.size() && !hadOld)
            for (auto& m : song.instruments[ins].sampleMap) m = (uint16_t)idx;
        sampleUndo_ = hadOld ? song.samples[idx] : Sample{};
        sampleUndoIndex_ = idx;
        song.samples[idx] = static_cast<Sample&&>(s);
    });
    editor_.markDirty();
    viewStart_ = viewEnd_ = selA_ = selB_ = 0;
    setStatus("SAMPLE LOADED");
}
