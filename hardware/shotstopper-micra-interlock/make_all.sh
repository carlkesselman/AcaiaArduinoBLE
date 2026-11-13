#!/bin/zsh
# Rebuild the board from design.py and export everything PCBWay needs.
#   ./make_all.sh [path-to-python-with-numpy-scipy-pillow]
set -e
cd "$(dirname "$0")"
KICAD=/Applications/KiCad/KiCad.app/Contents
KPY=$KICAD/Frameworks/Python.framework/Versions/Current/bin/python3
KCLI=$KICAD/MacOS/kicad-cli
PY=${1:-python3}
PCB=shotstopper-micra-interlock.kicad_pcb

$KPY build_board.py build 2>/dev/null
$PY route.py build | grep -v "^ok "
grep -q '"failed": \[\]' build/routes.json || { echo "routing failed"; exit 1; }
$KPY finish_board.py build $PCB 2>&1 | grep saved

$KCLI pcb drc --severity-error --exit-code-violations -o build/drc.txt $PCB > /dev/null \
  && echo "DRC: no errors" || { echo "DRC errors, see build/drc.txt"; exit 1; }

rm -rf fab && mkdir -p fab/gerbers
$KCLI pcb export gerbers -o fab/gerbers/ --subtract-soldermask \
  -l F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,B.Paste,Edge.Cuts $PCB > /dev/null
$KCLI pcb export drill -o fab/gerbers/ --format excellon --excellon-separate-th \
  --excellon-units mm --generate-map --map-format gerberx2 $PCB > /dev/null
$KCLI pcb export pos -o fab/centroid.csv --format csv --units mm --side both $PCB > /dev/null
python3 bom.py fab/bom.csv
(cd fab/gerbers && zip -q -r ../shotstopper-micra-interlock-gerbers.zip .)

mkdir -p renders
for side in top bottom; do
  $KCLI pcb render --side $side --width 1600 --height 1500 --quality high -o renders/$side.png $PCB > /dev/null
done
echo "done: fab/ and renders/"
