// Internal to ZxTrackerPlayer: the per-format replayers and their structure checks. See zx_trackers.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zxt {

// A module as the Z80 player sees it: 16 bit addresses; anything outside the file reads as 0.
struct Mem {
    std::vector<uint8_t> d;
    uint8_t at(uint32_t a) const { return a < d.size() ? d[a] : 0; }
    uint16_t word(uint32_t a) const { return (uint16_t)(at(a) | at(a + 1) << 8); }
    size_t size() const { return d.size(); }
};

// The AY registers as the player leaves them after a frame (registers keep their value between frames).
struct Ay {
    uint8_t r[14] = {};
    int envShape = -1;  // >= 0: register 13 was written this frame (restarts the envelope)
    void set(int reg, uint8_t v) {
        r[reg] = v;
        if (reg == 13) envShape = v & 15;
    }
};

class Replayer {
public:
    virtual ~Replayer() = default;
    virtual void reset(Ay& ay) = 0;    // the song's start
    virtual bool frame(Ay& ay) = 0;    // one 50 Hz frame; true when it went past the end of the position list
    std::string title, author;
};

// Structure checks: true only for a file whose header geometry, tables and every position's patterns are
// consistent. They read inside [d, d + n) only.
bool checkStc(const uint8_t* d, size_t n);
bool checkPt2(const uint8_t* d, size_t n);
bool checkAsc(const uint8_t* d, size_t n);  // ASC Sound Master 1.x (with a loop position) or 0.x
bool checkSqt(const uint8_t* d, size_t n);
// Sound Tracker 1.1 source module: offset of the raw module in the file (behind Ay_Emul's ZXAYST11 wrapper or
// at 0), or -1.
long findSt1(const uint8_t* d, size_t n);

// Replayers over a copy of a module that passed its check (nullptr when it does not).
std::unique_ptr<Replayer> makeStc(const uint8_t* d, size_t n);
std::unique_ptr<Replayer> makePt2(const uint8_t* d, size_t n);
std::unique_ptr<Replayer> makeAsc(const uint8_t* d, size_t n);
std::unique_ptr<Replayer> makeSqt(const uint8_t* d, size_t n);
std::unique_ptr<Replayer> makeSt1(const uint8_t* d, size_t n);  // converted to the compiled (STC) form

}  // namespace zxt
