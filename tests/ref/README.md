# Reference modules

Put `.mod` / `.xm` files here (they are not committed — see `.gitignore`), then run
`tools/make_refs.ps1` to create `<name>.mod.ref.wav` renders with openmpt123.

`fttest` (test `ref_modules_match`) renders every module that has a reference WAV with
FatTracker (no interpolation, no ramps) and requires a correlation of at least 0.98.
Without files the test prints `SKIP` and passes.

Use only modules you are allowed to keep locally; do not commit them.
