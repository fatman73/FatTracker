// Engine: owns the song, player and mixer; bridges the audio thread and the UI.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "audio/audio_out.h"
#include "core/song.h"

class Mixer;
class Player;
class Floppy;
struct Sample;

struct ChannelTelemetry {
    float vu;
    bool active, muted;
};

struct Telemetry {
    bool playing;
    uint16_t order, pattern, row;
    uint8_t speed, bpm;
    uint64_t playFrames;
    uint32_t rate;
    int numChannels;
    bool led;  // Amiga filter LED (Pure mode)
    ChannelTelemetry ch[32];
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool startAudio();
    void pollAudio();               // UI timer: reopen if lost, not open, or the default device changed
    void requestAudioReopen() { reopenRequested_ = true; }
    uint32_t audioOpenCount() const { return openCount_; }
    const char* audioStatus() const;  // nullptr when OK

    void setSong(std::unique_ptr<Song> s);
    const Song* song() const { return song_.get(); }
    void play();
    void playPattern(uint16_t pattern);
    void stop();
    void setMute(int ch, bool m);
    void previewNote(int sample, int note, uint32_t offset = 0);  // jam / keyboard preview (works while stopped)
    void setMetronome(bool on, int everyRows);
    void setMixing(int interp, float separation, float gain);  // interp: 0 none, 1 linear, 2 cubic
    void previewStop();
    // Pure mode: 0 off, 1 A500, 2 A1200. Turning it on plays a disk insert.
    void setPure(int model);
    int pure() const { return pure_; }
    void setDriveVolume(float v01);
    void setFloppySounds(const Sample* motor, const Sample* step, const Sample* insert);
    double floppyRead(uint32_t bytes);   // seconds until the simulated read finishes (0 when not in Pure)
    double floppyWrite(uint32_t bytes);
    double floppyDir();

    void lock();    // exclusive, for edits from the UI
    void unlock();
    // Runs f(Song&) under the lock and lets the player adapt (channel count etc.).
    template <class F>
    void edit(F&& f) {
        lock();
        if (song_) {
            f(*song_);
            songEdited();
        }
        publish();
        unlock();
    }
    // Like edit(), but for changes that reallocate sample data or remove objects: playback
    // stops and all mixer voices are dropped before `f` runs.
    template <class F>
    void editStructure(F&& f) {
        lock();
        if (song_) {
            resetPlayer();
            f(*song_);
            resetPlayer();
        }
        publish();
        unlock();
    }
    void play(uint16_t order);
    void render(float* out, uint32_t frames);
    void snapshot(Telemetry& out) const;
    void scope(int ch, float* dst, int n) const;

private:
    static void audioCallback(void* user, float* out, uint32_t frames, uint32_t rate);
    void publish();
    void songEdited();
    void resetPlayer();  // stops playback and drops every voice (lock held)

    void* srw_;  // SRWLOCK storage (pointer-sized)
    std::unique_ptr<Song> song_;
    Mixer* mixer_;
    Player* player_;
    Floppy* floppy_;
    int pure_ = 0;
    float driveGain_ = 0.7f;
    AudioOut audio_;
    DeviceWatcher watcher_;
    IMMDeviceEnumerator* watchEnum_ = nullptr;  // keeps the watcher registered
    bool audioWanted_ = false;
    bool reopenRequested_ = false;
    uint32_t openCount_ = 0;
    uint64_t playFrames_ = 0;
    mutable std::atomic<uint32_t> seq_{0};
    Telemetry tel_{};
};
