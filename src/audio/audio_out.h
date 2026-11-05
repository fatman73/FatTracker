// WASAPI shared-mode, event-driven output on the default render device.
#pragma once
#include <atomic>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>

// Receives Windows audio endpoint notifications; flags a change of the default
// render (console) device so the engine can reopen its output. Callbacks arrive on a
// system thread, so only an atomic flag is touched. Lifetime is owned by Engine.
class DeviceWatcher : public IMMNotificationClient {
public:
    bool takeChange() { return changed_.exchange(false); }
    // IUnknown (not reference-counted: owned by its creator)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) changed_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<bool> changed_{false};
};

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
