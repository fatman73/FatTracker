// XM instrument editing (pure C++, no Windows): envelopes with FastTracker 2's rules, the key map,
// fadeout and autovibrato, new instruments and the conversion of a sample-only (MOD style) song.
// The UI runs these inside Engine::edit after Editor::recordInstruments (undo).
#pragma once
#include <cstdint>

#include "core/song.h"

constexpr uint16_t kKeyNoSample = 0xFFFF;  // sampleMap entry of a key that plays nothing
constexpr int kEnvMaxPoints = 12;
constexpr int kEnvMaxTick = 324;   // FT2's envelope editor ends there
constexpr int kEnvMaxValue = 64;
constexpr int kMaxInstruments = 128;
constexpr int kMaxSamplesPerInstrument = 16;  // XM: one instrument holds up to 16 samples
constexpr int kMaxFadeout = 4095;

// ---- envelopes. Rules (FT2): 0..12 points, the first at tick 0, ticks strictly increasing up to
// kEnvMaxTick, values 0..64; sustain / loop indices always name existing points, loop start <= loop end.
void envSetDefault(Envelope& e, bool pan);  // FT2's standard envelope, switched off
int envAddPoint(Envelope& e, int tick, int value);  // index of the new point, -1 when full or the tick is taken
bool envDeletePoint(Envelope& e, int index);        // FT2 keeps at least two points
void envMovePoint(Envelope& e, int index, int tick, int value);  // clamped between the neighbours
void envSetSustain(Envelope& e, int index);
void envSetLoopStart(Envelope& e, int index);
void envSetLoopEnd(Envelope& e, int index);
// ENV_ON / ENV_SUSTAIN / ENV_LOOP. Switching on an envelope without points gives it the standard shape.
void envSetFlag(Envelope& e, uint8_t flag, bool on, bool pan);
void envFix(Envelope& e);  // makes any envelope follow the rules (clamps, sorts out indices)
bool envValid(const Envelope& e);

// ---- instruments
// The keys key0..key1 (0..95 = C-0..B-7, any order) play global sample `sample` (kKeyNoSample: nothing).
// False (nothing changes) when the instrument would need more than 16 different samples.
bool instSetKeys(Instrument& in, int key0, int key1, uint16_t sample);
int instSampleCount(const Instrument& in);  // different samples the key map uses
void instSetFadeout(Instrument& in, int fadeout);  // 0..4095
// FT2 ranges: type 0..3 (sine, square, ramp up, ramp down), sweep 0..255, depth 0..15, rate 0..63
void instSetVibrato(Instrument& in, int type, int sweep, int depth, int rate);
void instSetName(Instrument& in, const char* name);
// An instrument playing global sample `sample` on every key (-1 / out of range: no sample), named after
// it, with the standard envelopes switched off: it sounds exactly like the bare sample.
Instrument makeInstrument(const Song& s, int sample);

// ---- songs
// MOD style songs (no instruments: the sample column numbers samples) get one instrument per sample,
// mapped to every key. Playback stays the same. False when the song has instruments already, has no
// samples or more than 128.
bool convertToInstruments(Song& s);
// Appends an instrument for global sample `sample` (-1: none). Returns its index, or -1 when the song is
// MOD style with samples (convert it first, or every sample number would change meaning) or full.
int addInstrument(Song& s, int sample);
