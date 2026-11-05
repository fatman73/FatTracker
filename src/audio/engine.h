// Engine: owns the song, player and mixer; bridges the audio thread and the UI.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "audio/audio_out.h"
#include "core/song.h"

class Mixer;
class Player;

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

    void lock();    // exclusive, for edits from the UI
    void unlock();
    void render(float* out, uint32_t frames);
    void snapshot(Telemetry& out) const;
    void scope(int ch, float* dst, int n) const;

private:
    static void audioCallback(void* user, float* out, uint32_t frames, uint32_t rate);
    void publish();

    void* srw_;  // SRWLOCK storage (pointer-sized)
    std::unique_ptr<Song> song_;
    Mixer* mixer_;
    Player* player_;
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
