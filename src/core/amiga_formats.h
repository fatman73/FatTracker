// The ported Amiga synth replayers (one source file each: JamCracker, SidMon 1.0, SidMon 2.0);
// AmigaSynthPlayer::open tries them in turn.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

class AmigaSynthPlayer;

std::unique_ptr<AmigaSynthPlayer> openJamCracker(const uint8_t* data, size_t size);  // jamcracker.cpp
std::unique_ptr<AmigaSynthPlayer> openSidMon1(const uint8_t* data, size_t size);    // sidmon1.cpp
std::unique_ptr<AmigaSynthPlayer> openSidMon2(const uint8_t* data, size_t size);    // sidmon2.cpp

// Cheap signature checks for AmigaSynthPlayer::detect.
bool detectSidMon1(const uint8_t* data, size_t size);  // the replay routine's instructions (never PSID / RSID)
bool detectSidMon2(const uint8_t* data, size_t size);  // "SIDMON II - THE MIDI VERSION" at 58
