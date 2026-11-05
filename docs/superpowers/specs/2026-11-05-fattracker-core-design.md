# FatTracker — specyfikacja rdzenia (etapy 1 + 2)

Data: 2026-11-05
Status: do przeglądu

## 1. Cel i kontekst

FatTracker to tracker muzyczny dla Windows wzorowany na ProTrackerze z Amigi:
ten sam wygląd i styl pracy, ale bez amigowych ograniczeń — do 32 kanałów
i sample 16-bit. Główne zastosowanie: **komponowanie nowej muzyki**.
Wczytywanie starych formatów (Amiga i PC) jest ważne, ale wtórne wobec edytora.

Kryteria sukcesu:
- Wygląda i obsługuje się „jak ProTracker”: ktoś znający PT 2.3 od razu wie, gdzie co jest.
- Jest ładny: grafika wektorowa, płynnie skalowana, bez pikselozy, z dopracowanymi detalami.
- Jest minimalny: jeden plik `FatTracker.exe` < 1 MB, bez instalatora i bez DLL-i.
- Odtwarza MOD i XM wiernie (zachowania PT 2.3 / FT2).
- Pozwala skomponować utwór od zera i zapisać go w formacie czytelnym dla innych programów (XM).

### Ustalenia z rozmowy

| Temat | Decyzja |
|---|---|
| Platforma | Tylko Windows 10/11 x64 |
| Technologia | C++20, MSVC, CMake, Win32 + Direct2D/DirectWrite + WASAPI, zero zewnętrznych bibliotek |
| Główny cel | Komponowanie |
| Format zapisu | XM jako główny; eksport do MOD (gdy się mieści) i WAV |
| Model edycji | ProTracker + kolumna głośności + panorama na kanał; instrumenty/obwiednie XM odtwarzane, bez edytora (później) |
| Widok pattern | Przełączany 4 / 8 / 16 kanałów, przewijanie poziome |
| Grafika | Wektorowa, płynne skalowanie, motyw domyślny „Hybryda” wzorowany na PT 2.3F |
| Motywy | Przełączane w SETUP + edytor palety |
| Tryb „Pure” | Emulacja Amigi (etap 3, poza tym specem) |
| Lokalizacja | Wszystko wyłącznie w `C:\development\FatTracker` |

### Etapy całego projektu

1. **Rdzeń grający** — model utworu, odtwarzacz, mikser, WASAPI, wczytywanie MOD/XM, ekran odtwarzania z VU.
2. **Interfejs i edytor** — pełny UI, edytor pattern i sampli, zapis XM, eksport MOD/WAV, kursor Amigi, motywy.
3. **Tryb Pure** — emulacja Pauli (4 kanały, twarde stereo L-R-R-L, filtr A500 + „LED”, bez interpolacji), dźwięki stacji dysków (sample od użytkownika), motyw Klasyczny PT.
4. **Formaty PC** — S3M, IT, MTM, 669.
5. **Formaty Amigi** — OctaMED (MMD0–MMD3), DigiBooster 1.x / Pro, Symphonie, TFMX.

**Ten dokument obejmuje etapy 1 i 2.** Etapy 3–5 dostaną własne specyfikacje.
Architektura musi jednak przewidywać ich istnienie (punkty rozszerzeń opisane niżej).

## 2. Architektura

Jeden proces, jeden plik wykonywalny. Moduły z jednokierunkowymi zależnościami:

```
src/
  core/      song, player, mixer          (czysty C++, bez Windows)
  formats/   mod, xm, wav, iff8svx        (czysty C++, bez Windows)
  audio/     wasapi                        (Windows)
  ui/        gfx, font, theme, widgets, screens, cursor, input (Windows)
  app/       main, settings (ini), app state
tests/
  fttest     render do WAV bez karty dźwiękowej + testy jednostkowe
  data/      moduły testowe i wzorce
```

| Moduł | Odpowiedzialność | Zależy od |
|---|---|---|
| `core/song` | Model danych utworu | — |
| `core/player` | Interpretacja patternów i efektów, tempo, ticki | `song` |
| `core/mixer` | Miksowanie głosów, panorama, VU, bufory oscyloskopu | `song` |
| `formats/*` | Wczytanie bajtów → `Song` / zapis `Song` → bajty | `song` |
| `audio/wasapi` | Wątek audio, pobiera bloki z `player`+`mixer` | `core` |
| `ui/gfx` | Opakowanie Direct2D/DirectWrite, skalowanie, prymitywy z efektami | Windows |
| `ui/font` | Własny wektorowy font w stylu PT | `gfx` |
| `ui/theme` | Paleta i parametry efektów | — |
| `ui/widgets` | Przycisk, licznik ↑↓, pole tekstowe, suwak, VU, oscyloskop, widok fali | `gfx`, `theme` |
| `ui/screens` | Ekran główny, edytor pattern, SAMPLER, DISK OP., EDIT OP., SETUP | wszystko |
| `app` | Pętla komunikatów, stan aplikacji, `FatTracker.ini` | wszystko |

Zasada: `core` i `formats` nie zawierają żadnego nagłówka Windows i są w całości
testowalne przez `fttest`.

## 3. Model utworu (`core/song`)

- `Song`: nazwa (20 znaków jak w PT, w XM do 20), liczba kanałów (4–32),
  lista pozycji (do 256), punkt restartu, początkowe speed/BPM,
  flaga trybu częstotliwości (Amiga / liniowe), flagi zgodności formatu źródłowego,
  patterny (do 256), sample (do 255), instrumenty (opcjonalne, z XM).
- `Pattern`: liczba wierszy (1–256, domyślnie 64), komórki `[wiersz][kanał]`.
- `Cell`: nuta (C-0…B-9, „---”, key-off), numer sampla/instrumentu (0–255),
  kolumna głośności (pusta / 0–64 / komendy XM vol-column), efekt + parametr (MOD/XM 0–Z).
- `Sample`: nazwa, dane PCM 8- lub 16-bit mono (przechowywane natywnie, bez konwersji),
  długość, pętla (brak / forward / ping-pong), start/długość pętli,
  głośność 0–64, finetune, relative note, panorama 0–255.
- `Instrument` (tylko z importu XM): mapa nut → sampli, obwiednie vol/pan,
  fadeout, vibrato automatyczne. Edycja instrumentów nie jest w zakresie;
  utwór tworzony w FatTracker ma 1 sampel = 1 instrument.
- Panorama kanału: domyślnie L-R-R-L powtarzane (jak Amiga), ze zmiękczeniem
  (separacja 75%) poza trybem Pure; edytowalna na kanał.

## 4. Odtwarzacz (`core/player`)

- Jeden silnik z zestawem flag zachowań („quirks”) ustawianych przez loader:
  `pt23` (MOD) i `ft2` (XM). Kolejne formaty (etapy 4–5) dodają swoje flagi.
- Tempo: speed (ticki/wiersz) + BPM (CIA); tick = 2.5/BPM s.
- MOD: periody Amigi, zakres i clamp jak PT 2.3, efekty 0–F i E0–EF
  (w tym E6 pattern loop, EE pattern delay, EF/„funk” ignorowany jak w PT 2.3,
  E5 finetune, 9xx z błędem PT przy offsecie poza pętlą), vblank vs CIA.
- XM: częstotliwości liniowe i amigowe, kolumna głośności, efekty 0–Z jak FT2
  (Gxx, Hxy, Kxx, Lxx, Pxy, Rxy, Txy, X1/X2), instrumenty z obwiedniami vol/pan,
  fadeout, key-off, autovibrato.
- Stan na zewnątrz: aktualna pozycja/wiersz/tick, czy gra, tryb (song / pattern),
  per kanał: aktywny sampel, nuta, głośność efektywna.
- Tryby odtwarzania jak w PT: PLAY (song), PATTERN (pętla bieżącego patternu),
  RECORD (gra i nagrywa wpisywane nuty), podgląd nuty z klawiatury (jam).
- Mute/solo kanału (klik na nagłówek ścieżki, jak w PT).

## 5. Mikser (`core/mixer`)

- 32 głosy (+ 2 głosy podglądu na jam/sampler), obliczenia w `float`.
- Częstotliwość wyjścia = natywna urządzenia (zwykle 48 kHz), stereo.
- Interpolacja: brak / liniowa / sześcienna (Hermite) — domyślnie sześcienna; ustawiana w SETUP.
- Mikro-rampy głośności ~1 ms przy starcie/końcu/zmianie głośności, by uniknąć klików.
- Panorama na głos (prawo stałej mocy), wzmocnienie główne, miękki limiter na wyjściu.
- Na każdy kanał: poziom VU (szczyt z opadaniem w stylu PT) i pierścieniowy bufor
  ~1024 próbek dla oscyloskopu.
- Punkt rozszerzenia: interfejs `IMixer`, by w etapie 3 dodać `PaulaMixer`.

## 6. Dźwięk (`audio/wasapi`)

- WASAPI shared mode, event-driven, bufor ~10 ms, wątek z priorytetem „Pro Audio” (MMCSS).
- Wątek audio wywołuje `player.render(frames)` → `mixer`.
- Współbieżność: edycje z UI i render audio dzielą jeden lekki zamek (`SRWLOCK`),
  trzymany przez audio na czas jednego bloku (~5 ms max), przez UI na czas pojedynczej edycji.
  Zamiana całego `Song` (wczytanie) odbywa się pod tym samym zamkiem przez podmianę wskaźnika.
- Telemetria do UI (pozycja, VU, oscyloskopy) przez podwójny bufor bez blokad
  (wersjonowany snapshot), odczytywany przez UI w każdej klatce.
- Utrata urządzenia (`AUDCLNT_E_DEVICE_INVALIDATED`) lub zmiana domyślnego wyjścia
  → automatyczne ponowne otwarcie; komunikat w STATUS.

## 7. Grafika (`ui/gfx`, `ui/font`, `ui/theme`)

- Direct2D 1.1 (`ID2D1DeviceContext`) na swap chainie DXGI (flip model), VSync,
  renderowanie tylko gdy coś się zmieniło lub gra muzyka.
- **Płótno logiczne 1920×1080**, skalowane jednolicie do okna z zachowaniem proporcji
  (wolne marginesy wypełnione tłem motywu). Pełny ekran: Alt+Enter. Obsługa per-monitor DPI v2.
- Wszystko rysowane wektorowo z antyaliasingiem; żadnych bitmap skalowanych.
- **Font**: własny font w duchu ProTrackera (gruby, kanciasty, stała szerokość),
  zdefiniowany w kodzie jako kontury glifów (siatka 8×8 z zaokrąglonymi narożnikami
  i lekko fazowanymi krawędziami), cache'owany jako `ID2D1PathGeometry` / bitmapy w danej skali.
  Zestaw: ASCII 32–126 + znaki specjalne PT (strzałki ↑↓, ◄►, „#”, kropki).
  Nie kopiujemy oryginalnego fontu ani grafik PT — wszystko rysujemy od nowa w tym stylu.
- **Efekty motywu**: wypukłe ramki (bevel) z gradientem światła, poświata VU
  i oscyloskopów (Gaussian blur effect D2D, siła z motywu, 0 = brak), cień paska wiersza.
- **Motywy** (`Theme` = paleta ~24 kolorów + parametry efektów: siła glow, gradient bevela, zaokrąglenie):
  1. **Hybryda** (domyślny): panele jasnoszare jak PT 2.3F, cyfry niebieskie,
     pattern na czarnym tle z **niebieskim** tekstem, nuta biała, sampel niebieski,
     efekt bursztynowy, biały pasek bieżącego wiersza, zielone VU, zielony oscyloskop; delikatne światło i glow.
  2. **Klasyczny PT 2.3**: te same kolory, bez glow i gradientów (płaskie bevele).
  3. **PT 3.61 Blue**: turkusowo-niebieskie panele, zielony/żółty tekst patternu, żółty scope.
  4. **Neo-Amiga**: ciemny grafit, kolorowanie składni, mocniejszy glow.
  Wybór motywu i edytor palety (kolor po kolorze, suwaki R/G/B jak w SETUP PT)
  w ekranie SETUP; własny motyw zapisywany w `FatTracker.ini`.

## 8. Ekrany i układ (`ui/screens`)

Układ wzorowany na PT 2.3F, rozciągnięty do 16:9. Ekrany wymienne podmieniają
górną część (lub cały obszar), jak w PT.

### 8.1 Ekran główny
- **Lewa góra — liczniki** z przyciskami ↑↓ (klik, przytrzymanie = autorepeat,
  prawy klik = szybciej): POS, PATTERN, LENGTH, FINETUNE, SAMPLE, VOLUME,
  LENGTH (sampla), REPEAT, REPLEN; nowe: PAN (sampla), CHANNELS (4–32).
- **Środek góry — przyciski**: PLAY, STOP, PATTERN, CLEAR, EDIT, EDIT OP.,
  RECORD, DISK OP., PLST, SETUP, SAMPLER, HELP.
- **Prawa góra**: oscyloskopy dla aktualnie widocznych kanałów (4/8/16),
  DATE / TIME / PLAY (czas odtwarzania).
- **Pasek informacyjny**: SONGNAME, SAMPLENAME (edytowalne w miejscu),
  TEMPO (speed) i BPM, STATUS (komunikaty w stylu PT: „ALL RIGHT”, „ERROR: …”),
  pamięć zajęta przez sample.
- **Dół — edytor pattern**:
  - kolumna numerów wierszy, ścieżki z nagłówkami „TRACK #n” (klik = mute, prawy klik = solo);
  - komórka: `C-3 01 40 C20` (nuta, sampel, głośność, efekt), w widoku 16 kanałów ta sama treść mniejszym fontem;
  - **VU-metry rysowane wewnątrz kolumn ścieżek**, za tekstem (zielone z gradientem do żółci/czerwieni);
  - biały/podświetlony pasek bieżącego wiersza zawsze pośrodku, kursor edycji w kolorze akcentu;
  - przełącznik widoku **4 / 8 / 16**, poziomy pasek przewijania kanałów (i Tab/Shift+Tab);
  - prawy panel operacji (jak PT 3.61): TRACK/PATTERN, INSERT, DELETE, CUT, COPY, PASTE,
    CLEAR, OCT UP/DN, NOTE UP/DN, SCR UP/DN, ROT UP/DN; licznik wierszy patternu i BPM.

### 8.2 DISK OP.
Przeglądarka plików w stylu PT: lista z kolumnami nazwa/rozmiar, ścieżka, dyski,
filtr (Moduły / Sample / Wszystko), przyciski LOAD MODULE, SAVE MODULE (XM),
EXPORT MOD, EXPORT WAV, LOAD SAMPLE, SAVE SAMPLE, PARENT, RENAME, DELETE (z potwierdzeniem).
Dodatkowo przeciągnij-i-upuść pliku na okno = wczytanie.

### 8.3 SAMPLER
Fala sampla na całą szerokość (min/max na kolumnę piksela, płynny zoom kółkiem),
zaznaczanie myszą, znaczniki pętli przeciągane myszą, przyciski: PLAY (całość / zaznaczenie / pętla),
STOP, CUT, COPY, PASTE, CROP, VOLUME (+ rampy), REVERSE, NORMALIZE, CONVERT 8/16-bit,
LOOP (off/fwd/ping-pong), ZOOM ALL/SEL; wczytaj/zapisz WAV i IFF 8SVX.
Cofanie (undo) jednopoziomowe dla operacji sampla.

### 8.4 EDIT OP.
Kwantyzacja nagrywania, krok edycji (edit skip), metronom (kanał/co ile wierszy),
transpozycja (ścieżka / pattern / blok / cały utwór, z filtrem sampla),
operacje na blokach, zamiana samplem (exchange).

### 8.5 SETUP
Motyw + edytor palety, interpolacja, urządzenie audio i rozmiar bufora,
separacja stereo, wzmocnienie, domyślna liczba kanałów nowego utworu, VU tryb (szczyt/RMS).

### 8.6 Kursor myszy
Amigowa strzałka w stylu PT (żółto-zielona z ciemnym obrysem) i kursor „zajętości”
w stylu Workbench. Rysowane wektorowo do bitmapy w rozmiarze zależnym od skali okna
i ustawiane jako `HCURSOR` (`CreateIconIndirect`); odtwarzane po zmianie skali/DPI.

### 8.7 Klawiatura
Jak w PT: dolne dwa rzędy QWERTY = oktawa bazowa, górne dwa = oktawa wyżej;
F1/F2 = oktawa bazowa niżej/wyżej; Spacja = przełącz EDIT; Prawy Alt / Prawy Ctrl = PLAY song / PATTERN;
strzałki = ruch kursora, Tab = następny kanał; Shift+F3/F4/F5 = cut/copy/paste ścieżki,
Alt+F3/F4/F5 = cut/copy/paste patternu, lewy Ctrl w roli klawisza Amigi; Del = czyść komórkę;
Backspace = usuń wiersz i podciągnij, Insert = wstaw wiersz. Ctrl+Z = cofnij edycję patternu (wielopoziomowe, 100 kroków).
Pełna lista w ekranie HELP.

## 9. Formaty (`formats/`)

Każdy format w osobnym pliku z funkcjami `bool detect(span)`, `Result<Song> load(span)`,
opcjonalnie `Result<bytes> save(const Song&)`. Rejestr formatów pozwala dodać etapy 4–5
bez zmian w reszcie.

- **MOD (odczyt)**: sygnatury M.K., M!K!, M&K!, FLT4, FLT8, 4CHN–9CHN, xxCH (10–32),
  Soundtracker 15-sampli (heurystyka), sample 8-bit; ustawia quirks `pt23`.
- **XM (odczyt)**: wersja 0x0104, patterny pakowane, instrumenty, obwiednie,
  sample 8/16-bit delta; quirks `ft2`.
- **XM (zapis)**: format główny. Każdy sampel = jeden instrument z jednym samplem,
  kolumna głośności i panorama zapisywane natywnie, typ częstotliwości wg utworu.
  Utwór zapisany i wczytany ponownie musi dać identyczny model (test round-trip).
- **MOD (eksport)**: dozwolony, gdy ≤ 31 sampli, długość sampla ≤ 128 KB, nuty w zakresie MOD.
  Sample 16-bit konwertowane do 8-bit (z ditheringiem), kolumna głośności zamieniana
  na Cxx gdzie się da; wszystko, czego nie da się przenieść, wypisane w oknie ostrzeżeń
  przed zapisem (użytkownik potwierdza). Sygnatura wg liczby kanałów (M.K. / xCHN / xxCH).
- **WAV (eksport utworu)**: render offline 16-bit lub 32-bit float, 44.1/48 kHz.
- **Sample**: WAV PCM 8/16/24-bit i float (konwersja do 16-bit), mono/stereo (stereo → mono
  z wyborem: miks / L / R); IFF 8SVX (odczyt i zapis).

## 10. Obsługa błędów

- Wszystkie odczyty plików przez klasę czytnika z kontrolą granic; obcięty plik
  wczytuje się w miarę możliwości (brakujące dane sampli = cisza) z ostrzeżeniem w STATUS.
- Wczytywanie zawsze do nowego `Song`; bieżący utwór podmieniany dopiero po sukcesie.
- Nieznany format / uszkodzony plik → komunikat w STATUS („ERROR: NOT A MODULE” itp.),
  bez okien systemowych.
- Zapis: najpierw do pliku tymczasowego obok, potem atomowa zamiana (`ReplaceFileW` / `MoveFileExW`).
- Niezapisane zmiany: pytanie przy wyjściu / wczytaniu innego utworu (okno w stylu PT „YES / NO”).
- Brak urządzenia audio: aplikacja działa (edycja możliwa), STATUS informuje, ponowna próba co 2 s.

## 11. Ustawienia

`FatTracker.ini` obok `.exe` (wersja przenośna, bez rejestru): motyw i paleta własna,
interpolacja, urządzenie audio, bufor, separacja stereo, wzmocnienie, ostatni katalog
modułów i sampli, rozmiar/pozycja okna, pełny ekran, widok 4/8/16.

## 12. Budowanie i rozmiar

- CMake + MSVC (VS 2026), x64, C++20.
- Release: `/O1 /GL /Gy /GR- /EHs-c- /LTCG /OPT:REF /OPT:ICF`, statyczny CRT (`/MT`).
  Bez wyjątków (błędy przez `Result<T>`), bez RTTI, bez `iostream`.
- Zasoby (ikona, manifest DPI) w `.rc`; font, kursory, motywy jako dane w kodzie.
- Cel: `FatTracker.exe` **< 1 MB**. Rozmiar sprawdzany w buildzie (ostrzeżenie przy przekroczeniu).
- Jedynie biblioteki systemowe: `d2d1`, `dwrite`, `d3d11`, `dxgi`, `ole32`, `avrt`, `uuid`, `shell32`, `comdlg32` (opcjonalnie).

## 13. Testy

- `fttest.exe` (osobny cel CMake, nie wchodzi do aplikacji):
  - testy jednostkowe `core` i `formats` (prosty własny runner, bez frameworków);
  - render modułu do WAV offline;
  - porównanie z wzorcami: wzorcowe WAV-y generowane jednorazowo narzędziem
    `openmpt123` (libopenmpt — wyłącznie narzędzie deweloperskie, nie w repozytorium binarnym)
    przy wyłączonej interpolacji i rampach; porównanie z tolerancją RMS i korelacją;
  - testy efektów: małe ręcznie zbudowane utwory sprawdzające period/głośność na tick
    (vibrato, tremolo, portamento, arpeggio, pattern loop/delay, skoki Bxx/Dxx);
  - round-trip: XM zapisz → wczytaj → porównaj model; MOD wczytaj → zapisz XM → wczytaj → porównaj grę.
- UI testowane ręcznie wg listy kontrolnej (skalowanie 1280×720 → 3840×2160, motywy,
  edycja w trakcie grania, utrata urządzenia audio).
- Moduły testowe: tworzone samodzielnie lub z licencją pozwalającą na redystrybucję;
  inne trzymane lokalnie poza repozytorium.

## 14. Poza zakresem (etapy 1–2)

- Edytor instrumentów i obwiedni XM (odtwarzanie jest).
- MIDI in/out, VST, nagrywanie audio z wejścia.
- Tryb Pure i dźwięki stacji dysków (etap 3).
- Formaty S3M, IT, MTM, 669, OctaMED, DigiBooster, Symphonie, TFMX (etapy 4–5).
- Wersje na inne systemy.
