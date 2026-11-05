// Effect commands. Tick-0 handling in rowEffects, continuous handling in tickEffects.
#include <cmath>

#include "core/player.h"
#include "core/tables.h"

namespace {
inline uint8_t clampVol(int v) { return (uint8_t)(v < 0 ? 0 : (v > 64 ? 64 : v)); }
}  // namespace

void Player::doVolSlide(int c, uint8_t p) {
    ChannelState& ch = ch_[c];
    if (p >> 4)
        ch.volume = clampVol(ch.volume + (p >> 4));
    else
        ch.volume = clampVol(ch.volume - (p & 15));
}

void Player::doPortaUp(int c, int amount) {
    ChannelState& ch = ch_[c];
    if (isFT2()) {
        ch.period -= amount * 4;
        if (ch.period < 1) ch.period = 1;
    } else {
        ch.period -= amount;
        if (ch.period < 113) ch.period = 113;
    }
}

void Player::doPortaDown(int c, int amount) {
    ChannelState& ch = ch_[c];
    if (isFT2()) {
        ch.period += amount * 4;
        if (ch.period > 32000) ch.period = 32000;
    } else {
        ch.period += amount;
        if (ch.period > 856) ch.period = 856;
    }
}

void Player::doTonePorta(int c) {
    ChannelState& ch = ch_[c];
    if (!ch.portaTarget || !ch.period) return;
    int speed = isFT2() ? ch.portaSpeed * 4 : ch.portaSpeed;
    if (ch.period < ch.portaTarget) {
        ch.period += speed;
        if (ch.period > ch.portaTarget) ch.period = ch.portaTarget;
    } else if (ch.period > ch.portaTarget) {
        ch.period -= speed;
        if (ch.period < ch.portaTarget) ch.period = ch.portaTarget;
    }
}

static int waveValue(uint8_t wave, uint8_t pos) {
    const uint8_t idx = pos & 31;
    switch (wave & 3) {
        case 0:
            return kVibratoSine[idx];
        case 1: {
            int v = idx * 8;
            return (pos & 32) ? 255 - v : v;
        }
        default:
            return 255;
    }
}

void Player::doVibrato(int c) {
    ChannelState& ch = ch_[c];
    int v = waveValue(ch.vibWave, ch.vibPos);
    int delta = isFT2() ? (v * ch.vibDepth) >> 5 : (v * ch.vibDepth) >> 7;
    ch.periodDelta = (ch.vibPos & 32) ? -delta : delta;
    ch.vibPos = (uint8_t)((ch.vibPos + ch.vibSpeed) & 63);
}

void Player::doTremolo(int c) {
    ChannelState& ch = ch_[c];
    int v = waveValue(ch.tremWave, ch.tremPos);
    int delta = (v * ch.tremDepth) >> 6;
    ch.volDelta = (int16_t)((ch.tremPos & 32) ? -delta : delta);
    ch.tremPos = (uint8_t)((ch.tremPos + ch.tremSpeed) & 63);
}

void Player::rowEffects(int c) {
    ChannelState& ch = ch_[c];
    const uint8_t fx = ch.cell.fx, p = ch.cell.param;
    const bool ft2 = isFT2();
    switch (fx) {
        case 0x1:
            if (ft2 && p) ch.mem1 = p;
            break;
        case 0x2:
            if (ft2 && p) ch.mem2 = p;
            break;
        case 0x3:
            if (p) ch.portaSpeed = p;
            break;
        case 0x4:
            if (p & 0xF0) ch.vibSpeed = p >> 4;
            if (p & 0x0F) ch.vibDepth = p & 15;
            break;
        case 0x5:
        case 0x6:
        case 0xA:
            if (ft2 && p) ch.memA = p;
            break;
        case 0x7:
            if (p & 0xF0) ch.tremSpeed = p >> 4;
            if (p & 0x0F) ch.tremDepth = p & 15;
            break;
        case 0x8:
            if (ft2) ch.pan = p;
            break;
        case 0xB:
            posJump_ = true;
            jumpOrder_ = p;
            if (!break_) breakRow_ = 0;
            break;
        case 0xC:
            ch.volume = clampVol(p);
            break;
        case 0xD:
            break_ = true;
            breakRow_ = (uint16_t)((p >> 4) * 10 + (p & 15));
            break;
        case 0xE: {
            const uint8_t x = p & 15;
            switch (p >> 4) {
                case 0x1: {
                    uint8_t v = x;
                    if (ft2) {
                        if (v) ch.memE1 = v;
                        v = ch.memE1;
                    }
                    doPortaUp(c, v);
                    break;
                }
                case 0x2: {
                    uint8_t v = x;
                    if (ft2) {
                        if (v) ch.memE2 = v;
                        v = ch.memE2;
                    }
                    doPortaDown(c, v);
                    break;
                }
                case 0x3:
                    ch.glissando = x != 0;
                    break;
                case 0x4:
                    ch.vibWave = x;
                    break;
                case 0x6:
                    if (x == 0) {
                        ch.loopRow = (uint8_t)row_;
                    } else if (ch.loopCount == 0) {
                        ch.loopCount = x;
                        loopJump_ = true;
                        loopTarget_ = ch.loopRow;
                    } else if (--ch.loopCount) {
                        loopJump_ = true;
                        loopTarget_ = ch.loopRow;
                    }
                    break;
                case 0x7:
                    ch.tremWave = x;
                    break;
                case 0xA: {
                    uint8_t v = x;
                    if (ft2) {
                        if (v) ch.memEA = v;
                        v = ch.memEA;
                    }
                    ch.volume = clampVol(ch.volume + v);
                    break;
                }
                case 0xB: {
                    uint8_t v = x;
                    if (ft2) {
                        if (v) ch.memEB = v;
                        v = ch.memEB;
                    }
                    ch.volume = clampVol(ch.volume - v);
                    break;
                }
                case 0xC:
                    if (x == 0) ch.volume = 0;
                    break;
                case 0xE:
                    if (!repeatingRow_ && patDelay_ == 0) patDelay_ = x;
                    break;
            }
            break;
        }
        case 0xF:
            if (p == 0)
                stop();
            else if (p < 0x20)
                speed_ = p;
            else
                bpm_ = p;
            break;
    }
}

void Player::tickEffects(int c) {
    ChannelState& ch = ch_[c];
    const uint8_t fx = ch.cell.fx, p = ch.cell.param;
    const bool ft2 = isFT2();
    switch (fx) {
        case 0x0:
            if (p) {
                int t = tick_ % 3;
                if (t == 0) break;
                int semis = t == 1 ? p >> 4 : p & 15;
                if (!ft2) {
                    int ft = ptFinetuneIndex(ch.finetune);
                    int idx = 35;
                    for (int i = 0; i < 36; ++i)
                        if (kPtPeriods[ft][i] <= ch.period) {
                            idx = i;
                            break;
                        }
                    int ni = idx + semis;
                    ch.periodOverride = kPtPeriods[ft][ni > 35 ? 35 : ni];
                } else if (song_->freqMode == FreqMode::Linear) {
                    ch.periodOverride = ch.period - semis * 64;
                } else {
                    ch.periodOverride = (int32_t)std::lround(ch.period * std::pow(2.0, -semis / 12.0));
                }
                if (ch.periodOverride < 1) ch.periodOverride = 1;
            }
            break;
        case 0x1:
            doPortaUp(c, ft2 ? ch.mem1 : p);
            break;
        case 0x2:
            doPortaDown(c, ft2 ? ch.mem2 : p);
            break;
        case 0x3:
            doTonePorta(c);
            break;
        case 0x4:
            doVibrato(c);
            break;
        case 0x5:
            doTonePorta(c);
            doVolSlide(c, ft2 ? ch.memA : p);
            break;
        case 0x6:
            doVibrato(c);
            doVolSlide(c, ft2 ? ch.memA : p);
            break;
        case 0x7:
            doTremolo(c);
            break;
        case 0xA:
            doVolSlide(c, ft2 ? ch.memA : p);
            break;
        case 0xE: {
            const uint8_t x = p & 15;
            switch (p >> 4) {
                case 0x9:
                    if (x && tick_ % x == 0 && ch.sample >= 0) mixer_.trigger(c, &song_->samples[ch.sample], 0);
                    break;
                case 0xC:
                    if (tick_ == x) ch.volume = 0;
                    break;
            }
            break;
        }
    }
}

void Player::volumeColumn(int, bool) {}
