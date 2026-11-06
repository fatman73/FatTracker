ZX Spectrum AY tracker replayers (zx_trackers.*, zx_tracker_replayers.*)

Formats: Sound Tracker 1.x compiled / ST Song Compiler (.stc, Modland .st13), Sound Tracker 1.1 source
modules (Modland .st11, in Ay_Emul's ZXAYST11 wrapper or bare; converted to STC at load), Pro Tracker 2
(.pt2), ASC Sound Master 0.x/1.x (.asc), SQ-Tracker compiled (.sqt). Playback only, on ayumi (MIT).

Sources and licences:
- Players: Sergey Bulba's AY-3-8910/12 Emulator (Ay_Emul, (c) 1999-2004 S.V. Bulba,
  https://bulba.untergrund.net/), as ported to C/C++ in ayfly (https://github.com/l29ah/ayfly,
  src/libayfly/players/STCPlay.h, PT2Play.h, ASCPlay.h, SQTPlay.h, PT1Play.h). ayfly is distributed under the
  GNU General Public License, version 2 or (at your option) any later version. The C++ in
  zx_tracker_replayers.cpp follows those players and is distributed under the GPL as part of FatTracker.
- Structure checks and the Sound Tracker 1.1 source layout: modelled on ZXTune by Vitamin
  (https://github.com/vitamin-caig/zxtune, src/formats/chiptune/aym/, GNU LGPL v3). No ZXTune code is copied.

Differences from ayfly: bounds checked reads, bounded table searches and rows, 16 bit PT2 slides, ASC
portamento with a zero step count sets the slide at once, SQT channel state starts at zero. TurboSound and
the other ZX formats (PT1, STP, PSC, FTC, GTR ...) are not handled here.
