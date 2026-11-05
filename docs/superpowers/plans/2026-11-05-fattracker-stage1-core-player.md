# FatTracker — Plan 1: rdzeń grający (etap 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Jeden `FatTracker.exe`, który wczytuje MOD i XM (przeciągnij-upuść / Ctrl+O), gra je wiernie przez WASAPI i pokazuje ekran w stylu PT 2.3F (motyw Hybryda): liczniki, oscyloskopy, podgląd patternu 4/8/16 z VU-metrami w kolumnach — wszystko wektorowo i płynnie skalowane.

**Architecture:** `ftcore` (biblioteka statyczna, czysty C++ bez Windows: model utworu, tablice, odtwarzacz, mikser, loadery) + `FatTracker` (Win32 + Direct2D/DirectWrite + WASAPI) + `fttest` (testy jednostkowe i render offline do WAV). Odtwarzacz steruje mikserem; wątek WASAPI woła `Engine::render`, UI czyta telemetrię przez seqlock.

**Tech Stack:** C++20, MSVC (VS 2026, generator „Visual Studio 18 2026”), CMake ≥ 3.28, Direct2D 1.1, DirectWrite, DXGI flip-model, WASAPI shared event-driven, MMCSS (`avrt`).

**Spec:** `docs/superpowers/specs/2026-11-05-fattracker-core-design.md`

## Global Constraints

- Tylko Windows 10/11 x64. Wszystkie pliki wyłącznie w `C:\development\FatTracker`.
- Zero zewnętrznych bibliotek; linkowane tylko: `d2d1 dwrite d3d11 dxgi ole32 avrt uuid shell32 comdlg32`.
- Release: `/O1 /GL /Gy /GR- /EHs-c- /D_HAS_EXCEPTIONS=0 /utf-8`, link `/LTCG /OPT:REF /OPT:ICF`, statyczny CRT (`/MT`). Bez wyjątków (błędy przez `Result<T>`), bez RTTI, bez `iostream`.
- `src/core` i `src/formats` nie dołączają żadnego nagłówka Windows.
- Cel rozmiaru: `FatTracker.exe` < 1 MB (1 048 576 B) — ostrzeżenie w buildzie przy przekroczeniu.
- Płótno logiczne 1920×1080, skalowane jednolicie z zachowaniem proporcji; wszystko rysowane wektorowo z AA, żadnych skalowanych bitmap (atlas fontu rasteryzowany w docelowej skali urządzenia to nie skalowanie).
- Font i grafika własne „w duchu ProTrackera” — nie kopiujemy fontu ani grafik PT.
- Komunikaty STATUS wielkimi literami w stylu PT, np. `ERROR: NOT A MODULE`, `ALL RIGHT`.
- Maks. 32 kanały, sample 8/16-bit przechowywane natywnie.
- Commit po każdym zadaniu; tożsamość git jest już ustawiona lokalnie w repo. Wiadomości commitów kończą się linią `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

### Komendy (PowerShell, katalog `C:\development\FatTracker`)

- Konfiguracja (raz): `cmake -S . -B build -G "Visual Studio 18 2026" -A x64`
- Build: `cmake --build build --config Release`
- Testy: `build\Release\fttest.exe` → ostatnia linia `ALL TESTS PASSED (N)`, kod wyjścia 0
- Pojedynczy test: `build\Release\fttest.exe <nazwa_testu>`

## Review Focus

1. **Obcięte / uszkodzone pliki** (dane sampli krótsze niż w nagłówku, absurdalna liczba patternów, długości 0xFFFFFFFF) → wczytanie bez crasha, brakujące dane = cisza, ostrzeżenie; nigdy alokacja > rozmiar pliku × 4 dla danych z pliku. Testy: Task 3 `mod_truncated_*`, Task 4 `xm_truncated_*`, `xm_huge_counts_rejected`.
2. **Zdegenerowane pętle** (długość pętli 0/1, koniec pętli za końcem sampla, ping-pong na 1 próbce, offset 9xx za końcem) → brak crasha i brak nieskończonej pętli w mikserze. Testy: Task 5 `mixer_degenerate_loops`, Task 7 `mod_9xx_offset_past_end`.
3. **Nieskończone utwory** (Bxx do siebie, D00 w kółko, E6 w pętli) → render offline kończy się (`loopedOnce()` lub limit czasu). Testy: Task 6 `player_looped_once_*`, Task 9 `render_cli_terminates_on_infinite_song`.
4. **Okno zminimalizowane / 0×0 / bardzo małe i 4K** → brak crasha, brak rysowania przy 0, ostre rysowanie przy 4K. Test: Task 10 `viewport_*`; ręcznie w Task 13.
5. **32 kanały z interpolacją sześcienną** → render w czasie rzeczywistym z dużym zapasem. Test: Task 5 `mixer_perf_32_voices_cubic` (10 s audio w < 0,5 s w Release).

---

## Mapa plików

```
CMakeLists.txt                  cele: ftcore, fttest, FatTracker; flagi; size check
cmake/sizecheck.cmake           ostrzeżenie gdy exe > 1 MB
src/core/result.h               Result<T>
src/core/reader.h/.cpp          ByteReader z kontrolą granic
src/core/song.h/.cpp            Cell, Sample, Envelope, Instrument, Pattern, Song
src/core/tables.h/.cpp          periody PT, sinus vibrato, częstotliwości XM
src/core/mixer.h/.cpp           Mixer (głosy, interpolacja, rampy, pan, VU, scope, limiter)
src/core/player.h/.cpp          Player: sekwencja, ticki, wyzwalanie nut
src/core/player_fx.cpp          efekty 0–Z i kolumna głośności
src/core/player_inst.cpp        instrumenty XM: obwiednie, fadeout, autovibrato
src/formats/formats.h/.cpp      loadModule(): wykrycie i wybór loadera
src/formats/mod.cpp             loader MOD
src/formats/xm.cpp              loader XM
src/formats/wav.h/.cpp          zapis WAV (16-bit / float) — render i (w planie 2) eksport
src/audio/audio_out.h/.cpp      WASAPI
src/audio/engine.h/.cpp         Engine: Song + Player + Mixer + zamek + telemetria
src/ui/gfx.h/.cpp               Direct2D, viewport, prymitywy, glow
src/ui/theme.h/.cpp             Theme + preset Hybryda
src/ui/font_data.cpp            własne glify 8×8
src/ui/font.h/.cpp              atlas glifów i rysowanie tekstu
src/ui/widgets.h/.cpp           panel, licznik, VU, oscyloskop
src/ui/pattern_view.h/.cpp      podgląd patternu 4/8/16 z VU w kolumnach
src/ui/play_screen.h/.cpp       ekran główny etapu 1
src/app/main.cpp                WinMain, okno, pętla, klawisze, drag&drop, Ctrl+O
res/FatTracker.rc, res/app.manifest, res/icon.ico (ikona generowana skryptem w Task 13)
tests/test.h, tests/test_main.cpp   mini-runner
tests/build_mod.h/.cpp          budowanie bajtów MOD w testach
tests/build_xm.h/.cpp           budowanie bajtów XM w testach
tests/test_*.cpp                testy per moduł
tools/make_refs.ps1             opcjonalne wzorce WAV z openmpt123
```

---

### Task 1: Szkielet buildu, Result, ByteReader, runner testów

**Files:**
- Create: `CMakeLists.txt`, `cmake/sizecheck.cmake`, `.gitignore` (`build/`), `src/core/result.h`, `src/core/reader.h`, `src/core/reader.cpp`, `tests/test.h`, `tests/test_main.cpp`, `tests/test_reader.cpp`

**Interfaces:**
- Produces:
  - `template<class T> class Result { static Result ok(T&&); static Result fail(const char* msg); bool ok() const; T& value(); const char* error() const; }` — `msg` to stały literał.
  - `class ByteReader { ByteReader(const uint8_t* d, size_t n); uint8_t u8(); uint16_t u16le(); uint16_t u16be(); uint32_t u32le(); uint32_t u32be(); void bytes(void* dst, size_t n); void skip(size_t n); void seek(size_t pos); size_t pos() const; size_t size() const; size_t remaining() const; bool overrun() const; }` — odczyt za końcem zwraca 0 / zeruje `dst` i ustawia `overrun()`.
  - Runner: `TEST(name)`, `CHECK(cond)`, `CHECK_EQ(a,b)`, `CHECK_NEAR(a,b,eps)`; `fttest` bez argumentów uruchamia wszystkie testy, z argumentem — test o tej nazwie; argumenty `render`/`compare` zarezerwowane dla Task 9.
  - Cele CMake: `ftcore` (STATIC, `src/core/*.cpp`, `src/formats/*.cpp`), `fttest` (konsola, linkuje `ftcore`), `FatTracker` (WIN32; w tym zadaniu pusty `WinMain` zwracający 0 w `src/app/main.cpp`).

- [ ] **Step 1: Napisz testy `reader_reads_le_be`, `reader_overrun_returns_zero`, `reader_bytes_zero_fills`, `reader_seek_past_end`**

```cpp
TEST(reader_reads_le_be) {
  const uint8_t d[] = {0x34,0x12,0x12,0x34,0x78,0x56,0x34,0x12};
  ByteReader r(d, sizeof d);
  CHECK_EQ(r.u16le(), 0x1234); CHECK_EQ(r.u16be(), 0x1234); CHECK_EQ(r.u32le(), 0x12345678u);
  CHECK(!r.overrun()); CHECK_EQ(r.remaining(), 0u);
}
TEST(reader_overrun_returns_zero) {
  const uint8_t d[] = {0xFF};
  ByteReader r(d, 1); r.u8();
  CHECK_EQ(r.u32be(), 0u); CHECK(r.overrun());
}
TEST(reader_bytes_zero_fills) {
  const uint8_t d[] = {1,2};
  ByteReader r(d, 2); uint8_t out[4] = {9,9,9,9}; r.bytes(out, 4);
  CHECK_EQ(out[0],1); CHECK_EQ(out[1],2); CHECK_EQ(out[2],0); CHECK_EQ(out[3],0); CHECK(r.overrun());
}
TEST(reader_seek_past_end) {
  const uint8_t d[] = {1}; ByteReader r(d, 1); r.seek(100);
  CHECK_EQ(r.u8(), 0); CHECK(r.overrun()); CHECK_EQ(r.remaining(), 0u);
}
```

- [ ] **Step 2: Skonfiguruj i zbuduj — oczekiwany błąd kompilacji (brak `ByteReader`)**

Run: `cmake -S . -B build -G "Visual Studio 18 2026" -A x64; cmake --build build --config Release`
Expected: błąd `ByteReader: undeclared identifier`.

- [ ] **Step 3: Zaimplementuj `result.h`, `reader.h/.cpp`, runner i `CMakeLists.txt`**

`CMakeLists.txt`: `CMAKE_MSVC_RUNTIME_LIBRARY` = `MultiThreaded$<$<CONFIG:Debug>:Debug>`, flagi z Global Constraints dla Release, `/W4`, `target_include_directories(ftcore PUBLIC src)`, `POST_BUILD` dla `FatTracker`: `cmake -DFILE=$<TARGET_FILE:FatTracker> -P cmake/sizecheck.cmake` (wypisuje rozmiar; `message(WARNING ...)` gdy > 1048576). `fttest` wypisuje `FAIL <test>: <plik>:<linia> <wyrażenie>` przy porażce, na końcu `ALL TESTS PASSED (N)` albo `FAILED M of N`; kod wyjścia = liczba porażek.

- [ ] **Step 4: Zbuduj i uruchom testy**

Run: `cmake --build build --config Release; build\Release\fttest.exe`
Expected: `ALL TESTS PASSED (4)`; w logu buildu linia z rozmiarem `FatTracker.exe`.

- [ ] **Step 5: Commit** — `git add -A; git commit -m "build: scaffold CMake targets, Result, ByteReader, test runner"`

---

### Task 2: Model utworu i tablice

**Files:**
- Create: `src/core/song.h`, `src/core/song.cpp`, `src/core/tables.h`, `src/core/tables.cpp`, `tests/test_song.cpp`

**Interfaces:**
- Produces (`song.h`):
  - Nuty: `uint8_t note` — 0 = brak, 1..96 = C-0..B-7 (numeracja XM, nuta 49 = C-4 = 8363 Hz przy relative 0), 97 = key-off. `constexpr uint8_t NOTE_NONE=0, NOTE_OFF=97;`
  - `struct Cell { uint8_t note, instr, vol, fx, param; };` — `vol` w kodowaniu XM (0 = pusto, 0x10–0x50 = głośność 0–64, 0x60+ komendy); `fx` 0..35 (0–9, A–Z = 10–35); `fx==0 && param==0` = brak efektu. MOD `Exy` = `fx 14, param xy`.
  - `enum class LoopType : uint8_t { None, Forward, PingPong };`
  - `struct Sample { char name[23]; bool is16; std::vector<int8_t> data8; std::vector<int16_t> data16; uint32_t length, loopStart, loopLength; LoopType loop; uint8_t volume /*0-64*/; int8_t finetune /*-128..127*/; int8_t relativeNote; uint8_t pan /*0-255*/; };`
  - `struct EnvPoint { uint16_t tick; uint8_t value; }; struct Envelope { EnvPoint pts[12]; uint8_t count, sustain, loopStart, loopEnd, flags; }; constexpr uint8_t ENV_ON=1, ENV_SUSTAIN=2, ENV_LOOP=4;`
  - `struct Instrument { char name[23]; uint16_t sampleMap[96]; /* indeks w Song::samples */ Envelope volEnv, panEnv; uint16_t fadeout; uint8_t vibType, vibSweep, vibDepth, vibRate; };`
  - `struct Pattern { uint16_t rows; uint8_t channels; std::vector<Cell> cells; Cell& at(int row, int ch); const Cell& at(int row, int ch) const; };`
  - `enum class FreqMode : uint8_t { Amiga, Linear }; enum class Quirks : uint8_t { PT23, FT2 };`
  - `struct Song { char name[21]; uint8_t numChannels; std::vector<uint8_t> order; uint8_t restartPos; uint8_t initialSpeed /*6*/, initialBpm /*125*/; uint8_t globalVolume /*64*/; FreqMode freqMode; Quirks quirks; uint8_t channelPan[32]; std::vector<Pattern> patterns; std::vector<Sample> samples; std::vector<Instrument> instruments; };`
  - `Song makeEmptySong(int channels);` — 1 pattern 64 wierszy, order `{0}`, pan kanałów L-R-R-L powtarzane: L = 0x40, R = 0xC0 (separacja 75% wokół środka 0x80), `freqMode=Amiga`, `quirks=FT2`.
  - Komórka `Cell.instr` = numer instrumentu, gdy `instruments` niepuste, inaczej numer sampla (1-based).
- Produces (`tables.h`):
  - `extern const uint16_t kPtPeriods[16][36];` — indeks finetune 0..15 (0..7, potem −8..−1), oktawy PT 1–3 (C-1..B-3).
  - `extern const uint8_t kVibratoSine[32];` — `{0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24}`.
  - `constexpr double kPalClock = 3546895.0;` `double ptPeriodToHz(uint32_t period)` = `kPalClock / period`.
  - `int ptNoteToXm(int ptIndex)` — indeks 0..35 w tablicy PT → nuta XM: PT C-1 (856) = nuta 37 (C-3).
  - `uint32_t xmLinearPeriod(int note0, int finetune)` = `7680 - note0*64 - finetune/2` (note0: 0 = C-0, z doliczonym relativeNote); `double xmLinearHz(uint32_t period)` = `8363 * 2^((4608 - period)/768.0)`.
  - `uint32_t xmAmigaPeriod(int note0, int finetune); double xmAmigaHz(uint32_t period);` — wg formuły z dokumentacji XM (tabela 96 wartości z interpolacją po finetune).

- [ ] **Step 1: Napisz testy**

```cpp
TEST(pt_periods_finetune0) { CHECK_EQ(kPtPeriods[0][0], 856); CHECK_EQ(kPtPeriods[0][12], 428); CHECK_EQ(kPtPeriods[0][24], 214); CHECK_EQ(kPtPeriods[0][35], 113); }
TEST(pt_periods_finetune_minus8) { CHECK_EQ(kPtPeriods[8][0], 907); }
TEST(pt_period_hz) { CHECK_NEAR(ptPeriodToHz(428), 8287.14, 0.01); }
TEST(pt_note_to_xm) { CHECK_EQ(ptNoteToXm(0), 37); CHECK_EQ(ptNoteToXm(12), 49); }
TEST(xm_linear_c4) { CHECK_EQ(xmLinearPeriod(48, 0), 4608u); CHECK_NEAR(xmLinearHz(4608), 8363.0, 0.001); CHECK_NEAR(xmLinearHz(xmLinearPeriod(60, 0)), 16726.0, 0.01); }
TEST(xm_amiga_c4) { CHECK_NEAR(xmAmigaHz(xmAmigaPeriod(48, 0)), 8363.0, 2.0); CHECK_NEAR(xmAmigaHz(xmAmigaPeriod(60, 0)) / xmAmigaHz(xmAmigaPeriod(48, 0)), 2.0, 0.01); }
TEST(empty_song_defaults) {
  Song s = makeEmptySong(8);
  CHECK_EQ(s.numChannels, 8); CHECK_EQ(s.patterns.size(), 1u); CHECK_EQ(s.patterns[0].rows, 64);
  CHECK_EQ(s.patterns[0].cells.size(), 64u*8); CHECK_EQ(s.initialSpeed, 6); CHECK_EQ(s.initialBpm, 125);
  CHECK_EQ(s.channelPan[0], 0x40); CHECK_EQ(s.channelPan[1], 0xC0); CHECK_EQ(s.channelPan[2], 0xC0); CHECK_EQ(s.channelPan[3], 0x40);
}
```

- [ ] **Step 2: Uruchom — oczekiwany błąd kompilacji** (`kPtPeriods` niezdefiniowane).
- [ ] **Step 3: Zaimplementuj `song.*` i `tables.*`** — tablica `kPtPeriods` to pełna tablica PT 2.3 (16×36), wpisana jako dane.
- [ ] **Step 4: Build + `build\Release\fttest.exe`** → `ALL TESTS PASSED (11)`.
- [ ] **Step 5: Commit** — `feat(core): song model and period/frequency tables`

---

### Task 3: Loader MOD (+ budowniczy MOD w testach)

**Files:**
- Create: `src/formats/formats.h`, `src/formats/formats.cpp`, `src/formats/mod.cpp`, `tests/build_mod.h`, `tests/build_mod.cpp`, `tests/test_mod.cpp`

**Interfaces:**
- Consumes: `ByteReader`, `Result`, `Song`, `kPtPeriods`, `ptNoteToXm`.
- Produces:
  - `Result<Song> loadModule(const uint8_t* data, size_t size);` (`formats.h`) — próbuje loaderów w kolejności: XM (Task 4), MOD 31 sampli (po sygnaturze), Soundtracker 15 sampli (heurystyka). Nic nie pasuje → `fail("NOT A MODULE")`.
  - `bool modDetect(const uint8_t*, size_t); Result<Song> modLoad(const uint8_t*, size_t);` oraz `bool stDetect(...); Result<Song> stLoad(...)` (wewnętrzne, zadeklarowane w `formats.cpp`).
  - Ostrzeżenia wczytywania trzymamy w `Song`: dodaj do `Song` pole `uint8_t loadFlags = 0` i `constexpr uint8_t LOAD_TRUNCATED = 1;` (zmiana w `song.h`).
  - Test helper: `struct ModCell { int ptNoteIdx /*-1 brak, 0..35*/; int sample; int fx; int param; }; struct ModSample { int lenWords; int finetune; int volume; int repStartWords; int repLenWords; std::vector<int8_t> data; }; std::vector<uint8_t> buildMod(const char sig[4], int channels, const std::vector<uint8_t>& order, const std::vector<std::vector<ModCell>>& patterns /* [pattern][row*ch + c], 64 wierszy */, const std::vector<ModSample>& samples);`

Reguły MOD (z PT 2.3): nazwa 20 B, 31 × (nazwa 22, długość w słowach BE, finetune nibble ze znakiem, głośność ≤ 64 (clamp), repeat start w słowach, repeat length w słowach), długość order (clamp do 128), restart (ignorowany przez PT → `restartPos=0`), order 128 B, sygnatura. Kanały: `M.K.`,`M!K!`,`M&K!`,`FLT4`,`4CHN` → 4; `FLT8`,`8CHN` → 8; `xCHN` → x (1–9); `xxCH` → xx (10–32); `xxCN` → xx. Liczba patternów = max(order)+1. Komórka 4 B: sampel = (b0 & 0xF0) | (b2 >> 4), period = ((b0 & 0x0F) << 8) | b1, efekt = b2 & 0x0F, param = b3. Period → nuta: najbliższa wartość w `kPtPeriods[0]` → `ptNoteToXm`; period 0 → brak nuty. Pętla: `repLenWords <= 1` → brak pętli; repeat w bajtach = słowa × 2; koniec pętli > długość → przytnij pętlę do długości. Finetune MOD (−8..7) zapisany jako ×16 w `Sample::finetune`; `freqMode=Amiga`, `quirks=PT23`, pan L-R-R-L z separacją 75% (0x40/0xC0). Dane sampli 8-bit ze znakiem; brakujące bajty = 0 i `loadFlags |= LOAD_TRUNCATED`. Soundtracker: 15 sampli, brak sygnatury, heurystyka: wszystkie głośności ≤ 64, długość order 1..128, wszystkie wpisy order < 64, rozmiar pliku ≥ 600 + 1024 × liczba patternów.

- [ ] **Step 1: Napisz testy**

```cpp
TEST(mod_signature_channels) {  // dla każdej pary sygnatura → kanały
  struct { const char* sig; int ch; } cases[] = {{"M.K.",4},{"FLT8",8},{"6CHN",6},{"16CH",16},{"32CH",32}};
  for (auto& c : cases) { auto b = buildMod(c.sig, c.ch, {0}, {std::vector<ModCell>(64*c.ch, {-1,0,0,0})}, {});
    auto r = loadModule(b.data(), b.size()); CHECK(r.ok()); CHECK_EQ(r.value().numChannels, c.ch); }
}
TEST(mod_cell_decode) { // sampel 17, C-2 (idx 12 → nuta 49), efekt C 40
  std::vector<ModCell> p(64*4, {-1,0,0,0}); p[0] = {12, 17, 0xC, 0x40};
  auto b = buildMod("M.K.", 4, {0}, {p}, std::vector<ModSample>(17, {1,0,64,0,1,{0,0}}));
  auto s = loadModule(b.data(), b.size()).value(); auto& c = s.patterns[0].at(0,0);
  CHECK_EQ(c.note, 49); CHECK_EQ(c.instr, 17); CHECK_EQ(c.fx, 0xC); CHECK_EQ(c.param, 0x40);
  CHECK(s.quirks == Quirks::PT23); CHECK(s.freqMode == FreqMode::Amiga);
}
TEST(mod_sample_fields) { // len 100 słów, finetune -1 (nibble 0xF), vol 70→64, rep 10/20 słów
  auto b = buildMod("M.K.", 4, {0}, {std::vector<ModCell>(256,{-1,0,0,0})}, {{100, -1, 70, 10, 20, std::vector<int8_t>(200, 5)}});
  auto& sm = loadModule(b.data(), b.size()).value().samples[0];
  CHECK_EQ(sm.length, 200u); CHECK_EQ(sm.finetune, -16); CHECK_EQ(sm.volume, 64);
  CHECK(sm.loop == LoopType::Forward); CHECK_EQ(sm.loopStart, 20u); CHECK_EQ(sm.loopLength, 40u); CHECK_EQ(sm.data8[0], 5);
}
TEST(mod_replen_one_means_no_loop)  // repLenWords = 1 → LoopType::None
TEST(mod_loop_end_clamped)          // rep 90 + 20 słów przy len 100 → loopLength = 20 bajtów (do końca)
TEST(mod_truncated_sample_data)     // obetnij ostatnie 50 B → ok(), data8.size()==length, ostatnie bajty 0, loadFlags & LOAD_TRUNCATED
TEST(mod_truncated_header_rejected) // 500 B śmieci → !ok(), error() == "NOT A MODULE"
TEST(mod_soundtracker_15)           // plik 15-samplowy bez sygnatury → ok(), numChannels 4, samples.size()==15
```

- [ ] **Step 2: Uruchom — porażka** (`loadModule` niezdefiniowane).
- [ ] **Step 3: Zaimplementuj `formats.cpp` (kolejność wykrywania) i `mod.cpp`; dodaj `loadFlags` do `Song`.** Limit alokacji: nigdy więcej niż `size*4` bajtów na dane sampli/patternów z pliku.
- [ ] **Step 4: Build + testy** → wszystkie przechodzą.
- [ ] **Step 5: Commit** — `feat(formats): MOD/Soundtracker loader with truncation handling`

---

### Task 4: Loader XM (+ budowniczy XM w testach)

**Files:**
- Create: `src/formats/xm.cpp`, `tests/build_xm.h`, `tests/build_xm.cpp`, `tests/test_xm.cpp`
- Modify: `src/formats/formats.cpp` (XM pierwszy w kolejności)

**Interfaces:**
- Consumes: jak w Task 3.
- Produces: `bool xmDetect(const uint8_t*, size_t)` (tekst `"Extended Module: "` na początku); `Result<Song> xmLoad(const uint8_t*, size_t)`.
  - Test helper: `struct XmSampleSpec { std::vector<int16_t> pcm; bool is16; LoopType loop; uint32_t loopStart, loopLen; uint8_t vol; int8_t finetune; int8_t relNote; uint8_t pan; }; struct XmInstSpec { std::vector<XmSampleSpec> samples; uint8_t noteMap[96]; Envelope vol, pan; uint16_t fadeout; }; std::vector<uint8_t> buildXm(int channels, const std::vector<uint8_t>& order, const std::vector<Pattern>& pats, const std::vector<XmInstSpec>& insts, bool linear, uint8_t speed, uint8_t bpm);` — patterny zapisuje spakowane (bitmaska 0x80).

Reguły XM 0x0104: nagłówek (nazwa 20, wersja, rozmiar nagłówka od offsetu 60), długość song ≤ 256, restart, kanały 1–32 (więcej → `fail("UNSUPPORTED CHANNELS")`), patterny ≤ 256, instrumenty ≤ 128, flaga linear, tempo, BPM, order. Patterny: rozmiar nagłówka patternu respektowany, wiersze 1–256, spakowane dane; nieprawidłowe dane → reszta patternu pusta. Instrumenty: rozmiar nagłówka respektowany (bywa < 263); mapa nut 96 B → indeksy **globalne** w `Song::samples` (sample instrumentu dopisywane do globalnej listy), obwiednie 12 punktów, typ (bit0 on, bit1 sustain, bit2 loop), fadeout, vibrato. Sample: długości w bajtach (16-bit → /2 próbek), pętla typ 0/1/2 (bit 4 = 16-bit), dane delta. `quirks=FT2`, `freqMode` wg flagi. Liczniki > limitów → `fail("BAD XM HEADER")`.

- [ ] **Step 1: Napisz testy**

```cpp
TEST(xm_header_fields)        // buildXm(12 kanałów, order {0,1,0}, 2 patterny, linear, speed 3, bpm 150) → numChannels 12, order.size 3, initialSpeed 3, initialBpm 150, freqMode Linear, quirks FT2
TEST(xm_packed_pattern_roundtrip) // komórki: (r0,c0)={49,1,0x10+40,0xA,0x0F}, (r5,c11)={NOTE_OFF,0,0,0,0} → identyczne po wczytaniu; pozostałe zerowe
TEST(xm_16bit_delta_decode)   // pcm16 {0,1000,-1000,32767,-32768} → data16 identyczne, is16, length 5
TEST(xm_8bit_delta_decode)    // pcm {0,10,-10,127,-128} (is16=false) → data8 identyczne
TEST(xm_instrument_map_global_indices) // inst1: 2 sample, inst2: 1 sampel; noteMap inst2 = 0 → instruments[1].sampleMap[0] == 2
TEST(xm_envelope_fields)      // vol env pts {(0,64),(10,32),(20,0)}, sustain 1, flags ENV_ON|ENV_SUSTAIN, fadeout 0x200 → takie same
TEST(xm_truncated_sample_data) // obcięty plik → ok(), loadFlags & LOAD_TRUNCATED, brakujące dane = 0
TEST(xm_huge_counts_rejected) // nagłówek z patterns=0xFFFF → !ok(), "BAD XM HEADER"
TEST(xm_33_channels_rejected) // → !ok(), "UNSUPPORTED CHANNELS"
```

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj `xm.cpp`, podłącz w `formats.cpp`.**
- [ ] **Step 4: Build + testy** → wszystkie przechodzą.
- [ ] **Step 5: Commit** — `feat(formats): XM loader`

---

### Task 5: Mikser

**Files:**
- Create: `src/core/mixer.h`, `src/core/mixer.cpp`, `tests/test_mixer.cpp`

**Interfaces:**
- Consumes: `Sample`, `LoopType`.
- Produces:
  ```cpp
  enum class Interp : uint8_t { None, Linear, Cubic };
  class Mixer {
  public:
    static constexpr int kMaxVoices = 34;      // 32 kanały + 2 podglądu (32, 33)
    static constexpr int kScopeLen = 1024;
    explicit Mixer(uint32_t sampleRate = 48000);
    void setRate(uint32_t hz); uint32_t rate() const;
    void setInterp(Interp); void setRamp(bool on);   // rampy domyślnie on; testy mogą wyłączyć
    void setMasterGain(float g);                    // domyślnie 1.0
    void setNumChannels(int n);                     // autoskala = sqrt(4 / max(4, n))
    void trigger(int v, const Sample* s, uint32_t offset);
    void stopVoice(int v);
    void setFrequency(int v, double hz);
    void setVolume(int v, float vol01, float pan01); // pan 0 = L, 1 = R
    void mix(float* outStereo, uint32_t frames);     // nadpisuje bufor (interleaved L,R)
    bool active(int v) const;
    double position(int v) const;                    // pozycja w próbkach (testy, scope)
    float vu(int v) const;                           // 0..1
    void scope(int v, float* dst, int n) const;      // ostatnie n próbek (n ≤ kScopeLen)
  };
  ```
- Ustalone wartości: wzmocnienie na głos 0,5; pan stałej mocy (`L = cos(p·π/2)`, `R = sin(p·π/2)`, normalizacja tak, że p=0 daje L=1, R=0); rampa liniowa 1 ms (48 próbek przy 48 kHz) przy starcie, stopie i każdej zmianie głośności/pan; VU: szczyt |próbka(±1) × vol01| bloku (bez wzmocnienia głosu i panoramy, więc pełna skala = 1,0), opadanie z połówkowym czasem 80 ms (`vu = max(peak, vu·0.5^(frames/(rate·0.08)))`); limiter: `|x| ≤ 0.8` liniowo, powyżej `sign·(0.8 + 0.2·tanh((|x|−0.8)/0.2))`; scope = mono próbka po głośności, pierścień `kScopeLen`. Cubic = Catmull-Rom (4 punkty) z pobieraniem sąsiadów świadomym pętli; poza końcem bez pętli = 0. `trigger` z `offset ≥ length`: z pętlą → start od `loopStart`, bez pętli → głos nieaktywny. Pętla o długości < 2 traktowana jak brak pętli.

- [ ] **Step 1: Napisz testy** (przy `setRamp(false)`, `Interp::None`, gdzie nie zaznaczono inaczej)

```cpp
TEST(mixer_nearest_reproduces_sample) {
  Mixer m(48000); m.setRamp(false); m.setInterp(Interp::None); m.setNumChannels(4);
  Sample s = make8({0, 64, 127, -128});            // helper testowy: sampel 8-bit bez pętli
  m.trigger(0, &s, 0); m.setFrequency(0, 48000); m.setVolume(0, 1.f, 0.f);
  float out[8]; m.mix(out, 4);
  const float L[] = {0.f, 0.25f, 0.49609375f, -0.5f};
  for (int i = 0; i < 4; ++i) { CHECK_NEAR(out[i*2], L[i], 1e-6); CHECK_NEAR(out[i*2+1], 0.f, 1e-6); }
}
```
  Pozostałe testy:
  - `mixer_half_speed_linear` — freq = rate/2, `Interp::Linear`, sampel {0,100} 8-bit: L[1] = 50/128×0,5 (±1e-4).
  - `mixer_forward_loop_wraps` — sampel 0..9, pętla 4..9 (start 4, len 6), freq = rate: 16 ramek → indeksy próbek `0..9,4..9`.
  - `mixer_pingpong_loop` — sampel 0..5, pętla ping-pong 2..5: kolejność `0,1,2,3,4,5,4,3,2,3…`.
  - `mixer_degenerate_loops` — pętla długości 0, długości 1, `loopStart > length`, ping-pong 1 próbka: `mix(…, 100000)` kończy się, brak NaN, brak wyjścia poza bufor (kontrolne wartowniki za buforem nietknięte).
  - `mixer_offset_past_end` — `trigger` z offsetem 1000 przy długości 10 bez pętli → `active()==false`; z pętlą 2..10 → pierwsza próbka = sampel[2].
  - `mixer_ramp_no_click` — `setRamp(true)`, sampel stały 127: pierwsza ramka |L| < 0,02, ramka 48 ≈ 0,496.
  - `mixer_pan_law` — pan 0,5 → L == R == 0,5×0,7071×vol (±1e-4).
  - `mixer_vu_decay` — po bloku sampla pełnej skali (127/−128) przy vol 1 vu ≈ 1,0 (±0,01); po `stopVoice` i 80 ms ciszy vu ≈ 0,5 (±0,02).
  - `mixer_softclip` — 34 głosy pełnej skali na L: każda próbka |L| ≤ 1,0, a dla wejścia 0,5 wyjście == 0,5.
  - `mixer_scope_returns_last_samples` — po zmiksowaniu 2000 ramek rampy liniowej `scope(v, dst, 4)` = ostatnie 4 próbki po głośności.
  - `mixer_perf_32_voices_cubic` — 32 głosy, sample 16-bit 100 000 próbek z pętlą, różne częstotliwości, `Interp::Cubic`, ramp on: 10 s przy 48 kHz w blokach po 480; czas (QueryPerformanceCounter w teście — test jest w `fttest`, nie w `ftcore`) < 0,5 s w Release; w Debug test pomijany (`#ifdef NDEBUG`).

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj `Mixer`** — pozycja głosu jako `double pos`, `double step = hz/rate`, kierunek ±1 dla ping-pong; pętla wewnętrzna szablonowana po typie próbki (`int8_t`/`int16_t`, normalizacja do ±1: /128 i /32768) i trybie interpolacji.
- [ ] **Step 4: Build + testy.**
- [ ] **Step 5: Commit** — `feat(core): 34-voice float mixer with interpolation, ramps, VU, scope, limiter`

---

### Task 6: Odtwarzacz — sekwencja, ticki, wyzwalanie nut

**Files:**
- Create: `src/core/player.h`, `src/core/player.cpp`, `src/core/player_fx.cpp` (na razie pusty szkielet `processRowEffects/processTickEffects`), `tests/test_player.cpp`

**Interfaces:**
- Consumes: `Song`, `Mixer`, tablice.
- Produces:
  ```cpp
  struct ChannelState {
    int16_t sample = -1;        // indeks w Song::samples, -1 brak
    int16_t instrument = -1;    // indeks w Song::instruments, -1 brak
    uint8_t note = 0;           // ostatnia nuta (1..96)
    int32_t period = 0;         // bazowy (po slidach)
    int32_t outPeriod = 0;      // wysłany do miksera (po arpeggio/vibrato)
    uint8_t volume = 0;         // 0..64 bazowa
    uint8_t outVolume = 0;      // 0..64 po tremolo/obwiedni/fadeout/global (zaokrąglone)
    uint8_t pan = 128;          // 0..255
    bool muted = false;
    // + prywatne pola efektów (pamięć parametrów, pozycje vibrato/tremolo, pętla E6, itp.)
  };
  struct PlayerPos { uint16_t order, pattern, row; uint8_t tick, speed, bpm; };
  class Player {
  public:
    explicit Player(Mixer& m);
    void setSong(const Song* s);          // reset; speed/bpm/global vol/pan kanałów z utworu; mixer.setNumChannels
    void play(uint16_t order = 0, uint16_t row = 0);
    void playPattern(uint16_t pattern, uint16_t row = 0);
    void stop();                           // zatrzymuje wszystkie głosy
    bool playing() const;
    void render(float* outStereo, uint32_t frames); // przeplata ticki i Mixer::mix; cisza gdy !playing
    void tick();                           // jeden tick bez miksowania (testy)
    PlayerPos pos() const;
    bool loopedOnce() const;               // sekwencja weszła na order już odwiedzony
    uint32_t samplesPerTick() const;       // rate * 5 / (bpm * 2)
    double periodToHz(int32_t period) const; // PT23: ptPeriodToHz; FT2: xmLinearHz / xmAmigaHz wg freqMode
    void setMute(int ch, bool m);
    const ChannelState& channel(int ch) const;
  };
  ```
- Zasady sekwencji: tick 0 = odczyt wiersza, wyzwolenie nut i efekty tick-0; ticki 1..speed−1 = efekty ciągłe; po ostatnim ticku przejście do następnego wiersza; koniec patternu → następny order; koniec listy → `restartPos` (MOD: 0) i `loopedOnce=true`. Odwiedzone ordery w bitmapie 256 bitów; wejście (przez Bxx/Dxx/zwykłe przejście) na order już odwiedzony ustawia `loopedOnce`. Tryb pattern: zapętla jeden pattern, Bxx/Dxx tylko zmieniają wiersz w tym patternie. Nuta: nowy sampel/instrument (jeśli podany) ustawia głośność = `Sample::volume` i pan = `Sample::pan` (tylko FT2); nuta bez 3xx/5xx/vol-column-Fx → `period` z nuty + finetune + relativeNote, `Mixer::trigger(ch, …, 0)`. Sampel bez nuty → tylko reset głośności. Nuta z pustym/nieistniejącym samplem → brak dźwięku, bez crasha. Po każdym ticku: `setFrequency(periodToHz(outPeriod))`, `setVolume(outVolume/64 × globalVolume/64, pan/255)`; wyciszony kanał → głośność 0.
- Efekty sekwencji realizowane w tym zadaniu: Bxx, Dxx (PT/FT2: parametr dziesiętny BCD, wiersz ≥ liczba wierszy → 0), E6x, EEx, Fxx (`< 0x20` speed, `≥ 0x20` BPM, `F00` = stop).

- [ ] **Step 1: Napisz testy** (utwory budowane bezpośrednio przez `makeEmptySong` + wpisywanie komórek; helper `Song testSong(int ch, int rows)` w teście)

```cpp
TEST(player_samples_per_tick) { /* rate 48000: bpm 125 → 960; bpm 32 → 3750; bpm 255 → 470 */ }
TEST(player_trigger_mod_note) // PT23, sampel vol 64, nuta 49 (PT C-2), tick() → channel(0).period == 428, outVolume == 64; periodToHz(428) ≈ 8287.14; mixer.active(0)
TEST(player_row_advance_speed) // speed 3: po 3 tickach pos().row == 1, po 6 == 2
TEST(player_fxx_speed_bpm_stop) // F03 → speed 3; F7D → bpm 125; F20 → bpm 32; F00 → !playing()
TEST(player_bxx_dxx) // B02 → order 2 row 0; D10 → następny order, row 10; B01 + D05 w tym samym wierszu → order 1 row 5
TEST(player_e6_pattern_loop) // E60 w wierszu 0, E62 w wierszu 2: kolejność wierszy 0,1,2,0,1,2,0,1,2,3
TEST(player_ee_pattern_delay) // EE2 przy speed 6: wiersz trwa 18 ticków
TEST(player_looped_once_on_end) // order {0,1}: po przejściu obu patternów loopedOnce()==true
TEST(player_looped_once_on_backjump) // B00 w ostatnim wierszu patternu 1 → loopedOnce()==true
TEST(player_pattern_mode_loops) // playPattern(1): po końcu patternu wiersz 0 tego samego patternu, order bez zmian
TEST(player_missing_sample_no_crash) // komórka z instr 31 przy 1 samplu → brak crasha, mixer.active(0)==false
TEST(player_mute) // setMute(0,true) → mixer gra głos z głośnością 0 (render daje ciszę), outVolume bez zmian
```

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj `Player`** (bez efektów ciągłych — te w Task 7/8).
- [ ] **Step 4: Build + testy.**
- [ ] **Step 5: Commit** — `feat(core): player sequencing, timing and note triggering`

---

### Task 7: Efekty MOD (zachowanie PT 2.3)

**Files:**
- Modify: `src/core/player_fx.cpp`, `src/core/player.h` (pola efektów w `ChannelState`)
- Create: `tests/test_fx_mod.cpp`

**Interfaces:**
- Consumes: `Player`, `ChannelState` z Task 6.
- Produces: brak nowych publicznych symboli — zachowanie `outPeriod`/`outVolume` na tick. Testy używają `Mixer::position()` z Task 5.

Zasady PT 2.3 (quirks `PT23`): efekty ciągłe tylko na tickach > 0; period clamp 113..856 dla 1xx/2xx; pamięć parametrów tylko tam, gdzie PT ją ma (3xx, 4xy (osobno x i y), 7xy, 9xx); 8xx ignorowany; EFx ignorowany; vibrato: indeks 0..63, krok = speed, delta = `kVibratoSine[idx&31] × depth >> 7`, idx ≥ 32 → odejmij; vibrato zmienia tylko `outPeriod`; E4x/E7x kształt (0 sinus, 1 rampa, 2 prostokąt, bit 2 = bez resetu przy nowej nucie); arpeggio na ticku `t%3`: 0 → bazowy, 1 → +x półtonów, 2 → +y (z tablicy PT dla finetune sampla); 9xx: offset = xx×256 (00 = ostatni użyty), offset ≥ długość → jak `Mixer::trigger` z Task 5.

- [ ] **Step 1: Napisz testy** — każdy: nuta 49 (period 428), sampel 8-bit 4000 próbek bez pętli, vol 64, speed 6, sprawdzenie wartości po każdym `tick()`:

```cpp
TEST(mod_1xx_porta_up)    // 103: period 428,425,422,419,416,413
TEST(mod_1xx_clamp)       // nuta A#3 (PT idx 34, period 120) + 105 → 120,115,113,113,113,113
TEST(mod_2xx_porta_down)  // 203: 428,431,434,437,440,443
TEST(mod_3xx_tone_porta)  // wiersz 0: C-2; wiersz 1: C-3 + 310 → tick0 428, potem 412,396,380,364,348; nie retriggeruje
TEST(mod_3xx_reaches_target) // 3FF → po ticku 1 period == 214 (cel), nie przekracza
TEST(mod_arpeggio)        // 037: outPeriod 428 (t0), 360 (t1), 285 (t2), 428 (t3), …; period bazowy 428
TEST(mod_vibrato)         // 44F: outPeriod t0 428, t1 428, t2 439, t3 449; period bazowy 428
TEST(mod_axy_vol_slide)   // vol 32: A04 → 32,28,24,20,16,12; A40 → 32,36,40,44,48,52; clamp 0..64
TEST(mod_cxx_clamp)       // C50 → volume 64
TEST(mod_5xy_6xy)         // 5xy = 3xx (pamięć) + Axy; 6xy = 4xy (pamięć) + Axy
TEST(mod_7xy_tremolo)     // 74F przy vol 32: outVolume t0 32, t1 32, t2 = 32 + (97·15>>6) = 54; volume bazowa 32 (tremolo PT: >>6, vibrato: >>7)
TEST(mod_9xx_offset)      // 902 → mixer.position(0) == 512 tuż po ticku 0
TEST(mod_9xx_offset_past_end) // 9FF przy 4000 próbkach bez pętli → mixer.active(0)==false
TEST(mod_e1_e2_fine_porta)// E12 → tylko tick0: 426; E22 → 430
TEST(mod_e9_retrig)       // E92 → trigger w tickach 0,2,4 (licz wywołania przez position()==0 po ticku)
TEST(mod_ea_eb_fine_vol)  // EA4 przy 32 → 36 (tylko tick0); EB4 → 28
TEST(mod_ec_note_cut)     // EC3 → outVolume 64,64,64,0,0,0
TEST(mod_ed_note_delay)   // ED2 z nutą → głos startuje w ticku 2, nie 0
TEST(mod_8xx_ignored)     // 880 → pan bez zmian
```
  (Dodaj do `Mixer` metodę `double position(int v) const` — publiczna, używana tylko w testach i scope.)

- [ ] **Step 2: Uruchom — porażki.**
- [ ] **Step 3: Zaimplementuj efekty w `player_fx.cpp`** (tremolo: delta = `kVibratoSine[idx&31] × depth >> 6`) z gałęzią `quirks == PT23` tam, gdzie PT różni się od FT2.
- [ ] **Step 4: Build + testy** (wszystkie dotychczasowe też przechodzą).
- [ ] **Step 5: Commit** — `feat(core): ProTracker 2.3 effect semantics`

---

### Task 8: Efekty i instrumenty XM (zachowanie FT2)

**Files:**
- Modify: `src/core/player_fx.cpp`, `src/core/player.h`
- Create: `src/core/player_inst.cpp`, `tests/test_fx_xm.cpp`

**Interfaces:**
- Consumes: jak w Task 7; `Instrument`, `Envelope`.
- Produces: brak nowych publicznych symboli.

Zasady FT2: częstotliwości wg `freqMode`; `period` w jednostkach liniowych (`xmLinearPeriod`) lub amigowych; nuta z instrumentem → sampel = `sampleMap[note-1]`; relativeNote i finetune sampla doliczane; pamięć parametrów dla 1xx, 2xx, 3xx, 4xy, Axy, Exx-fine itp. jak FT2; 8xx = pan; kolumna głośności: `0x10–0x50` set vol, `0x60` slide down, `0x70` slide up, `0x80` fine down, `0x90` fine up, `0xA0` vibrato speed, `0xB0` vibrato, `0xC0` set pan (×17), `0xD0` pan slide left, `0xE0` pan slide right, `0xF0` tone porta (×16); Gxx global vol (clamp 64), Hxy global slide, Kxx key-off w ticku xx, Lxx pozycja obwiedni, Pxy pan slide, Rxy multi-retrig (tablica zmian głośności FT2), Txy tremor, X1x/X2x extra-fine porta. Key-off: bez obwiedni vol → `volume = 0`; z obwiednią → zwolnienie sustain + fadeout. Final: `outVolume = volume × env/64 × fade/32768 × global/64` (fade startuje 32768, po key-off `fade -= fadeout` co tick, min 0). Obwiednia: interpolacja liniowa między punktami, sustain trzyma do key-off, pętla `loopStart..loopEnd`. Autovibrato instrumentu: typ, sweep, depth, rate jak FT2.

- [ ] **Step 1: Napisz testy** (utwór `quirks=FT2`, `freqMode=Linear`, jeden instrument z jednym samplem, chyba że zaznaczono)

```cpp
TEST(xm_linear_note_freq)     // nuta 49, relNote 0, finetune 0 → period 4608, periodToHz ≈ 8363
TEST(xm_relative_note)        // relNote +12 → periodToHz ≈ 16726
TEST(xm_volcol_set_and_slide) // vol 0x10+40 → volume 40; 0x64 → ticki: 40,36,32,28,24,20
TEST(xm_volcol_pan)           // 0xCF → pan 255
TEST(xm_8xx_pan)              // 880 → pan 0x80
TEST(xm_keyoff_no_env)        // nuta, potem NOTE_OFF → outVolume 0 w tym ticku
TEST(xm_env_interp)           // env {(0,64),(10,0)} ON → po 5 tickach outVolume == 32 (vol 64)
TEST(xm_env_sustain_and_fade) // env {(0,64),(4,64)} sustain 1, fadeout 0x1000 → stoi na 64 do key-off; po key-off: 64×(32768−4096·k)/32768 dla k=1,2,…
TEST(xm_env_loop)             // env {(0,0),(4,64),(8,0)} pętla 1..2 → wartości powtarzają się z okresem 4 ticków
TEST(xm_gxx_hxy)              // G20 → outVolume = volume/2; H01 (slide down) obniża global co tick
TEST(xm_kxx_keyoff_at_tick)   // K03 → outVolume 0 od ticku 3 (bez obwiedni)
TEST(xm_rxy_retrig)           // R02 → trigger co 2 ticki
TEST(xm_x1_extra_fine)        // X14 → period − 4 tylko w ticku 0 (linear: jednostki 1/4 jak FT2)
TEST(xm_amiga_mode)           // freqMode Amiga: nuta 49 → periodToHz ≈ 8363 (±2)
TEST(xm_instrument_sample_map)// instrument z 2 samplami, nuta 25 mapowana na sampel 2 → channel(0).sample == indeks globalny drugiego
```

- [ ] **Step 2: Uruchom — porażki.**
- [ ] **Step 3: Zaimplementuj** kolumnę głośności i efekty FT2 w `player_fx.cpp`, instrumenty/obwiednie/fadeout/autovibrato w `player_inst.cpp`.
- [ ] **Step 4: Build + testy.**
- [ ] **Step 5: Commit** — `feat(core): FastTracker 2 effects, volume column and instruments`

---

### Task 9: Zapis WAV, `fttest render/compare`, wzorce referencyjne

**Files:**
- Create: `src/formats/wav.h`, `src/formats/wav.cpp`, `src/core/render.h`, `src/core/render.cpp`, `tests/test_render.cpp`, `tools/make_refs.ps1`, `tests/ref/README.md`
- Modify: `tests/test_main.cpp` (podkomendy)

**Interfaces:**
- Consumes: `loadModule`, `Player`, `Mixer`.
- Produces:
  - `std::vector<uint8_t> writeWav(const float* interleavedStereo, size_t frames, uint32_t rate, bool float32);` — 16-bit PCM z ditherem TPDF lub 32-bit float, nagłówek RIFF/WAVE.
  - `struct WavData { uint32_t rate; uint16_t channels; std::vector<float> samples; }; Result<WavData> readWav(const uint8_t*, size_t);` (16-bit PCM i float; do porównań).
  - `size_t renderSong(const Song&, uint32_t rate, Interp, bool ramps, float maxSeconds, std::vector<float>& out);` (`src/core/render.h`) — gra od order 0 do `loopedOnce()` lub `maxSeconds`; zwraca liczbę ramek.
  - CLI: `fttest render <in> <out.wav> [--interp none|linear|cubic] [--rate N] [--maxsec S] [--float]`; `fttest compare <a.wav> <b.wav>` → wypisuje `corr=<x> rms=<y>`, kod 0 gdy `corr ≥ 0.98`.
  - Test `ref_modules_match`: dla każdej pary `tests/ref/*.mod|*.xm` + `<nazwa>.ref.wav` renderuje z `Interp::None`, rampy wyłączone, rate 48000, porównuje (korelacja ≥ 0.98 po wyrównaniu długości do krótszej); brak plików → wypisuje `SKIP ref_modules_match (no reference files)` i przechodzi.
  - `tools/make_refs.ps1`: jeśli `openmpt123` jest w PATH, generuje `<nazwa>.ref.wav` dla każdego modułu w `tests/ref/`: render do pliku, 48 kHz, stereo, bez interpolacji (filtr 1 = nearest), bez ramp głośności, pełna separacja stereo — dokładne nazwy flag ustal przez `openmpt123 --help` w trakcie implementacji; inaczej wypisuje instrukcję. Moduły i wzorce nie są commitowane (`tests/ref/*.mod|xm|wav` w `.gitignore`), README opisuje skąd je wziąć.

- [ ] **Step 1: Napisz testy**

```cpp
TEST(wav_roundtrip_16) // 100 ramek sinusa → writeWav(16-bit) → readWav: rate, channels 2, próbki ±1/32768·2
TEST(wav_roundtrip_float) // jak wyżej, float32 → identyczne
TEST(render_stops_at_loop) // utwór 2 patterny × 64 wiersze, speed 6, bpm 125 → ramek = 128·6·960 (±960)
TEST(render_cli_terminates_on_infinite_song) // B00 w każdym wierszu → renderSong kończy się dzięki loopedOnce; z samym E6 w pętli bez końca → kończy się na maxSeconds
TEST(ref_modules_match)
```

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj** `wav.*`, `core/render.*`, podkomendy CLI, skrypt i README.
- [ ] **Step 4: Build + testy; ręcznie:** `build\Release\fttest.exe render <dowolny .mod> out.wav` i odsłuch → brzmi poprawnie.
- [ ] **Step 5: Commit** — `feat: offline render, WAV writer, fttest render/compare, reference test harness`

---

### Task 10: Audio WASAPI i Engine

**Files:**
- Create: `src/audio/audio_out.h`, `src/audio/audio_out.cpp`, `src/audio/engine.h`, `src/audio/engine.cpp`, `tests/test_engine.cpp`
- Modify: `CMakeLists.txt` (`src/audio/*.cpp` do `FatTracker` i `fttest`)

**Interfaces:**
- Consumes: `Player`, `Mixer`, `Song`.
- Produces:
  ```cpp
  using RenderFn = void(*)(void* user, float* outStereo, uint32_t frames);
  class AudioOut {
  public:
    bool open(RenderFn fn, void* user);   // domyślne urządzenie, shared, event-driven, bufor ~10 ms, wątek z MMCSS "Pro Audio"
    void close();
    uint32_t sampleRate() const;
    bool isOpen() const;
    bool lost() const;                     // ustawiane przy AUDCLNT_E_DEVICE_INVALIDATED lub błędzie
  };
  struct ChannelTelemetry { float vu; bool active, muted; };
  struct Telemetry { bool playing; uint16_t order, pattern, row; uint8_t speed, bpm; uint64_t playFrames; uint32_t rate; int numChannels; ChannelTelemetry ch[32]; };
  class Engine {
  public:
    Engine();
    bool startAudio();                     // AudioOut::open + mixer.setRate
    void pollAudio();                      // wywoływane przez UI co 2 s: lost/!isOpen → ponowne open
    const char* audioStatus() const;       // nullptr gdy OK, inaczej np. "ERROR: NO AUDIO DEVICE"
    void setSong(std::unique_ptr<Song> s); // zatrzymuje grę, podmienia pod zamkiem
    const Song* song() const;
    void play(); void playPattern(uint16_t pattern); void stop();
    void setMute(int ch, bool m);
    void lock(); void unlock();            // SRWLOCK exclusive — edycje z UI (plan 2)
    void render(float* out, uint32_t frames); // pod zamkiem; po bloku publikuje telemetrię
    void snapshot(Telemetry& out) const;   // seqlock: std::atomic<uint32_t> seq, ponawia przy nieparzystym/zmienionym
    void scope(int ch, float* dst, int n) const;
  };
  ```
  Format wyjścia: preferowany float32 z `GetMixFormat`; jeśli urządzenie daje 16-bit PCM — konwersja w `AudioOut`. Mono/wielokanałowe miksy urządzenia: L/R do pierwszych dwóch kanałów, reszta zera.

- [ ] **Step 1: Napisz testy (bez urządzenia audio)**

```cpp
TEST(engine_render_without_audio) // setSong(utwór z nutą), play(), render(buf, 960) → niezerowe próbki; snapshot: playing, row 0, tick przeszedł
TEST(engine_snapshot_consistent)  // wątek A woła render 1000×, wątek B 100 000× snapshot: zawsze numChannels == song.numChannels i row < 64 (brak rozdartych odczytów)
TEST(engine_setsong_while_rendering) // wątek A renderuje, wątek B 100× setSong(nowy) → brak crasha
TEST(engine_no_song_renders_silence) // bez setSong: render → zera, brak crasha
```

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj `AudioOut` i `Engine`.**
- [ ] **Step 4: Build + testy.**
- [ ] **Step 5: Commit** — `feat(audio): WASAPI output and engine with lock and seqlock telemetry`

---

### Task 11: Grafika — okno, Direct2D, viewport, motyw Hybryda

**Files:**
- Create: `src/ui/gfx.h`, `src/ui/gfx.cpp`, `src/ui/theme.h`, `src/ui/theme.cpp`, `tests/test_ui_math.cpp`, `res/app.manifest`, `res/FatTracker.rc`
- Modify: `src/app/main.cpp` (okno, pętla, rysowanie pustego tła motywu), `CMakeLists.txt` (`src/ui/*.cpp`, rc, biblioteki)

**Interfaces:**
- Produces:
  ```cpp
  struct Color { float r, g, b, a; };
  constexpr Color rgb(uint32_t hex, float a = 1.f);
  struct RectF { float x, y, w, h; };
  struct Viewport { float scale, offX, offY; };
  Viewport computeViewport(uint32_t winW, uint32_t winH); // logiczne 1920×1080, letterbox, scale 0 dla 0×0
  struct Theme {
    Color background, panel, panelLight, panelDark, text, digits, label;
    Color patternBg, patternText, patternNote, patternSample, patternVol, patternFx, rowNumber;
    Color rowBar, rowBarText, cursor, vuLow, vuMid, vuHigh, scopeBg, scopeLine, status;
    float bevelGradient /*0..1*/, glow /*0..1*/, corner /*px logiczne*/;
  };
  const Theme& themeHybrid();
  class Gfx {
  public:
    bool init(HWND hwnd);                  // D3D11 device (BGRA), DXGI flip swap chain, D2D device context
    void resize(uint32_t w, uint32_t h);
    bool beginFrame(const Color& clear);   // false gdy 0×0 lub urządzenie utracone (wtedy odtwarza przy następnym)
    void endFrame();                       // Present(1,0); DXGI_ERROR_DEVICE_REMOVED → recreate
    const Viewport& viewport() const;
    float deviceScale() const;             // scale × DPI-aware piksele (do rasteryzacji atlasu)
    ID2D1DeviceContext* dc();
    void fillRect(RectF r, Color c);
    void fillRoundRect(RectF r, float radius, Color c);
    void fillGradientV(RectF r, Color top, Color bottom);
    void line(float x0, float y0, float x1, float y1, Color c, float width);
    void polyline(const float* xy, int points, Color c, float width);
    void bevel(RectF r, const Theme& t, bool pressed);  // panel + jasna krawędź góra/lewo + ciemna dół/prawo, gradient wg t.bevelGradient
    void beginGlow();                      // kolejne rysowanie trafia do warstwy offscreen
    void endGlow(float radiusLogical, float strength); // Gaussian blur (CLSID_D2D1GaussianBlur) + kompozycja addytywna na wierzch
  };
  ```
- Hybryda (z PT 2.3F, tekst patternu niebieski): `background #2B2F36`, `panel #B8BCC4`, `panelLight #EEF1F5`, `panelDark #5A606B`, `text #1A1C20`, `digits #1F4FD1`, `label #2A2D33`, `patternBg #04060A`, `patternText #3F6FE8`, `patternNote #F2F5FA`, `patternSample #6FA0FF`, `patternVol #5FD38A`, `patternFx #FFC15E`, `rowNumber #8A93A6`, `rowBar #DCE3EE`, `rowBarText #1F4FD1`, `cursor #FF5A5A`, `vuLow #2FE06A`, `vuMid #F5E14A`, `vuHigh #FF4A3D`, `scopeBg #000000`, `scopeLine #46FF72`, `status #1A1C20`, `bevelGradient 0.35`, `glow 0.5`, `corner 3`.
- Okno: klasa `FatTrackerWnd`, startowy rozmiar 1600×900 (lub 1920×1080 na ekranach ≥ 2560 szer.), per-monitor DPI v2 w manifeście, `WM_SIZE` → `resize`, `WM_DPICHANGED` → sugerowany prostokąt, Alt+Enter → bezramkowy pełny ekran na bieżącym monitorze i z powrotem, rysowanie w pętli `PeekMessage` tylko gdy `dirty` lub gra muzyka (inaczej `WaitMessage`).

- [ ] **Step 1: Napisz testy `viewport_*`**

```cpp
TEST(viewport_exact)   { auto v = computeViewport(1920,1080); CHECK_NEAR(v.scale,1,1e-6); CHECK_NEAR(v.offX,0,1e-6); CHECK_NEAR(v.offY,0,1e-6); }
TEST(viewport_4k)      { auto v = computeViewport(3840,2160); CHECK_NEAR(v.scale,2,1e-6); }
TEST(viewport_tall)    { auto v = computeViewport(1920,1200); CHECK_NEAR(v.scale,1,1e-6); CHECK_NEAR(v.offY,60,1e-6); }
TEST(viewport_narrow)  { auto v = computeViewport(1280,1080); CHECK_NEAR(v.scale,2.0/3,1e-5); CHECK_NEAR(v.offY,180,1e-3); }
TEST(viewport_zero)    { auto v = computeViewport(0,0); CHECK_EQ(v.scale, 0.f); }
```
  (`computeViewport` i `Theme` w plikach bez D2D w nagłówku — `gfx.h` włącza `d2d1_1.h`; daj `computeViewport` do `src/ui/viewport.h/.cpp`, żeby `fttest` nie musiał linkować D2D.)

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj** `viewport.*`, `theme.*`, `gfx.*`, okno w `main.cpp` (rysuje tło `background` i jeden `bevel` 400×200 pośrodku jako dymny test).
- [ ] **Step 4: Build + testy; ręcznie:** uruchom `build\Release\FatTracker.exe`: panel ostry i wycentrowany przy zmianie rozmiaru, Alt+Enter działa, minimalizacja nie crashuje.
- [ ] **Step 5: Commit** — `feat(ui): Direct2D window, scalable viewport, Hybrid theme`

---

### Task 12: Font wektorowy w stylu PT

**Files:**
- Create: `src/ui/font_data.h`, `src/ui/font_data.cpp`, `src/ui/font.h`, `src/ui/font.cpp`, `tests/test_font.cpp`

**Interfaces:**
- Consumes: `Gfx`, `Color`.
- Produces:
  - `extern const uint8_t kGlyphs[128][8];` — wiersze bitowe (bit 7 = lewa kolumna) dla kodów 0..127; projektujemy sami w duchu PT: gruby (pionowe kreski 2 kolumny), kanciasty, wersaliki pełne 7 wierszy, małe litery x-height 5 wierszy, zero ze skośną kreską odróżnialne od „O”; kody `1`=↑, `2`=↓, `3`=◄, `4`=►, `5`=▲ (strzałka wypełniona), `6`=▼; kody 0, 7–31 i 127 puste.
  - `class Font { public: void drawText(Gfx& g, float x, float y, float size, const char* s, Color c); float advance(float size) const /* = size */; void invalidate(); };` — `x,y,size` w jednostkach logicznych; `size` = wysokość komórki 8×8.
  - Rasteryzacja: dla danej wielkości w pikselach urządzenia (`size × viewport.scale`, zaokrąglone do 0,25 px) budowany jest atlas 16×8 komórek: każdy glif to `ID2D1PathGeometry` z unii zapalonych komórek (prostokąty łączone w `ID2D1GeometryGroup` z `D2D1_FILL_MODE_WINDING`, narożniki zewnętrzne zaokrąglone promieniem 0,12 komórki), rysowany z AA biało na przezroczystym `ID2D1Bitmap1` (premultiplied). Cache do 4 rozmiarów (LRU); `invalidate()` przy zmianie skali/urządzenia. Rysowanie: `ID2D1SpriteBatch` (`ID2D1DeviceContext3`, Windows 10) z kolorem per sprite; pozycje zaokrąglone do pełnych pikseli urządzenia; na czas `DrawSpriteBatch` `D2D1_ANTIALIAS_MODE_ALIASED`.

- [ ] **Step 1: Napisz testy (dane, bez D2D)**

```cpp
TEST(font_printable_glyphs_nonempty) { for (int c = 33; c <= 126; ++c) { int bits = 0; for (int r = 0; r < 8; ++r) bits |= kGlyphs[c][r]; CHECK(bits != 0); } }
TEST(font_space_empty)               { for (int r = 0; r < 8; ++r) CHECK_EQ(kGlyphs[' '][r], 0); }
TEST(font_arrows_present)            { for (int c = 1; c <= 6; ++c) { int b = 0; for (int r = 0; r < 8; ++r) b |= kGlyphs[c][r]; CHECK(b != 0); } }
TEST(font_zero_differs_from_O)       { CHECK(memcmp(kGlyphs['0'], kGlyphs['O'], 8) != 0); }
TEST(font_no_duplicate_glyphs)       // każde dwa różne znaki 33..126 mają różne bitmapy
```

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaprojektuj glify w `font_data.cpp`** (komentarz nad każdym glifem z rysunkiem `#`/`.` dla czytelności) **i zaimplementuj `Font`.**
- [ ] **Step 4: Build + testy; ręcznie:** w `main.cpp` tymczasowo narysuj `"FATTRACKER 0123456789 abc \x01\x02\x03\x04"` w rozmiarach 8, 16, 32, 64 — litery ostre przy każdym rozmiarze okna, bez rozmycia i bez szwów między komórkami.
- [ ] **Step 5: Commit** — `feat(ui): own PT-style vector font with scaled glyph atlas`

---

### Task 13: Widgety, podgląd patternu, ekran odtwarzania, powłoka aplikacji

**Files:**
- Create: `src/ui/widgets.h`, `src/ui/widgets.cpp`, `src/ui/pattern_view.h`, `src/ui/pattern_view.cpp`, `src/ui/play_screen.h`, `src/ui/play_screen.cpp`, `tools/make_icon.ps1`, `res/icon.ico` (generowana), `tests/test_pattern_layout.cpp`
- Modify: `src/app/main.cpp`, `res/FatTracker.rc`

**Interfaces:**
- Consumes: `Gfx`, `Font`, `Theme`, `Engine`, `Telemetry`, `Song`.
- Produces:
  - `void drawPanel(Gfx&, const Theme&, RectF);` `void drawCounter(Gfx&, Font&, const Theme&, RectF, const char* label, const char* value);` (z narysowanymi, nieaktywnymi w tym planie strzałkami ↑↓), `void drawVu(Gfx&, const Theme&, RectF, float level01);` (pionowy słupek: gradient vuLow→vuMid od 60%→vuHigh od 85%, w `beginGlow/endGlow` gdy `theme.glow > 0`), `void drawScope(Gfx&, const Theme&, RectF, const float* samples, int n);` (tło scopeBg, linia scopeLine 2 px logiczne, glow).
  - `struct PatternLayout { int visible; float rowNumW, chanW, glyph, gap; int firstChannel; };` `PatternLayout computePatternLayout(int viewMode /*4,8,16*/, int numChannels, int firstChannel, float areaW);` — komórka = 10 glifów (`C-3`,`01`,`40`,`C20` bez spacji) + 3 przerwy po 0,25 glifu + 1 glif marginesu = 11,75 glifu; `rowNumW = 3,5 glifu`, więc `glyph = min(28, areaW / (visible × 11.75 + 3.5))`; `visible = min(viewMode, numChannels)`; `firstChannel` przycinane do `[0, numChannels − visible]`.
  - `class PatternView { void draw(Gfx&, Font&, const Theme&, RectF area, const Song&, const Telemetry&, int viewMode, int firstChannel); };` — numery wierszy, nagłówki `TRACK #n` (wyciszony kanał przyciemniony), komórki w kolorach: nuta `patternNote`, sampel `patternSample`, głośność `patternVol`, efekt `patternFx`, puste pola `patternText` (`---`, `..`), VU kanału wewnątrz kolumny za tekstem (szer. 40% kolumny, wycentrowany, przezroczystość 0,85), bieżący wiersz zawsze w pionowym środku obszaru na pasku `rowBar` z tekstem `rowBarText`, wiersze spoza patternu puste.
  - `class PlayScreen { void draw(Gfx&, Font&, const Theme&, Engine&); void setStatus(const char* msg); void onKey(UINT vk, bool shift, bool ctrl, bool alt); int viewMode() const; };`
  - Układ PlayScreen (logiczne px): górny pas 0..360 — lewa kolumna liczników x 0..560 (POS, PATTERN, LENGTH, FINETUNE, SAMPLE, VOLUME, LENGTH, REPEAT, REPLEN, CHANNELS — wartości z utworu/telemetrii, sampel bieżący = 1), środek 560..1180 siatka przycisków 3×4 (PLAY, STOP, PATTERN, CLEAR, EDIT, EDIT OP., RECORD, DISK OP., PLST, SETUP, SAMPLER, HELP — w tym planie tylko rysowane; klik PLAY/STOP/PATTERN działa), prawa 1180..1920 oscyloskopy widocznych kanałów (4/8/16 w jednym rzędzie, przy 16 w dwóch rzędach po 8) + DATE / TIME / PLAY; pas 360..440: SONGNAME, TEMPO, BPM, STATUS; dół 440..1080: `PatternView` na całej szerokości minus prawy panel 260 px (w widoku 16 bez prawego panelu, który w tym planie jest pustym panelem z napisem `TRACK`).
- Klawisze (tymczasowe dla etapu 1, finalne w planie 2): prawy Alt lub F5 = PLAY song; prawy Ctrl = PLAY pattern; Spacja = STOP; F11 = przełącz widok 4 → 8 → 16 → 4; Tab / Shift+Tab = przewiń kanały o 1; Ctrl+O = okno otwarcia pliku (`GetOpenFileNameW`, filtr `*.mod;*.xm;*.stk`); 1..9 na klawiaturze numerycznej z Ctrl = mute kanału widocznego; Alt+Enter = pełny ekran. Klik w nagłówek ścieżki = mute, prawy klik = solo.
- Wczytywanie (drag&drop `WM_DROPFILES` i Ctrl+O): odczyt pliku do pamięci (limit 256 MB → `ERROR: FILE TOO BIG`), `loadModule`; sukces → `engine.setSong`, STATUS `ALL RIGHT` (z ostrzeżeniem `LOADED: FILE TRUNCATED` gdy `LOAD_TRUNCATED`), tytuł okna `FatTracker — <nazwa pliku>`; błąd → STATUS `ERROR: <komunikat>` i bieżący utwór bez zmian. Brak audio → STATUS z `engine.audioStatus()`, `pollAudio()` co 2 s (`SetTimer`).
- Ikona: `tools/make_icon.ps1` generuje `res/icon.ico` (16/32/48/256) z prostego rysunku (bevel + „FT” + mini VU) przez System.Drawing; ikona commitowana.

- [ ] **Step 1: Napisz testy układu**

```cpp
TEST(pattern_layout_4ch)   { auto l = computePatternLayout(4, 4, 0, 1660); CHECK_EQ(l.visible, 4); CHECK_NEAR(l.glyph, 28, 1e-3); }
TEST(pattern_layout_8ch)   { auto l = computePatternLayout(8, 32, 0, 1660); CHECK_EQ(l.visible, 8); CHECK(l.glyph >= 16 && l.glyph <= 18); }
TEST(pattern_layout_16ch)  { auto l = computePatternLayout(16, 32, 0, 1920); CHECK_EQ(l.visible, 16); CHECK(l.glyph >= 9.5f && l.glyph <= 10.5f); }
TEST(pattern_layout_clamp_first) { auto l = computePatternLayout(8, 12, 10, 1660); CHECK_EQ(l.firstChannel, 4); }
TEST(pattern_layout_fewer_channels) { auto l = computePatternLayout(16, 6, 0, 1920); CHECK_EQ(l.visible, 6); }
```
  (Liczby w asercjach muszą wynikać z formuły powyżej; jeśli wyjdą inne — popraw asercję po przeliczeniu formuły, nie formułę.)

- [ ] **Step 2: Uruchom — porażka.**
- [ ] **Step 3: Zaimplementuj widgety, `PatternView`, `PlayScreen`, ikonę i powłokę w `main.cpp`.**
- [ ] **Step 4: Build + testy, potem lista ręczna** (z dowolnymi modułami MOD 4-kan., MOD 8/16-kan. i XM 32-kan. trzymanymi lokalnie):
  - przeciągnięcie pliku i Ctrl+O wczytuje; prawy Alt gra; dźwięk czysty, bez trzasków;
  - VU w kolumnach i oscyloskopy reagują w rytm muzyki; pasek wiersza stoi pośrodku, pattern przewija się płynnie;
  - F11 przełącza 4/8/16, Tab przewija kanały, klik w nagłówek wycisza;
  - zmiana rozmiaru okna od 800×450 do pełnego ekranu 1920×1080 (i 4K, jeśli dostępny) — wszystko ostre, proporcje zachowane, minimalizacja bez crasha;
  - plik `.txt` przemianowany na `.mod` → `ERROR: NOT A MODULE`, poprzedni utwór gra dalej;
  - odłączenie słuchawek / zmiana domyślnego urządzenia w trakcie gry → dźwięk wraca w ≤ 2 s, STATUS informuje.
- [ ] **Step 5: Commit** — `feat(ui): play screen with scopes, in-column VU meters and 4/8/16 pattern view`

---

### Task 14: Weryfikacja rozmiaru i porządki

**Files:**
- Modify: `CMakeLists.txt` (jeśli trzeba), `README.md` (nowy: jak zbudować, uruchomić, klawisze etapu 1)

- [ ] **Step 1: Czysty build Release** — `Remove-Item -Recurse build; cmake -S . -B build -G "Visual Studio 18 2026" -A x64; cmake --build build --config Release`
  Expected: brak ostrzeżeń `/W4` w `src/`, brak ostrzeżenia rozmiaru.
- [ ] **Step 2: Rozmiar** — `(Get-Item build\Release\FatTracker.exe).Length` → < 1 048 576. Jeśli więcej: sprawdź `/MAP` (największe symbole), usuń przypadkowe zależności (np. `<string>`/`<iostream>` w UI), nie obniżaj jakości grafiki.
- [ ] **Step 3: Pełne testy** — `build\Release\fttest.exe` → `ALL TESTS PASSED (N)`.
- [ ] **Step 4: Uruchom na czystym profilu** — skopiuj sam `FatTracker.exe` do `C:\development\FatTracker\build\portable_test\` i uruchom → działa bez innych plików.
- [ ] **Step 5: Commit** — `docs: README for stage 1; verify size budget`

---

## Pokrycie specyfikacji (etap 1)

| Spec | Zadanie |
|---|---|
| §2 architektura, §12 build/rozmiar | 1, 14 |
| §3 model utworu | 2 |
| §4 odtwarzacz MOD/XM, tryby PLAY/PATTERN, mute/solo | 6, 7, 8, 13 |
| §5 mikser (interpolacja, rampy, pan, VU, scope, limiter). Interfejs `IMixer` z §5 wydzielimy dopiero w etapie 3, gdy powstanie `PaulaMixer` (YAGNI); do tego czasu `Mixer` to klasa konkretna | 5 |
| §6 WASAPI, zamek, telemetria, utrata urządzenia | 10, 13 |
| §7 grafika, font, motyw Hybryda | 11, 12, 13 |
| §8.1 ekran główny (wersja tylko do odczytu) | 13 |
| §9 MOD/XM odczyt, WAV zapis | 3, 4, 9 |
| §10 obsługa błędów | 3, 4, 13 |
| §13 testy, render, wzorce | 1–10 |

**Plan 2 (etap 2)** — pisany po ukończeniu tego planu: edytor pattern i klawiatura PT, SAMPLER, DISK OP., EDIT OP., SETUP z motywami i edytorem palety, kursor Amigi, zapis XM, eksport MOD/WAV, sample WAV/IFF, undo, `FatTracker.ini`, aktywne liczniki ↑↓.
