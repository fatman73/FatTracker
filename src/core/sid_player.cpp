#include "core/sid_player.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "core/song.h"

#include "core/crsid/libcRSID.h"  // compiled as C++ (see CMakeLists.txt)

struct SidPlayer::Impl {
    cRSID_C64instance* c64 = nullptr;
    std::vector<uint8_t> file;  // cRSID keeps a pointer to the header: the data stays here
    cRSID_SIDheader* header = nullptr;
    uint32_t rate = 48000;
    int subtune = 0;
    int voices = 3;
    char format[32] = "C64 SID";
    char title[33] = {};
    float vu[kMaxVoices] = {};
    float scope[kScopeLen] = {};
    uint32_t scopePos = 0;

    ~Impl() { release(); }
    void release() {
        if (c64) cRSID_free(c64);
        c64 = nullptr;
        header = nullptr;
    }
    bool start() {  // (re)build the C64 at the current rate and init the tune
        release();
        c64 = cRSID_init((unsigned short)rate, 0);
        if (!c64) return false;
        header = cRSID_processSIDfile(c64, file.data(), (int)file.size());
        if (!header) return false;
        cRSID_initSIDtune(c64, header, (char)subtune);
        int chips = 1;
        for (int s = 2; s <= 3; ++s)
            if (c64->SID[s].BaseAddress) ++chips;
        voices = chips * 3 > kMaxVoices ? kMaxVoices : chips * 3;
        return true;
    }
};

SidPlayer::SidPlayer() : impl_(new Impl) {}
SidPlayer::~SidPlayer() = default;

bool SidPlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 0x7C || n > 0x10000 + 0x7C + 2) return false;
    if (!((d[0] == 'P' || d[0] == 'R') && d[1] == 'S' && d[2] == 'I' && d[3] == 'D')) return false;
    const int version = d[5], header = d[7];
    if (version < 1 || version > 4 || d[4] || d[6]) return false;
    if (header != (version == 1 ? 0x76 : 0x7C)) return false;
    // a PRG style load address (0 in the header) needs its two bytes after the header
    return (size_t)header + ((d[8] | d[9]) ? 1 : 3) <= n;
}

bool SidPlayer::load(const uint8_t* data, size_t size, int subtune) {
    Impl& m = *impl_;
    m.release();
    if (!detect(data, size)) return false;
    m.file.assign(data, data + size);
    const int count = data[0x0F] | (data[0x0E] << 8);
    m.subtune = subtune > 0 ? subtune : (data[0x11] ? data[0x11] : 1);
    if (count > 0 && m.subtune > count) m.subtune = count;
    if (m.subtune > 255) m.subtune = 255;
    std::memcpy(m.title, data + 0x16, 32);
    m.title[32] = 0;
    for (char& c : m.title)
        if (c && (c < 32 || c > 126)) c = ' ';
    std::strcpy(m.format, data[0] == 'R' ? "C64 RSID" : "C64 PSID");
    if (!m.start()) {
        m.release();
        return false;
    }
    std::strcat(m.format, m.voices > 3 ? (m.voices > 6 ? " 3SID" : " 2SID") : "");
    return true;
}

void SidPlayer::restart() {
    if (impl_->c64) impl_->start();
}

void SidPlayer::setRate(uint32_t hz) {
    hz = hz ? hz : 48000;
    if (hz == impl_->rate) return;
    impl_->rate = hz;
    if (impl_->c64) impl_->start();
}

void SidPlayer::clearMeters() {
    std::memset(impl_->vu, 0, sizeof impl_->vu);
    std::memset(impl_->scope, 0, sizeof impl_->scope);
}

void SidPlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.c64) return;
    for (uint32_t f = 0; f < frames; ++f) {
        const float s = cRSID_generateSample(m.c64) / 32768.f;
        out[2 * f] = out[2 * f + 1] = s * gain * 1.6f;
        m.scope[(m.scopePos + f) & (kScopeLen - 1)] = s * 2.f;
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
    // meters: the envelope level of each voice, scaled by the master volume
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    for (int v = 0; v < m.voices; ++v) {
        cRSID_SIDinstance& sid = m.c64->SID[1 + v / 3];
        const float master = (sid.BasePtr ? (sid.BasePtr[0x18] & 15) : 15) / 15.f;
        const float level = sid.EnvelopeCounter[(v % 3) * 7] / 255.f * master;
        const float d = m.vu[v] * decay;
        m.vu[v] = level > d ? level : d;
    }
}

int SidPlayer::voices() const { return impl_->c64 ? impl_->voices : 0; }
float SidPlayer::vu(int v) const { return v >= 0 && v < kMaxVoices ? impl_->vu[v] * 0.6f : 0.f; }

void SidPlayer::scope(int v, float* dst, int n) const {
    if (v < 0 || v >= kMaxVoices || n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    if (v >= voices()) {  // no such SID voice: a flat line
        for (int k = 0; k < n; ++k) dst[k] = 0;
        return;
    }
    const uint32_t start = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = impl_->scope[(start + k) & (kScopeLen - 1)];
}

const char* SidPlayer::format() const { return impl_->c64 ? impl_->format : ""; }
const char* SidPlayer::title() const { return impl_->title; }
int SidPlayer::subtunes() const { return impl_->file.size() > 0x10 ? (impl_->file[0x0F] | (impl_->file[0x0E] << 8)) : 0; }

bool SidPlayer::buildDisplay(Song& out) {
    if (!impl_->c64) return false;
    out = makeEmptySong(voices() < 4 ? 4 : voices());
    return true;
}
