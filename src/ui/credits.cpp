#include "ui/credits.h"

namespace credits {
namespace {
// Keep in step with CREDITS.txt (tests/test_credits.cpp compares the authors, components and web addresses).
// Only characters of the Pure mode pixel font: letters, digits, space and . , : - _ / ( ) ' " # + = ! ? % * < > &
const char* const kIntro[] = {
    "FatTracker - Copyright (C) 2026 fatman73 - https://github.com/fatman73/FatTracker",
    "FatTracker is free software under the GNU GPL v2 or later, WITHOUT ANY WARRANTY (see LICENSE).",
    "It includes or is based on the following free software. Many thanks to their authors.",
};

struct Entry {
    const char* what;     // what it plays or does
    const char* who;      // the component and its authors
    const char* licence;
    const char* url;      // may be empty
};
const Entry kEntries[] = {
    {"TFMX decoder (Chris Huelsbeck's TFMX)", "libtfmxaudiodecoder by Michael Schwendt", "GNU GPL v2 or later",
     "https://github.com/mschwendt/libtfmxaudiodecoder"},
    {"Future Composer 1.0-1.4 and Jochen Hippel formats (TFMX, COSO, MCMD)", "libtfmxaudiodecoder by Michael Schwendt",
     "GNU GPL v2 or later", ""},
    {"C64 SID emulation (6502, SID, CIA, VIC)", "cRSID by Mihaly \"Hermit\" Horvath (copy from DOjS by SuperIlu)",
     "\"Do what you want, mention the author\"", "https://csdb.dk/scener/?id=8261"},
    {"AY-3-8910 / YM2149 emulation", "ayumi by Peter Sovietov", "MIT", "https://github.com/true-grue/ayumi"},
    {"SNES SPC, Sega Mega Drive / Master System VGM, NES NSF, Game Boy GBS, Atari 8-bit SAP, ZX Spectrum / Amstrad AY, "
     "PC Engine HES, MSX KSS",
     "Game_Music_Emu by Shay Green (blargg) and the libgme authors", "GNU LGPL 2.1 or later",
     "https://github.com/libgme/game-music-emu"},
    {"YM2612 emulation (in Game_Music_Emu)", "Nuked OPN2 (YM2612) by Alexey Khokholov (Nuke.YKT)",
     "GNU LGPL 2.1 or later", ""},
    {"YM2413 emulation (in Game_Music_Emu)", "emu2413 (YM2413) by Mitsutaka Okazaki", "MIT", ""},
    {"Amiga replayers: JamCracker, Delta Music 1.0 / 2.0, SidMon 1.0 / 2.0, SoundMon V.2 / V.3, Digital Mugician 1 / 2, "
     "David Whittaker, IFF SMUS, Sonic Arranger, Art Of Noise",
     "ported from NostalgicPlayer by Thomas Neumann (the original replayers were written by the authors of those "
     "music programs)",
     "MIT", "https://github.com/neumatho/NostalgicPlayer"},
    {"AHX and HivelyTracker", "HivelyTracker replayer by Pete Gordon (Xeron), AHX by Dexter / Abyss", "BSD 3-clause",
     "https://github.com/pete-gordon/hivelytracker"},
    {"Motorola 68000 emulation (Atari ST SNDH / SC68 / YMST)", "Musashi by Karl Stenerud", "MIT",
     "https://github.com/kstenerud/Musashi"},
    {"ZX Spectrum Pro Tracker 3 / Vortex Tracker II", "PT3Play by Sergey Bulba, C# port PT3PlayBlazor by Ben Baker",
     "MIT (port), with code from ayfly", "https://github.com/benbaker76/PT3PlayBlazor"},
    {"ZX Spectrum Sound Tracker, ST Song Compiler, Pro Tracker 2, ASC Sound Master, SQ-Tracker",
     "players from Sergey Bulba's Ay_Emul, as ported in ayfly, format checks modelled on ZXTune by Vitamin "
     "(no code copied)",
     "GNU GPL v2 or later (ZXTune: GNU LGPL v3)", "https://github.com/l29ah/ayfly"},
    {"Module loaders: S3M, IT (incl. IT214 / IT215 sample decompression), MTM, 669, OctaMED, DigiBooster, Symphonie",
     "structures and effect conversion follow OpenMPT (OpenMPT Devs, the Symphonie loader by Devin Acker)",
     "BSD 3-clause", "https://openmpt.org"},
    {"Own code after known algorithms",
     "the LH5 (LHA) unpacker, the deflate / gzip decoder (after puff by Mark Adler, zlib), the ICE 2.4 depacker", "",
     ""},
};
const char* const kOutro[] = {
    "The music in the modules folder (test tunes from the High Voltage SID Collection, Modland, the SNDH archive "
    "and other archives) belongs to its composers. It is not distributed with FatTracker, nor are the sc68 replay "
    "routines (sc68 by Benjamin Gerard, GNU GPL).",
};

// Word wrap: continuation lines are indented by `cont`; a word longer than a line is cut after a '/' if it can be.
void wrap(std::vector<Line>& out, const char* s, int indent, int cont, int cols, bool heading) {
    std::string cur(indent, ' ');
    bool empty = true;
    auto flush = [&] {
        out.push_back({cur, heading});
        cur.assign(cont, ' ');
        empty = true;
    };
    for (const char* p = s; *p;) {
        while (*p == ' ') ++p;
        const char* q = p;
        while (*q && *q != ' ') ++q;
        std::string w(p, q);
        p = q;
        while (!w.empty()) {
            if ((int)(cur.size() + (empty ? 0 : 1) + w.size()) <= cols) {
                if (!empty) cur += ' ';
                cur += w;
                empty = false;
                break;
            }
            if (!empty) {
                flush();
                continue;
            }
            const size_t room = (size_t)cols - cur.size();
            size_t cut = room;
            const size_t slash = w.rfind('/', room - 1);
            if (slash != std::string::npos && slash > 0) cut = slash + 1;
            cur += w.substr(0, cut);
            w.erase(0, cut);
            flush();
        }
    }
    if (!empty) flush();
}
}  // namespace

std::vector<Line> lines(int cols) {
    if (cols < 16) cols = 16;
    std::vector<Line> out;
    for (const char* s : kIntro) wrap(out, s, 0, 0, cols, s == kIntro[0]);  // paragraphs: no indent, so the address fits
    out.push_back({"", false});
    for (const Entry& e : kEntries) {
        wrap(out, e.what, 0, 2, cols, true);
        wrap(out, e.who, 2, 4, cols, false);
        if (*e.licence) wrap(out, e.licence, 2, 4, cols, false);
        if (*e.url) wrap(out, e.url, 2, 4, cols, false);
        out.push_back({"", false});
    }
    for (const char* s : kOutro) wrap(out, s, 0, 0, cols, false);
    return out;
}
}  // namespace credits
