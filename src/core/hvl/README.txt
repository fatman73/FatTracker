HivelyTracker replayer by Pete Gordon (https://github.com/pete-gordon/hivelytracker), BSD 3-clause licence
(see LICENSE.txt). Plays AHX ("THX" header, versions 0 and 1) and HivelyTracker (.hvl, "HVL" header) modules.
Used by core/hvl_player.cpp.

Taken from the GitHub repository, folder Replayer_Windows, at commit f393ca7 (June 2023, the 1.9 replayer):
  hvl_replay.c, hvl_replay.h, hvl_tables.c, hvl_tables.h (line endings LF)
The Windows command line player (main.c, SDL/waveOut output) and the tracker itself are not used.

FatTracker changes (all marked "FatTracker" in the code):
  hvl_replay.h  - include guard, extern "C" for the C++ player; hvl_play_irq and hvl_mixchunk declared (the
                  player runs a tick and the mixer itself so it can see each voice for the scopes).
  hvl_tables.h  - the tables are declared extern (the header had plain declarations, i.e. a definition in
                  every file that included it); hvl_tables.c defines waves[] and the panning tables.
  hvl_replay.c  - hvl_InitReplayer generates the tables only once (HvlPlayer calls it through a C++ static).
                  Loaders (hostile or truncated files):
                  - files shorter than the header are refused; positions 0 or over 1000, track length 0 or
                    over 64, more than 64 instruments and (HVL) more than 16 channels are refused before
                    anything else is read (16+ channels overflowed the voices and the position entries);
                  - the size pass walks the subsongs, positions, tracks and instruments as offsets checked
                    against the end of the file and refuses the file when anything is missing, so the
                    fill pass that follows the same path stays inside the file;
                  - names are copied without reading past the end of the file;
                  - the tune is allocated zeroed (tracks the file does not store were uninitialised);
                  - HVL sub-song start positions past the last position become 0 (as AHX already did);
                    HVL stereo separation is held at 4 (it indexes 5 entry tables);
                  - wave lengths over 5 are held at 5 and play list wave forms over 4 become 0 (keep),
                    both indexed past their tables.
                  Playback:
                  - notes over 60 (a damaged file) are held at 60 for the tone portamento period lookup;
                  - a pattern break to row TrackLength goes to row 0 (was a row past the track), a
                    position jump past the last position restarts the song (was past the positions);
                  - the filter position used for the wave tables is kept to 1..63;
                  - the ring modulation step is never 0 (divided by) and hvl_DecodeFrame does nothing at
                    rates under 50 samples per tick (the mixer always wrote one sample).
