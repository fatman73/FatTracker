# FatTracker Stage 5 — Amiga Formats Implementation Plan
> Executed inline (executing-plans), TDD per task.
**Spec:** docs/superpowers/specs/2026-11-05-fattracker-stage5-amiga-formats-design.md
## Global Constraints
- Files only inside C:\development\FatTracker; no exceptions/RTTI; core/formats without Windows headers.
## Review Focus
- Corrupt / truncated files (offsets past the end, huge counts) must fail cleanly.
- Songs above the model limits (64 MED tracks, 128 DBM channels, >256 blocks) load truncated, flagged.
- Tempo conversions (MED SPD/BPM/8ch) give plausible BPM 32..255.
- Samples: 16-bit big endian, delta, stereo, loops past the end.
- TFMX: missing smpl file, infinite macro loops (must not hang the audio thread).
### Task 1: OctaMED loader (src/formats/med.cpp, tests/test_med.cpp)
### Task 2: DigiBooster 1.x + Pro (src/formats/dbm.cpp, tests/test_dbm.cpp) + download test modules
### Task 3: Symphonie (src/formats/symmod.cpp, tests/test_symmod.cpp)
### Task 4: TFMX player (src/core/tfmx.*, engine/UI integration, tests/test_tfmx.cpp)
### Task 5: Final review, fixes, merge
