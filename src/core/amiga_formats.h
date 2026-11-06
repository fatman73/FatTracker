// The ported Amiga synth replayers (one source file each); AmigaSynthPlayer::open tries them in turn.
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
