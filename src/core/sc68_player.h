// Atari ST tunes in two containers of 68000 replay code and music data, played on the emulated ST of
// core/sndh_player (Musashi 68000, YM2149, MFP timers, minimal TOS). Playback only; voices = YM channels.
//
// SC68 (sc68 by Benjamin Gerard, GPL): "SC68 Music-file / (c) (BeN)jamin Gerard / SasHipA-Dev  " then "SCxx"
// chunks with little-endian sizes (file68.c of sc68 2.2.1): tracks (SCMU) with their data (SCDA, inherited by
// the next tracks), D0 value, load address (default $8000), replay rate, length, hardware flags (YM / STE /
// Amiga) and optionally the name of an external replay (SCRE) that sc68 ships separately (Replay/<name>.bin):
// the replay is loaded at the load address and the data behind it. init = load address (D0 = SCD0, D1 = 1 on an
// STF, D2 = data size, A0 = data), play = load address + 8. Amiga (Paula) and STE DMA only tracks are refused.
// gzip and ICE! packed files are unpacked first.
//
// YMST (MYST by Nicolas Pomarede / Mr.Styckx, the YM2149 emulator for the Amiga): "YMST" then tags of 8 bytes
// ($8000, 'Y', letter, a big-endian long) up to a zero long. The tags as the MYST player (YMPlayer 1.51) reads
// them: M = 0 tune with its replay, 1 replay file, 2 music data for the replay file named by O ("YM.xxx_replay",
// a YMST file of kind 1 next to it); ] .. ^ = sub-songs, R = their lengths (minutes << 16 | seconds), ` = their
// D0 values; S title, T composer, U ripper; X relocate (called once), Y patch for the Amiga emulator, Z init
// (D0 = sub-song), [ play (each VBL), \ copy the YM registers into the emulator's buffer (slot P); for a data
// file the replay gets the data in slots c (address) and d (size). The ST code inside is the original, so on
// the emulated ST X, Z and [ run and the Amiga patches (Y, \) are left out: the replay writes the YM itself.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/sndh_player.h"

// Reads a companion file (an external replay) by name; false when there is none.
using StFileResolver = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;

class Sc68Player : public SndhPlayer {
public:
    static bool detect(const uint8_t* data, size_t size);      // SC68 (plain, gzip or ICE! packed)
    static bool detectYmst(const uint8_t* data, size_t size);  // YMST: tune, music data or replay file
    // External replays are read through `files` (SC68: "<name>.bin", YMST: "YM.<name>_replay"). On failure
    // `error` (if given) says why in capitals, e.g. "SC68: REPLAY 'HPN-JAM' NOT FOUND".
    bool load(const uint8_t* data, size_t size, const StFileResolver& files = {}, int subsong = -1,
              std::string* error = nullptr);

    bool ymst() const { return ymst_; }
    const char* replay() const { return replay_.c_str(); }  // the external replay ("" = in the file)

private:
    bool loadSc68(const uint8_t* data, size_t size, const StFileResolver& files, int subsong, std::string& error);
    bool loadYmst(const uint8_t* data, size_t size, const StFileResolver& files, int subsong, std::string& error);

    bool ymst_ = false;
    std::string replay_;
};
