#include "core/gme_player.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/gme/gme.h"
#include "core/inflate.h"
#include "core/song.h"

struct GmePlayer::Impl {
    Music_Emu* emu = nullptr;
    std::vector<uint8_t> file;  // emulators may keep pointers into the loaded data
    uint32_t rate = 48000;
    int track = 0, voices = 4;
    double seconds = 0;
    std::string title, format;
    std::vector<short> buf;
    float level = 0, vu[kMaxVoices] = {};
    float scope[kScopeLen] = {};
    uint32_t scopePos = 0;

    ~Impl() { close(); }
    void close() {
        if (emu) gme_delete(emu);
        emu = nullptr;
    }
    bool open() {
        close();
        if (gme_open_data(file.data(), (long)file.size(), &emu, (int)rate) || !emu) {
            close();
            return false;
        }
        gme_set_autoload_playback_limit(emu, 0);  // keep looping, the tracker decides when to stop
        gme_ignore_silence(emu, 1);
        const int count = gme_track_count(emu);
        if (track >= count) track = 0;
        if (gme_start_track(emu, track)) {
            close();
            return false;
        }
        const int n = gme_voice_count(emu);
        voices = n < 1 ? 4 : (n > kMaxVoices ? kMaxVoices : n);
        return true;
    }
};

GmePlayer::GmePlayer() : impl_(new Impl) {}
GmePlayer::~GmePlayer() = default;

namespace {
constexpr size_t kMaxUnpacked = 64u << 20;
}

bool GmePlayer::detect(const uint8_t* d, size_t n) {
    if (!d || n < 16 || n > kMaxUnpacked) return false;
    if (isGzip(d, n)) {  // .vgz: only the start is needed to see what is inside
        std::vector<uint8_t> head;
        gunzip(d, n, head, 64);
        return head.size() >= 16 && *gme_identify_header(head.data());
    }
    const char* type = gme_identify_header(d);
    return type && *type;
}

bool GmePlayer::load(const uint8_t* data, size_t size, int track) {
    Impl& m = *impl_;
    m.close();
    if (!detect(data, size)) return false;
    if (isGzip(data, size)) {
        if (!gunzip(data, size, m.file, kMaxUnpacked) || m.file.size() < 16) return false;
    } else {
        m.file.assign(data, data + size);
    }
    data = m.file.data();
    m.track = track < 0 ? 0 : track;
    if (!m.open()) return false;
    gme_info_t* info = nullptr;
    m.title.clear();
    m.seconds = 0;
    if (!gme_track_info(m.emu, &info, m.track) && info) {
        m.title = info->song && *info->song ? info->song : (info->game ? info->game : "");
        const int ms = info->length > 0 ? info->length
                     : (info->intro_length >= 0 && info->loop_length > 0 ? info->intro_length + info->loop_length : -1);
        m.seconds = ms > 0 ? ms / 1000.0 : 0.0;
        gme_free_info(info);
    }
    const char* sys = gme_type_system(gme_type(m.emu));
    m.format = std::string(sys ? sys : "GME") + " (" + gme_identify_header(data) + ")";
    for (char& c : m.format) c = (char)toupper((unsigned char)c);
    return true;
}

void GmePlayer::restart() {
    if (impl_->emu) gme_start_track(impl_->emu, impl_->track);
}

void GmePlayer::setRate(uint32_t hz) {
    hz = hz ? hz : 48000;
    if (hz == impl_->rate) return;
    impl_->rate = hz;
    if (impl_->emu) impl_->open();  // the emulator is built for one output rate
}

void GmePlayer::clearMeters() {
    impl_->level = 0;
    std::memset(impl_->vu, 0, sizeof impl_->vu);
    std::memset(impl_->scope, 0, sizeof impl_->scope);
}

void GmePlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.emu) return;
    m.buf.resize((size_t)frames * 2);
    if (gme_track_ended(m.emu)) gme_start_track(m.emu, m.track);
    if (gme_play(m.emu, (int)frames * 2, m.buf.data())) return;
    float peak = 0;
    for (uint32_t f = 0; f < frames; ++f) {
        const float l = m.buf[2 * f] / 32768.f, r = m.buf[2 * f + 1] / 32768.f;
        out[2 * f] = l * gain * 2.f;
        out[2 * f + 1] = r * gain * 2.f;
        const float s = (l + r) * 0.5f;
        m.scope[(m.scopePos + f) & (kScopeLen - 1)] = s * 2.f;
        peak = std::fabs(s) > peak ? std::fabs(s) : peak;
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    const float d = m.level * decay;
    m.level = peak > d ? peak : d;
}

int GmePlayer::voices() const { return impl_->emu ? impl_->voices : 0; }
// GME mixes the voices itself: every voice of the chip shows the output level
float GmePlayer::vu(int v) const { return v >= 0 && v < voices() ? impl_->level * 1.5f : 0.f; }

void GmePlayer::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    const bool on = v >= 0 && v < voices() && v < 4;
    const uint32_t start = (impl_->scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = on ? impl_->scope[(start + k) & (kScopeLen - 1)] : 0.f;
}

double GmePlayer::seconds() const { return impl_->seconds; }
const char* GmePlayer::format() const { return impl_->emu ? impl_->format.c_str() : ""; }
const char* GmePlayer::title() const { return impl_->title.c_str(); }
int GmePlayer::tracks() const { return impl_->emu ? gme_track_count(impl_->emu) : 0; }

bool GmePlayer::buildDisplay(Song& out) {
    if (!impl_->emu) return false;
    out = makeEmptySong(voices() < 4 ? 4 : voices());
    return true;
}
