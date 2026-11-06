// Round-trips every MOD/XM in modules/ (downloaded test music, not in git) through the writers.
#include <cstdio>
#include <filesystem>

#include "formats/formats.h"
#include "formats/writers.h"
#include "test.h"

namespace {
std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::vector<uint8_t> d;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return d;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    d.resize(n > 0 ? (size_t)n : 0);
    if (n > 0) std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    return d;
}

bool sameCells(const Song& a, const Song& b) {
    if (a.patterns.size() != b.patterns.size()) return false;
    for (size_t p = 0; p < a.patterns.size(); ++p) {
        const Pattern &x = a.patterns[p], &y = b.patterns[p];
        if (x.rows != y.rows) return false;
        for (int r = 0; r < x.rows; ++r)
            for (int c = 0; c < a.numChannels; ++c) {
                const Cell &u = x.at(r, c), &v = y.at(r, c);
                if (u.note != v.note || u.instr != v.instr || u.vol != v.vol || u.fx != v.fx || u.param != v.param)
                    return false;
            }
    }
    return true;
}

bool sameSamples(const Song& a, const Song& b) {
    for (size_t i = 0; i < a.samples.size(); ++i) {
        const Sample& x = a.samples[i];
        if (x.length == 0) continue;  // empty slots are not written
        bool found = false;
        for (const Sample& y : b.samples)
            if (y.length == x.length && y.is16 == x.is16 && y.data8 == x.data8 && y.data16 == x.data16 &&
                y.volume == x.volume && y.finetune == x.finetune && y.loop == x.loop)
                found = true;
        if (!found) return false;
    }
    return true;
}
}  // namespace

TEST(real_modules_xm_roundtrip) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(FT_SOURCE_DIR) / "modules";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        std::printf("SKIP real_modules_xm_roundtrip (no modules folder)\n");
        return;
    }
    int n = 0, bad = 0;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
        auto ext = e.path().extension().wstring();
        for (auto& ch : ext) ch = (wchar_t)towlower(ch);
        if (ext != L".mod" && ext != L".xm") continue;
        auto data = readAll(e.path());
        auto a = loadModule(data.data(), data.size());
        if (!a.ok()) continue;
        auto bytes = saveXm(a.value());
        auto b = loadModule(bytes.data(), bytes.size());
        ++n;
        if (!b.ok() || !sameCells(a.value(), b.value()) || !sameSamples(a.value(), b.value())) {
            if (bad < 5) std::printf("  roundtrip mismatch: %ls\n", e.path().filename().wstring().c_str());
            ++bad;
        }
    }
    std::printf("  xm roundtrip: %d modules, %d mismatches\n", n, bad);
    CHECK_EQ(bad, 0);
}
