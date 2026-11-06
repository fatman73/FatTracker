// Song data model shared by loaders, player, mixer and UI.
#pragma once
#include <cstdint>
#include <vector>

constexpr uint8_t NOTE_NONE = 0;  // 1..96 = C-0..B-7 (XM numbering, 49 = C-4 = 8363 Hz)
constexpr uint8_t NOTE_OFF = 97;
constexpr int kMaxChannels = 32;

// vol: XM encoding (0 empty, 0x10..0x50 = volume 0..64, 0x60+ commands)
// fx: 0..35 (0-9, A-Z); fx==0 && param==0 means no effect. MOD Exy = fx 14, param xy.
struct Cell {
    uint8_t note = 0, instr = 0, vol = 0, fx = 0, param = 0;
};

enum class LoopType : uint8_t { None, Forward, PingPong };

struct Sample {
    char name[23] = {};
    bool is16 = false;
    std::vector<int8_t> data8;
    std::vector<int16_t> data16;
    uint32_t length = 0, loopStart = 0, loopLength = 0;
    LoopType loop = LoopType::None;
    uint8_t volume = 64;     // 0..64
    int8_t finetune = 0;     // -128..127 (MOD finetune * 16)
    int8_t relativeNote = 0;
    uint8_t pan = 128;       // 0..255
    bool hasPan = true;      // false: notes keep the channel panning (S3M, 669, IT without default pan)
};

struct EnvPoint {
    uint16_t tick = 0;
    uint8_t value = 0;  // 0..64
};
constexpr uint8_t ENV_ON = 1, ENV_SUSTAIN = 2, ENV_LOOP = 4;
struct Envelope {
    EnvPoint pts[12];
    uint8_t count = 0, sustain = 0, loopStart = 0, loopEnd = 0, flags = 0;
};

struct Instrument {
    char name[23] = {};
    uint16_t sampleMap[96] = {};  // global index into Song::samples
    Envelope volEnv, panEnv;
    uint16_t fadeout = 0;
    uint8_t vibType = 0, vibSweep = 0, vibDepth = 0, vibRate = 0;
};

struct Pattern {
    uint16_t rows = 64;
    uint8_t channels = 4;
    std::vector<Cell> cells;
    Cell& at(int row, int ch) { return cells[(size_t)row * channels + ch]; }
    const Cell& at(int row, int ch) const { return cells[(size_t)row * channels + ch]; }
};

enum class FreqMode : uint8_t { Amiga, Linear };
enum class Quirks : uint8_t { PT23, FT2 };
constexpr uint8_t LOAD_TRUNCATED = 1;

struct Song {
    char name[21] = {};
    uint8_t numChannels = 4;
    std::vector<uint8_t> order;
    uint8_t restartPos = 0;
    uint8_t initialSpeed = 6, initialBpm = 125;
    uint8_t globalVolume = 64;
    FreqMode freqMode = FreqMode::Amiga;
    Quirks quirks = Quirks::FT2;
    uint8_t channelPan[kMaxChannels] = {};
    uint8_t loadFlags = 0;
    // Symphonie continuous slides: effects U/V (volume up / down) and W/Y (pitch up / down) set a
    // slide that runs every tick until changed (see symVolumeStep and Player::symphonieSlideTick). Off for every
    // other format, where those effect letters do nothing.
    bool symphonieSlides = false;
    // Symphonie DSP echo settings: effect Q xx switches to symEcho[xx]. Empty: no echo, Q does nothing.
    struct SymEcho {
        uint8_t type = 0;      // 0 off, 1 echo, 2 cross echo, 3 cross echo 2, 4 center echo
        uint8_t delay = 0;     // in ticks
        uint8_t feedback = 0;  // level: the echo feeds back 2^-feedback of itself
    };
    std::vector<SymEcho> symEcho;
    std::vector<Pattern> patterns;
    std::vector<Sample> samples;
    std::vector<Instrument> instruments;
};

// One tick of a Symphonie volume slide (rate: signed slide parameter): the original adds rate/16 to
// its 0..100 volume, rate/25 on our 0..64 scale, and stops the slide at either end.
inline void symVolumeStep(float& vol, int& rate) {
    vol += (float)rate * 0.04f;
    if (vol > 64.f) {
        vol = 64.f;
        rate = 0;
    } else if (vol < 0.f) {
        vol = 0.f;
        rate = 0;
    }
}

Pattern makePattern(int rows, int channels);
void setDefaultPanning(Song& s);  // L-R-R-L with 75% separation (0x40 / 0xC0)
Song makeEmptySong(int channels);
// Changes the channel count (clamped to 4..32), keeping existing cells.
void setChannelCount(Song& s, int channels);
