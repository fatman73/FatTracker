#include "core/player.h"

#include <cmath>
#include <cstring>

#include "core/tables.h"

Player::Player(Mixer& m) : mixer_(m) {}

void Player::setSong(const Song* s) {
    playing_ = false;
    mixer_.reset();  // voices may point at samples of the song being replaced
    song_ = s;
    if (s) mixer_.setNumChannels(s->numChannels);
}

void Player::start(uint16_t order, uint16_t row, bool patternMode, uint16_t pattern) {
    if (!song_ || song_->order.empty() || song_->patterns.empty()) return;
    for (int c = 0; c < kMaxChannels; ++c) {
        bool m = ch_[c].muted;
        ch_[c] = ChannelState{};
        ch_[c].muted = m;
        ch_[c].pan = song_->channelPan[c];
        mixer_.stopVoice(c);
    }
    speed_ = song_->initialSpeed ? song_->initialSpeed : 6;
    bpm_ = song_->initialBpm >= 32 ? song_->initialBpm : 125;
    globalVol_ = song_->globalVolume > 64 ? 64 : song_->globalVolume;
    std::memset(visited_, 0, sizeof visited_);
    looped_ = false;
    patternMode_ = patternMode;
    if (patternMode) {
        order_ = 0;
        pattern_ = pattern < song_->patterns.size() ? pattern : 0;
    } else {
        order_ = order < song_->order.size() ? order : 0;
        visited_[order_ >> 5] |= 1u << (order_ & 31);
        pattern_ = song_->order[order_] < song_->patterns.size() ? song_->order[order_] : 0;
    }
    row_ = row < patternRows() ? row : 0;
    tick_ = 0;
    patDelay_ = 0;
    repeatingRow_ = false;
    posJump_ = break_ = loopJump_ = false;
    samplesLeft_ = 0;
    tickFrac_ = 0;
    playing_ = true;
}

void Player::play(uint16_t order, uint16_t row) { start(order, row, false, 0); }

void Player::playPattern(uint16_t pattern, uint16_t row) { start(0, row, true, pattern); }

void Player::stop() {
    playing_ = false;
    for (int c = 0; c < Mixer::kMaxVoices; ++c) mixer_.stopVoice(c);
}

uint32_t Player::samplesPerTick() const { return mixer_.rate() * 5 / (bpm_ * 2u); }

PlayerPos Player::pos() const { return {order_, pattern_, row_, tick_, speed_, bpm_}; }

void Player::setMute(int c, bool m) {
    if (c < 0 || c >= kMaxChannels) return;
    ch_[c].muted = m;
    mixer_.setVolume(c, m ? 0.f : ch_[c].outVol01, ch_[c].outPan01);
}

uint16_t Player::patternRows() const {
    if (!song_ || pattern_ >= song_->patterns.size()) return 64;
    return song_->patterns[pattern_].rows;
}

double Player::periodToHz(int32_t period) const {
    if (period < 1) period = 1;
    if (!song_ || song_->quirks == Quirks::PT23) return ptPeriodToHz((uint32_t)period);
    return song_->freqMode == FreqMode::Linear ? xmLinearHz((uint32_t)period) : xmAmigaHz((uint32_t)period);
}

int32_t Player::notePeriod(int c, int note) const {
    const ChannelState& ch = ch_[c];
    if (!isFT2()) {
        int ft = ptFinetuneIndex(ch.finetune);
        int idx = xmNoteToPt(note);
        if (idx >= 0) return kPtPeriods[ft][idx];
        return (int32_t)std::lround(kPtPeriods[ft][12] * std::pow(2.0, (49 - note) / 12.0));
    }
    int n0 = note - 1 + ch.relNote;
    if (n0 < 0) n0 = 0;
    if (n0 > 118) n0 = 118;
    return song_->freqMode == FreqMode::Linear ? (int32_t)xmLinearPeriod(n0, ch.finetune)
                                               : (int32_t)xmAmigaPeriod(n0, ch.finetune);
}

void Player::keyOff(int c) {
    ChannelState& ch = ch_[c];
    ch.keyOn = false;
    const Instrument* in = (ch.instrument >= 0 && song_ && ch.instrument < (int)song_->instruments.size())
                               ? &song_->instruments[ch.instrument]
                               : nullptr;
    if (!in || !(in->volEnv.flags & ENV_ON)) ch.volume = 0;
}

void Player::noteStart(int c) {
    ChannelState& ch = ch_[c];
    const Cell& cell = ch.cell;
    const Song& s = *song_;
    const bool ft2 = isFT2();
    const bool porta = cell.fx == 3 || cell.fx == 5 || (ft2 && (cell.vol >> 4) == 0xF);
    const bool useInstruments = !s.instruments.empty();

    if (cell.instr) {
        if (!useInstruments) {
            int si = cell.instr - 1;
            ch.sample = si < (int)s.samples.size() ? (int16_t)si : (int16_t)-1;
            ch.instrument = -1;
        } else {
            int ii = cell.instr - 1;
            ch.instrument = ii < (int)s.instruments.size() ? (int16_t)ii : (int16_t)-1;
            int note = cell.note && cell.note != NOTE_OFF ? cell.note : ch.note;
            if (ch.instrument >= 0 && note >= 1 && note <= 96) {
                uint16_t m = s.instruments[ch.instrument].sampleMap[note - 1];
                ch.sample = m < s.samples.size() ? (int16_t)m : (int16_t)-1;
            } else if (ch.instrument < 0) {
                ch.sample = -1;
            }
        }
        if (ch.sample >= 0) {
            const Sample& sm = s.samples[ch.sample];
            ch.volume = sm.volume;
            ch.finetune = sm.finetune;
            ch.relNote = sm.relativeNote;
            if (ft2) ch.pan = sm.pan;
        }
        if (ft2) {
            ch.keyOn = true;
            ch.fadeVol = 32768;
            ch.volEnvTick = ch.panEnvTick = 0;
            ch.avSweep = 0;
            ch.avPos = 0;
        }
    }

    if (cell.note == NOTE_OFF) {
        keyOff(c);
        return;
    }
    if (!cell.note) return;

    if (useInstruments && ch.instrument >= 0 && !cell.instr) {
        uint16_t m = s.instruments[ch.instrument].sampleMap[cell.note - 1];
        int16_t ns = m < s.samples.size() ? (int16_t)m : (int16_t)-1;
        if (ns != ch.sample && ns >= 0 && !porta) {
            ch.finetune = s.samples[ns].finetune;
            ch.relNote = s.samples[ns].relativeNote;
        }
        ch.sample = ns;
    }
    // E5x: set finetune for this note
    if (cell.fx == 0xE && (cell.param >> 4) == 5) {
        int v = cell.param & 15;
        ch.finetune = ft2 ? (int8_t)(v * 16 - 128) : (int8_t)((v > 7 ? v - 16 : v) * 16);
    }
    if (ch.sample < 0) {
        mixer_.stopVoice(c);
        ch.note = cell.note;
        return;
    }
    int32_t target = notePeriod(c, cell.note);
    if (porta && ch.period != 0 && mixer_.active(c)) {
        ch.portaTarget = target;
        ch.note = cell.note;
        return;
    }
    ch.note = cell.note;
    ch.period = target;
    ch.portaTarget = target;
    uint32_t offset = 0;
    if (cell.fx == 9) {
        if (cell.param) ch.mem9 = cell.param;
        offset = (uint32_t)ch.mem9 * 256;
    }
    mixer_.trigger(c, &s.samples[ch.sample], offset);
    if (!(ch.vibWave & 4)) ch.vibPos = 0;
    if (!(ch.tremWave & 4)) ch.tremPos = 0;
    ch.retrigCount = 0;
    ch.tremorPos = 0;
    if (ft2) {
        ch.keyOn = true;
        ch.fadeVol = 32768;
        ch.volEnvTick = ch.panEnvTick = 0;
        ch.avSweep = 0;
        ch.avPos = 0;
    }
}

void Player::updateOutputs(int c) {
    ChannelState& ch = ch_[c];
    ch.outPeriod = ch.periodOverride ? ch.periodOverride : ch.period + ch.periodDelta;
    ch.envVol = 64;
    ch.envPan = 32;
    ch.avDelta = 0;
    if (isFT2()) instrumentTick(c);
    int v = ch.volume + ch.volDelta;
    if (v < 0) v = 0;
    if (v > 64) v = 64;
    if (ch.tremorMute) v = 0;
    float vol = (float)v / 64.f * (ch.envVol / 64.f) * (ch.fadeVol / 32768.f) * (globalVol_ / 64.f);
    ch.outVolume = (uint8_t)std::lround(vol * 64.f);
    ch.outVol01 = vol;
    // pan envelope (FT2): final = pan + (env-32) * (128 - |pan-128|) / 32
    int pan = ch.pan;
    if (ch.envPan != 32) pan = pan + (ch.envPan - 32) * (128 - std::abs(pan - 128)) / 32;
    if (pan < 0) pan = 0;
    if (pan > 255) pan = 255;
    ch.outPan01 = pan / 255.f;
    if (ch.sample >= 0 && ch.outPeriod > 0) mixer_.setFrequency(c, periodToHz(ch.outPeriod + ch.avDelta));
    mixer_.setVolume(c, ch.muted ? 0.f : ch.outVol01, ch.outPan01);
}

void Player::processRow() {
    const Pattern& pat = song_->patterns[pattern_];
    const int n = song_->numChannels < pat.channels ? song_->numChannels : pat.channels;
    for (int c = 0; c < n; ++c) {
        ChannelState& ch = ch_[c];
        ch.cell = pat.at(row_, c);
        ch.periodDelta = 0;
        ch.periodOverride = 0;
        ch.volDelta = 0;
        ch.tremorMute = false;
        const Cell& cell = ch.cell;
        if (cell.fx == 0xE && (cell.param >> 4) == 0xD && (cell.param & 15)) {
            ch.delayPending = true;
            ch.delayTick = cell.param & 15;
        } else {
            ch.delayPending = false;
            noteStart(c);
        }
        if (isFT2() && !ch.delayPending) volumeColumn(c, true);
        rowEffects(c);
        updateOutputs(c);
    }
}

void Player::tick() {
    if (!playing_ || !song_) return;
    if (tick_ == 0 && !repeatingRow_) {
        processRow();
    } else {
        const int n = song_->numChannels;
        for (int c = 0; c < n; ++c) {
            ChannelState& ch = ch_[c];
            ch.periodDelta = 0;
            ch.periodOverride = 0;
            ch.volDelta = 0;
            ch.tremorMute = false;
            if (ch.delayPending && tick_ == ch.delayTick) {
                ch.delayPending = false;
                noteStart(c);
                if (isFT2()) volumeColumn(c, true);
            }
            if (isFT2()) volumeColumn(c, false);
            tickEffects(c);
            updateOutputs(c);
        }
    }
    if (!playing_) return;  // F00 stopped the song
    if (++tick_ >= speed_) {
        tick_ = 0;
        endOfRow();
    }
}

void Player::enterOrder(int o) {
    if (o >= (int)song_->order.size()) {
        o = song_->restartPos < song_->order.size() ? song_->restartPos : 0;
        looped_ = true;
    }
    if (visited_[o >> 5] & (1u << (o & 31))) looped_ = true;
    visited_[o >> 5] |= 1u << (o & 31);
    order_ = (uint16_t)o;
    pattern_ = song_->order[o] < song_->patterns.size() ? song_->order[o] : 0;
}

void Player::endOfRow() {
    if (patDelay_ > 0) {
        --patDelay_;
        repeatingRow_ = true;
        return;
    }
    repeatingRow_ = false;
    if (loopJump_) {
        row_ = loopTarget_;
    } else if (patternMode_) {
        if (posJump_ || break_)
            row_ = break_ ? breakRow_ : 0;
        else
            ++row_;
    } else if (posJump_ || break_) {
        enterOrder(posJump_ ? jumpOrder_ : order_ + 1);
        row_ = break_ ? breakRow_ : 0;
    } else if (++row_ >= patternRows()) {
        enterOrder(order_ + 1);
        row_ = 0;
    }
    if (row_ >= patternRows()) row_ = 0;
    posJump_ = break_ = loopJump_ = false;
}

void Player::render(float* out, uint32_t frames) {
    while (frames) {
        if (!playing_ || !song_) {
            mixer_.mix(out, frames);
            return;
        }
        if (samplesLeft_ == 0) {
            tick();
            double exact = mixer_.rate() * 2.5 / bpm_ + tickFrac_;
            samplesLeft_ = (uint32_t)exact;
            tickFrac_ = exact - samplesLeft_;
            if (samplesLeft_ == 0) samplesLeft_ = 1;
            continue;
        }
        uint32_t n = frames < samplesLeft_ ? frames : samplesLeft_;
        mixer_.mix(out, n);
        out += n * 2;
        frames -= n;
        samplesLeft_ -= n;
    }
}
