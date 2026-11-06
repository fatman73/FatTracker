#!/bin/sh
# Compares this build's renders with renders made on another system (developer tool, macOS / Linux shell).
# The folder holds source files and, for each, win_<name>.wav (<name> with or without its extension,
# or win_<stem>_<ext>.wav)
# rendered there with `ftrender <file> win_<name>.wav --seconds S`.
# Usage: tools/compare_renders.sh <folder> [build-dir] [seconds]
# Renders each source to mac_<name>.wav next to it and prints `fttest compare` (pass: corr >= 0.98).
set -u
dir=${1:?usage: compare_renders.sh <folder> [build-dir] [seconds]}
build=${2:-build-mac}
secs=${3:-30}
pass=0
fail=0
for src in "$dir"/*; do
    [ -f "$src" ] || continue
    base=$(basename "$src")
    case "$base" in win_* | mac_* | .*) continue ;; esac
    stem=${base%.*}
    ext=${base##*.}
    ref="$dir/win_$base.wav"
    [ -f "$ref" ] || ref="$dir/win_${stem}_$ext.wav"  # two sources with one stem (test.nsf, test.vgz)
    [ -f "$ref" ] || ref="$dir/win_$stem.wav"
    if [ ! -f "$ref" ]; then
        echo "NO REF   $base"
        continue
    fi
    out="$dir/mac_$base.wav"
    fmt=$("$build/ftrender" "$src" "$out" --seconds "$secs" | head -1)
    res=$("$build/fttest" compare "$out" "$ref")
    if [ $? -eq 0 ]; then
        pass=$((pass + 1))
        echo "OK       $base [$fmt] $res"
    else
        fail=$((fail + 1))
        echo "DIFFERS  $base [$fmt] $res"
    fi
done
echo "pass=$pass differs=$fail"
[ "$fail" -eq 0 ]
