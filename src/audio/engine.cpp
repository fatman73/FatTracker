#include "audio/engine.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

#include "core/floppy.h"
#include "core/mixer.h"
#include "core/player.h"

static_assert(sizeof(SRWLOCK) == sizeof(void*), "SRWLOCK storage");
#define SRW reinterpret_cast<PSRWLOCK>(const_cast<void**>(&srw_))

Engine::Engine() : srw_(nullptr), mixer_(new Mixer(48000)), player_(new Player(*mixer_)), floppy_(new Floppy) {
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
        ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(&e->srw_));
    }
    e->render(out, frames);
}

void Engine::lock() { AcquireSRWLockExclusive(SRW); }
void Engine::unlock() { ReleaseSRWLockExclusive(SRW); }

void Engine::setSong(std::unique_ptr<Song> s) {
    lock();
    player_->setSong(nullptr);
    song_ = static_cast<std::unique_ptr<Song>&&>(s);
    player_->setSong(song_.get());
    playFrames_ = 0;
    publish();
    unlock();
}

void Engine::play() { play(0); }

void Engine::play(uint16_t order) {
    lock();
    player_->play(order);
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
    player_->playPattern(pattern);
    playFrames_ = 0;
    publish();
    unlock();
}

void Engine::stop() {
    lock();
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
    if (song_) {
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
    for (int c = 0; c < 32; ++c) {
        tel_.ch[c].vu = mixer_->vu(c);
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
    mixer_->scope(ch, dst, n);
    ReleaseSRWLockShared(SRW);
}
