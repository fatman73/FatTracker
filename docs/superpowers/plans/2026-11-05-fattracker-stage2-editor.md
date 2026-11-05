# FatTracker — Plan 2: edytor (etap 2)

> Wykonanie: inline (executing-plans), bez przerw — decyzja użytkownika („jedziesz z etapem 2 i do końca”).
> Rejestr postępu i decyzji: `.superpowers/sdd/2026-11-05-fattracker-stage2-editor/progress.md`.

**Goal:** Pełny tracker do komponowania w stylu ProTrackera: edycja patternów z klawiatury PT, bloki, cofanie,
lista pozycji, SAMPLER, DISK OP. w stylu PT, EDIT OP., SETUP z motywami i paletą, kursor Amigi,
zapis XM, eksport MOD/WAV, ustawienia w `FatTracker.ini`.

**Spec:** `docs/superpowers/specs/2026-11-05-fattracker-core-design.md` (§3, §8–§12).
Zmiany względem specu zaakceptowane przez użytkownika w trakcie etapu 1: interfejs wypełnia całe okno,
gładkie fonty systemowe zamiast pikselowego, widoki 4/8/16/32 (zawsze tyle kolumn), szare tło,
płaskie VU jak w PT, oscyloskopy dla wszystkich kanałów.

## Global Constraints
- Wszystkie pliki (także tymczasowe, zrzuty, testowe dane) tylko w `C:\development\FatTracker`.
- C++20/MSVC, bez wyjątków i RTTI, bez zewnętrznych bibliotek; `FatTracker.exe` < 1 MB.
- `src/core` i `src/formats` bez nagłówków Windows; logika edycji testowana w `fttest` (TDD).
- Edycje utworu z UI zawsze przez `Engine::edit` (blokada SRW) — bezpieczne w trakcie grania.
- Każdy ekran weryfikowany zrzutem `--shot` w 1920×1009 z prawdziwym modułem z `modules/`.

## Review Focus
1. Edycja w trakcie grania (zmiana liczby wierszy/kanałów, usuwanie patternu/sampla) → brak crasha, odtwarzacz nie czyta poza tablicą.
2. Cofanie po operacjach blokowych i zmianie rozmiaru patternu → stan identyczny jak przed operacją.
3. Zapis XM → wczytanie → identyczny model (round-trip), także 32 kanały, 16-bit, pętle ping-pong.
4. Eksport MOD utworu, który się nie mieści (16-bit, >31 sampli, kolumna głośności) → ostrzeżenia, plik poprawny.
5. Uszkodzony WAV/IFF przy wczytywaniu sampla → komunikat, bez crasha, bieżący sampel nietknięty.

## Mapa plików
```
src/core/edit.h/.cpp          EditCursor, SongEditor: komórki, wiersze, bloki, transpozycja, pozycje, patterny, undo
src/core/sample_edit.h/.cpp   operacje na samplu (zaznaczenie, cut/copy/paste/crop, głośność, odwrócenie,
                              normalizacja, konwersja 8/16) + undo 1 poziom
src/core/keymap.h/.cpp        klawiatura PT → nuta; hex-cyfry
src/formats/xm_write.cpp      saveXm(Song) → bytes
src/formats/mod_write.cpp     exportMod(Song, warnings) → bytes
src/formats/sample_io.h/.cpp  loadSampleFile (WAV 8/16/24/float, IFF 8SVX), saveWav/saveIff(Sample)
src/formats/formats.cpp       identify(): nazwa formatu nieobsługiwanego (TFMX, MED, Symphonie, S3M, IT...)
src/app/settings.h/.cpp       FatTracker.ini (GetPrivateProfileString)
src/app/files.h/.cpp          odczyt/zapis plików z atomową zamianą
src/ui/theme.cpp              4 motywy: Hybryda, Klasyczny PT 2.3, PT 3.61 Blue, Neo-Amiga + własny
src/ui/cursor.h/.cpp          kursor Amigi (strzałka i „zajęty”) jako HCURSOR skalowany do DPI
src/ui/screen.h               wspólny interfejs ekranów (draw, onKey, onMouse, onMouseMove)
src/ui/play_screen.*          + kursor edycji, wpisywanie, bloki, przyciski boczne, EDIT/RECORD
src/ui/sampler_screen.*       SAMPLER
src/ui/disk_screen.*          DISK OP. (przeglądarka w stylu PT)
src/ui/editop_screen.*        EDIT OP.
src/ui/setup_screen.*         SETUP
src/ui/help_screen.*          HELP (lista klawiszy)
src/ui/dialog.*               okno YES/NO i komunikaty
```

## Zadania (każde: testy → RED → kod → GREEN → commit; UI: zrzut → ocena → commit)

### T1. SongEditor + undo (core/edit)
Interfejs: `struct EditPos { int pattern, row, channel, column; };` kolumny: 0 nuta, 1–2 sampel (cyfry),
3–4 głośność, 5 efekt, 6–7 parametr.
`class UndoStack { void push(Snapshot); bool undo(Song&); bool redo(Song&); }` — snapshot = kopia
dotkniętych patternów + order + numChannels (proste i odporne), limit 100.
Operacje (wszystkie zapisują undo): `setNote, setInstrDigit, setVolDigit, setFxDigit, clearCell,
insertRow(track|pattern), deleteRow(track|pattern), clearTrack/Pattern/Block, copy/cut/paste
(Track|Pattern|Block), transpose(Track|Pattern|Block|Song, semitones, onlySample), setPatternRows,
insertOrder/deleteOrder/setOrder, newPattern, clonePattern`.
Testy: każda operacja + undo przywraca identyczny stan; paste poza krawędź przycina; transpozycja
clamp do C-0..B-7, NOTE_OFF nietknięty; deleteRow podciąga i dopisuje pustą komórkę.

### T2. Klawiatura PT (core/keymap)
`int noteForKey(unsigned vk, int baseOctave)` — dolne rzędy Z S X D C V G B H N J M , L . ; / = oktawa bazowa,
Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ] = oktawa wyżej (jak PT/FT2); `int hexDigit(unsigned vk)`.
Testy: Z→C, S→C#, Q→C oktawę wyżej, I→C+2 oktawy? (I = C następnej oktawy w górnym rzędzie), klawisze spoza mapy → -1.

### T3. Edytor patternu w UI
EDIT (spacja przełącza; spacja zatrzymuje też odtwarzanie jak w PT), kursor z kolumną (strzałki, Tab/Shift+Tab
między kanałami), wpisywanie nut/cyfr, edit skip (z EDIT OP.), F1/F2 oktawa, Del czyści, Backspace usuwa wiersz,
Insert wstawia, Ctrl+Z/Ctrl+Y, Shift+strzałki zaznaczenie bloku, Shift+F3/F4/F5 ścieżka, Alt+F3/F4/F5 pattern,
Ctrl+C/X/V blok, RECORD (wpisywanie w trakcie grania w bieżący wiersz), podgląd nuty (jam) głosem 32.
Przyciski boczne działają (INSERT, DELETE, CUT, COPY, PASTE, CLEAR, OCT/NOTE UP/DN, SCR UP/DN, ROT UP/DN,
FLIP, EXCHG). POS/PATTERN/LENGTH: edycja listy pozycji; przycisk CLEAR → czyść utwór (dialog YES/NO).
Weryfikacja: zrzut z kursorem, zaznaczonym blokiem, edycją.

### T4. Zapis i formaty
`saveXm` (1 sampel = 1 instrument; istniejące instrumenty XM zachowane), round-trip test;
`exportMod` (≤31 sampli, 8-bit z ditherem, kolumna głośności → Cxx gdy brak efektu, ostrzeżenia);
eksport WAV (render offline, 16-bit/48 kHz); `identify()` → „UNSUPPORTED FORMAT: TFMX/OCTAMED/...”.
Zapis atomowy (plik tymczasowy obok + `MoveFileExW`). Dialog przy niezapisanych zmianach.

### T5. Edycja sampli (core/sample_edit + sample_io)
Zaznaczenie [a,b), cut/copy/paste/crop, volume (procent + rampy), reverse, normalize, to8/to16, ustaw pętlę;
WAV 8/16/24/float mono/stereo→mono, IFF 8SVX (odczyt/zapis). Testy jednostkowe na każdą operację i format.

### T6. Ekrany SAMPLER, DISK OP., EDIT OP., HELP
SAMPLER: fala (min/max na piksel), zoom kółkiem, zaznaczanie myszą, znaczniki pętli przeciągane, przyciski operacji,
odsłuch. DISK OP.: lista plików/katalogów, dyski, filtr (moduły/sample/wszystko), LOAD/SAVE XM/EXPORT MOD/EXPORT WAV/
LOAD SAMPLE/SAVE SAMPLE/PARENT/RENAME/DELETE (z potwierdzeniem), nazwa pliku do zapisu edytowana w polu.
EDIT OP.: edit skip, kwantyzacja nagrywania, metronom, transpozycja zakresów, exchange sampli.
HELP: lista klawiszy. Ekrany podmieniają górny pas (jak PT); SAMPLER zajmuje obszar patternu.

### T7. SETUP, motywy, ini, kursor
SETUP: wybór motywu (4 presety), edytor palety (kolor + suwaki R/G/B), interpolacja, bufor audio, separacja stereo,
wzmocnienie, domyślna liczba kanałów. `FatTracker.ini` obok exe: motyw, paleta własna, ustawienia audio,
ostatnie katalogi, okno/pełny ekran, widok. Kursor Amigi (strzałka + zajęty przy wczytywaniu).

### T8. Przegląd końcowy, poprawki, scalenie, uruchomienie.
