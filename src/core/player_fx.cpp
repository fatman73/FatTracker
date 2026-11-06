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
                case 0x0:  // E0x: Amiga "LED" filter, 0 = on (FT2 ignores it)
                    if (!ft2) mixer_.setLed(x == 0);
                    break;
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
                pendingStop_ = true;  // applied after the whole row, see processRow
            else if (p < 0x20)
                speed_ = p;
            else
                bpm_ = p;
            break;
        default:
            if (ft2) ft2RowEffects(c);
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
                // PT counts ticks up; FT2 indexes its arpeggio table with the down-counting timer
                int t = ft2 ? (speed_ - tick_) % 3 : tick_ % 3;
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
        default:
            if (ft2) ft2TickEffects(c);
            break;
    }
}

void Player::volumeColumn(int c, bool tick0) {
    ChannelState& ch = ch_[c];
    const uint8_t v = ch.cell.vol, hi = v >> 4, lo = v & 15;
    if (v < 0x10) return;
    if (tick0) {
        if (v >= 0x10 && v <= 0x50) {
            ch.volume = (uint8_t)(v - 0x10);
            return;
        }
        switch (hi) {
            case 0x8:
                ch.volume = clampVol(ch.volume - lo);
                break;
            case 0x9:
                ch.volume = clampVol(ch.volume + lo);
                break;
            case 0xA:
                ch.vibSpeed = lo;
                break;
            case 0xB:
                if (lo) ch.vibDepth = lo;
                break;
            case 0xC:
                ch.pan = (uint8_t)(lo * 17);
                break;
            case 0xF:
                if (lo) ch.portaSpeed = (uint8_t)(lo * 16);
                break;
        }
        return;
    }
    switch (hi) {
        case 0x6:
            ch.volume = clampVol(ch.volume - lo);
            break;
        case 0x7:
            ch.volume = clampVol(ch.volume + lo);
            break;
        case 0xB:
            doVibrato(c);
            break;
        case 0xD: {
            int p = ch.pan - lo;
            ch.pan = (uint8_t)(p < 0 ? 0 : p);
            break;
        }
        case 0xE: {
            int p = ch.pan + lo;
            ch.pan = (uint8_t)(p > 255 ? 255 : p);
            break;
        }
        case 0xF:
            doTonePorta(c);
            break;
    }
}

// Symphonie slides run every tick (tick 0 too), from the fractional value they left, unless
// something else has changed the volume / period since.
void Player::symphonieSlideTick(int c) {
    ChannelState& ch = ch_[c];
    if (ch.symVolRate) {
        if (ch.volume != (uint8_t)std::lround(ch.symVol)) ch.symVol = ch.volume;
        symVolumeStep(ch.symVol, ch.symVolRate);
        ch.volume = (uint8_t)std::lround(ch.symVol);
    }
    if (ch.symPitchRate && ch.period > 0) {
        if (ch.period != (int32_t)std::lround(ch.symPeriod)) ch.symPeriod = (float)ch.period;
        // the original multiplies its frequency by (1 + rate / 8192): 768 linear period units per octave
        ch.symPeriod -= 768.f * std::log2(1.f + (float)ch.symPitchRate / 8192.f);
        ch.symPeriod = ch.symPeriod < 1.f ? 1.f : (ch.symPeriod > 32000.f ? 32000.f : ch.symPeriod);
        ch.period = (int32_t)std::lround(ch.symPeriod);
    }
}

void Player::ft2RowEffects(int c) {
    ChannelState& ch = ch_[c];
    const uint8_t fx = ch.cell.fx, p = ch.cell.param;
    if (fx == kFxQ && !song_->symEcho.empty()) echoSet(p);
    if (song_->symphonieSlides) {
        switch (fx) {
            case kFxU: ch.symVolRate = p; break;
            case kFxV: ch.symVolRate = -p; break;
            case kFxW: ch.symPitchRate = p; break;
            case kFxY: ch.symPitchRate = -p; break;
        }
    }
    switch (fx) {
        case kFxG:
            globalVol_ = p > 64 ? 64 : p;
            break;
        case kFxH:
            if (p) ch.memH = p;
            break;
        case kFxK:
            if (p == 0) keyOff(c);
            break;
        case kFxL:
            ch.volEnvTick = ch.panEnvTick = p;
            break;
        case kFxP:
            if (p) ch.memP = p;
            break;
        case kFxR:
            if (p) ch.memR = p;
            break;
        case kFxT:
            if (p) ch.memT = p;
            break;
        case kFxX: {
            const uint8_t x = p & 15;
            if ((p >> 4) == 1) {
                if (x) ch.memX1 = x;
                ch.period -= ch.memX1;
                if (ch.period < 1) ch.period = 1;
            } else if ((p >> 4) == 2) {
                if (x) ch.memX2 = x;
                ch.period += ch.memX2;
            }
            break;
        }
    }
}

void Player::ft2TickEffects(int c) {
    ChannelState& ch = ch_[c];
    const uint8_t fx = ch.cell.fx, p = ch.cell.param;
    switch (fx) {
        case kFxH: {
            int g = globalVol_;
            if (ch.memH >> 4)
                g += ch.memH >> 4;
            else
                g -= ch.memH & 15;
            globalVol_ = (uint8_t)(g < 0 ? 0 : (g > 64 ? 64 : g));
            break;
        }
        case kFxK:
            if (tick_ == p) keyOff(c);
            break;
        case kFxP: {
            int pan = ch.pan;
            if (ch.memP >> 4)
                pan += ch.memP >> 4;
            else
                pan -= ch.memP & 15;
            ch.pan = (uint8_t)(pan < 0 ? 0 : (pan > 255 ? 255 : pan));
            break;
        }
        case kFxR: {
            const int interval = ch.memR & 15;
            if (!interval || ch.sample < 0) break;
            if (++ch.retrigCount < interval) break;
            ch.retrigCount = 0;
            int v = ch.volume;
            switch (ch.memR >> 4) {
                case 1: v -= 1; break;
                case 2: v -= 2; break;
                case 3: v -= 4; break;
                case 4: v -= 8; break;
                case 5: v -= 16; break;
                case 6: v = v * 2 / 3; break;
                case 7: v /= 2; break;
                case 9: v += 1; break;
                case 0xA: v += 2; break;
                case 0xB: v += 4; break;
                case 0xC: v += 8; break;
                case 0xD: v += 16; break;
                case 0xE: v = v * 3 / 2; break;
                case 0xF: v *= 2; break;
            }
            ch.volume = clampVol(v);
            mixer_.trigger(c, &song_->samples[ch.sample], 0);
            break;
        }
        case kFxT: {
            int on = (ch.memT >> 4) + 1, off = (ch.memT & 15) + 1;
            ch.tremorMute = (ch.tremorPos % (on + off)) >= on;
            ++ch.tremorPos;
            break;
        }
    }
}
