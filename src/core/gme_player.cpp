#include "core/gme_player.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#include "core/gme/gme.h"
#include "core/inflate.h"
#include "core/song.h"

#pragma warning(push, 1)  // Game_Music_Emu's own headers (built with /W1)
#include "core/gme/Effects_Buffer.h"
#include "core/gme/Music_Emu.h"
#include "core/gme/Spc_Emu.h"
#include "core/gme/Vgm_Emu.h"
#include "core/gme/Voice_Tap_Buffer.h"
#pragma warning(pop)

namespace {
// The emulators built on Classic_Emu (Blip_Buffer voices): their voices can be given buffers of their own.
bool tappable(gme_type_t t) {
    return t == gme_ay_type || t == gme_gbs_type || t == gme_hes_type || t == gme_kss_type || t == gme_nsf_type ||
           t == gme_nsfe_type || t == gme_sap_type || t == gme_vgm_type || t == gme_vgz_type;
}
constexpr float kScopeGain = 2.f;            // a voice's output (1.0 = a full scale sample) on its scope
constexpr float kVuGain = 1.5f;              // ... and on its meter (its peak)
constexpr float kFmFull = 1.f / 1536.f;      // a YM2612 channel at full level (Nuked, left + right) to 1.0
}  // namespace

struct GmePlayer::Impl {
    // How the voices are metered: each from its own Blip_Buffers (the Classic_Emu formats), from the SNES DSP's
    // voice outputs, from the YM2612's channels plus the PCM / PSG buffers (Mega Drive VGM), or (where the
    // voices cannot be told apart: GYM, YM2413 VGM) all from the mixed output.
    enum class Meters { Mix, Taps, Spc, Fm };
    Music_Emu* emu = nullptr;
    Meters meters = Meters::Mix;
    // the emulators write into these: they outlive emu
    std::unique_ptr<Voice_Tap_Buffer> taps;
    std::unique_ptr<Spc_Dsp::ft_voice_taps_t> spcTaps;
    std::unique_ptr<Ym2612_Emu::ft_voice_taps_t> fmTaps;
    std::unique_ptr<Vgm_Emu::ft_voice_taps_t> vgmTaps;
    std::vector<uint8_t> file;  // emulators may keep pointers into the loaded data
    size_t size = 0;            // of the data in file (the rest is padding)
    uint32_t rate = 48000;
    int track = 0, voices = 4;
    double seconds = 0;
    std::string title, trackTitle, format;
    std::vector<short> buf;
    float level = 0, vu[kMaxVoices] = {};
    float scope[kScopeLen] = {};                   // the mix (Meters::Mix)
    float voiceScope[kMaxVoices][kScopeLen] = {};  // each voice's own
    uint32_t scopePos = 0;
    float vpeak[kMaxVoices] = {};  // of the block being rendered
    uint32_t skip = 0;             // frames of the block before the last kScopeLen (not on the scopes)

    ~Impl() { close(); }
    void close() {
        if (emu) gme_delete(emu);
        emu = nullptr;
        taps.reset();
        spcTaps.reset();
        fmTaps.reset();
        vgmTaps.reset();
        meters = Meters::Mix;
    }
    bool open() {
        close();
        const gme_type_t type = size >= 4 ? gme_identify_extension(gme_identify_header(file.data())) : nullptr;
        if (!type) return false;
        if (tappable(type)) {  // as gme_open_data builds it (Effects_Buffer(1)), with every voice tapped
            Effects_Buffer* inner = new (std::nothrow) Effects_Buffer(1);
            if (inner) taps.reset(new (std::nothrow) Voice_Tap_Buffer(inner));
            if (!taps) delete inner;
            emu = taps ? type->new_emu() : nullptr;
            if (!emu) {
                close();
                return false;
            }
            emu->set_buffer(taps.get());
            if (emu->set_sample_rate((long)rate) || gme_load_data(emu, file.data(), (long)size)) {
                close();
                return false;
            }
            meters = Meters::Taps;
            if (type == gme_vgm_type || type == gme_vgz_type) {
                Vgm_Emu* vgm = static_cast<Vgm_Emu*>(emu);
                if (!vgm->is_classic_emu()) {  // FM: mixed by Dual_Resampler, not through the voice buffers
                    meters = Meters::Mix;
                    if (vgm->ft_uses_ym2612()) {
                        fmTaps.reset(new (std::nothrow) Ym2612_Emu::ft_voice_taps_t());
                        vgmTaps.reset(new (std::nothrow) Vgm_Emu::ft_voice_taps_t());
                        if (fmTaps && vgmTaps) {
                            vgm->ft_set_voice_taps(vgmTaps.get(), fmTaps.get());
                            meters = Meters::Fm;
                        }
                    }
                }
            }
        } else if (gme_open_data(file.data(), (long)size, &emu, (int)rate) || !emu) {
            close();
            return false;
        }
        if (type == gme_spc_type) {
            spcTaps.reset(new (std::nothrow) Spc_Dsp::ft_voice_taps_t());
            if (spcTaps) {
                static_cast<Spc_Emu*>(emu)->ft_apu()->ft_dsp()->ft_set_voice_taps(spcTaps.get());
                meters = Meters::Spc;
            }
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
    void readInfo() {  // title and length of the track
        gme_info_t* info = nullptr;
        title.clear();
        trackTitle.clear();
        seconds = 0;
        if (!gme_track_info(emu, &info, track) && info) {
            trackTitle = info->song ? info->song : "";
            title = !trackTitle.empty() ? trackTitle : (info->game ? info->game : "");
            const int ms = info->length > 0 ? info->length
                         : (info->intro_length >= 0 && info->loop_length > 0 ? info->intro_length + info->loop_length : -1);
            seconds = ms > 0 ? ms / 1000.0 : 0.0;
            gme_free_info(info);
        }
    }

    // ---- per-voice meters of a block of 'frames' (render)
    void put(int v, uint32_t f, float x) {
        vpeak[v] = std::fabs(x) > vpeak[v] ? std::fabs(x) : vpeak[v];
        if (f >= skip) voiceScope[v][(scopePos + f) & (kScopeLen - 1)] = x * kScopeGain;
    }
    // Voice v from a ring at the output rate that holds the block as its last 'got' entries before 'end'.
    template <class At>
    void fromOutputRate(int v, uint32_t frames, unsigned end, unsigned got, unsigned ringSize, At at) {
        for (uint32_t f = 0; f < frames; ++f) {
            const uint32_t back = frames - f;  // samples before the end
            put(v, f, back <= got && back <= ringSize ? at((end - back) & (ringSize - 1)) : 0.f);
        }
    }
    // Voices [first, first + n) from a ring at the chip's own rate (the block: 'got' entries before 'end'),
    // stretched over the frames.
    template <class At>
    void fromChipRate(int first, int n, uint32_t frames, unsigned end, unsigned got, unsigned ringSize, At at) {
        if (got > ringSize) got = ringSize;
        for (uint32_t f = 0; f < frames; ++f) {
            const unsigned i = (end - got + (unsigned)((uint64_t)f * got / frames)) & (ringSize - 1);
            for (int v = first; v < first + n && v < voices; ++v) put(v, f, got ? at(i, v - first) : 0.f);
        }
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
    if (std::memcmp(d, "ZXAY", 4) == 0 && std::memcmp(d + 4, "EMUL", 4) != 0) return false;  // ZXAYST11 etc.: not GME's
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
    m.size = m.file.size();
    m.file.resize(m.size + 4096, 0);  // zero padding: short over-reads in the emulators stay inside the buffer
    data = m.file.data();             // (after the resize, which may move the buffer)
    m.track = track < 0 ? 0 : track;
    if (!m.open()) return false;
    m.readInfo();
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
    std::memset(impl_->voiceScope, 0, sizeof impl_->voiceScope);
}

void GmePlayer::render(float* out, uint32_t frames, float gain) {
    std::memset(out, 0, sizeof(float) * 2 * frames);
    Impl& m = *impl_;
    if (!m.emu) return;
    m.buf.resize((size_t)frames * 2);
    if (gme_track_ended(m.emu)) gme_start_track(m.emu, m.track);
    const unsigned tapsBefore = m.taps ? m.taps->written() : 0;
    const unsigned spcBefore = m.spcTaps ? m.spcTaps->pos : 0;
    const unsigned fmBefore = m.fmTaps ? m.fmTaps->pos : 0;
    const unsigned vgmBefore = m.vgmTaps ? m.vgmTaps->pos : 0;
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
    const float decay = (float)std::pow(0.5, (double)frames / (m.rate * 0.08));
    const float d = m.level * decay;
    m.level = peak > d ? peak : d;
    if (m.meters != Impl::Meters::Mix) {  // each voice its own output
        std::memset(m.vpeak, 0, sizeof m.vpeak);
        m.skip = frames > (uint32_t)kScopeLen ? frames - kScopeLen : 0;
        switch (m.meters) {
            case Impl::Meters::Taps: {
                const unsigned end = m.taps->written();
                const int nv = m.voices < m.taps->taps() ? m.voices : m.taps->taps();
                for (int v = 0; v < nv; ++v) {
                    const float* ring = m.taps->ring(v);
                    m.fromOutputRate(v, frames, end, end - tapsBefore, Voice_Tap_Buffer::ring_size,
                                     [ring](unsigned i) { return ring[i]; });
                }
                break;
            }
            case Impl::Meters::Spc: {  // the DSP's 32 kHz voice outputs
                const Spc_Dsp::ft_voice_taps_t& t = *m.spcTaps;
                m.fromChipRate(0, Spc_Dsp::voice_count, frames, t.pos, t.pos - spcBefore, t.size,
                               [&t](unsigned i, int v) { return t.out[i][v] * (1.f / 32768.f); });
                break;
            }
            case Impl::Meters::Fm: {  // FM 1-6 from the YM2612 (~53 kHz), PCM and PSG at the output rate
                const Ym2612_Emu::ft_voice_taps_t& fm = *m.fmTaps;
                m.fromChipRate(0, Ym2612_Emu::channel_count, frames, fm.pos, fm.pos - fmBefore, fm.size,
                               [&fm](unsigned i, int v) { return fm.out[i][v] * kFmFull; });
                const Vgm_Emu::ft_voice_taps_t& t = *m.vgmTaps;
                for (int k = 0; k < 2 && 6 + k < m.voices; ++k)
                    m.fromOutputRate(6 + k, frames, t.pos, t.pos - vgmBefore, t.size,
                                     [&t, k](unsigned i) { return t.out[i][k]; });
                break;
            }
            default: break;
        }
        for (int v = 0; v < kMaxVoices; ++v) {
            const float dv = m.vu[v] * decay;
            m.vu[v] = m.vpeak[v] > dv ? m.vpeak[v] : dv;
        }
    }
    m.scopePos = (m.scopePos + frames) & (kScopeLen - 1);
}

int GmePlayer::voices() const { return impl_->emu ? impl_->voices : 0; }

float GmePlayer::vu(int v) const {
    if (v < 0 || v >= voices()) return 0.f;
    // where the voices cannot be told apart (GYM, YM2413 VGM) every voice shows the output level
    return (impl_->meters == Impl::Meters::Mix ? impl_->level : impl_->vu[v]) * kVuGain;
}

void GmePlayer::scope(int v, float* dst, int n) const {
    if (n <= 0) return;
    if (n > kScopeLen) n = kScopeLen;
    const Impl& m = *impl_;
    const bool mix = m.meters == Impl::Meters::Mix;
    const bool on = v >= 0 && v < voices() && (!mix || v < 4);
    const float* src = on ? (mix ? m.scope : m.voiceScope[v]) : nullptr;
    const uint32_t start = (m.scopePos - (uint32_t)n) & (kScopeLen - 1);
    for (int k = 0; k < n; ++k) dst[k] = src ? src[(start + k) & (kScopeLen - 1)] : 0.f;
}

double GmePlayer::seconds() const { return impl_->seconds; }
const char* GmePlayer::format() const { return impl_->emu ? impl_->format.c_str() : ""; }
const char* GmePlayer::title() const { return impl_->title.c_str(); }
int GmePlayer::tracks() const { return impl_->emu ? gme_track_count(impl_->emu) : 0; }

int GmePlayer::subsongs() const {
    const int n = tracks();
    return n > 0 ? n : 1;
}

int GmePlayer::subsong() const { return impl_->emu ? impl_->track : 0; }

bool GmePlayer::selectSubsong(int i) {
    Impl& m = *impl_;
    if (!m.emu || i < 0 || i >= tracks()) return false;
    if (gme_start_track(m.emu, i)) {
        gme_start_track(m.emu, m.track);  // (it started before)
        return false;
    }
    m.track = i;
    m.readInfo();
    return true;
}

const char* GmePlayer::subsongTitle() const { return impl_->emu ? impl_->trackTitle.c_str() : ""; }

bool GmePlayer::buildDisplay(Song& out) {
    if (!impl_->emu) return false;
    out = makeEmptySong(voices() < 4 ? 4 : voices());
    return true;
}
