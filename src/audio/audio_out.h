// WASAPI shared-mode, event-driven output on the default render device.
#pragma once
#include <atomic>
#include <cstdint>

// Called on the audio thread. `rate` is the device sample rate.
using RenderFn = void (*)(void* user, float* outStereo, uint32_t frames, uint32_t rate);

class AudioOut {
public:
    AudioOut() = default;
    ~AudioOut() { close(); }
    bool open(RenderFn fn, void* user);  // blocks until the device is initialized (or failed)
    void close();
    uint32_t sampleRate() const { return rate_; }
    bool isOpen() const { return open_; }
    bool lost() const { return lost_.load(); }

private:
    static unsigned long __stdcall threadMain(void* self);
    void run();

    RenderFn fn_ = nullptr;
    void* user_ = nullptr;
    void* thread_ = nullptr;
    void* readyEvent_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> lost_{false};
    bool initOk_ = false;
    bool open_ = false;
    uint32_t rate_ = 48000;
};
