#!/bin/bash
# analyze-classics.sh : compute blur/sharpness/coverage metrics for every
# capture under <root>/captures/<hack>/<variant>/seed-*.  Uses the pure-Python
# analyzer (Pillow + numpy, no ImageMagick) so it runs anywhere.
#
# Usage: analyze-classics.sh <root> [<out-tsv>]
#   <root> defaults to /home/jasonperlow/build-tmp/classics-fidelity
set -u

ROOT="${1:-/home/jasonperlow/build-tmp/classics-fidelity}"
OUT_TSV="${2:-${ROOT}/metrics/metrics.tsv}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$(dirname "$OUT_TSV")"

python3 "$SCRIPT_DIR/analyze-classics.py" "$ROOT" "$OUT_TSV"
echo "wrote: $OUT_TSV"
ls -la "$OUT_TSV"
