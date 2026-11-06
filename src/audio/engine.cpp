#include "audio/engine.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

#include "core/floppy.h"
#include "core/song.h"
#include "core/amiga_synth.h"
#include "core/ay_player.h"
#include "core/hippel_player.h"
#include "core/sid_player.h"
#include "core/tfmx_player.h"
#include "core/mixer.h"
#include "core/player.h"

static_assert(sizeof(SRWLOCK) == sizeof(void*), "SRWLOCK storage");
#define SRW reinterpret_cast<PSRWLOCK>(const_cast<void**>(&srw_))

Engine::Engine() : srw_(nullptr), mixer_(new Mixer(48000)), player_(new Player(*mixer_)), floppy_(new Floppy), tfmx_(new TfmxPlayer) {
    InitializeSRWLock(SRW);
    tel_.rate = 48000;
}

Engine::~Engine() {
    if (watchEnum_) {
        watchEnum_->UnregisterEndpointNotificationCallback(&watcher_);
        watchEnum_->Release();
    }
    audio_.close();
    delete player_;
    delete mixer_;
    delete floppy_;
    delete tfmx_;
}

bool Engine::startAudio() {
    audioWanted_ = true;
    if (!watchEnum_ &&
        SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void**)&watchEnum_)) &&
        FAILED(watchEnum_->RegisterEndpointNotificationCallback(&watcher_))) {
        watchEnum_->Release();
        watchEnum_ = nullptr;
    }
    const bool ok = audio_.open(&Engine::audioCallback, this);
    if (ok) ++openCount_;
    return ok;
}

void Engine::pollAudio() {
    if (!audioWanted_) return;
    const bool changed = watcher_.takeChange() || reopenRequested_;
    reopenRequested_ = false;
    if (changed || !audio_.isOpen() || audio_.lost()) {
        audio_.close();
        if (audio_.open(&Engine::audioCallback, this)) ++openCount_;
    }
}

const char* Engine::audioStatus() const {
    if (audio_.isOpen() && !audio_.lost()) return nullptr;
    return "ERROR: NO AUDIO DEVICE";
}

void Engine::audioCallback(void* user, float* out, uint32_t frames, uint32_t rate) {
    Engine* e = static_cast<Engine*>(user);
    if (e->mixer_->rate() != rate) {
        AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&e->srw_));
        e->mixer_->setRate(rate);
        e->floppy_->setRate(rate);
        e->tfmx_->setRate(rate);
        ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&e->srw_));
    }
    e->render(out, frames);
}

void Engine::lock() { AcquireSRWLockExclusive(SRW); }
void Engine::unlock() { ReleaseSRWLockExclusive(SRW); }

void Engine::setSong(std::unique_ptr<Song> s) {
    lock();
    player_->setSong(nullptr);
    tfmxOn_ = tfmxPlaying_ = false;  // a pattern song replaces a TFMX song
    song_ = static_cast<std::unique_ptr<Song>&&>(s);
    player_->setSong(song_.get());
    playFrames_ = 0;
    publish();
    unlock();
}

void Engine::play() { play(0); }

void Engine::play(uint16_t order) {
    lock();
    if (tfmxOn_) {
        tfmx_->restart();
        tfmxPlaying_ = true;
    } else {
        player_->play(order);
    }
    playFrames_ = 0;
    publish();
    unlock();
}

void Engine::songEdited() { player_->songEdited(); }

void Engine::resetPlayer() { player_->setSong(song_.get()); }

void Engine::setMetronome(bool on, int everyRows) {
    lock();
    player_->setMetronome(on, everyRows);
    unlock();
}

void Engine::setMixing(int interp, float separation, float gain) {
    lock();
    mixer_->setInterp(interp == 0 ? Interp::None : interp == 1 ? Interp::Linear : Interp::Cubic);
    mixer_->setSeparation(separation);
    mixer_->setMasterGain(gain);
    unlock();
}

void Engine::previewNote(int sample, int note, uint32_t offset) {
    lock();
    player_->previewNote(sample, note, offset);
    unlock();
}

bool Engine::loadTfmx(const uint8_t* mdat, size_t mdatSize, const uint8_t* smpl, size_t smplSize, const char* name,
                      bool play) {
    TfmxPlayer* p = new TfmxPlayer;
    if (!p->load(mdat, mdatSize, smpl, smplSize)) {
        delete p;
        return false;
    }
    return installChip(p, name, play);
}

bool Engine::loadHippel(const uint8_t* data, size_t size, const char* name, bool play) {
    HippelPlayer* p = new HippelPlayer;
    if (!p->load(data, size)) {
        delete p;
        return false;
    }
    return installChip(p, name, play);
}

bool Engine::loadSid(const uint8_t* data, size_t size, const char* name, bool play) {
    SidPlayer* p = new SidPlayer;
    p->setRate(mixer_->rate());  // the emulated C64 is built for one output rate
    if (!p->load(data, size)) {
        delete p;
        return false;
    }
    return installChip(p, name && *name ? name : p->title(), play);
}

bool Engine::loadAy(const uint8_t* data, size_t size, const char* name, bool play) {
    AyPlayer* p = new AyPlayer;
    p->setRate(mixer_->rate());
    if (!p->load(data, size)) {
        delete p;
        return false;
    }
    return installChip(p, *p->title() ? p->title() : name, play);
}

bool Engine::loadAmigaSynth(const uint8_t* data, size_t size, const char* name, bool play) {
    std::unique_ptr<AmigaSynthPlayer> p = AmigaSynthPlayer::open(data, size);
    if (!p) return false;
    return installChip(p.release(), name, play);
}

bool Engine::installChip(ChipPlayer* p, const char* name, bool play) {
    auto song = std::make_unique<Song>(makeEmptySong(4));
    p->buildDisplay(*song);  // what the pattern view shows (playback only)
    for (int i = 0; i < 20 && name && name[i]; ++i) song->name[i] = name[i];
    setSong(static_cast<std::unique_ptr<Song>&&>(song));
    lock();
    p->setRate(mixer_->rate());  // under the lock: the audio thread may have changed the rate meanwhile
    delete tfmx_;
    tfmx_ = p;
    tfmxOn_ = true;
    tfmxPlaying_ = play;
    if (!play) tfmx_->clearMeters();
    playFrames_ = 0;
    publish();
    unlock();
    return true;
}

const char* Engine::tfmxFormat() const { return tfmxOn_ ? tfmx_->format() : ""; }

void Engine::setPure(int model) {
    lock();
    const int m = model < 0 || model > 2 ? 0 : model;
    if (m && !pure_) floppy_->insert();
    if (!m && pure_) floppy_->reset();
    pure_ = m;
    mixer_->setPaula(m == 1 ? PaulaModel::A500 : m == 2 ? PaulaModel::A1200 : PaulaModel::None);
    unlock();
}

void Engine::setDriveVolume(float v01) {
    lock();
    driveGain_ = v01 < 0 ? 0 : (v01 > 1 ? 1 : v01);
    unlock();
}

void Engine::setFloppySounds(const Sample* motor, const Sample* step, const Sample* insert) {
    lock();
    floppy_->setSounds(motor, step, insert);
    unlock();
}

double Engine::floppyRead(uint32_t bytes) {
    lock();
    const double t = pure_ ? floppy_->read(bytes) : 0.0;
    unlock();
    return t;
}

double Engine::floppyWrite(uint32_t bytes) {
    lock();
    const double t = pure_ ? floppy_->write(bytes) : 0.0;
    unlock();
    return t;
}

void Engine::floppyCancel() {
    lock();
    floppy_->cancel();
    unlock();
}

double Engine::floppyDir() {
    lock();
    const double t = pure_ ? floppy_->dir() : 0.0;
    unlock();
    return t;
}

void Engine::previewStop() {
    lock();
    player_->previewStop();
    unlock();
}

void Engine::playPattern(uint16_t pattern) {
    lock();
    if (tfmxOn_) {
        tfmx_->restart();
        tfmxPlaying_ = true;
    } else {
        player_->playPattern(pattern);
    }
    playFrames_ = 0;
    publish();
    unlock();
}

void Engine::stop() {
    lock();
    tfmxPlaying_ = false;
    tfmx_->clearMeters();  // nothing renders while stopped: let the meters fall
    player_->stop();
    publish();
    unlock();
}

void Engine::setMute(int ch, bool m) {
    lock();
    player_->setMute(ch, m);
    publish();
    unlock();
}

void Engine::render(float* out, uint32_t frames) {
    lock();
    if (tfmxOn_) {
        if (tfmxPlaying_) {
            tfmx_->render(out, frames, 0.45f);
            playFrames_ += frames;
        } else {
            std::memset(out, 0, sizeof(float) * 2 * frames);
        }
    } else if (song_) {
        player_->render(out, frames);
        if (player_->playing()) playFrames_ += frames;
    } else {
        std::memset(out, 0, sizeof(float) * 2 * frames);
    }
    if (pure_) {
        floppy_->mix(out, frames, driveGain_);  // mechanical noise: not through Paula's filters
        for (uint32_t k = 0; k < frames * 2; ++k) out[k] = out[k] > 1.f ? 1.f : (out[k] < -1.f ? -1.f : out[k]);
    }
    publish();
    unlock();
}

// Seqlock writer: called with the exclusive lock held.
void Engine::publish() {
    seq_.fetch_add(1, std::memory_order_acq_rel);  // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    PlayerPos p = player_->pos();
    tel_.playing = player_->playing();
    tel_.order = p.order;
    tel_.pattern = p.pattern;
    tel_.row = p.row;
    tel_.speed = p.speed;
    tel_.bpm = p.bpm;
    tel_.playFrames = playFrames_;
    tel_.rate = mixer_->rate();
    tel_.numChannels = song_ ? song_->numChannels : 0;
    tel_.led = mixer_->led();
    tel_.drive = pure_ && floppy_->busySeconds() > 0;
    if (tfmxOn_) {
        tel_.playing = tfmxPlaying_;
        // the display song has one 64-row pattern per 64 ticks, in order
        const int tick = tfmx_->tick();
        const int pats = song_ && !song_->patterns.empty() ? (int)song_->patterns.size() : 1;
        int pat = tick / ChipPlayer::kDisplayRows;
        if (pat >= pats) pat = pats - 1;
        tel_.order = tel_.pattern = (uint16_t)pat;
        tel_.row = (uint16_t)(tick / ChipPlayer::kDisplayRows >= pats ? ChipPlayer::kDisplayRows - 1 : tick % ChipPlayer::kDisplayRows);
    }
    for (int c = 0; c < 32; ++c) {
        tel_.ch[c].vu = tfmxOn_ ? (c < ChipPlayer::kMaxVoices ? tfmx_->vu(c) : 0.f) : mixer_->vu(c);
        tel_.ch[c].active = mixer_->active(c);
        tel_.ch[c].muted = player_->channel(c).muted;
    }
    std::atomic_thread_fence(std::memory_order_release);
    seq_.fetch_add(1, std::memory_order_acq_rel);  // even: stable
}

void Engine::snapshot(Telemetry& out) const {
    for (;;) {
        uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1) {
            YieldProcessor();
            continue;
        }
        std::memcpy(&out, (const void*)&tel_, sizeof out);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_acquire) == s1) return;
    }
}

void Engine::scope(int ch, float* dst, int n) const {
    AcquireSRWLockShared(SRW);
    if (tfmxOn_) tfmx_->scope(ch, dst, n);
    else mixer_->scope(ch, dst, n);
    ReleaseSRWLockShared(SRW);
}
