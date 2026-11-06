// INSTRUMENT: XM instrument editor in the pattern area (volume / panning envelopes, key map,
// fadeout, autovibrato). Every change runs under the engine lock with an undo step.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/instrument_edit.h"
#include "core/keymap.h"
#include "ui/play_screen.h"
#include "ui/widgets.h"

namespace {
constexpr int kHitInstr = 1000;
// hit ids (instrRect / instrHit)
enum {
    I_NAME, I_NEW, I_CONVERT, I_UNDO, I_SAMPLER, I_EXIT, I_TOP_COUNT,
    I_ENV = 10,    // + envelope * E_COUNT + E_*
    I_PARAM = 60,  // + row * 3 + (0 down, 1 value, 2 up)
    I_KEYS = 90,
    I_COUNT
};
enum { E_ON, E_SUS, E_LOOP, E_SUS_DN, E_SUS_UP, E_LS_DN, E_LS_UP, E_LE_DN, E_LE_UP, E_GRAPH, E_COUNT };
enum { P_FADE, P_VTYPE, P_VSWEEP, P_VDEPTH, P_VRATE, P_MAP, P_COUNT };
const char* const kTopLabels[I_TOP_COUNT] = {"", "NEW INSTRUMENT", "CONVERT SONG TO XM INSTRUMENTS", "UNDO", "SAMPLER",
                                             "EXIT"};
const float kTopW[I_TOP_COUNT] = {440, 230, 400, 110, 130, 110};
const char* const kParamLabels[P_COUNT] = {"FADEOUT", "VIBRATO TYPE", "VIBRATO SWEEP", "VIBRATO DEPTH",
                                           "VIBRATO RATE", "MAP SAMPLE"};
const char* const kVibTypes[4] = {"SINE", "SQUARE", "RAMP UP", "RAMP DOWN"};
constexpr int kWhiteOf[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};

struct Geo {
    RectF top, env[2], keys, params;
};

Geo geo(RectF a) {
    Geo g;
    g.top = {a.x + 8, a.y + 6, a.w - 16, 36};
    const float y1 = a.y + 48;
    const float envH = std::floor((a.h - 56) * 0.55f);
    const float bw = (a.w - 16 - 8) / 2;
    for (int i = 0; i < 2; ++i) g.env[i] = {a.x + 8 + i * (bw + 8), y1, bw, envH};
    const float y2 = y1 + envH + 8, pw = 600;
    g.keys = {a.x + 8, y2, a.w - 16 - 8 - pw, a.y + a.h - 8 - y2};
    g.params = {g.keys.x + g.keys.w + 8, y2, pw, g.keys.h};
    return g;
}

RectF graphInner(RectF r) { return {r.x + 12, r.y + 10, r.w - 24, r.h - 20}; }
float tickX(RectF in, int tick) { return in.x + tick * in.w / kEnvMaxTick; }
float valueY(RectF in, int v) { return in.y + (kEnvMaxValue - v) * in.h / kEnvMaxValue; }

RectF piano(RectF keys) { return {keys.x, keys.y + 68, keys.w, keys.h - 68}; }

RectF keyRect(RectF p, int k) {
    const float ww = p.w / 56.f;
    const int oct = k / 12, n = k % 12;
    if (kWhiteOf[n] >= 0) return {p.x + (oct * 7 + kWhiteOf[n]) * ww, p.y, ww, p.h};
    const float cx = p.x + (oct * 7 + kWhiteOf[n - 1] + 1) * ww, bw = ww * 0.62f;
    return {cx - bw / 2, p.y, bw, p.h * 0.6f};
}

Color sampleColor(const Theme& t, int g) {
    const Color c[6] = {t.patternSample, t.patternFx, t.patternVol, t.vuLow, t.vuHigh, t.digits};
    return c[g % 6];
}

const Envelope& envOf(const Instrument& in, int which) { return which ? in.panEnv : in.volEnv; }
Envelope& envOf(Instrument& in, int which) { return which ? in.panEnv : in.volEnv; }

int nearPoint(const Envelope& e, RectF in, float x, float y) {
    int best = -1;
    float bd = 11.f * 11.f;
    for (int i = 0; i < e.count; ++i) {
        const float dx = tickX(in, e.pts[i].tick) - x, dy = valueY(in, e.pts[i].value) - y;
        if (dx * dx + dy * dy <= bd) bd = dx * dx + dy * dy, best = i;
    }
    return best;
}

void drawEnvelope(Gfx& g, Text& tx, const Theme& t, RectF r, const Envelope& e, bool pan, int sel) {
    g.well(r, t, t.patternBg);
    const RectF in = graphInner(r);
    char b[16];
    for (int tick = 0; tick <= kEnvMaxTick; tick += 25) {
        const float x = tickX(in, tick);
        g.line(x, in.y, x, in.y + in.h, withAlpha(t.patternText, tick % 50 ? 0.05f : 0.12f), 1.f);
        if (tick % 50 == 0 && tick) {
            std::snprintf(b, sizeof b, "%d", tick);
            tx.draw(g, {x + 3, in.y + in.h - 16, 40, 16}, b, 11.f, withAlpha(t.rowNumber, 0.8f), Align::Left,
                    FontRole::Mono);
        }
    }
    for (int v = 0; v <= 64; v += 16)
        g.line(in.x, valueY(in, v), in.x + in.w, valueY(in, v),
               withAlpha(t.patternText, pan && v == 32 ? 0.3f : 0.08f), 1.f);
    if (e.count == 0) {
        tx.draw(g, r, "NO POINTS - CLICK TO ADD ONE", 16.f, t.rowNumber, Align::Center);
        return;
    }
    const bool on = (e.flags & ENV_ON) != 0;
    if (e.loopEnd < e.count) {  // loop region (dim when the loop is off)
        const bool lp = (e.flags & ENV_LOOP) != 0;
        const float x0 = tickX(in, e.pts[e.loopStart].tick), x1 = tickX(in, e.pts[e.loopEnd].tick);
        g.fillRect({x0, in.y, std::max(1.f, x1 - x0), in.h}, withAlpha(t.patternSample, lp ? 0.16f : 0.05f));
        for (float x : {x0, x1}) g.line(x, in.y, x, in.y + in.h, withAlpha(t.patternSample, lp ? 0.9f : 0.25f), 2.f);
        tx.draw(g, {x0 + 3, in.y, 30, 16}, "L", 12.f, withAlpha(t.patternSample, lp ? 1.f : 0.35f), Align::Left);
    }
    if (e.sustain < e.count) {
        const bool su = (e.flags & ENV_SUSTAIN) != 0;
        const float x = tickX(in, e.pts[e.sustain].tick);
        g.line(x, in.y, x, in.y + in.h, withAlpha(t.patternVol, su ? 0.95f : 0.25f), 2.f);
        tx.draw(g, {x + 3, in.y + 16, 30, 16}, "S", 12.f, withAlpha(t.patternVol, su ? 1.f : 0.35f), Align::Left);
    }
    float pts[2 * (kEnvMaxPoints + 1)];
    for (int i = 0; i < e.count; ++i) {
        pts[i * 2] = tickX(in, e.pts[i].tick);
        pts[i * 2 + 1] = valueY(in, e.pts[i].value);
    }
    const Color curve = withAlpha(t.scopeLine, on ? 1.f : 0.4f);
    g.polyline(pts, e.count, curve, 2.f);
    const float lastY = pts[(e.count - 1) * 2 + 1];
    g.line(pts[(e.count - 1) * 2], lastY, in.x + in.w, lastY, withAlpha(t.scopeLine, on ? 0.35f : 0.15f), 1.f);
    for (int i = 0; i < e.count; ++i) {
        const RectF p = {pts[i * 2] - 5, pts[i * 2 + 1] - 5, 10, 10};
        g.fillRect(p, i == sel ? t.cursor : withAlpha(t.digits, on ? 1.f : 0.5f));
        g.strokeRect(p, t.patternBg, 1.f);
    }
    if (!on) tx.draw(g, {in.x + in.w - 70, in.y, 70, 20}, "OFF", 14.f, t.rowNumber, Align::Right);
}
}  // namespace

// ---------------------------------------------------------------- geometry

RectF PlayScreen::instrRect(int id) const {
    const Geo gm = geo(bottomArea());
    if (id < I_TOP_COUNT) {
        if (id == I_NAME) return {gm.top.x + 258, gm.top.y, kTopW[I_NAME], gm.top.h};
        float x = gm.top.x + gm.top.w;  // the buttons sit at the right, EXIT last
        for (int i = I_EXIT; i >= id; --i) x -= kTopW[i] + (i == I_EXIT ? 0 : 6);
        return {x, gm.top.y, kTopW[id], gm.top.h};
    }
    if (id >= I_ENV && id < I_ENV + 2 * E_COUNT) {
        const int which = (id - I_ENV) / E_COUNT, k = (id - I_ENV) % E_COUNT;
        const RectF b = gm.env[which];
        const float cy = b.y + b.h - 38;
        switch (k) {
            case E_ON: return {b.x + 270, b.y, 70, 34};
            case E_SUS: return {b.x + 346, b.y, 110, 34};
            case E_LOOP: return {b.x + 462, b.y, 90, 34};
            case E_GRAPH: return {b.x, b.y + 40, b.w, b.h - 40 - 44};
            default: {
                const int grp = (k - E_SUS_DN) / 2, up = (k - E_SUS_DN) % 2;
                const float gx = b.x + grp * 240.f;
                return up ? RectF{gx + 196, cy, 34, 38} : RectF{gx + 104, cy, 34, 38};
            }
        }
    }
    if (id >= I_PARAM && id < I_PARAM + 3 * P_COUNT) {
        const int row = (id - I_PARAM) / 3, k = (id - I_PARAM) % 3;
        const RectF p = gm.params;
        const float rh = std::min(41.f, p.h / (float)P_COUNT), y = p.y + row * rh;
        if (k == 0) return {p.x + 214, y, 40, rh - 5};
        if (k == 1) return {p.x + 258, y, p.w - 258 - 44, rh - 5};
        return {p.x + p.w - 40, y, 40, rh - 5};
    }
    if (id == I_KEYS) return piano(gm.keys);
    return {};
}

int PlayScreen::instrHit(float x, float y) const {
    for (int id = 0; id < I_COUNT; ++id) {
        const RectF r = instrRect(id);
        if (r.w > 0 && r.contains(x, y)) return id;
    }
    return -1;
}

int PlayScreen::instrKeyAt(float x, float y) const {
    const RectF p = instrRect(I_KEYS);
    if (!p.contains(x, y)) return -1;
    for (int k = 0; k < 96; ++k)  // black keys lie on top
        if (kWhiteOf[k % 12] < 0 && keyRect(p, k).contains(x, y)) return k;
    for (int k = 0; k < 96; ++k)
        if (kWhiteOf[k % 12] >= 0 && keyRect(p, k).contains(x, y)) return k;
    return -1;
}

// ---------------------------------------------------------------- state helpers

void PlayScreen::syncMapSample(const Song& s) {
    if (mapInstr_ == sample_ && mapSample_ >= 0 && mapSample_ < (int)s.samples.size()) return;
    mapInstr_ = sample_;
    mapSample_ = curSampleIndex(s, 49);
    if (mapSample_ < 0 && sample_ < (int)s.instruments.size())
        for (uint16_t m : s.instruments[sample_].sampleMap)
            if (m < s.samples.size()) {
                mapSample_ = m;
                break;
            }
    if (mapSample_ < 0) mapSample_ = s.samples.empty() ? -1 : 0;
}

bool PlayScreen::instrEditable(Engine& e) {
    const Song* s = e.song();
    if (!s) return false;
    if (e.tfmx()) {
        setStatus("PLAYBACK ONLY");
        return false;
    }
    if (s->instruments.empty() || sample_ >= (int)s->instruments.size()) {
        setStatus("NO XM INSTRUMENTS: NEW INSTRUMENT OR CONVERT SONG");
        return false;
    }
    return true;
}

// f(Instrument&) -> true when it changed something. It runs on a copy; the result replaces the
// instrument under the engine lock (after an undo step when `record`), so a playing song hears it at once.
template <class F>
bool PlayScreen::instrApply(Engine& e, F&& f, bool record) {
    const Song* s = e.song();
    const int ins = sample_;
    if (!s || ins < 0 || ins >= (int)s->instruments.size()) return false;
    Instrument copy = s->instruments[ins];
    if (!f(copy)) return false;
    e.edit([&](Song& song) {
        if (ins >= (int)song.instruments.size()) return;
        if (record) editor_.recordInstruments(song);
        song.instruments[ins] = copy;
    });
    editor_.markDirty();
    return true;
}

// ---------------------------------------------------------------- actions

void PlayScreen::instrNew(Engine& e) {
    const Song* s = e.song();
    if (!s) return;
    if (e.tfmx()) return setStatus("PLAYBACK ONLY");
    if (s->instruments.empty() && !s->samples.empty()) {  // sample numbers would change meaning
        ask("NEW INSTRUMENTS NEED XM INSTRUMENTS: CONVERT THE SONG?", {"CONVERT", "CANCEL"}, [this, &e](int a) {
            if (a == 0) instrConvert(e);
        });
        return;
    }
    if ((int)s->instruments.size() >= kMaxInstruments) return setStatus("ERROR: 128 INSTRUMENTS MAXIMUM");
    const int smp = s->instruments.empty() ? -1 : curSampleIndex(*s);
    int idx = -1;
    e.edit([&](Song& song) {
        editor_.recordInstruments(song);
        idx = addInstrument(song, smp);
    });
    if (idx < 0) return setStatus("ERROR: NO INSTRUMENT CREATED");
    sample_ = idx;
    mapInstr_ = -1;
    char m[64];
    std::snprintf(m, sizeof m, smp < 0 ? "INSTRUMENT %02d CREATED - LOAD A SAMPLE" : "INSTRUMENT %02d CREATED",
                  idx + 1);
    setStatus(m);
}

void PlayScreen::instrConvert(Engine& e) {
    const Song* s = e.song();
    if (!s) return;
    if (e.tfmx()) return setStatus("PLAYBACK ONLY");
    if (!s->instruments.empty()) return setStatus("THE SONG HAS XM INSTRUMENTS ALREADY");
    if (s->samples.empty()) return setStatus("NO SAMPLES TO CONVERT");
    if (s->samples.size() > (size_t)kMaxInstruments) return setStatus("ERROR: 128 SAMPLES MAXIMUM");
    bool ok = false;
    e.edit([&](Song& song) {
        editor_.recordInstruments(song);
        ok = convertToInstruments(song);
    });
    mapInstr_ = -1;
    char m[64];
    std::snprintf(m, sizeof m, "CONVERTED: %d INSTRUMENTS, THE SONG PLAYS THE SAME", (int)e.song()->instruments.size());
    setStatus(ok ? m : "ERROR: NOT CONVERTED");
}

void PlayScreen::instrStep(int row, int dir, bool big, Engine& e) {
    const Song* s = e.song();
    if (!s) return;
    if (row == P_MAP) {  // the pen of the key map: no change to the song
        syncMapSample(*s);
        const int n = (int)s->samples.size();
        if (n == 0) return setStatus("NO SAMPLES - LOAD ONE IN THE SAMPLER");
        mapSample_ = ((mapSample_ + dir * (big ? 16 : 1)) % n + n) % n;
        return;
    }
    instrApply(e, [&](Instrument& in) {
        const Instrument old = in;
        switch (row) {
            case P_FADE: instSetFadeout(in, in.fadeout + dir * (big ? 256 : 16)); break;
            case P_VTYPE: in.vibType = (uint8_t)((in.vibType + dir + 4) % 4); break;
            case P_VSWEEP: instSetVibrato(in, in.vibType, in.vibSweep + dir * (big ? 16 : 1), in.vibDepth, in.vibRate); break;
            case P_VDEPTH: instSetVibrato(in, in.vibType, in.vibSweep, in.vibDepth + dir * (big ? 4 : 1), in.vibRate); break;
            case P_VRATE: instSetVibrato(in, in.vibType, in.vibSweep, in.vibDepth, in.vibRate + dir * (big ? 8 : 1)); break;
        }
        return old.fadeout != in.fadeout || old.vibType != in.vibType || old.vibSweep != in.vibSweep ||
               old.vibDepth != in.vibDepth || old.vibRate != in.vibRate;
    });
}

// ---------------------------------------------------------------- drawing

void PlayScreen::instrDraw(Gfx& g, Text& tx, const Theme& t, Engine& e) {
    const RectF a = bottomArea();
    const Geo gm = geo(a);
    Ui u{g, tx, t};
    g.panel(a, t, Gfx::PanelState::Normal);
    const Song* s = e.song();
    const bool has = s && sample_ < (int)s->instruments.size();
    const Instrument* in = has ? &s->instruments[sample_] : nullptr;
    if (s) syncMapSample(*s);
    char buf[160];
    const Color fieldBg = mixColor(t.panelLight, t.panel, 0.25f);

    // ---- top row: number, name, buttons
    const RectF num = {gm.top.x, gm.top.y, 250, gm.top.h};
    g.well(num, t, fieldBg);
    if (has) std::snprintf(buf, sizeof buf, "INSTRUMENT %02d / %02d", sample_ + 1, (int)s->instruments.size());
    else std::snprintf(buf, sizeof buf, "NO INSTRUMENTS");
    tx.draw(g, {num.x + 8, num.y, num.w - 8, num.h}, buf, 15.f, t.digits, Align::Left, FontRole::Mono);
    const RectF nameR = instrRect(I_NAME);
    g.well(nameR, t, nameEdit_ == 3 ? t.panelLight : fieldBg);
    if (nameEdit_ == 3) {
        drawLineEdit(g, tx, t, nameR, line_);
    } else {
        char name[23] = {};
        if (in) std::memcpy(name, in->name, 22);
        for (char& ch : name)
            if (ch && (ch < 32 || ch > 126)) ch = ' ';
        tx.draw(g, {nameR.x + 6, nameR.y, nameR.w - 6, nameR.h}, has ? (name[0] ? name : "(CLICK TO NAME)") : "", 16.f,
                name[0] ? t.digits : t.rowNumber, Align::Left, FontRole::Mono);
    }
    for (int i = I_NEW; i < I_TOP_COUNT; ++i)
        drawButton(u, instrRect(i), kTopLabels[i],
                   hover_ == kHitInstr + i ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 14.f);

    if (!has) {  // MOD style song (or a chip song): explain what to do
        const RectF w = {a.x + 8, gm.env[0].y, a.w - 16, a.y + a.h - 8 - gm.env[0].y};
        g.well(w, t, t.patternBg);
        const char* l1 = e.tfmx() ? "PLAYBACK ONLY: THIS SONG HAS NO EDITABLE INSTRUMENTS"
                                  : "THIS SONG PLAYS ITS SAMPLES DIRECTLY (MOD STYLE): IT HAS NO XM INSTRUMENTS";
        const char* l2 = e.tfmx() ? ""
                                  : "CONVERT SONG TO XM INSTRUMENTS GIVES EVERY SAMPLE AN INSTRUMENT - THE SONG PLAYS THE SAME";
        const char* l3 = e.tfmx() ? "" : "THEN EDIT ENVELOPES, FADEOUT, AUTOVIBRATO AND THE KEY MAP HERE (CTRL+S SAVES XM)";
        tx.draw(g, {w.x, w.y + w.h / 2 - 60, w.w, 36}, l1, 20.f, t.patternNote, Align::Center);
        tx.draw(g, {w.x, w.y + w.h / 2 - 16, w.w, 30}, l2, 16.f, t.patternText, Align::Center);
        tx.draw(g, {w.x, w.y + w.h / 2 + 18, w.w, 30}, l3, 16.f, t.patternText, Align::Center);
        return;
    }

    // ---- envelopes
    for (int which = 0; which < 2; ++which) {
        const RectF b = gm.env[which];
        const Envelope& env = envOf(*in, which);
        tx.draw(g, {b.x + 4, b.y, 260, 34}, which ? "PANNING ENVELOPE" : "VOLUME ENVELOPE", 17.f, t.label, Align::Left);
        const int base = kHitInstr + I_ENV + which * E_COUNT;
        const uint8_t flags[3] = {ENV_ON, ENV_SUSTAIN, ENV_LOOP};
        const char* names[3] = {"ON", "SUSTAIN", "LOOP"};
        for (int k = 0; k < 3; ++k) {
            Gfx::PanelState st = hover_ == base + k ? Gfx::PanelState::Hover : Gfx::PanelState::Normal;
            if (env.flags & flags[k]) st = Gfx::PanelState::Active;
            drawButton(u, instrRect(I_ENV + which * E_COUNT + k), names[k], st, 14.f);
        }
        const int sel = std::min(instPoint_[which], std::max(0, env.count - 1));
        if (env.count)
            std::snprintf(buf, sizeof buf, "POINT %02d/%02d  TICK %03d  VALUE %02d", sel + 1, env.count,
                          env.pts[sel].tick, env.pts[sel].value);
        else
            std::snprintf(buf, sizeof buf, "NO POINTS");
        tx.draw(g, {b.x + 560, b.y, b.w - 560, 34}, buf, 14.f, t.digits, Align::Right, FontRole::Mono);
        drawEnvelope(g, tx, t, instrRect(I_ENV + which * E_COUNT + E_GRAPH), env, which == 1, sel);
        const char* grp[3] = {"SUSTAIN", "LOOP START", "LOOP END"};
        const int vals[3] = {env.sustain, env.loopStart, env.loopEnd};
        for (int k = 0; k < 3; ++k) {
            const RectF dn = instrRect(I_ENV + which * E_COUNT + E_SUS_DN + k * 2);
            const RectF up = instrRect(I_ENV + which * E_COUNT + E_SUS_UP + k * 2);
            tx.draw(g, {dn.x - 104, dn.y, 100, dn.h}, grp[k], 14.f, t.label, Align::Left);
            const RectF v = {dn.x + dn.w + 4, dn.y + 2, up.x - dn.x - dn.w - 8, dn.h - 4};
            g.well(v, t, fieldBg);
            std::snprintf(buf, sizeof buf, "%02d", vals[k] + 1);
            tx.draw(g, v, env.count ? buf : "--", 15.f, t.digits, Align::Center, FontRole::Mono);
            drawButton(u, dn, "\x03", hover_ == base + E_SUS_DN + k * 2 ? Gfx::PanelState::Hover : Gfx::PanelState::Normal,
                       12.f);
            drawButton(u, up, "\x04", hover_ == base + E_SUS_UP + k * 2 ? Gfx::PanelState::Hover : Gfx::PanelState::Normal,
                       12.f);
        }
        if (which == 0) {
            const RectF gr = instrRect(I_ENV + (int)E_GRAPH);
            tx.draw(g, {b.x + 700, gr.y + gr.h + 4, b.w - 700, 38}, "CLICK: ADD / DRAG  RIGHT: DELETE", 12.f,
                    t.rowNumber, Align::Right);
        }
    }

    // ---- key map
    const RectF k = gm.keys;
    char pen[40];
    if (mapSample_ >= 0) std::snprintf(pen, sizeof pen, "SAMPLE %02X", mapSample_ + 1);
    else std::snprintf(pen, sizeof pen, "(NO SAMPLES)");
    std::snprintf(buf, sizeof buf, "KEY MAP  -  CLICK / DRAG: KEYS PLAY %s   RIGHT: NO SAMPLE   (%d OF 16 SAMPLES USED)",
                  pen, instSampleCount(*in));
    tx.draw(g, {k.x + 4, k.y, k.w - 4, 26}, buf, 14.f, t.label, Align::Left);
    const RectF p = instrRect(I_KEYS);
    g.well({k.x, k.y + 28, k.w, 38}, t, t.patternBg);
    for (int pass = 0; pass < 2; ++pass)  // white keys, then the black ones on top
        for (int key = 0; key < 96; ++key) {
            const bool black = kWhiteOf[key % 12] < 0;
            if (black != (pass == 1)) continue;
            const RectF kr = keyRect(p, key);
            const uint16_t m = in->sampleMap[key];
            const bool mapped = m < s->samples.size();
            Color base = black ? Color{0.07f, 0.08f, 0.1f, 1.f} : Color{0.86f, 0.87f, 0.9f, 1.f};
            if (mapped) base = mixColor(base, sampleColor(t, m), black ? 0.55f : 0.45f);
            else base = mixColor(base, t.patternBg, 0.6f);
            g.fillRect({kr.x + 0.5f, kr.y, kr.w - 1, kr.h}, base);
            if (mapped && (int)m == mapSample_)
                g.fillRect({kr.x + 2, kr.y + kr.h - 6, kr.w - 4, 4}, t.cursor);
            g.strokeRect(kr, withAlpha(t.panelDark, 0.9f), 1.f);
            // the sample each key plays, above it: black keys in the upper row
            const RectF nr = {kr.x - (black ? 3.f : 0.f), k.y + 30 + (black ? 0.f : 17.f), kr.w + (black ? 6.f : 0.f), 17};
            if (mapped) std::snprintf(buf, sizeof buf, "%02X", (m + 1) & 0xFF);
            else std::snprintf(buf, sizeof buf, "--");
            tx.draw(g, nr, buf, 10.f, mapped ? sampleColor(t, m) : withAlpha(t.rowNumber, 0.5f), Align::Center,
                    FontRole::Mono);
            if (key % 12 == 0) {
                std::snprintf(buf, sizeof buf, "C%d", key / 12);
                tx.draw(g, {kr.x, kr.y + kr.h - 26, kr.w, 18}, buf, 10.f, {0.15f, 0.16f, 0.2f, 1.f}, Align::Center,
                        FontRole::Mono);
            }
        }

    // ---- fadeout, autovibrato, pen
    for (int row = 0; row < P_COUNT; ++row) {
        const RectF dn = instrRect(I_PARAM + row * 3), v = instrRect(I_PARAM + row * 3 + 1),
                    up = instrRect(I_PARAM + row * 3 + 2);
        tx.draw(g, {gm.params.x + 4, dn.y, 206, dn.h}, kParamLabels[row], 15.f, t.label, Align::Left);
        switch (row) {
            case P_FADE: std::snprintf(buf, sizeof buf, "%03X  (%d)", in->fadeout, in->fadeout); break;
            case P_VTYPE: std::snprintf(buf, sizeof buf, "%s", kVibTypes[in->vibType & 3]); break;
            case P_VSWEEP: std::snprintf(buf, sizeof buf, "%02X", in->vibSweep); break;
            case P_VDEPTH: std::snprintf(buf, sizeof buf, "%02X", in->vibDepth); break;
            case P_VRATE: std::snprintf(buf, sizeof buf, "%02X", in->vibRate); break;
            case P_MAP: {
                char nm[23] = {};
                if (mapSample_ >= 0) std::memcpy(nm, s->samples[mapSample_].name, 22);
                for (char& ch : nm)
                    if (ch && (ch < 32 || ch > 126)) ch = ' ';
                if (mapSample_ >= 0) std::snprintf(buf, sizeof buf, "%02X %s", mapSample_ + 1, nm);
                else std::snprintf(buf, sizeof buf, "--");
                break;
            }
        }
        g.well(v, t, fieldBg);
        tx.draw(g, {v.x + 8, v.y, v.w - 12, v.h}, buf, 15.f, t.digits,
                row == P_MAP ? Align::Left : Align::Center, FontRole::Mono);
        drawButton(u, dn, "\x03", hover_ == kHitInstr + I_PARAM + row * 3 ? Gfx::PanelState::Hover : Gfx::PanelState::Normal,
                   12.f);
        drawButton(u, up, "\x04",
                   hover_ == kHitInstr + I_PARAM + row * 3 + 2 ? Gfx::PanelState::Hover : Gfx::PanelState::Normal, 12.f);
    }
}

// ---------------------------------------------------------------- input

UiAction PlayScreen::instrMouse(float x, float y, bool right, Engine& e) {
    const int id = instrHit(x, y);
    if (id < 0) return UiAction::None;
    switch (id) {
        case I_NAME:
            if (!right && instrEditable(e)) startNameEdit(3, e);
            return UiAction::Redraw;
        case I_NEW: instrNew(e); return UiAction::Redraw;
        case I_CONVERT: instrConvert(e); return UiAction::Redraw;
        case I_UNDO:
            e.edit([&](Song& song) { editor_.undo(song); });
            setStatus(editor_.canUndo() ? "UNDO" : "NOTHING MORE TO UNDO");
            return UiAction::Redraw;
        case I_SAMPLER: openPanel(Panel::Sampler); return UiAction::Redraw;
        case I_EXIT: panel_ = Panel::Main; return UiAction::Redraw;
        default: break;
    }
    if (!instrEditable(e)) return UiAction::Redraw;
    const Song& s = *e.song();
    if (id >= I_ENV && id < I_ENV + 2 * E_COUNT) {
        const int which = (id - I_ENV) / E_COUNT, k = (id - I_ENV) % E_COUNT;
        const bool pan = which == 1;
        instEnv_ = which;
        if (k <= E_LOOP) {
            const uint8_t flag = k == E_ON ? ENV_ON : k == E_SUS ? ENV_SUSTAIN : ENV_LOOP;
            instrApply(e, [&](Instrument& in) {
                Envelope& env = envOf(in, which);
                envSetFlag(env, flag, !(env.flags & flag), pan);
                return true;
            });
            return UiAction::Redraw;
        }
        if (k == E_GRAPH) {
            const RectF gin = graphInner(instrRect(id));
            const Envelope& env = envOf(s.instruments[sample_], which);
            const int hit = nearPoint(env, gin, x, y);
            if (right) {
                if (hit < 0) return UiAction::Redraw;
                const bool done = instrApply(e, [&](Instrument& in) { return envDeletePoint(envOf(in, which), hit); });
                if (!done) setStatus("AN ENVELOPE KEEPS TWO POINTS AT LEAST");
                else instPoint_[which] = std::max(0, hit - 1);
                return UiAction::Redraw;
            }
            instDrag_ = 1 + which;
            instDragRecorded_ = false;
            if (hit >= 0) {
                instPoint_[which] = hit;
                return UiAction::Redraw;
            }
            const int tick = (int)std::lround((x - gin.x) / gin.w * kEnvMaxTick);
            const int val = (int)std::lround((gin.y + gin.h - y) / gin.h * kEnvMaxValue);
            int added = -1;
            instrApply(e, [&](Instrument& in) {
                added = envAddPoint(envOf(in, which), tick, val);
                return added >= 0;
            });
            if (added < 0) {
                instDrag_ = 0;
                setStatus(env.count >= kEnvMaxPoints ? "12 POINTS MAXIMUM" : "A POINT IS THERE ALREADY");
            } else {
                instPoint_[which] = added;
                instDragRecorded_ = true;  // the add is the undo step of this drag
            }
            return UiAction::Redraw;
        }
        // sustain / loop index arrows
        const int grp = (k - E_SUS_DN) / 2, dir = (k - E_SUS_DN) % 2 ? 1 : -1;
        instrApply(e, [&](Instrument& in) {
            Envelope& env = envOf(in, which);
            const Envelope old = env;
            if (grp == 0) envSetSustain(env, env.sustain + dir);
            else if (grp == 1) envSetLoopStart(env, env.loopStart + dir);
            else envSetLoopEnd(env, env.loopEnd + dir);
            return old.sustain != env.sustain || old.loopStart != env.loopStart || old.loopEnd != env.loopEnd;
        });
        return UiAction::Redraw;
    }
    if (id >= I_PARAM && id < I_PARAM + 3 * P_COUNT) {
        const int row = (id - I_PARAM) / 3, k = (id - I_PARAM) % 3;
        if (k == 1 && row == P_VTYPE) instrStep(row, 1, false, e);  // click the type: next one
        else if (k != 1) instrStep(row, k == 0 ? -1 : 1, right, e);
        return UiAction::Redraw;
    }
    if (id == I_KEYS) {
        const int key = instrKeyAt(x, y);
        if (key < 0) return UiAction::Redraw;
        syncMapSample(s);
        if (!right && mapSample_ < 0) {
            setStatus("NO SAMPLES - LOAD ONE IN THE SAMPLER");
            return UiAction::Redraw;
        }
        const uint16_t smp = right ? kKeyNoSample : (uint16_t)mapSample_;
        const bool ok = instrApply(e, [&](Instrument& in) {
            if (in.sampleMap[key] == smp) return false;
            return instSetKeys(in, key, key, smp);
        });
        if (!ok && !right && s.instruments[sample_].sampleMap[key] != smp)
            setStatus("AN XM INSTRUMENT HOLDS 16 SAMPLES MAXIMUM");
        instDrag_ = right ? 4 : 3;
        instDragRecorded_ = ok;
        instLastKey_ = key;
        if (!right) e.previewNote(mapSample_, key + 1, 0);
        return UiAction::Redraw;
    }
    return UiAction::Redraw;
}

bool PlayScreen::instrDragMove(float x, float y, Engine& e) {
    const Song* s = e.song();
    if (!s || sample_ >= (int)s->instruments.size()) {
        instDrag_ = 0;
        return false;
    }
    bool changed = false;
    if (instDrag_ == 1 || instDrag_ == 2) {
        const int which = instDrag_ - 1, pt = instPoint_[which];
        const RectF gin = graphInner(instrRect(I_ENV + which * E_COUNT + E_GRAPH));
        const int tick = (int)std::lround((x - gin.x) / gin.w * kEnvMaxTick);
        const int val = (int)std::lround((gin.y + gin.h - y) / gin.h * kEnvMaxValue);
        changed = instrApply(e, [&](Instrument& in) {
            Envelope& env = envOf(in, which);
            if (pt >= env.count) return false;
            const EnvPoint old = env.pts[pt];
            envMovePoint(env, pt, tick, val);
            return old.tick != env.pts[pt].tick || old.value != env.pts[pt].value;
        }, !instDragRecorded_);
        if (changed) instDragRecorded_ = true;
    } else if (instDrag_ == 3 || instDrag_ == 4) {
        // the key under the mouse (above or below the piano: the column's white key)
        const RectF p = instrRect(I_KEYS);
        const int key = instrKeyAt(std::clamp(x, p.x, p.x + p.w - 1), std::clamp(y, p.y + p.h * 0.7f, p.y + p.h - 1));
        if (key < 0 || key == instLastKey_) return false;
        const uint16_t smp = instDrag_ == 4 ? kKeyNoSample : (uint16_t)mapSample_;
        const int from = instLastKey_ < 0 ? key : instLastKey_;
        changed = instrApply(e, [&](Instrument& in) {
            const Instrument old = in;
            if (!instSetKeys(in, from, key, smp)) return false;
            return std::memcmp(old.sampleMap, in.sampleMap, sizeof in.sampleMap) != 0;
        }, !instDragRecorded_);
        if (changed) instDragRecorded_ = true;
        instLastKey_ = key;
        return true;
    }
    return changed;
}

UiAction PlayScreen::instrKey(unsigned vk, bool ctrl, Engine& e) {
    if (vk == VK_ESCAPE) {
        panel_ = Panel::Main;
        return UiAction::Redraw;
    }
    if (ctrl && (vk == 'Z' || vk == 'Y')) {
        e.edit([&](Song& song) { vk == 'Z' ? editor_.undo(song) : editor_.redo(song); });
        setStatus(vk == 'Z' ? (editor_.canUndo() ? "UNDO" : "NOTHING MORE TO UNDO") : "REDO");
        return UiAction::Redraw;
    }
    if (vk == VK_DELETE) {
        if (!instrEditable(e)) return UiAction::Redraw;
        const int which = instEnv_, pt = instPoint_[which];
        if (!instrApply(e, [&](Instrument& in) { return envDeletePoint(envOf(in, which), pt); }))
            setStatus("AN ENVELOPE KEEPS TWO POINTS AT LEAST");
        else
            instPoint_[which] = std::max(0, pt - 1);
        return UiAction::Redraw;
    }
    if (ctrl) return UiAction::None;
    const int note = noteForKey(vk, octave_);
    if (note > 0 && e.song()) {  // jam: the sample the current instrument plays on that key
        const int idx = curSampleIndex(*e.song(), note);
        if (idx >= 0) e.previewNote(idx, note, 0);
        jamKey_ = vk;
        return UiAction::Redraw;
    }
    return UiAction::None;
}
