#include "audio/capture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <cmath>
#include <cstring>

namespace {
template <class T>
void rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}  // namespace

bool AudioCapture::start() {
    if (running_) return true;
    stop();
    data_.clear();
    frames_ = 0;
    full_ = false;
    lost_ = false;
    stop_ = false;
    initOk_ = false;
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, &AudioCapture::threadMain, this, 0, nullptr);
    if (!thread_) {
        CloseHandle(readyEvent_);
        readyEvent_ = nullptr;
        return false;
    }
    WaitForSingleObject(readyEvent_, 5000);
    CloseHandle(readyEvent_);
    readyEvent_ = nullptr;
    if (!initOk_) {
        stop();
        return false;
    }
    return true;
}

void AudioCapture::stop() {
    stop_ = true;
    if (thread_) {
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    running_ = false;
}

double AudioCapture::seconds() const { return rate_ ? (double)frames_.load() / rate_ : 0.0; }

std::vector<float> AudioCapture::take() {
    stop();
    std::vector<float> out = static_cast<std::vector<float>&&>(data_);
    data_.clear();
    frames_ = 0;
    return out;
}

unsigned long __stdcall AudioCapture::threadMain(void* self) {
    static_cast<AudioCapture*>(self)->run();
    return 0;
}

void AudioCapture::run() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInit = SUCCEEDED(hr);
    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* cap = nullptr;
    WAVEFORMATEX* fmt = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumr);
    if (SUCCEEDED(hr)) hr = enumr->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client);
    if (SUCCEEDED(hr)) hr = client->GetMixFormat(&fmt);
    bool isFloat = false;
    int bits = 0, channels = 1;
    if (SUCCEEDED(hr)) {
        channels = fmt->nChannels;
        bits = fmt->wBitsPerSample;
        rate_ = fmt->nSamplesPerSec;
        isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)  // the sub-format GUID starts with the format tag
            isFloat = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
        if (!(isFloat && bits == 32) && !(!isFloat && (bits == 16 || bits == 24 || bits == 32))) hr = E_FAIL;
    }
    // 1 s of device buffer (units of 100 ns): the 10 ms polling never lets it overflow
    if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, fmt, nullptr);
    if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
    if (SUCCEEDED(hr)) {
        data_.reserve((size_t)(kMaxSeconds * rate_) + rate_);  // no reallocation while recording
        hr = client->Start();
    }
    initOk_ = SUCCEEDED(hr);
    running_ = initOk_;
    if (readyEvent_) SetEvent(readyEvent_);
    const size_t maxFrames = (size_t)(kMaxSeconds * rate_);
    const int bytes = bits / 8;
    while (initOk_ && !stop_) {
        Sleep(10);
        UINT32 packet = 0;
        for (;;) {
            if (FAILED(cap->GetNextPacketSize(&packet))) {  // device unplugged / invalidated
                lost_ = true;
                stop_ = true;
                break;
            }
            if (!packet) break;
            BYTE* buf = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&buf, &frames, &flags, nullptr, nullptr))) {
                lost_ = true;
                stop_ = true;
                break;
            }
            float peak = 0;
            for (UINT32 f = 0; f < frames && data_.size() < maxFrames; ++f) {
                float sum = 0;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                    for (int c = 0; c < channels; ++c) {
                        const BYTE* p = buf + ((size_t)f * channels + c) * bytes;
                        float v;
                        if (isFloat) {
                            std::memcpy(&v, p, 4);
                        } else if (bits == 16) {
                            v = (int16_t)(p[0] | (p[1] << 8)) / 32768.f;
                        } else if (bits == 24) {
                            int32_t x = p[0] | (p[1] << 8) | (p[2] << 16);
                            if (x & 0x800000) x |= ~0xFFFFFF;
                            v = x / 8388608.f;
                        } else {
                            v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) / 2147483648.f;
                        }
                        sum += v;
                    }
                const float m = sum / channels;
                data_.push_back(m);
                peak = std::fabs(m) > peak ? std::fabs(m) : peak;
            }
            cap->ReleaseBuffer(frames);
            level_ = peak;
            frames_ = data_.size();
            if (data_.size() >= maxFrames) {
                full_ = true;
                stop_ = true;
                break;
            }
        }
    }
    if (client) client->Stop();
    running_ = false;
    rel(cap);
    if (fmt) CoTaskMemFree(fmt);
    rel(client);
    rel(dev);
    rel(enumr);
    if (comInit) CoUninitialize();
}
