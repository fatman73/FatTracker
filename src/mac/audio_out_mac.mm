// Core Audio output (macOS): the default output unit (AUHAL) pulls interleaved float stereo from RenderFn
// at the device's nominal sample rate, so the unit does not resample and `rate` is the real one.
// Changes of the default device or of its sample rate set a flag (takeDeviceChange); the engine's
// pollAudio() then reopens, like the WASAPI version does.
#include "audio/audio_out.h"

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>

#include <atomic>

struct AudioOut::Impl {
    AudioComponentInstance unit = nullptr;
    RenderFn fn = nullptr;
    void* user = nullptr;
    uint32_t rate = 48000;
    bool open = false;
    std::atomic<bool> lost{false}, changed{false};
    AudioObjectID device = kAudioObjectUnknown;  // the device the unit plays on (for its listeners)
    dispatch_queue_t queue = nullptr;            // property listeners run here (not the main queue: CLI tools)
    AudioObjectPropertyListenerBlock defaultListener = nil, deviceListener = nil;
};

namespace {
const AudioObjectPropertyAddress kDefaultOut = {kAudioHardwarePropertyDefaultOutputDevice,
                                                kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
const AudioObjectPropertyAddress kRate = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementMain};
const AudioObjectPropertyAddress kAlive = {kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain};

AudioObjectID defaultDevice() {
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 size = sizeof dev;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &kDefaultOut, 0, nullptr, &size, &dev) != noErr)
        return kAudioObjectUnknown;
    return dev;
}

double nominalRate(AudioObjectID dev) {
    Float64 r = 0;
    UInt32 size = sizeof r;
    return dev != kAudioObjectUnknown && AudioObjectGetPropertyData(dev, &kRate, 0, nullptr, &size, &r) == noErr ? r : 0;
}

// Real-time thread: no allocation, no locks other than the engine's, no Objective-C.
OSStatus renderCb(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*, UInt32, UInt32 frames,
                  AudioBufferList* io) {
    auto* m = static_cast<AudioOut::Impl*>(ref);
    if (!io || io->mNumberBuffers < 1 || !io->mBuffers[0].mData ||
        io->mBuffers[0].mDataByteSize < frames * 2 * sizeof(float)) {
        if (flags) *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }
    m->fn(m->user, static_cast<float*>(io->mBuffers[0].mData), frames, m->rate);
    return noErr;
}
}  // namespace

AudioOut::AudioOut() : impl_(new Impl) { impl_->queue = dispatch_queue_create("FatTracker.audio.listeners", nullptr); }

AudioOut::~AudioOut() {
    close();
    dispatch_sync(impl_->queue, ^{});  // a listener already running finishes before Impl goes away
}

bool AudioOut::open(RenderFn fn, void* user) {
    close();
    Impl& m = *impl_;
    m.fn = fn;
    m.user = user;
    m.lost = false;
    m.changed = false;
    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                   kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(nullptr, &d);
    if (!comp || AudioComponentInstanceNew(comp, &m.unit) != noErr) {
        m.unit = nullptr;
        return false;
    }
    m.device = defaultDevice();
    const double r = nominalRate(m.device);
    m.rate = r >= 8000 ? (uint32_t)(r + 0.5) : 48000;
    AudioStreamBasicDescription f = {};
    f.mSampleRate = m.rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;  // interleaved
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 32;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = f.mBytesPerPacket = 2 * sizeof(float);
    AURenderCallbackStruct cb = {renderCb, &m};
    UInt32 maxFrames = 4096;
    if (AudioUnitSetProperty(m.unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) ||
        AudioUnitSetProperty(m.unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) ||
        AudioUnitSetProperty(m.unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames,
                             sizeof maxFrames) ||
        AudioUnitInitialize(m.unit) || AudioOutputUnitStart(m.unit)) {
        close();
        return false;
    }
    Impl* mp = &m;
    m.defaultListener = ^(UInt32, const AudioObjectPropertyAddress*) { mp->changed = true; };
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &kDefaultOut, m.queue, m.defaultListener);
    if (m.device != kAudioObjectUnknown) {
        m.deviceListener = ^(UInt32 n, const AudioObjectPropertyAddress* a) {
            for (UInt32 i = 0; i < n; ++i) {
                if (a[i].mSelector == kAudioDevicePropertyNominalSampleRate) mp->changed = true;
                if (a[i].mSelector == kAudioDevicePropertyDeviceIsAlive) {
                    UInt32 alive = 1, size = sizeof alive;
                    if (AudioObjectGetPropertyData(mp->device, &kAlive, 0, nullptr, &size, &alive) != noErr || !alive)
                        mp->lost = true;
                }
            }
        };
        AudioObjectAddPropertyListenerBlock(m.device, &kRate, m.queue, m.deviceListener);
        AudioObjectAddPropertyListenerBlock(m.device, &kAlive, m.queue, m.deviceListener);
    }
    m.open = true;
    return true;
}

void AudioOut::close() {
    Impl& m = *impl_;
    if (m.defaultListener) {
        AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &kDefaultOut, m.queue, m.defaultListener);
        m.defaultListener = nil;
    }
    if (m.deviceListener) {
        AudioObjectRemovePropertyListenerBlock(m.device, &kRate, m.queue, m.deviceListener);
        AudioObjectRemovePropertyListenerBlock(m.device, &kAlive, m.queue, m.deviceListener);
        m.deviceListener = nil;
    }
    if (m.unit) {
        AudioOutputUnitStop(m.unit);  // the render callback does not run after this returns
        AudioUnitUninitialize(m.unit);
        AudioComponentInstanceDispose(m.unit);
        m.unit = nullptr;
    }
    m.device = kAudioObjectUnknown;
    m.open = false;
}

uint32_t AudioOut::sampleRate() const { return impl_->rate; }
bool AudioOut::isOpen() const { return impl_->open; }
bool AudioOut::lost() const { return impl_->lost.load(); }
bool AudioOut::takeDeviceChange() { return impl_->changed.exchange(false); }
