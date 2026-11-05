#include "core/floppy.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSpinUp = 0.5, kStep = 0.003, kSettle = 0.015, kRev = 0.2, kCylinder = 0.4;
constexpr double kMotorRunOn = 2.0, kLoadTime = 5.0, kMaxWrite = 5.0;
constexpr double kMinUserRate = 1000.0, kMaxSound[3] = {10.0, 0.5, 3.0};  // motor, step, insert (seconds)
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

double sampleRate(const Sample& s) { return 8363.0 * std::pow(2.0, (s.relativeNote + s.finetune / 128.0) / 12.0); }

// A user sample resampled (linear) to the output rate; its pitch at C-4 gives its rate.
std::vector<float> fromSample(const Sample& s, uint32_t rate, double maxSeconds) {
    const double src = sampleRate(s);
    uint32_t len = s.length;
    if (len > maxSeconds * src) len = (uint32_t)(maxSeconds * src);
    const size_t out = (size_t)(len * (double)rate / src);
    std::vector<float> v(out);
    auto at = [&](uint32_t i) -> float {
        if (i >= len) i = len - 1;
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
    // keep the remaining times: everything is counted in frames of the old rate
    const double k = (double)hz / rate_;
    auto rescale = [&](uint64_t t) { return t > now_ ? now_ + (uint64_t)((t - now_) * k + 0.5) : t; };
    busyUntil_ = rescale(busyUntil_);
    motorOffAt_ = rescale(motorOffAt_);
    size_t n = 0;
    for (Shot& sh : shots_)
        if (sh.at > now_) {  // sounds already playing are dropped, future ones move
            sh.at = rescale(sh.at);
            shots_[n++] = sh;
        }
    shots_.resize(n);
    rate_ = hz;
    rebuild();
}

void Floppy::setSounds(const Sample* motor, const Sample* step, const Sample* insert) {
    const Sample* in[3] = {motor, step, insert};
    for (int i = 0; i < 3; ++i) {
        const Sample* s = in[i];
        hasUser_[i] = s && s->length > 1 && (s->is16 ? s->data16.size() : s->data8.size()) >= s->length &&
                      sampleRate(*s) >= kMinUserRate;  // a broken rate would blow the sound up
        user_[i] = Sample{};
        if (hasUser_[i]) {  // keep only what is used
            user_[i] = *s;
            const uint32_t keep = (uint32_t)(kMaxSound[i] * sampleRate(*s));
            if (user_[i].length > keep) {
                user_[i].length = keep;
                if (s->is16) user_[i].data16.resize(keep);
                else user_[i].data8.resize(keep);
            }
        }
    }
    shots_.clear();
    rebuild();
}

void Floppy::rebuild() {
    motor_ = hasUser_[0] ? fromSample(user_[0], rate_, kMaxSound[0]) : synthMotor(rate_);
    step_ = hasUser_[1] ? fromSample(user_[1], rate_, kMaxSound[1]) : synthStep(rate_);
    insert_ = hasUser_[2] ? fromSample(user_[2], rate_, kMaxSound[2]) : synthInsert(rate_);
    if (motor_.empty()) motor_ = synthMotor(rate_);
    if (step_.empty()) step_ = synthStep(rate_);
    if (insert_.empty()) insert_ = synthInsert(rate_);
    motorPos_ = 0;
}

uint64_t Floppy::begin() {
    uint64_t t = now_;
    if (!(motorOn_ && motorOffAt_ > t)) t += sec(kSpinUp);
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

void Floppy::dropFuture() {
    size_t n = 0;
    for (const Shot& sh : shots_)
        if (sh.at <= now_) shots_[n++] = sh;
    shots_.resize(n);
}

// A file access starts now (a new load replaces whatever the drive was doing): the head
// goes to the directory, reads it, then walks the file's cylinders until `seconds` are used.
double Floppy::access(double seconds) {
    dropFuture();
    const uint64_t end = now_ + sec(seconds);
    uint64_t t = begin();
    seek(t, kDirCylinder);
    t += sec(kRev);  // directory block
    for (int i = 0; t + sec(kStep + kSettle) < end; ++i) {
        // the data sits next to the directory: upwards first, then below it
        const int up = kDirCylinder + 1 + i;
        const int c = up < kCylinders ? up : kDirCylinder - 1 - (up - kCylinders);
        if (c < 0) break;
        seek(t, c);
        t += sec(kCylinder);
    }
    busyUntil_ = end;
    motorOffAt_ = end + sec(kMotorRunOn);
    return (double)(end - now_) / rate_;
}

double Floppy::read(uint32_t) { return access(kLoadTime); }

double Floppy::write(uint32_t bytes) {
    const double model = kSpinUp + 0.335 + (bytes / kBytesPerCylinder + 1) * (kCylinder + kStep + kSettle);
    return access(model < kMaxWrite ? model : kMaxWrite);
}

double Floppy::dir() {
    if (busyUntil_ > now_) return 0.0;  // the drive is busy: the listing comes from the cache
    uint64_t t = begin();
    seek(t, kDirCylinder);
    t += sec(kCylinder);
    busyUntil_ = t;
    motorOffAt_ = t + sec(kMotorRunOn);
    return (double)(t - now_) / rate_;
}

void Floppy::cancel() {
    dropFuture();
    busyUntil_ = now_;
    motorOffAt_ = now_ + sec(kMotorRunOn);
}

void Floppy::reset() {
    shots_.clear();
    busyUntil_ = motorOffAt_ = now_;
    motorOn_ = false;
    motorLevel_ = 0;
}

void Floppy::insert() {
    dropFuture();
    shots_.push_back({&insert_, now_});
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
        // a new head step cuts the previous one (one head, one click at a time)
        const Shot* lastStep = nullptr;
        for (const Shot& sh : shots_) {
            if (t < sh.at || t - sh.at >= sh.snd->size()) continue;
            if (sh.snd == &step_) {
                if (!lastStep || sh.at > lastStep->at) lastStep = &sh;
            } else {
                s += (*sh.snd)[(size_t)(t - sh.at)];
            }
        }
        if (lastStep) s += step_[(size_t)(t - lastStep->at)];
        out[2 * f] += s * gain;
        out[2 * f + 1] += s * gain;
    }
    now_ += frames;
    size_t k = 0;  // drop finished one-shots
    for (const Shot& sh : shots_)
        if (now_ < sh.at || now_ - sh.at < sh.snd->size()) shots_[k++] = sh;
    shots_.resize(k);
}
