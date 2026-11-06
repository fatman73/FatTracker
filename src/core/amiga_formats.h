// The ported Amiga synth replayers (one source file each); AmigaSynthPlayer::open tries them in turn.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

class AmigaSynthPlayer;

std::unique_ptr<AmigaSynthPlayer> openJamCracker(const uint8_t* data, size_t size);  // jamcracker.cpp
