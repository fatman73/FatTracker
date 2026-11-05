// Pattern sequencer and effect interpreter (ProTracker 2.3 / FastTracker 2 quirks).
#pragma once
#include <cstdint>

#include "core/mixer.h"
#include "core/song.h"

struct ChannelState {
    int16_t sample = -1;      // index into Song::samples, -1 none
    int16_t instrument = -1;  // index into Song::instruments, -1 none
    uint8_t note = 0;         // last note 1..96
    int32_t period = 0;       // base period (after slides)
    int32_t outPeriod = 0;    // sent to mixer (after arpeggio / vibrato)
    uint8_t volume = 0;       // 0..64 base
    uint8_t outVolume = 0;    // 0..64 after tremolo / envelope / fadeout / global
    uint8_t pan = 128;        // 0..255
    bool muted = false;

    // ---- internal effect state ----
    Cell cell;
    int8_t finetune = 0;
    int8_t relNote = 0;
    int32_t portaTarget = 0;
    uint8_t portaSpeed = 0;
    uint8_t mem1 = 0, mem2 = 0, memA = 0, mem9 = 0, memE1 = 0, memE2 = 0, memEA = 0, memEB = 0;
    uint8_t memX1 = 0, memX2 = 0, memH = 0, memP = 0, memR = 0, memT = 0;
    uint8_t vibSpeed = 0, vibDepth = 0, vibPos = 0, vibWave = 0;
    uint8_t tremSpeed = 0, tremDepth = 0, tremPos = 0, tremWave = 0;
    uint8_t loopRow = 0, loopCount = 0;
    uint8_t retrigCount = 0;
    uint8_t tremorPos = 0;
    bool tremorMute = false;
    bool glissando = false;
    bool delayPending = false;
    uint8_t delayTick = 0;
    int32_t periodDelta = 0;     // vibrato (output only)
    int32_t periodOverride = 0;  // arpeggio (output only), 0 = none
    int16_t volDelta = 0;        // tremolo (output only)
    // instrument (FT2)
    bool keyOn = true;
    int32_t fadeVol = 32768;
    uint16_t volEnvTick = 0, panEnvTick = 0;
    uint8_t envVol = 64, envPan = 32;
    uint16_t avSweep = 0;
    uint8_t avPos = 0;
    int32_t avDelta = 0;
    float outVol01 = 0.f;
    float outPan01 = 0.5f;
};

struct PlayerPos {
    uint16_t order, pattern, row;
    uint8_t tick, speed, bpm;
};

class Player {
public:
    explicit Player(Mixer& m);
    void setSong(const Song* s);
    void play(uint16_t order = 0, uint16_t row = 0);
    void playPattern(uint16_t pattern, uint16_t row = 0);
    void stop();
    bool playing() const { return playing_; }
    void render(float* outStereo, uint32_t frames);
    void tick();
    PlayerPos pos() const;
    bool loopedOnce() const { return looped_; }
    uint32_t samplesPerTick() const;
    double periodToHz(int32_t period) const;
    void setMute(int ch, bool m);
    const ChannelState& channel(int ch) const { return ch_[ch < 0 ? 0 : (ch >= kMaxChannels ? kMaxChannels - 1 : ch)]; }
    const Song* song() const { return song_; }

private:
    // player.cpp
    void start(uint16_t order, uint16_t row, bool patternMode, uint16_t pattern);
    void processRow();
    void noteStart(int c);
    void keyOff(int c);
    int32_t notePeriod(int c, int note) const;
    void updateOutputs(int c);
    void endOfRow();
    void enterOrder(int o);
    uint16_t patternRows() const;
    bool isFT2() const { return song_ && song_->quirks == Quirks::FT2; }
    // player_fx.cpp
    void rowEffects(int c);   // tick 0
    void tickEffects(int c);  // ticks > 0 (and tick 0 of delayed rows)
    void volumeColumn(int c, bool tick0);
    void doVolSlide(int c, uint8_t param);
    void doPortaUp(int c, int amount);
    void doPortaDown(int c, int amount);
    void doTonePorta(int c);
    void doVibrato(int c);
    void doTremolo(int c);
    // player_inst.cpp
    void instrumentTick(int c);

    Mixer& mixer_;
    const Song* song_ = nullptr;
    ChannelState ch_[kMaxChannels];
    bool playing_ = false;
    bool patternMode_ = false;
    uint16_t order_ = 0, pattern_ = 0, row_ = 0;
    uint8_t tick_ = 0, speed_ = 6, bpm_ = 125, globalVol_ = 64;
    uint8_t patDelay_ = 0;
    bool repeatingRow_ = false;
    bool posJump_ = false, break_ = false, loopJump_ = false;
    uint16_t jumpOrder_ = 0, breakRow_ = 0, loopTarget_ = 0;
    uint32_t visited_[8] = {};
    bool looped_ = false;
    uint32_t samplesLeft_ = 0;
    double tickFrac_ = 0;
};
