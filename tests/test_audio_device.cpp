// Following the Windows default output device (spec section 6).
#include "audio/audio_out.h"
#include "audio/engine.h"
#include "test.h"

#include <mmdeviceapi.h>

TEST(device_watcher_flags_default_render_change) {
    DeviceWatcher w;
    CHECK(!w.takeChange());
    w.OnDefaultDeviceChanged(eCapture, eConsole, L"mic");
    CHECK(!w.takeChange());
    w.OnDefaultDeviceChanged(eRender, eCommunications, L"headset");
    CHECK(!w.takeChange());
    w.OnDefaultDeviceChanged(eRender, eConsole, L"usb-dac");
    CHECK(w.takeChange());
    CHECK(!w.takeChange());  // consumed
}

TEST(engine_reopens_audio_on_request) {
    Engine e;  // no song: silent
    if (!e.startAudio()) {
        std::printf("  SKIP: no audio device\n");
        return;
    }
    const uint32_t before = e.audioOpenCount();
    e.requestAudioReopen();
    e.pollAudio();
    CHECK_EQ(e.audioOpenCount(), before + 1);
    CHECK(e.audioStatus() == nullptr);
    e.pollAudio();  // nothing pending: no reopen
    CHECK_EQ(e.audioOpenCount(), before + 1);
}
