# FatTracker — etap 5: formaty Amigi (specyfikacja)

Kontynuacja `2026-11-05-fattracker-core-design.md` (punkt 5). Użytkownik: „najpierw obsługa innych
formatów — TFMX, OctaMED, DigiBooster, Symphonie”, „jedziemy z koksem” — bez osobnej akceptacji.

## Cel
Wczytywanie i odtwarzanie modułów: OctaMED (MMD0–MMD3), DigiBooster 1.x (`DIGI Booster module`),
DigiBooster Pro (`DBM0`), Symphonie (`SymM`) oraz TFMX (Turrican II, pliki `mdat.*` + `smpl.*`).

## Zasady
- MED, DigiBooster i Symphonie są wzorcowane na pattern/sample: loader zamienia je na `Song`
  (edytowalny, zapisywalny jako XM). Efekty mapowane na efekty PT/XM, których odtwarzacz już używa.
- Ograniczenia modelu: maks. 32 kanały, 256 patternów, 256 wierszy, nuty C-0…B-7. Nadmiar jest obcinany,
  plik dostaje flagę `LOAD_TRUNCATED` (status „LOADED: FILE TRUNCATED”).
- TFMX nie jest formatem patternowym (makra, trackstep): dostaje własny odtwarzacz (`core/tfmx`)
  emulujący 4 głosy Pauli; tylko odtwarzanie (bez edycji), ekran pokazuje pozycję i scope'y.
  `smpl.*` wczytuje się automatycznie obok `mdat.*` (i odwrotnie).
- Wzorce działania: dokumentacja formatów i loadery OpenMPT (licencja BSD, przypisanie w kodzie).
- Pliki referencyjne i tymczasowe tylko w `build/` projektu, usuwane po pracy.

## Odwzorowanie (skrót)
- MED: nuta n → XM n+36+playTranspose (MMD3: −24), zawijanie oktaw powyżej B-5 dla miksu sprzętowego;
  tempo wg trybu (SPD/BPM/8ch); 0C głośność dziesiętnie lub hex; synth → fala 0 jako pętla, hybrid → sampel;
  wielooktawowe IFF → pierwszy segment.
- DigiBooster 1.x: komórki jak MOD, wzorce pakowane (maska wierszy), do 8 kanałów.
- DigiBooster Pro: chunki NAME/INFO/SONG/INST/PATT/SMPL/VENV/PENV; 2 kolumny efektów (druga do kolumny
  głośności, gdy się da); obwiednie głośności jako instrumenty XM.
- Symphonie: wg OpenMPT; kanały > 32 obcinane; DSP/echo pomijane.

## Testy
Syntetyczne minimalne pliki dla nagłówków i mapowania; wszystkie moduły z `modules/` danego formatu
muszą się wczytać i dać niecichy render 10 s; śmieci nie mogą wywrócić programu.
