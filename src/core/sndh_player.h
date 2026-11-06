// Atari ST SNDH tunes (https://sndh.atari.org): each file is the tune's own 68000 player. It runs on an
// emulated ST - Musashi 68000 (core/m68k, MIT), 4 MB RAM, a minimal TOS (supervisor mode, exception vectors,
// GEMDOS / BIOS / XBIOS stubs), the YM2149 at $FF8800 through ayumi, the MFP 68901 timers and the VBL.
// init is called once per sub-song, play at the rate of the header's timer tag. ICE! 2.4 packed files are
// unpacked first. STe DMA sound is not played (the tune's YM part is). Playback only; voices = YM channels.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/chip_player.h"

// ICE! 2.x ("ICE!") depacker. false when this is not ICE packed or the stream is broken / cut.
bool iceDepack(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

// One sub-song of another Atari ST container (core/sc68_player: SC68, YMST) for the same machine: the memory
// blocks it needs, the init call (D0 = d0, once) and the play call made hz times a second (both return with RTS).
struct StSong {
    struct Block {
        uint32_t address = 0;
        std::shared_ptr<const std::vector<uint8_t>> bytes;
    };
    std::vector<Block> blocks;  // inside [SndhPlayer::kProgramLow, kProgramHigh)
    uint32_t init = 0, play = 0, d0 = 0;
    int hz = 50;
    double seconds = 0;  // 0 = unknown
    std::string name;    // "" = none
};

class SndhPlayer : public ChipPlayer {
public:
    static constexpr uint32_t kLoadAddress = 0x20000;  // where the tune lives in ST memory
    // StSong blocks: above the machine's own low memory, below the stack the other containers get
    static constexpr uint32_t kProgramLow = 0x1000, kProgramHigh = 0x1F0000;

    SndhPlayer();
    ~SndhPlayer() override;
    SndhPlayer(const SndhPlayer&) = delete;
    SndhPlayer& operator=(const SndhPlayer&) = delete;

    static bool detect(const uint8_t* data, size_t size);  // "SNDH" at 12, or ICE! packed
    bool load(const uint8_t* data, size_t size, int subsong = -1);  // -1 = the header's default (#!nn)

    void restart() override;
    void setRate(uint32_t hz) override;
    void clearMeters() override;
    void render(float* stereo, uint32_t frames, float gain) override;
    int voices() const override { return 3; }
    float vu(int v) const override;
    void scope(int v, float* dst, int n) const override;
    double seconds() const override;  // FRMS / TIME of the sub-song (0 = unknown or endless)
    const char* format() const override;
    bool buildDisplay(Song& out) override;
    int tick() const override { return 0; }
    int subsongs() const override;
    int subsong() const override;
    bool selectSubsong(int i) override;
    const char* subsongTitle() const override;

    const char* title() const;
    const char* composer() const;
    int playRate() const;  // play calls per second
    bool crashed() const;  // the 68000 program ran away (it is silenced until restart)
    // inspection (tests): a YM register, a long word of ST memory, MFP interrupts served since load/restart
    uint8_t ymRegister(int r) const;
    uint32_t peek32(uint32_t address) const;
    uint32_t mfpInterrupts() const;

    struct Impl;  // the emulated machine (named here for the 68000 core's memory callbacks)

protected:
    // Other containers on this machine: the songs replace the SNDH image; format() says `format`. false (nothing
    // loaded) for no songs, a block outside the program area, or an init that runs away.
    bool loadSongs(std::vector<StSong> songs, const std::string& title, const std::string& composer,
                   const std::string& format, int subsong);

private:
    std::unique_ptr<Impl> impl_;
};
