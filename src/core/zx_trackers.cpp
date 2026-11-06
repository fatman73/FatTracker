// ZX Spectrum AY tracker modules (STC / ST1, PT2, ASC, SQT) on ayumi. Playback only.
//
// The format replayers are in zx_tracker_replayers.cpp (ports of Sergey Bulba's players by way of ayfly, GPL v2
// or later; structure checks modelled on ZXTune, LGPL v3; see zx_trackers_README.txt). This file drives the
// emulated AY the same way Pt3Player does: the replayer's registers go to ayumi (MIT, src/core/ayumi) once per
// 50 Hz frame, ZX Spectrum 128 clock, ABC stereo.
#include "core/zx_trackers.h"

#include <cmath>
#include <cstring>
#include <string>

#include "core/song.h"
#include "core/zx_tracker_replayers.h"

#include "core/ay_meters.h"

extern "C" {
#include "core/ayumi/ayumi.h"
}

namespace {
constexpr int kHz = 50;
constexpr int kMaxFrames = kHz * 60 * 30;  // the length dry run stops after 30 minutes
constexpr double kClock = 1773400;          // ZX Spectrum 128 AY
}  // namespace

struct ZxTrackerPlayer::Impl {
    std::unique_ptr<zxt::Replayer> rep;
    Kind kind = Kind::None;
    zxt::Ay regs;
    int length = 0;
    std::string format;
    bool loaded = false;

    struct ayumi ay;
    uint32_t rate = 48000;
    double frameLeft = 0;
    AyMeters meters;  // each channel's own output

    void configure() {
        std::memset(&ay, 0, sizeof ay);
        ayumi_configure(&ay, 0, kClock, (int)rate);
        static const double kPan[3] = {0.1, 0.5, 0.9};
        for (int c = 0; c < 3; ++c) ayumi_set_pan(&ay, c, kPan[c], 0);
        rep->reset(regs);
        frameLeft = 0;
    }
    void apply() {
        const uint8_t* r = regs.r;
        for (int c = 0; c < 3; ++c) {
            ayumi_set_tone(&ay, c, r[c * 2] | (r[c * 2 + 1] & 15) << 8);
            ayumi_set_mixer(&ay, c, (r[7] >> c) & 1, (r[7] >> (c + 3)) & 1, (r[8 + c] >> 4) & 1);
            ayumi_set_volume(&ay, c, r[8 + c] & 15);
        }
        ayumi_set_noise(&ay, r[6] & 31);
        ayumi_set_envelope(&ay, r[11] | r[12] << 8);
        if (regs.envShape >= 0) ayumi_set_envelope_shape(&ay, regs.envShape);
    }
};

ZxTrackerPlayer::ZxTrackerPlayer() : impl_(new Impl) {}
ZxTrackerPlayer::~ZxTrackerPlayer() = default;

ZxTrackerPlayer::Kind ZxTrackerPlayer::identify(const uint8_t* d, size_t n) {
    if (!d || n == 0 || n > 0x10000) return Kind::None;
    if (zxt::findSt1(d, n) >= 0) return Kind::St1;
    if (zxt::checkAsc(d, n)) return Kind::Asc;
    if (zxt::checkPt2(d, n)) return Kind::Pt2;
    if (zxt::checkStc(d, n)) return Kind::Stc;
    if (zxt::checkSqt(d, n)) return Kind::Sqt;
    return Kind::None;
}

bool ZxTrackerPlayer::load(const uint8_t* data, size_t size) {
    Impl& m = *impl_;
    m.loaded = false;
    m.rep.reset();
    m.kind = identify(data, size);
    switch (m.kind) {
        case Kind::St1:
            m.rep = zxt::makeSt1(data, size);
            m.format = "ST1 (SOUND TRACKER 1.1)";
            break;
        case Kind::Stc:
            m.rep = zxt::makeStc(data, size);
            m.format = "STC (SOUND TRACKER)";
            break;
        case Kind::Pt2:
            m.rep = zxt::makePt2(data, size);
            m.format = "PT2 (PRO TRACKER 2)";
            break;
        case Kind::Asc:
            m.rep = zxt::makeAsc(data, size);
            m.format = "ASC (ASC SOUND MASTER)";
            break;
        case Kind::Sqt:
            m.rep = zxt::makeSqt(data, size);
            m.format = "SQT (SQ-TRACKER)";
            break;
        case Kind::None: break;
    }
    if (!m.rep) {
        m.kind = Kind::None;
        return false;
    }
    // Song length: play it silently until it passes the end of the position list (capped).
    zxt::Ay r;
    m.rep->reset(r);
    m.length = kMaxFrames;
    for (int f = 0; f < kMaxFrames; ++f)
        if (m.rep->frame(r)) {
            m.length = f;
            break;
        }
    if (m.length <= 0) m.length = 1;
    m.configure();
    m.loaded = true;
    return true;
}

void ZxTrackerPlayer::restart() {
    if (impl_->loaded) impl_->configure();
}

void ZxTrackerPlayer::setRate(uint32_t hz) {
    impl_->rate = hz ? hz : 48000;
    if (impl_->loaded) impl_->configure();
}

void ZxTrackerPlayer::clearMeters() { impl_->meters.clear(); }

void ZxTrackerPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.loaded) return;
    m.meters.begin();
    for (uint32_t f = 0; f < frames; ++f) {
        if (m.frameLeft <= 0) {
            m.rep->frame(m.regs);
            m.apply();
            m.frameLeft += (double)m.rate / kHz;
        }
        m.frameLeft -= 1;
        ayumi_process(&m.ay);
        ayumi_remove_dc(&m.ay);
        out[2 * f] = (float)m.ay.left * gain * 1.4f;
        out[2 * f + 1] = (float)m.ay.right * gain * 1.4f;
        m.meters.sample(m.ay);
    }
    m.meters.end(m.rate);
}

float ZxTrackerPlayer::vu(int v) const { return impl_->meters.vuOf(v); }

void ZxTrackerPlayer::scope(int v, float* dst, int n) const { impl_->meters.scopeOf(v, dst, n); }

double ZxTrackerPlayer::seconds() const { return impl_->loaded ? (double)impl_->length / kHz : 0.0; }
const char* ZxTrackerPlayer::format() const { return impl_->loaded ? impl_->format.c_str() : ""; }
ZxTrackerPlayer::Kind ZxTrackerPlayer::kind() const { return impl_->loaded ? impl_->kind : Kind::None; }
const char* ZxTrackerPlayer::title() const { return impl_->loaded ? impl_->rep->title.c_str() : ""; }
const char* ZxTrackerPlayer::author() const { return impl_->loaded ? impl_->rep->author.c_str() : ""; }
int ZxTrackerPlayer::frames() const { return impl_->loaded ? impl_->length : 0; }

bool ZxTrackerPlayer::buildDisplay(Song& out) {
    if (!impl_->loaded) return false;
    out = makeEmptySong(4);
    return true;
}
