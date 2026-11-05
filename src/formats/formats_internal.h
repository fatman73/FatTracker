// Per-format entry points used by the registry in formats.cpp.
#pragma once
#include "formats/formats.h"

bool modDetect(const uint8_t* data, size_t size);
Result<Song> modLoad(const uint8_t* data, size_t size);
bool stDetect(const uint8_t* data, size_t size);
Result<Song> stLoad(const uint8_t* data, size_t size);
bool xmDetect(const uint8_t* data, size_t size);
Result<Song> xmLoad(const uint8_t* data, size_t size);
bool medDetect(const uint8_t* data, size_t size);
Result<Song> medLoad(const uint8_t* data, size_t size);
bool dbmDetect(const uint8_t* data, size_t size);
Result<Song> dbmLoad(const uint8_t* data, size_t size);
bool digiDetect(const uint8_t* data, size_t size);
Result<Song> digiLoad(const uint8_t* data, size_t size);
bool symDetect(const uint8_t* data, size_t size);
Result<Song> symLoad(const uint8_t* data, size_t size);

// shared helpers
uint8_t modPeriodToNote(int period);                 // Amiga period -> XM note (mod.cpp)
void samplePitchFromRate(Sample& s, double rateHz);  // relative note + finetune for a C-4 rate (sample_io.cpp)
