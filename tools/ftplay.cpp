// ftplay <file> [--seconds S] [--subsong N]
// Plays a song on the default audio output through the Engine (macOS: Core Audio), for checking the audio
// output without the app: device rate, re-opening on device changes (pollAudio every second), and status.
// Ends after S seconds (default 30). PLAYS SOUND.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "app/chip_route.h"
#include "audio/engine.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: ftplay <file> [--seconds S] [--subsong N]\n");
        return 2;
    }
    double seconds = 30;
    int subsong = -1;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--subsong") && i + 1 < argc) subsong = std::atoi(argv[++i]);
    }
    const std::wstring path = std::filesystem::path(argv[1]).wstring();
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::absolute(std::filesystem::path(argv[0]), ec);
    const std::wstring replayDir = ec ? std::wstring() : (exe.parent_path() / "Replay").wstring();

    Engine e;
    std::string error;
    if (!loadSongFile(e, path, replayDir, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    if (subsong >= 0 && !e.selectChipSubsong(subsong)) {
        std::printf("ERROR: NO SUB-SONG %d\n", subsong);
        return 1;
    }
    if (!e.startAudio()) std::printf("audio: %s (will retry)\n", e.audioStatus() ? e.audioStatus() : "?");
    e.play();
    const auto start = std::chrono::steady_clock::now();
    uint32_t opens = e.audioOpenCount();
    Telemetry t{};
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        e.pollAudio();  // the app's UI timer does this every 2 s
        e.snapshot(t);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (e.audioOpenCount() != opens) {
            opens = e.audioOpenCount();
            std::printf("\naudio re-opened (%u)\n", opens);
        }
        const char* st = e.audioStatus();
        if (e.tfmx())
            std::printf("\r%5.1f s  %s  song %d/%d  %u Hz  %s   ", el, e.tfmxFormat(), t.subsong + 1, t.subsongs, t.rate,
                        st ? st : "OK");
        else
            std::printf("\r%5.1f s  pos %02u pat %02u row %02u  speed %u bpm %u  %u Hz  %s   ", el, t.order, t.pattern,
                        t.row, t.speed, t.bpm, t.rate, st ? st : "OK");
        std::fflush(stdout);
        if (el >= seconds) break;
    }
    e.stop();
    std::printf("\n");
    return 0;
}
