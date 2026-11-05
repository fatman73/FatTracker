#include "audio/audio_out.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>

#include <vector>

namespace {
template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}  // namespace

bool AudioOut::open(RenderFn fn, void* user) {
    close();
    fn_ = fn;
    user_ = user;
    stop_ = false;
    lost_ = false;
    initOk_ = false;
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, &AudioOut::threadMain, this, 0, nullptr);
    if (!thread_) {
        CloseHandle(readyEvent_);
        readyEvent_ = nullptr;
        return false;
    }
    WaitForSingleObject(readyEvent_, 5000);
    open_ = initOk_;
    if (!open_) close();
    return open_;
}

void AudioOut::close() {
    stop_ = true;
    if (thread_) {
        WaitForSingleObject(thread_, 3000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (readyEvent_) {
        CloseHandle(readyEvent_);
        readyEvent_ = nullptr;
    }
    open_ = false;
}

unsigned long __stdcall AudioOut::threadMain(void* self) {
    static_cast<AudioOut*>(self)->run();
    return 0;
}

void AudioOut::run() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInit = SUCCEEDED(hr);
    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* fmt = nullptr;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE mmcss = nullptr;
    UINT32 bufferFrames = 0;
    bool isFloat = false;
    int channels = 2;
    std::vector<float> tmp;

    auto fail = [&]() {
        initOk_ = false;
        lost_ = true;
    };

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumr);
    if (SUCCEEDED(hr)) hr = enumr->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client);
    if (SUCCEEDED(hr)) hr = client->GetMixFormat(&fmt);
    if (SUCCEEDED(hr)) {
        channels = fmt->nChannels;
        WORD tag = fmt->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE) tag = (WORD)((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat.Data1;
        isFloat = tag == WAVE_FORMAT_IEEE_FLOAT && fmt->wBitsPerSample == 32;
        bool isPcm16 = tag == WAVE_FORMAT_PCM && fmt->wBitsPerSample == 16;
        if (!isFloat && !isPcm16) hr = E_FAIL;
    }
    if (SUCCEEDED(hr))
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 100000, 0, fmt, nullptr);
    if (SUCCEEDED(hr)) hr = client->SetEventHandle(ev);
    if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioRenderClient), (void**)&render);
    if (SUCCEEDED(hr)) {
        rate_ = fmt->nSamplesPerSec;
        tmp.resize((size_t)bufferFrames * 2);
        initOk_ = true;
    } else {
        fail();
    }
    SetEvent(readyEvent_);

    if (initOk_) {
        DWORD taskIndex = 0;
        mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        hr = client->Start();
        if (FAILED(hr)) lost_ = true;
        while (!stop_ && !lost_) {
            DWORD w = WaitForSingleObject(ev, 200);
            if (stop_) break;
            if (w != WAIT_OBJECT_0) continue;
            UINT32 padding = 0;
            hr = client->GetCurrentPadding(&padding);
            if (FAILED(hr)) {
                lost_ = true;
                break;
            }
            UINT32 avail = bufferFrames - padding;
            if (!avail) continue;
            BYTE* data = nullptr;
            hr = render->GetBuffer(avail, &data);
            if (FAILED(hr)) {
                lost_ = true;
                break;
            }
            fn_(user_, tmp.data(), avail, rate_);
            if (isFloat) {
                float* o = (float*)data;
                for (UINT32 f = 0; f < avail; ++f)
                    for (int c = 0; c < channels; ++c) o[f * channels + c] = c < 2 ? tmp[f * 2 + c] : 0.f;
            } else {
                int16_t* o = (int16_t*)data;
                for (UINT32 f = 0; f < avail; ++f)
                    for (int c = 0; c < channels; ++c) {
                        float v = c < 2 ? tmp[f * 2 + c] : 0.f;
                        v = v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
                        o[f * channels + c] = (int16_t)(v * 32767.f);
                    }
            }
            hr = render->ReleaseBuffer(avail, 0);
            if (FAILED(hr)) lost_ = true;
        }
        client->Stop();
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    }
    if (fmt) CoTaskMemFree(fmt);
    release(render);
    release(client);
    release(dev);
    release(enumr);
    CloseHandle(ev);
    if (comInit) CoUninitialize();
}
