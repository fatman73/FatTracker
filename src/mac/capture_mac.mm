// AudioCapture on macOS: AVAudioEngine's input node, tapped and mixed to mono like the WASAPI version
// (src/audio/capture.cpp). The microphone needs the user's permission (NSMicrophoneUsageDescription in
// Info.plist): the first start() asks the system and fails with a status text; SAMPLE is pressed again then.
#include "audio/capture.h"

#import <AVFoundation/AVFoundation.h>

#include <cmath>

struct AudioCapture::Impl {
    AVAudioEngine* engine = nil;
    id configObserver = nil;
    std::atomic<bool> running{false}, full{false}, lost{false};
    std::atomic<float> level{0.f};
    std::atomic<size_t> frames{0};
    uint32_t rate = 48000;
    size_t maxFrames = 0;
    std::vector<float> data;  // written by the tap only while running; read after stop()
    const char* error = nullptr;
};

AudioCapture::AudioCapture() : impl_(new Impl) {}

AudioCapture::~AudioCapture() {
    stop();
    delete impl_;
}

bool AudioCapture::start() {
    Impl& m = *impl_;
    if (m.running) return true;
    stop();
    m.error = nullptr;
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized: break;
        case AVAuthorizationStatusNotDetermined:
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {}];
            m.error = "ALLOW THE MICROPHONE, THEN PRESS SAMPLE AGAIN";
            return false;
        default:
            m.error = "NO MICROPHONE ACCESS (SYSTEM SETTINGS: PRIVACY)";
            return false;
    }
    m.data.clear();
    m.frames = 0;
    m.full = false;
    m.lost = false;
    m.level = 0.f;
    AVAudioEngine* engine = [AVAudioEngine new];
    AVAudioInputNode* input = engine.inputNode;
    AVAudioFormat* fmt = [input outputFormatForBus:0];
    if (!fmt || fmt.sampleRate <= 0 || fmt.channelCount == 0) return false;  // no input device
    m.rate = (uint32_t)std::lround(fmt.sampleRate);
    m.maxFrames = (size_t)(kMaxSeconds * m.rate);
    m.data.reserve(m.maxFrames + m.rate);  // no reallocation while recording
    Impl* p = &m;
    [input installTapOnBus:0
                bufferSize:4096
                    format:fmt
                     block:^(AVAudioPCMBuffer* buf, AVAudioTime*) {
                         if (!p->running) return;
                         const AVAudioFrameCount n = buf.frameLength;
                         const int channels = (int)buf.format.channelCount;
                         float* const* ch = buf.floatChannelData;
                         if (!ch) return;
                         float peak = 0;
                         for (AVAudioFrameCount f = 0; f < n && p->data.size() < p->maxFrames; ++f) {
                             float sum = 0;
                             for (int c = 0; c < channels; ++c) sum += ch[c][f];
                             const float v = sum / channels;
                             p->data.push_back(v);
                             peak = std::fabs(v) > peak ? std::fabs(v) : peak;
                         }
                         p->level = peak;
                         p->frames = p->data.size();
                         if (p->data.size() >= p->maxFrames) {
                             p->full = true;
                             p->running = false;
                         }
                     }];
    // the input device went away or changed its format: the recording so far is kept
    m.configObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:AVAudioEngineConfigurationChangeNotification
                    object:engine
                     queue:nil
                usingBlock:^(NSNotification*) {
                    if (p->running) p->lost = true;
                    p->running = false;
                }];
    [engine prepare];
    NSError* err = nil;
    m.running = true;
    if (![engine startAndReturnError:&err]) {
        m.running = false;
        [input removeTapOnBus:0];
        [NSNotificationCenter.defaultCenter removeObserver:m.configObserver];
        m.configObserver = nil;
        return false;
    }
    m.engine = engine;
    return true;
}

void AudioCapture::stop() {
    Impl& m = *impl_;
    m.running = false;
    if (m.engine) {
        [m.engine.inputNode removeTapOnBus:0];  // waits for a tap block in progress
        [m.engine stop];
        m.engine = nil;
    }
    if (m.configObserver) {
        [NSNotificationCenter.defaultCenter removeObserver:m.configObserver];
        m.configObserver = nil;
    }
}

bool AudioCapture::running() const { return impl_->running; }
bool AudioCapture::full() const { return impl_->full; }
bool AudioCapture::lost() const { return impl_->lost; }
float AudioCapture::level() const { return impl_->level; }
double AudioCapture::seconds() const { return impl_->rate ? (double)impl_->frames.load() / impl_->rate : 0.0; }
uint32_t AudioCapture::rate() const { return impl_->rate; }
const char* AudioCapture::error() const { return impl_->error; }

std::vector<float> AudioCapture::take() {
    stop();
    std::vector<float> out = static_cast<std::vector<float>&&>(impl_->data);
    impl_->data.clear();
    impl_->frames = 0;
    return out;
}
