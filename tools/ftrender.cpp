// ftrender <in> <out.wav> [--seconds S] [--subsong N] [--float]
// Renders a song through the Engine, with no audio device: tracker modules and every playback-only chip
// format (TFMX, FC/Hippel, SID, SNDH, SC68, AY/YM, PT3, GME, AHX/HVL, NostalgicPlayer ports), routed exactly
// like the app does it. The mixer runs at 48 kHz. Compare two renders with `fttest compare a.wav b.wav`.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/chip_route.h"
#include "audio/engine.h"
#include "core/song.h"
#include "formats/formats.h"
#include "formats/wav.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: ftrender <in> <out.wav> [--seconds S] [--subsong N] [--float]\n");
        return 2;
    }
    double seconds = 60;
    int subsong = -1;
    bool fl = false;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--subsong") && i + 1 < argc) subsong = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--float")) fl = true;
    }
    const std::wstring path = std::filesystem::path(argv[1]).wstring();
    // SC68 / YMST external replays: a Replay folder next to this program, like the app's
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::absolute(std::filesystem::path(argv[0]), ec);
    const std::wstring replayDir = ec ? std::wstring() : (exe.parent_path() / "Replay").wstring();

    std::vector<uint8_t> data;
    if (const char* err = readWholeFile(path, data)) {
        std::printf("%s\n", err);
        return 1;
    }
    Engine e;  // no audio device: the mixer runs at 48 kHz
    const ChipRoute route = routeChipFile(path, data, replayDir);
    if (route.kind == ChipRoute::Error) {
        std::printf("ERROR: %s\n", route.error.c_str());
        return 1;
    }
    if (route.kind == ChipRoute::Ok) {
        if (!loadRoutedChip(e, route, data, replayDir, false)) {
            std::printf("ERROR: CANNOT PLAY THIS FILE\n");
            return 1;
        }
        std::printf("%s", e.tfmxFormat());
        if (e.chipSubsongs() > 1) std::printf(" (%d sub-songs)", e.chipSubsongs());
        std::printf("\n");
    } else {
        auto r = loadModule(data.data(), data.size());
        if (!r.ok()) {
            std::printf("ERROR: %s\n", r.error());
            return 1;
        }
        e.setSong(std::make_unique<Song>(static_cast<Song&&>(r.value())));
        std::printf("module\n");
    }
    if (subsong >= 0 && !e.selectChipSubsong(subsong)) {
        std::printf("ERROR: NO SUB-SONG %d\n", subsong);
        return 1;
    }
    e.play();
    const uint32_t rate = 48000, block = 480;
    const size_t total = (size_t)(seconds * rate);
    std::vector<float> pcm, buf(block * 2);
    pcm.reserve(total * 2 + block * 2);
    for (size_t f = 0; f < total; f += block) {
        e.render(buf.data(), block);
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }
    const std::vector<uint8_t> wav = writeWav(pcm.data(), pcm.size() / 2, rate, fl);
    std::ofstream out(std::filesystem::path(argv[2]), std::ios::binary);
    if (!out || !out.write(reinterpret_cast<const char*>(wav.data()), (std::streamsize)wav.size())) {
        std::printf("ERROR: CANNOT WRITE %s\n", argv[2]);
        return 1;
    }
    std::printf("rendered %.1f s\n", pcm.size() / 2.0 / rate);
    return 0;
}
