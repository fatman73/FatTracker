#include "core/floppy.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSpinUp = 0.5, kStep = 0.003, kSettle = 0.015, kRev = 0.2, kCylinder = 0.4;
constexpr double kMotorRunOn = 2.0, kMaxAccess = 12.0;
constexpr uint32_t kBytesPerCylinder = 11264;  // 2 sides x 11 sectors x 512 bytes
constexpr int kDirCylinder = 40, kCylinders = 80;
constexpr float kMotorLevel = 0.12f, kStepLevel = 0.55f, kInsertLevel = 0.6f;

struct Noise {  // deterministic white noise in [-1, 1]
    uint32_t s = 0x1234567;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (float)((int32_t)s) / 2147483648.f;
    }
};

std::vector<float> synthStep(uint32_t rate) {
    std::vector<float> v((size_t)(0.03 * rate));
    Noise n;
    for (size_t i = 0; i < v.size(); ++i) {
        const double t = (double)i / rate;
        const double thump = std::sin(2 * kPi * 90 * t) * std::exp(-t / 0.008) * 0.5;
        const double tick = n.next() * std::exp(-t / 0.0015) * 0.6;
        const double ring = std::sin(2 * kPi * 2300 * t) * std::exp(-t / 0.004) * 0.3;
        v[i] = (float)(thump + tick + ring) * kStepLevel;
    }
    return v;
}

std::vector<float> synthInsert(uint32_t rate) {
    std::vector<float> v((size_t)(0.4 * rate));
    Noise n;
    for (size_t i = 0; i < v.size(); ++i) {
        double s = 0;
        for (double at : {0.0, 0.13}) {  // the disk slides in, then the shutter latches
            const double t = (double)i / rate - at;
            if (t < 0) continue;
            s += std::sin(2 * kPi * 60 * t) * std::exp(-t / 0.03) * 0.6 + n.next() * std::exp(-t / 0.01) * 0.4;
        }
        v[i] = (float)s * kInsertLevel;
    }
    return v;
}

// One revolution (0.2 s at 300 rpm) of motor whirr, cross-faded so it loops without a seam.
std::vector<float> synthMotor(uint32_t rate) {
    const size_t len = (size_t)(kRev * rate), fade = len / 8;
    std::vector<float> raw(len + fade), v(len);
    Noise n;
    float lp = 0;
    const float a = (float)(1.0 - std::exp(-2 * kPi * 400.0 / rate));
    for (size_t i = 0; i < raw.size(); ++i) {
        lp += a * (n.next() - lp);
        const double t = (double)i / rate;
        raw[i] = (float)((lp * 1.6 + 0.25 * std::sin(2 * kPi * 100 * t)) * (0.8 + 0.2 * std::sin(2 * kPi * 5 * t)));
    }
    for (size_t i = 0; i < len; ++i) v[i] = raw[i] * kMotorLevel;
    for (size_t i = 0; i < fade; ++i) {
        const float w = (float)i / fade;
        v[i] = (raw[len + i] * (1 - w) + raw[i] * w) * kMotorLevel;
    }
    return v;
}

// A user sample resampled (linear) to the output rate; its pitch at C-4 gives its rate.
std::vector<float> fromSample(const Sample& s, uint32_t rate) {
    const double src = 8363.0 * std::pow(2.0, (s.relativeNote + s.finetune / 128.0) / 12.0);
    const size_t out = (size_t)(s.length * (double)rate / src);
    std::vector<float> v(out);
    auto at = [&](uint32_t i) -> float {
        if (i >= s.length) i = s.length - 1;
        return s.is16 ? s.data16[i] / 32768.f : s.data8[i] / 128.f;
    };
    for (size_t i = 0; i < out; ++i) {
        const double p = i * src / rate;
        const uint32_t k = (uint32_t)p;
        const float f = (float)(p - k);
        v[i] = at(k) * (1 - f) + at(k + 1) * f;
    }
    return v;
}
}  // namespace

Floppy::Floppy() { rebuild(); }

void Floppy::setRate(uint32_t hz) {
    if (!hz || hz == rate_) return;
    rate_ = hz;
    shots_.clear();
    rebuild();
}

void Floppy::setSounds(const Sample* motor, const Sample* step, const Sample* insert) {
    const Sample* in[3] = {motor, step, insert};
    for (int i = 0; i < 3; ++i) {
        const Sample* s = in[i];
        hasUser_[i] = s && s->length > 0 && (s->is16 ? s->data16.size() : s->data8.size()) >= s->length;
        user_[i] = hasUser_[i] ? *s : Sample{};
    }
    shots_.clear();
    rebuild();
}

void Floppy::rebuild() {
    motor_ = hasUser_[0] ? fromSample(user_[0], rate_) : synthMotor(rate_);
    step_ = hasUser_[1] ? fromSample(user_[1], rate_) : synthStep(rate_);
    insert_ = hasUser_[2] ? fromSample(user_[2], rate_) : synthInsert(rate_);
    motorPos_ = 0;
}

uint64_t Floppy::begin() {
    uint64_t t = busyUntil_ > now_ ? busyUntil_ : now_;
    const bool spinning = motorOn_ && motorOffAt_ > t;
    if (!spinning) t += sec(kSpinUp);
    motorOn_ = true;
    return t;
}

void Floppy::seek(uint64_t& t, int cyl) {
    const int steps = cyl > cyl_ ? cyl - cyl_ : cyl_ - cyl;
    for (int i = 0; i < steps; ++i) {
        shots_.push_back({&step_, t});
        t += sec(kStep);
    }
    if (steps) t += sec(kSettle);
    cyl_ = cyl;
}

double Floppy::access(uint32_t bytes, bool file) {
    const uint64_t base = busyUntil_ > now_ ? busyUntil_ : now_, limit = base + sec(kMaxAccess);
    uint64_t t = begin();
    seek(t, kDirCylinder);
    if (!file) {
        t += sec(kCylinder);
    } else {
        t += sec(kRev);  // directory block
        const uint32_t cylinders = bytes / kBytesPerCylinder + (bytes % kBytesPerCylinder ? 1 : 0);
        for (uint32_t i = 0; i < (cylinders ? cylinders : 1); ++i) {
            // the data sits next to the directory: upwards first, then below it
            const int up = kDirCylinder + 1 + (int)i;
            const int c = up < kCylinders ? up : kDirCylinder - 1 - (up - kCylinders);
            if (c < 0) break;
            if (t + sec(kStep + kSettle + kCylinder) > limit) break;
            seek(t, c);
            t += sec(kCylinder);
        }
    }
    if (t > limit) t = limit;
    busyUntil_ = t;
    motorOffAt_ = t + sec(kMotorRunOn);
    return (double)(t - now_) / rate_;
}

double Floppy::read(uint32_t bytes) { return access(bytes, true); }
double Floppy::write(uint32_t bytes) { return access(bytes, true); }
double Floppy::dir() { return access(0, false); }

void Floppy::insert() {
    const uint64_t t0 = busyUntil_ > now_ ? busyUntil_ : now_;
    shots_.push_back({&insert_, t0});
    busyUntil_ = t0 + sec(0.4);
    cyl_ = kCylinders - 1;  // unknown head position: the drive recalibrates to cylinder 0
    uint64_t t = begin();
    seek(t, 0);
    busyUntil_ = t;
    motorOffAt_ = t + sec(kMotorRunOn);
}

double Floppy::busySeconds() const { return busyUntil_ > now_ ? (double)(busyUntil_ - now_) / rate_ : 0.0; }

bool Floppy::audible() const { return motorLevel_ > 0.f || motorOn_ || !shots_.empty(); }

void Floppy::mix(float* out, uint32_t frames, float gain) {
    if (!audible()) {
        now_ += frames;
        return;
    }
    const float up = 1.f / (float)(0.3 * rate_), down = 1.f / (float)(0.5 * rate_);
    for (uint32_t f = 0; f < frames; ++f) {
        const uint64_t t = now_ + f;
        if (motorOn_ && t >= motorOffAt_ && t >= busyUntil_) motorOn_ = false;
        motorLevel_ += motorOn_ ? up : -down;
        motorLevel_ = motorLevel_ < 0 ? 0 : (motorLevel_ > 1 ? 1 : motorLevel_);
        float s = 0;
        if (motorLevel_ > 0 && !motor_.empty()) {
            s += motor_[motorPos_] * motorLevel_;
            if (++motorPos_ >= motor_.size()) motorPos_ = 0;
        }
        for (const Shot& sh : shots_)
            if (t >= sh.at && t - sh.at < sh.snd->size()) s += (*sh.snd)[(size_t)(t - sh.at)];
        out[2 * f] += s * gain;
        out[2 * f + 1] += s * gain;
    }
    now_ += frames;
    size_t k = 0;  // drop finished one-shots
    for (const Shot& sh : shots_)
        if (now_ < sh.at || now_ - sh.at < sh.snd->size()) shots_[k++] = sh;
    shots_.resize(k);
}
