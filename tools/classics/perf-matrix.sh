#!/bin/bash
# perf-matrix.sh NEWDIR OUTDIR SECS : classic vs enhanced perf matrix (no frame dumps)
#
# The legacy baseline loop is gone: the GL1/emulation builds were retired
# (docs/CLASSICS-SHADER-PORT.md, docs/archive/gl1-era/). The old perf
# tables that were collected against those builds are kept as history.
NB=$1; OUT=$2; S=$3
PORTED="voronoi gravitywell hexstrut cubestorm crackberg cityflow noof geodesic gibson"
cd "$(dirname "$0")"
for h in $PORTED; do ./run-hack.sh $NB $OUT $S "--style=classic --seed=1" perf-classic $h 2>&1 | grep -E "^${h} "; done
for h in $PORTED; do ./run-hack.sh $NB $OUT $S "--style=enhanced --seed=1" perf-enh $h 2>&1 | grep -E "^${h} "; done
for h in hypertorus klein projectiveplane; do ./run-hack.sh $NB $OUT $S "" perf-upstream $h 2>&1 | grep -E "^${h} "; done
echo DONE
