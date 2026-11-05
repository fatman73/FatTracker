# Generates reference renders for tests/ref/*.mod|*.xm using openmpt123 (developer tool only).
# Output: <module>.ref.wav next to each module. Settings: 48 kHz stereo, no interpolation,
# no volume ramping, full stereo separation. Check flag names with `openmpt123 --help`.
$ErrorActionPreference = 'Stop'
$refDir = Join-Path $PSScriptRoot '..\tests\ref'
$tool = Get-Command openmpt123 -ErrorAction SilentlyContinue
if (-not $tool) {
    Write-Host 'openmpt123 not found in PATH.'
    Write-Host 'Install libopenmpt tools (https://lib.openmpt.org) and re-run this script.'
    exit 1
}
Get-ChildItem $refDir -File | Where-Object { $_.Extension -in '.mod', '.xm' } | ForEach-Object {
    $out = "$($_.FullName).ref.wav"
    & $tool.Source --batch --force --render --samplerate 48000 --channels 2 --filter 1 `
        --ramping 0 --stereo 100 --output $out $_.FullName
    Write-Host "rendered $out"
}
