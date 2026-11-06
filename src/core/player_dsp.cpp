// Symphonie DSP echo: a delay line on the final mix, `delay` ticks long, that feeds back into itself
// (straight, crossed between the sides or through the centre), as Symphonie Pro and OpenMPT's
// SymMOD Echo do it. Only songs with echo settings (Song::symEcho) run it.
#include <algorithm>
#include <cmath>

#include "core/player.h"

namespace {
enum : uint8_t { kOff, kNormal, kCross, kCross2, kCenter, kNumTypes };
constexpr int kMaxDelay = 127;  // ticks
constexpr int kMinBpm = 32;
}  // namespace

void Player::echoReset() {
    echoType_ = kOff;
    echoDelay_ = 0;
    echoFeedback_ = 0.5f;
    echoPos_ = 0;
    if (!song_ || song_->symEcho.empty()) {
        std::vector<float>().swap(echoLine_);
        return;
    }
    // room for the longest delay at the slowest tempo, so playback never allocates
    const size_t need = (size_t)2 * kMaxDelay * (mixer_.rate() * 5 / (kMinBpm * 2) + 1);
    if (echoLine_.size() < need) echoLine_.resize(need);
    std::fill(echoLine_.begin(), echoLine_.end(), 0.f);
}

void Player::echoSet(uint8_t index) {
    if (!song_ || index >= song_->symEcho.size()) return;
    const Song::SymEcho& e = song_->symEcho[index];
    const int level = e.feedback > kMaxDelay ? kMaxDelay : e.feedback;
    // the level always applies; a zero delay is an error that leaves type and delay alone
    if (e.delay) {
        const uint8_t delay = (uint8_t)(e.delay > kMaxDelay ? kMaxDelay : e.delay);
        if (delay != echoDelay_) {  // a new length starts an empty delay line
            std::fill(echoLine_.begin(), echoLine_.end(), 0.f);
            echoPos_ = 0;
        }
        echoDelay_ = delay;
        echoType_ = e.type < kNumTypes ? e.type : kOff;
    }
    echoFeedback_ = echoType_ == kCross2 ? 1.f - std::pow(2.f, -(float)(level + 1)) : std::pow(2.f, -(float)level);
}

void Player::echoProcess(float* out, uint32_t frames) {
    if (echoType_ == kOff || !echoDelay_) return;
    uint32_t delay = samplesPerTick() * echoDelay_;
    if ((size_t)delay * 2 > echoLine_.size()) delay = (uint32_t)(echoLine_.size() / 2);
    if (!delay) return;
    const float fb = echoFeedback_;
    for (uint32_t i = 0; i < frames; ++i) {
        if (echoPos_ >= delay) echoPos_ = 0;
        float* d = &echoLine_[(size_t)echoPos_ * 2];
        const float l = out[i * 2], r = out[i * 2 + 1], dl = d[0], dr = d[1];
        out[i * 2] = l + dl;
        out[i * 2 + 1] = r + dr;
        float nl, nr;
        switch (echoType_) {
            case kNormal:
                nl = (dl + l) * fb;
                nr = (dr + r) * fb;
                break;
            case kCenter:
                nl = nr = (dl + (l + r) * 0.5f) * fb;
                break;
            default:  // cross echo (1 and 2): each side feeds the other
                nl = (dr + r) * fb;
                nr = (dl + l) * fb;
                break;
        }
        d[0] = std::fabs(nl) < 1e-24f ? 0.f : nl;  // no denormals
        d[1] = std::fabs(nr) < 1e-24f ? 0.f : nr;
        ++echoPos_;
    }
}
