#!/bin/sh
# Renders the reference --shot scenes (the same list the Windows side renders as win_<name>.png).
# usage: tools/shots.sh <module folder> <out folder> [build folder]
# Runs from <module folder> so module names resolve and DISK OP. lists that folder. Writes mac_<name>.png.
set -e
mods=$(cd "$1" && pwd)
mkdir -p "$2"
out=$(cd "$2" && pwd)
build=$(cd "${3:-build-mac}" && pwd)
app="$build/FatTracker.app/Contents/MacOS/FatTracker"
cd "$mods"
shot() { name=$1; shift; "$app" --shot "$out/mac_$name.png" 1920 1080 "$@" || echo "FAILED: $name"; }
shot main
shot mod "a sleep so deep.mod" 3
shot diskop "" 0 8 "P:DISKOP"
shot sampler "a sleep so deep.mod" 3 8 "P:SAMPLER"
shot setup "" 0 8 "P:SETUP"
shot instr "dope.xm" 3 8 "P:INSTR"
shot pure "a sleep so deep.mod" 3 8 "PURE:1"
shot help "" 0 8 "P:HELP"
for n in 0 1 2 3 4; do shot "fx$n" "" 0 8 "FX:$n FXT:2"; done
ls "$out"/mac_*.png | wc -l
