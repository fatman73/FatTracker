#include "build_mod.h"

#include <cstring>

#include "core/tables.h"

namespace {
void put16be(std::vector<uint8_t>& b, int v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)v);
}

std::vector<uint8_t> build(int numSamples, const char* sig, int channels, const std::vector<uint8_t>& order,
                           const std::vector<std::vector<ModCell>>& patterns,
                           const std::vector<ModSample>& samples) {
    std::vector<uint8_t> b;
    const char* title = "test song";
    for (int i = 0; i < 20; ++i) b.push_back(i < (int)std::strlen(title) ? (uint8_t)title[i] : 0);
    for (int s = 0; s < numSamples; ++s) {
        for (int i = 0; i < 22; ++i) b.push_back(0);
        if (s < (int)samples.size()) {
            const ModSample& m = samples[s];
            put16be(b, m.lenWords);
            b.push_back((uint8_t)(m.finetune & 0x0F));
            b.push_back((uint8_t)m.volume);
            put16be(b, m.repStartWords);
            put16be(b, m.repLenWords);
        } else {
            for (int i = 0; i < 8; ++i) b.push_back(0);
        }
    }
    b.push_back((uint8_t)order.size());
    b.push_back(0x7F);
    for (int i = 0; i < 128; ++i) b.push_back(i < (int)order.size() ? order[i] : 0);
    if (sig) b.insert(b.end(), sig, sig + 4);
    for (const auto& p : patterns) {
        for (int r = 0; r < 64; ++r) {
            for (int c = 0; c < channels; ++c) {
                ModCell m = p[(size_t)r * channels + c];
                int period = m.ptNoteIdx >= 0 ? kPtPeriods[0][m.ptNoteIdx] : 0;
                b.push_back((uint8_t)((m.sample & 0xF0) | ((period >> 8) & 0x0F)));
                b.push_back((uint8_t)(period & 0xFF));
                b.push_back((uint8_t)(((m.sample & 0x0F) << 4) | (m.fx & 0x0F)));
                b.push_back((uint8_t)m.param);
            }
        }
    }
    for (const auto& m : samples) {
        for (int i = 0; i < m.lenWords * 2; ++i) b.push_back(i < (int)m.data.size() ? (uint8_t)m.data[i] : 0);
    }
    return b;
}
}  // namespace

std::vector<uint8_t> buildMod(const char sig[4], int channels, const std::vector<uint8_t>& order,
                              const std::vector<std::vector<ModCell>>& patterns,
                              const std::vector<ModSample>& samples) {
    return build(31, sig, channels, order, patterns, samples);
}

std::vector<uint8_t> buildSoundtracker(const std::vector<uint8_t>& order,
                                       const std::vector<std::vector<ModCell>>& patterns,
                                       const std::vector<ModSample>& samples) {
    return build(15, nullptr, 4, order, patterns, samples);
}
