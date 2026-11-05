# FatTracker — etap 3: tryb „Pure” (specyfikacja)

Kontynuacja `2026-11-05-fattracker-core-design.md` (sekcja „Etapy”, punkt 3).
Użytkownik: „jedziesz … do końca, nie ma co czekać” — specyfikacja powstaje bez osobnej akceptacji.

## Cel

Tryb „Pure” udaje Amigę: brzmi jak Paula w A500/A1200, przy wczytywaniu i zapisie
słychać stację dysków, a wygląd przełącza się na klasyczny motyw ProTrackera.
Tryb włącza się jednym widocznym przyciskiem w SETUP.

## Zachowanie

### Przełącznik
- SETUP → wiersz `PURE MODE` z przyciskami `OFF` / `A500` / `A1200`; zapis w `FatTracker.ini` (`PureMode=0/1/2`).
- Pod logo, gdy tryb działa: `PURE  ·  A500  ·  LED ON|OFF` (zamiast „32 CHANNELS · 16 BIT”).
- Włączenie Pure zapamiętuje bieżący motyw i ustawia `CLASSIC PT 2.3`; wyłączenie przywraca poprzedni.
  Ręczna zmiana motywu w trybie Pure jest dozwolona.

### Dźwięk (Paula) — tylko w trybie Pure
- Próbka trzymana (zero-order hold) jak w Pauli, uśredniana po przedziale próbki wyjściowej
  (box-filter ZOH) — bez interpolacji liniowej/sześciennej.
- Przetwornik 8-bitowy: sample 16-bit są obcinane do 8 bitów (`v >> 8`).
- Twarde stereo L-R-R-L według numeru kanału (0,3 → L; 1,2 → R; powtarzane co 4),
  panorama z utworu i efektów jest ignorowana. Głosy podglądu i metronom — środek.
- Bez mikro-ramp głośności (kliki jak na Amidze).
- Filtry analogowe na sumie stereo:
  - A500: 1-biegunowy dolnoprzepustowy 4420.97 Hz, 1-biegunowy górnoprzepustowy 5.20 Hz;
  - A1200: tylko górnoprzepustowy 5.20 Hz (jego dolnoprzepustowy 34.4 kHz leży poza pasmem);
  - „LED”: 2-biegunowy Butterworth 3090.53 Hz, Q = 0.660, gdy dioda włączona.
- Dioda LED: efekt `E0x` w utworach MOD (`E00` = włącz, `E01` = wyłącz); start odtwarzania od początku ją gasi.
  W XM `E0x` nic nie robi (jak w FT2).
- Utwory z więcej niż 4 kanałami grają dalej wszystkimi kanałami (twarde L-R-R-L).

### Stacja dysków — tylko w trybie Pure
- Dźwięki: silnik (pętla), krok głowicy, opcjonalnie włożenie dyskietki.
  Domyślnie syntezowane w kodzie (zero plików). Jeśli istnieją pliki
  `sounds\floppy_motor.wav`, `sounds\floppy_step.wav`, `sounds\floppy_insert.wav`
  (WAV lub IFF 8SVX; szukane obok exe, potem w katalogu roboczym), zastępują syntezę.
- Model czasu (DD 880 KB, 11 264 B na cylinder, 300 obr./min):
  - rozpędzenie silnika 0.5 s;
  - krok głowicy co 3 ms przy przeszukiwaniu, 1 krok między kolejnymi cylindrami;
  - odczyt/zapis cylindra 0.4 s (dwie strony);
  - wczytanie pliku: skok na cylinder 40 (katalog), 1 obrót, potem skok na cylinder pliku i odczyt
    `ceil(bajty / 11264)` cylindrów; całość obcięta do 12 s;
  - silnik gaśnie 2 s po ostatniej operacji.
- Wczytanie modułu w Pure czeka na koniec odczytu (status `LOADING...`, kursor „busy”);
  dowolny klawisz albo klik pomija czekanie. Błędny plik pokazuje błąd od razu.
- Czytanie katalogu w DISK OP. i zapis pliku grają dźwięk bez blokowania.
- Głośność stacji: SETUP `DRIVE VOLUME` 0–100 % (`DriveVolume`, domyślnie 70).
- Dźwięk stacji nie przechodzi przez filtry Pauli (to dźwięk mechaniczny, nie z Pauli).

## Architektura
- `core/paula.h/.cpp` — `PaulaFilter` (stan filtrów stereo, `setModel`, `setLed`, `process`).
- `Mixer`: `setPaula(PaulaModel)` (None/A500/A1200), `setLed(bool)`, `led()`; nowa ścieżka ZOH,
  8 bit, twarde pan, bez ramp; filtr na końcu `mix`.
- `Player`: `E0x` dla pt23 → `mixer_.setLed(x == 0)`; `play()` od początku → LED off.
- `core/floppy.h/.cpp` — `Floppy` (czysty C++): `setRate`, `setSounds`, `read(bytes)`,
  `dir()`, `write(bytes)`, `busySeconds()`, `mix(out, frames, gain)`.
- `Engine`: posiada `Floppy`; `setPure(model)`, `floppyRead/Dir/Write`, `floppyBusy()`, `setDriveVolume`;
  miesza stację po playerze.
- UI: SETUP (wiersz PURE, DRIVE VOLUME), logo, opóźnione wczytanie w `main.cpp`.

## Testy (fttest)
Paula: twarde pan, 8 bit, ZOH, A500 tłumi wysokie, LED tłumi mocniej, A1200 przepuszcza wysokie,
E00/E01 w MOD przełącza LED, w XM nie. Stacja: czas odczytu wg modelu, cisza po wygaszeniu silnika,
dźwięk użytkownika zastępuje syntezę. Ustawienia: zapis/odczyt `PureMode`, `DriveVolume`.

## Zmiany po uwagach użytkownika (2026-11-05)
- Ekran Pure to wierny ekran ProTrackera 2.3 z Amigi: płótno 320×256 pikseli w układzie PT
  (liczniki POS…REPLEN, siatka PLAY…SAMPLER, QUADRASCOPE, SONGNAME/SAMPLENAME/LOAD, TEMPO/STATUS,
  4 ścieżki z paskiem środkowym i zielonymi VU), własna pikselowa czcionka w stylu Amigi,
  skalowanie całkowite bez wygładzania (×4 przy 1080p) — tu pikseloza jest zamierzona.
  Obok ekranu: diody POWER / LED FILTER / DRIVE i przycisk EXIT PURE MODE.
  DISK OP. i LOAD otwierają systemowe okno plików, MOD2WAV zapisuje WAV; EDIT OP., POS ED.,
  PAT2SMP, SAMPLER pokazują „NOT IN PURE”.
- Wczytanie pliku w Pure trwa zawsze ok. 5 s (głowica i silnik), bez kolejkowania;
  klawisz/klik przerywa odczyt.
