#include "core/instrument_edit.h"

#include <algorithm>
#include <cstring>

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// FastTracker 2's standard envelopes (what a new instrument gets)
constexpr EnvPoint kStdVol[6] = {{0, 48}, {4, 64}, {8, 44}, {14, 8}, {24, 22}, {32, 8}};
constexpr EnvPoint kStdPan[6] = {{0, 32}, {20, 40}, {40, 24}, {60, 32}, {80, 32}, {100, 32}};

void fixIndices(Envelope& e) {
    const int last = e.count > 0 ? e.count - 1 : 0;
    e.sustain = (uint8_t)clampi(e.sustain, 0, last);
    e.loopEnd = (uint8_t)clampi(e.loopEnd, 0, last);
    e.loopStart = (uint8_t)clampi(e.loopStart, 0, e.loopEnd);
    if (e.count == 0) e.flags = 0;
}
}  // namespace

// ---------------------------------------------------------------- envelopes

void envSetDefault(Envelope& e, bool pan) {
    e = Envelope{};
    const EnvPoint* src = pan ? kStdPan : kStdVol;
    const int n = pan ? 5 : 6;
    for (int i = 0; i < n; ++i) e.pts[i] = src[i];
    e.count = (uint8_t)n;
    e.sustain = 2;
    e.loopStart = pan ? 1 : 3;
    e.loopEnd = pan ? 3 : 5;
    e.flags = 0;
}

int envAddPoint(Envelope& e, int tick, int value) {
    if (e.count > kEnvMaxPoints) envFix(e);
    if (e.count >= kEnvMaxPoints) return -1;
    value = clampi(value, 0, kEnvMaxValue);
    if (e.count == 0) {  // the first point always starts the envelope
        e.pts[0] = {0, (uint8_t)value};
        e.count = 1;
        fixIndices(e);
        return 0;
    }
    tick = clampi(tick, 0, kEnvMaxTick);
    int at = 0;
    while (at < e.count && e.pts[at].tick < tick) ++at;
    if (at < e.count && e.pts[at].tick == tick) return -1;  // taken (tick 0 always is)
    for (int i = e.count; i > at; --i) e.pts[i] = e.pts[i - 1];
    e.pts[at] = {(uint16_t)tick, (uint8_t)value};
    ++e.count;
    // sustain and loop stay on the points they marked
    if (e.sustain >= at) ++e.sustain;
    if (e.loopStart >= at) ++e.loopStart;
    if (e.loopEnd >= at) ++e.loopEnd;
    fixIndices(e);
    return at;
}

bool envDeletePoint(Envelope& e, int index) {
    if (e.count <= 2 || index < 0 || index >= e.count) return false;
    for (int i = index; i + 1 < e.count; ++i) e.pts[i] = e.pts[i + 1];
    --e.count;
    e.pts[e.count] = {};
    if (index == 0) e.pts[0].tick = 0;
    if (e.sustain > index) --e.sustain;
    if (e.loopStart > index) --e.loopStart;
    if (e.loopEnd > index) --e.loopEnd;
    fixIndices(e);
    return true;
}

void envMovePoint(Envelope& e, int index, int tick, int value) {
    if (index < 0 || index >= e.count) return;
    int lo = 0, hi = 0;
    if (index > 0) {
        lo = e.pts[index - 1].tick + 1;
        hi = index + 1 < e.count ? e.pts[index + 1].tick - 1 : kEnvMaxTick;
    }
    e.pts[index].tick = (uint16_t)clampi(tick, lo, std::max(lo, hi));
    e.pts[index].value = (uint8_t)clampi(value, 0, kEnvMaxValue);
}

void envSetSustain(Envelope& e, int index) {
    e.sustain = (uint8_t)clampi(index, 0, e.count > 0 ? e.count - 1 : 0);
}

void envSetLoopStart(Envelope& e, int index) { e.loopStart = (uint8_t)clampi(index, 0, e.loopEnd); }

void envSetLoopEnd(Envelope& e, int index) {
    e.loopEnd = (uint8_t)clampi(index, e.loopStart, e.count > 0 ? e.count - 1 : 0);
    if (e.loopStart > e.loopEnd) e.loopStart = e.loopEnd;
}

void envSetFlag(Envelope& e, uint8_t flag, bool on, bool pan) {
    if (on && e.count < 2) {  // the player needs points: start from the standard shape
        const uint8_t keep = e.flags;
        envSetDefault(e, pan);
        e.flags = keep;
    }
    e.flags = (uint8_t)(on ? (e.flags | flag) : (e.flags & ~flag));
    fixIndices(e);
}

void envFix(Envelope& e) {
    if (e.count > kEnvMaxPoints) e.count = kEnvMaxPoints;
    if (e.count > 0) {
        e.pts[0].tick = 0;
        for (int i = 1; i < e.count; ++i)  // strictly increasing
            e.pts[i].tick = (uint16_t)std::max<int>(e.pts[i].tick, e.pts[i - 1].tick + 1);
        // then squeezed into the editor range from the end, keeping the order
        for (int i = e.count - 1; i > 0; --i) {
            const int hi = i + 1 < e.count ? e.pts[i + 1].tick - 1 : kEnvMaxTick;
            if (e.pts[i].tick > hi) e.pts[i].tick = (uint16_t)hi;
        }
    }
    for (int i = 0; i < e.count; ++i) e.pts[i].value = (uint8_t)std::min<int>(e.pts[i].value, kEnvMaxValue);
    e.flags &= ENV_ON | ENV_SUSTAIN | ENV_LOOP;
    fixIndices(e);
}

bool envValid(const Envelope& e) {
    if (e.count > kEnvMaxPoints) return false;
    if (e.count == 0) return e.flags == 0;
    if (e.pts[0].tick != 0) return false;
    for (int i = 0; i < e.count; ++i) {
        if (e.pts[i].value > kEnvMaxValue || e.pts[i].tick > kEnvMaxTick) return false;
        if (i && e.pts[i].tick <= e.pts[i - 1].tick) return false;
    }
    return e.sustain < e.count && e.loopEnd < e.count && e.loopStart <= e.loopEnd &&
           (e.flags & ~(ENV_ON | ENV_SUSTAIN | ENV_LOOP)) == 0;
}

// ---------------------------------------------------------------- instruments

int instSampleCount(const Instrument& in) {
    uint16_t seen[96];
    int n = 0;
    for (uint16_t m : in.sampleMap) {
        if (m == kKeyNoSample) continue;
        bool found = false;
        for (int i = 0; i < n && !found; ++i) found = seen[i] == m;
        if (!found) seen[n++] = m;
    }
    return n;
}

bool instSetKeys(Instrument& in, int key0, int key1, uint16_t sample) {
    if (key0 > key1) std::swap(key0, key1);
    key0 = clampi(key0, 0, 95);
    key1 = clampi(key1, 0, 95);
    Instrument t = in;
    for (int k = key0; k <= key1; ++k) t.sampleMap[k] = sample;
    if (instSampleCount(t) > kMaxSamplesPerInstrument) return false;
    std::memcpy(in.sampleMap, t.sampleMap, sizeof in.sampleMap);
    return true;
}

void instSetFadeout(Instrument& in, int fadeout) { in.fadeout = (uint16_t)clampi(fadeout, 0, kMaxFadeout); }

void instSetVibrato(Instrument& in, int type, int sweep, int depth, int rate) {
    in.vibType = (uint8_t)clampi(type, 0, 3);
    in.vibSweep = (uint8_t)clampi(sweep, 0, 255);
    in.vibDepth = (uint8_t)clampi(depth, 0, 15);
    in.vibRate = (uint8_t)clampi(rate, 0, 63);
}

void instSetName(Instrument& in, const char* name) {
    std::memset(in.name, 0, sizeof in.name);
    for (size_t i = 0; name && name[i] && i + 1 < sizeof in.name; ++i) in.name[i] = name[i];
}

Instrument makeInstrument(const Song& s, int sample) {
    Instrument in;
    const bool ok = sample >= 0 && sample < (int)s.samples.size();
    for (auto& m : in.sampleMap) m = ok ? (uint16_t)sample : kKeyNoSample;
    if (ok) std::memcpy(in.name, s.samples[sample].name, sizeof in.name - 1);
    envSetDefault(in.volEnv, false);
    envSetDefault(in.panEnv, true);
    return in;
}

// ---------------------------------------------------------------- songs

bool convertToInstruments(Song& s) {
    if (!s.instruments.empty() || s.samples.empty() || s.samples.size() > (size_t)kMaxInstruments) return false;
    s.instruments.reserve(s.samples.size());
    for (int i = 0; i < (int)s.samples.size(); ++i) s.instruments.push_back(makeInstrument(s, i));
    return true;
}

int addInstrument(Song& s, int sample) {
    if (s.instruments.empty() && !s.samples.empty()) return -1;  // MOD style: convert first
    if ((int)s.instruments.size() >= kMaxInstruments) return -1;
    s.instruments.push_back(makeInstrument(s, sample));
    return (int)s.instruments.size() - 1;
}
