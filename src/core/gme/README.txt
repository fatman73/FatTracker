Game_Music_Emu (libgme, https://github.com/libgme/game-music-emu), originally by Shay Green (blargg),
GNU LGPL 2.1 (LICENSE.txt). ext/emu2413 by Mitsutaka Okazaki, MIT (ext/LICENSE).
Used unchanged apart from LF line endings and these changes, all marked FatTracker:
- Ay_Emu.cpp: a block offset outside a truncated file was copied from a null pointer.
- Per-voice meters and scopes (none of them changes the mixed output):
  - Voice_Tap_Buffer.h/.cpp (new): a Multi_Buffer giving each voice of a Classic_Emu (NSF, GBS, SAP, AY, HES,
    KSS, PSG-only VGM) Blip_Buffers of its own; at the end of a frame their deltas are added into the wrapped
    buffer (so its mix is bit-identical) and into one buffer per voice that is read alongside.
  - Spc_Dsp.h/.cpp: each voice's output per DSP sample into an optional ring (ft_set_voice_taps);
    Snes_Spc.h / Spc_Emu.h: accessors to reach the DSP.
  - Ym2612_Nuked.h/.cpp: each channel's output per chip sample into an optional ring.
  - Vgm_Emu_Impl.h/.cpp, Vgm_Emu.h/.cpp: with FM, the PCM DAC and the PSG write into buffers of their own that
    are moved into blip_buf at the end of each frame (bit-identical) and read per voice.
Built with BLARGG_LITTLE_ENDIAN and VGM_YM2612_NUKED (CMakeLists.txt). gme_types.h is the library's default
(all formats). Wrapped by core/gme_player.cpp.
