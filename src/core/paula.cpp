#include "core/paula.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
// Measured component values of the Amiga output stage (as used by accurate emulators).
constexpr double kA500Lowpass = 4420.971;  // one-pole RC
constexpr double kHighpass = 5.2;          // one-pole RC (A500 and A1200)
constexpr double kLedCutoff = 3090.533;    // two-pole Sallen-Key "LED" filter
constexpr double kLedQ = 0.660225;

float onePole(double hz, uint32_t rate) { return (float)(1.0 - std::exp(-2.0 * kPi * hz / rate)); }
}  // namespace

void PaulaFilter::setRate(uint32_t hz) {
    rate_ = hz ? hz : 48000;
    update();
}

void PaulaFilter::setModel(PaulaModel m) {
    model_ = m;
    update();
}

void PaulaFilter::reset() {
    for (int c = 0; c < 2; ++c) lp_[c] = hp_[c] = z1_[c] = z2_[c] = 0;
}

void PaulaFilter::update() {
    lpA_ = onePole(kA500Lowpass, rate_);
    hpA_ = onePole(kHighpass, rate_);
    // RBJ low-pass biquad (bilinear transform), cutoff kept below Nyquist
    double f = kLedCutoff < rate_ * 0.45 ? kLedCutoff : rate_ * 0.45;
    const double w0 = 2.0 * kPi * f / rate_, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * kLedQ);
    const double a0 = 1.0 + alpha;
    b0_ = (float)((1.0 - cw) / 2.0 / a0);
    b1_ = (float)((1.0 - cw) / a0);
    b2_ = b0_;
    a1_ = (float)(-2.0 * cw / a0);
    a2_ = (float)((1.0 - alpha) / a0);
}

void PaulaFilter::process(float* s, uint32_t frames) {
    if (model_ == PaulaModel::None) return;
    const bool a500 = model_ == PaulaModel::A500;
    for (uint32_t f = 0; f < frames; ++f)
        for (int c = 0; c < 2; ++c) {
            float x = s[2 * f + c];
            if (a500) x = lp_[c] += lpA_ * (x - lp_[c]);
            if (led_) {  // transposed direct form II
                const float y = b0_ * x + z1_[c];
                z1_[c] = b1_ * x - a1_ * y + z2_[c];
                z2_[c] = b2_ * x - a2_ * y;
                x = y;
            }
            hp_[c] += hpA_ * (x - hp_[c]);  // high-pass = input minus its 5.2 Hz low-pass
            s[2 * f + c] = x - hp_[c];
        }
}
