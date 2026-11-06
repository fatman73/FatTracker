// Which playback-only player a file goes to (TFMX pair, FC/Hippel, SID, SNDH, GME, ...), decided by trial
// loads exactly like the app's loadTfmxPair, but without UI side effects: shared by tools (ftrender) and,
// later, the app on both systems.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/amiga_synth.h"

class Engine;

enum class Chip { Tfmx, Sid, Pt3, Gme, Synth, SynthFiles, Ay, Hippel, Sndh, Hvl, ZxTracker, Sc68 };  // SynthFiles: with companion files

struct ChipRoute {
    enum Kind { NotChip, Ok, Error } kind = NotChip;
    Chip chip = Chip::Tfmx;
    std::wstring mdatPath;      // the file the song itself is in (the mdat when a TFMX smpl file was chosen)
    std::vector<uint8_t> smpl;  // TFMX samples (empty for one-file chip songs)
    char name[21] = {};         // shown name, upper case
    std::string error;          // Error: the status text (upper case, like the app shows it)
    bool driveLooked = false;   // Error: the simulated floppy still looked at the file (app: floppyDir())
};

// `data` holds the contents of `path`; when `path` is a TFMX sample file it is replaced by its mdat file.
// `exeReplayDir`: the program's own Replay folder for SC68 / YMST external replays ("" = none).
ChipRoute routeChipFile(const std::wstring& path, std::vector<uint8_t>& data, const std::wstring& exeReplayDir);

// Loads a routed chip song into the engine (stopped unless `play`). False when the player refuses it.
bool loadRoutedChip(Engine& engine, const ChipRoute& route, const std::vector<uint8_t>& mdat,
                    const std::wstring& exeReplayDir, bool play);

// Companion files of a song (IFF SMUS instruments): plain names in the song's own folder (or its Instruments
// subfolder), nothing else.
AmigaFileResolver songFolderFiles(const std::wstring& songPath);
// External replays of SC68 / YMST tunes: plain file names in the song's folder, its Replay subfolder or
// `exeReplayDir`.
AmigaFileResolver replayFiles(const std::wstring& songPath, const std::wstring& exeReplayDir);

// Whole file (at most 256 MB, like the app). nullptr = ok, else the status text.
const char* readWholeFile(const std::wstring& path, std::vector<uint8_t>& out);
