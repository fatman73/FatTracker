// Effect commands. Tick-0 handling in rowEffects, continuous handling in tickEffects.
#include "core/player.h"
#include "core/tables.h"

void Player::rowEffects(int c) {
    ChannelState& ch = ch_[c];
    const uint8_t fx = ch.cell.fx, p = ch.cell.param;
    switch (fx) {
        case 0xB:
            posJump_ = true;
            jumpOrder_ = p;
            if (!break_) breakRow_ = 0;
            break;
        case 0xD:
            break_ = true;
            breakRow_ = (uint16_t)((p >> 4) * 10 + (p & 15));
            break;
        case 0xE:
            switch (p >> 4) {
                case 0x6:
                    if ((p & 15) == 0) {
                        ch.loopRow = (uint8_t)row_;
                    } else if (ch.loopCount == 0) {
                        ch.loopCount = p & 15;
                        loopJump_ = true;
                        loopTarget_ = ch.loopRow;
                    } else if (--ch.loopCount) {
                        loopJump_ = true;
                        loopTarget_ = ch.loopRow;
                    }
                    break;
                case 0xE:
                    if (!repeatingRow_ && patDelay_ == 0) patDelay_ = p & 15;
                    break;
            }
            break;
        case 0xF:
            if (p == 0) {
                stop();
            } else if (p < 0x20) {
                speed_ = p;
            } else {
                bpm_ = p;
            }
            break;
    }
}

void Player::tickEffects(int) {}

void Player::volumeColumn(int, bool) {}

void Player::doVolSlide(int, uint8_t) {}
void Player::doPortaUp(int, int) {}
void Player::doPortaDown(int, int) {}
void Player::doTonePorta(int) {}
void Player::doVibrato(int) {}
void Player::doTremolo(int) {}
