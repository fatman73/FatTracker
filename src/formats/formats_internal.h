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
bool s3mDetect(const uint8_t* data, size_t size);
Result<Song> s3mLoad(const uint8_t* data, size_t size);
bool mtmDetect(const uint8_t* data, size_t size);
Result<Song> mtmLoad(const uint8_t* data, size_t size);
bool c669Detect(const uint8_t* data, size_t size);
Result<Song> c669Load(const uint8_t* data, size_t size);
bool itDetect(const uint8_t* data, size_t size);
Result<Song> itLoad(const uint8_t* data, size_t size);

// shared helpers
uint8_t modPeriodToNote(int period);                 // Amiga period -> XM note (mod.cpp)
void samplePitchFromRate(Sample& s, double rateHz);  // relative note + finetune for a C-4 rate (sample_io.cpp)
void pcName(char* dst, const uint8_t* src, size_t n, size_t cap);  // printable, trimmed (s3m.cpp)
void pcLoop(Sample& s, uint32_t start, uint32_t end);              // forward loop, clamped (s3m.cpp)
void pcEffect(Cell& c, int cmd, int param, bool it);               // S3M / IT effect letter -> XM (s3m.cpp)
class ByteReader;
void pcReadPcm(ByteReader& r, Sample& s, uint32_t frames, bool is16, bool isUnsigned);  // LE PCM (s3m.cpp)
