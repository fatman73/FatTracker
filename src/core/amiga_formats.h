// The ported Amiga synth replayers (one source file each: JamCracker, Delta Music, SidMon, SoundMon,
// Digital Mugician); AmigaSynthPlayer::open tries them in turn.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

class AmigaSynthPlayer;

std::unique_ptr<AmigaSynthPlayer> openJamCracker(const uint8_t* data, size_t size);  // jamcracker.cpp

// Delta Music 1.0 ("ALL " and 25 chunk lengths that fit the file), deltamusic1.cpp
bool isDeltaMusic1(const uint8_t* data, size_t size);
std::unique_ptr<AmigaSynthPlayer> openDeltaMusic1(const uint8_t* data, size_t size);
// Delta Music 2.0 (".FNL" at 0xBC6 and a chain of chunk lengths that fits the file), deltamusic2.cpp
bool isDeltaMusic2(const uint8_t* data, size_t size);
std::unique_ptr<AmigaSynthPlayer> openDeltaMusic2(const uint8_t* data, size_t size);
std::unique_ptr<AmigaSynthPlayer> openSidMon1(const uint8_t* data, size_t size);    // sidmon1.cpp
std::unique_ptr<AmigaSynthPlayer> openSidMon2(const uint8_t* data, size_t size);    // sidmon2.cpp

// Cheap signature checks for AmigaSynthPlayer::detect.
bool detectSidMon1(const uint8_t* data, size_t size);  // the replay routine's instructions (never PSID / RSID)
bool detectSidMon2(const uint8_t* data, size_t size);  // "SIDMON II - THE MIDI VERSION" at 58

// BP SoundMon V.2 / V.3 ("V.2" / "V.3" at 26 and position, track and sample tables that fit the file),
// soundmon.cpp
bool isSoundMon(const uint8_t* data, size_t size);
std::unique_ptr<AmigaSynthPlayer> openSoundMon(const uint8_t* data, size_t size);
// Digital Mugician 1 / 2 (" MUGICIAN/SOFTEYES 1990 " / " MUGICIAN2/SOFTEYES 1990" and tables that fit the
// file, with a playable sub-song), mugician.cpp
bool isMugician(const uint8_t* data, size_t size);
std::unique_ptr<AmigaSynthPlayer> openMugician(const uint8_t* data, size_t size);
