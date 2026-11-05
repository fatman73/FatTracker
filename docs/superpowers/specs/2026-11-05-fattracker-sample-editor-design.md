# FatTracker — edytor sampli i nazwy (specyfikacja)

Użytkownik: „Sample editor first” (po pytaniu, czego brakuje do komponowania). Bez osobnej akceptacji.

## Sampler — nowe przyciski (trzeci rząd)
- **RESAMPLE**: pytanie „HALF / DOUBLE / CANCEL” — długość ×0,5 / ×2 (interpolacja liniowa),
  relative note / finetune przestawione tak, by wysokość się nie zmieniła; pętla przeskalowana.
- **TUNE**: wykrycie częstotliwości podstawowej (autokorelacja) i ustawienie relative note / finetune
  tak, by nuta C-4 dawała C (261,63 Hz). Gdy nie da się wykryć — komunikat.
- **MIX**: schowek sampla dodawany do zaznaczenia (albo od początku), z nasyceniem; wydłuża sampel w razie potrzeby.
- **LOW PASS / HIGH PASS**: filtr jednobiegunowy na zaznaczeniu (lub całości), każde kliknięcie mocniej.
- **ECHO**: opóźnienie 1/8 s (w częstotliwości sampla), sprzężenie 40 %, ogon do końca sampla.
- **BOOST**: +6 dB z miękkim nasyceniem.
- **DRAW**: przełącza tryb ołówka — przeciąganie myszą rysuje falę (linia między kolejnymi punktami).
- **SAMPLE**: nagrywanie z domyślnego wejścia audio (WASAPI shared capture), mono, do 60 s;
  drugie kliknięcie (lub dowolny przycisk STOP) kończy. Wynik: 16-bit, strojenie z częstotliwości urządzenia.
- Każda operacja zapisuje UNDO sampla i oznacza utwór jako zmieniony.

## Nazwy
- Klik w pole SONGNAME / SAMPLENAME (normalny ekran i Pure) — edycja w miejscu: litery, cyfry,
  spacja, Backspace/Delete/strzałki/Home/End, Enter zatwierdza, Esc anuluje (20 / 22 znaki).
- DISK OP.: przycisk **RENAME** zmienia nazwę zaznaczonego pliku na tekst z pola NAME (pyta przy kolizji).

## Testy
Operacje na samplach (długość, wysokość, nasycenie, zakresy), wykrywanie wysokości na syntetycznym sinusie,
konwersja nagrania na Sample; edycja tekstu (wstawianie, kasowanie, limit długości).
