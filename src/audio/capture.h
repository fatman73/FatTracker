// Recording from the default audio input for the sampler's SAMPLE button: WASAPI shared mode on Windows
// (src/audio/capture.cpp), AVAudioEngine on macOS (src/mac/capture_mac.mm). Mono float frames are collected
// until stop() or the time limit.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

#ifdef _WIN32
class AudioCapture {
public:
    static constexpr double kMaxSeconds = 60.0;

    AudioCapture() = default;
    ~AudioCapture() { stop(); }
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool start();      // false: no input device or it could not be opened
    void stop();       // waits for the worker thread
    bool running() const { return running_.load(); }
    bool full() const { return full_.load(); }   // reached kMaxSeconds
    bool lost() const { return lost_.load(); }   // the input device went away
    float level() const { return level_.load(); }  // recent peak 0..1 (for the status line)
    double seconds() const;
    uint32_t rate() const { return rate_; }
    std::vector<float> take();  // the recording (after stop)
    const char* error() const { return nullptr; }  // why start() failed, when the system says more

private:
    static unsigned long __stdcall threadMain(void* self);
    void run();

    void* thread_ = nullptr;
    void* readyEvent_ = nullptr;
    std::atomic<bool> stop_{false}, running_{false}, full_{false}, lost_{false};
    std::atomic<float> level_{0.f};
    std::atomic<size_t> frames_{0};
    bool initOk_ = false;
    uint32_t rate_ = 48000;
    std::vector<float> data_;  // written by the worker only while running
};
#else
class AudioCapture {
public:
    static constexpr double kMaxSeconds = 60.0;

    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool start();
    void stop();
    bool running() const;
    bool full() const;
    bool lost() const;
    float level() const;
    double seconds() const;
    uint32_t rate() const;
    std::vector<float> take();
    const char* error() const;  // e.g. the microphone permission is missing

private:
    struct Impl;
    Impl* impl_;
};
#endif
