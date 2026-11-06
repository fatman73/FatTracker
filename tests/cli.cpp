// fttest render <in> <out.wav> [--interp none|linear|cubic] [--rate N] [--maxsec S] [--float]
// fttest compare <a.wav> <b.wav>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "compat.h"  // fopen_s outside Windows
#include "core/render.h"
#include "formats/formats.h"
#include "formats/wav.h"

namespace {
bool readAll(const char* path, std::vector<uint8_t>& d) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    d.resize(n > 0 ? (size_t)n : 0);
    bool ok = n <= 0 || std::fread(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

bool writeAll(const char* path, const std::vector<uint8_t>& d) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    bool ok = std::fwrite(d.data(), 1, d.size(), f) == d.size();
    std::fclose(f);
    return ok;
}

int render(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: fttest render <in> <out.wav> [--interp none|linear|cubic] [--rate N] [--maxsec S] [--float]\n");
        return 2;
    }
    Interp interp = Interp::Cubic;
    uint32_t rate = 48000;
    float maxsec = 600;
    bool fl = false;
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--interp") && i + 1 < argc) {
            const char* v = argv[++i];
            interp = !std::strcmp(v, "none") ? Interp::None : (!std::strcmp(v, "linear") ? Interp::Linear : Interp::Cubic);
        } else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) {
            rate = (uint32_t)std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--maxsec") && i + 1 < argc) {
            maxsec = (float)std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--float")) {
            fl = true;
        }
    }
    std::vector<uint8_t> in;
    if (!readAll(argv[2], in)) {
        std::printf("ERROR: CANNOT READ %s\n", argv[2]);
        return 1;
    }
    auto song = loadModule(in.data(), in.size());
    if (!song.ok()) {
        std::printf("ERROR: %s\n", song.error());
        return 1;
    }
    std::vector<float> out;
    size_t frames = renderSong(song.value(), rate, interp, interp != Interp::None, maxsec, out);
    if (!writeAll(argv[3], writeWav(out.data(), frames, rate, fl))) {
        std::printf("ERROR: CANNOT WRITE %s\n", argv[3]);
        return 1;
    }
    std::printf("rendered %zu frames (%.1f s)\n", frames, (double)frames / rate);
    return 0;
}

int compare(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: fttest compare <a.wav> <b.wav>\n");
        return 2;
    }
    std::vector<uint8_t> a, b;
    if (!readAll(argv[2], a) || !readAll(argv[3], b)) {
        std::printf("ERROR: CANNOT READ INPUT\n");
        return 1;
    }
    auto wa = readWav(a.data(), a.size());
    auto wb = readWav(b.data(), b.size());
    if (!wa.ok() || !wb.ok()) {
        std::printf("ERROR: %s\n", !wa.ok() ? wa.error() : wb.error());
        return 1;
    }
    const auto& x = wa.value().samples;
    const auto& y = wb.value().samples;
    double corr = correlation(x.data(), x.size(), y.data(), y.size());
    size_t n = x.size() < y.size() ? x.size() : y.size();
    double e = 0;
    for (size_t i = 0; i < n; ++i) e += (x[i] - y[i]) * (double)(x[i] - y[i]);
    std::printf("corr=%.5f rms=%.6f\n", corr, n ? std::sqrt(e / n) : 0.0);
    return corr >= 0.98 ? 0 : 1;
}
}  // namespace

int runCli(int argc, char** argv) {
    if (!std::strcmp(argv[1], "render")) return render(argc, argv);
    return compare(argc, argv);
}
