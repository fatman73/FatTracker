// AudioOut without a device, for fttest on macOS: open() fails, nothing is played.
#include "audio/audio_out.h"

struct AudioOut::Impl {};
AudioOut::AudioOut() : impl_(new Impl) {}
AudioOut::~AudioOut() = default;
bool AudioOut::open(RenderFn, void*) { return false; }
void AudioOut::close() {}
uint32_t AudioOut::sampleRate() const { return 48000; }
bool AudioOut::isOpen() const { return false; }
bool AudioOut::lost() const { return false; }
bool AudioOut::takeDeviceChange() { return false; }
