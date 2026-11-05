# FatTracker Stage 3 — Pure Mode Implementation Plan

> Executed inline (executing-plans); TDD per task.

**Goal:** Amiga "Pure" mode: Paula sound (A500/A1200 + LED), floppy drive sounds, classic theme.
**Spec:** docs/superpowers/specs/2026-11-05-fattracker-stage3-pure-design.md

## Global Constraints
- All files only inside C:\development\FatTracker (temp/shots in build/).
- No exceptions/RTTI, no external libraries; exe < 1 MB.
- core/ stays free of Windows headers.

## Review Focus
- Switching Pure on/off while playing (no clicks stuck, no crash, filter state reset).
- Songs with >4 channels and XM songs in Pure.
- Pending (delayed) load replaced by another load or app close.
- Missing/corrupt floppy WAV files → synthesis fallback.
- Output rate changes (device switch) → filter coefficients recomputed.

### Task 1: PaulaFilter + Mixer Paula path
Files: src/core/paula.h/.cpp, src/core/mixer.h/.cpp, tests/test_paula.cpp
Produces: `enum class PaulaModel {None, A500, A1200}`; `Mixer::setPaula(PaulaModel)`, `setLed(bool)`, `led()`, `paula()`.
Tests: paula_hard_pan_lrrl, paula_8bit_dac, paula_zoh_holds_samples, paula_a500_attenuates_highs,
paula_led_attenuates_more, paula_a1200_keeps_highs.

### Task 2: E0x LED in the player
Files: src/core/player_fx.cpp, player.cpp, tests/test_paula.cpp
Tests: mod_e00_turns_led_on_e01_off, xm_e0x_ignored, play_from_start_turns_led_off.

### Task 3: Floppy simulator
Files: src/core/floppy.h/.cpp, tests/test_floppy.cpp
Produces: `Floppy::setRate(uint32_t)`, `setSounds(const Sample* motor, const Sample* step, const Sample* insert)`,
`double read(uint32_t bytes)` (returns seconds), `dir()`, `write(uint32_t)`, `insert()`, `double busySeconds() const`,
`void mix(float* out, uint32_t frames, float gain)`.
Tests: floppy_read_time_matches_model, floppy_silent_after_motor_off, floppy_user_step_sample_used, floppy_read_is_capped.

### Task 4: Engine + settings + UI
Files: src/audio/engine.*, src/ui/setup.cpp, play_screen.*, disk_op.cpp, src/app/main.cpp, src/ui/theme.cpp
- Engine: `setPure(int model)`, `setDriveVolume(float)`, `floppyRead/Dir/Write/Insert`, `floppyBusy()`, `led()`.
- SETUP rows: DRIVE VOLUME, PURE MODE OFF/A500/A1200; ini PureMode, DriveVolume, ThemeBeforePure.
- Logo subtitle in Pure. Classic theme pattern text blue (user: "patterny niebieskie a nie fioletowe").
- main.cpp: delayed load in Pure (pending song, timer, key/click skips), floppy sounds loaded from sounds\.
Verification: build, fttest, --shot with T:/P:SETUP in Pure.

### Task 5: Final review, fixes, merge, launch.
