# FatTracker

A ProTracker-style music tracker for Windows: up to 32 channels, 16-bit samples, a vector UI
that scales smoothly to any window size, and one self-contained `FatTracker.exe` (~250 KB,
no installer, no DLLs).

**Stage 1 (this version):** loads and plays MOD (all ProTracker/Soundtracker variants up to
32 channels) and XM modules, with per-channel scopes and VU meters in the ProTracker 2.3F
look ("Hybryda" theme). Editing, saving, the sampler, Disk Op. and themes come in stage 2.

## Build

Requirements: Visual Studio 2026 (MSVC, C++ workload) and CMake ≥ 3.28.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output: `build\Release\FatTracker.exe` (the app) and `build\Release\fttest.exe` (tests).

## Run

Start `FatTracker.exe`, then drag a `.mod` / `.xm` file onto the window or press **Ctrl+O**.

| Key / mouse | Action |
|---|---|
| Right Alt or F5 | Play song |
| Right Ctrl | Play current pattern (loop) |
| Space | Stop |
| F11 | Pattern view: 4 → 8 → 16 channels |
| Tab / Shift+Tab | Scroll channels |
| Up/Down, PgUp/PgDn (stopped) | Scroll rows |
| Left/Right (stopped) | Previous / next pattern |
| Ctrl+Numpad 1–9 | Mute a visible channel |
| Click track header | Mute / unmute |
| Right-click track header | Solo |
| Alt+Enter | Fullscreen |
| PLAY / STOP / PATTERN buttons | As in ProTracker |

## Tests

```powershell
build\Release\fttest.exe                  # all tests
build\Release\fttest.exe mod_vibrato      # one test
build\Release\fttest.exe render in.mod out.wav [--interp none|linear|cubic] [--rate 48000]
build\Release\fttest.exe compare a.wav b.wav
```

Optional reference comparisons against OpenMPT renders: see `tests/ref/README.md`.

## Developer screenshot mode

`FatTracker.exe --shot out.png [width height [module [seconds [view]]]]` renders one frame
offscreen (optionally after playing `module` silently for `seconds`, with view 4/8/16).

## Layout

- `src/core` — song model, period tables, mixer, player (PT 2.3 / FT2 behaviour), offline render
- `src/formats` — MOD, XM, WAV
- `src/audio` — WASAPI output and the engine (song + player + mixer, telemetry for the UI)
- `src/ui` — Direct2D renderer, own vector font, theme, widgets, pattern view, play screen
- `src/app` — window, input, file loading
- `docs/superpowers` — design spec and implementation plans
