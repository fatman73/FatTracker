// Amiga Paula output stage for Pure mode: the analog filters of the A500 / A1200 and the "LED" filter.
#pragma once
#include <cstdint>

enum class PaulaModel : uint8_t { None, A500, A1200 };

class PaulaFilter {
public:
    void setRate(uint32_t hz);
    void setModel(PaulaModel m);
    void setLed(bool on) { led_ = on; }
    bool led() const { return led_; }
    void reset();                                 // clears the filter memory
    void process(float* stereo, uint32_t frames);  // in place; does nothing for PaulaModel::None

private:
    void update();
    uint32_t rate_ = 48000;
    PaulaModel model_ = PaulaModel::None;
    bool led_ = false;
    float lpA_ = 0, hpA_ = 0;                    // one-pole coefficients
    float b0_ = 0, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;  // LED biquad
    float lp_[2] = {}, hp_[2] = {}, z1_[2] = {}, z2_[2] = {};
};
