#!/bin/bash
# sweep-classics.sh : run all 12 classics through a fixed set of variants on
# build-host, write captures and metrics.
#
# Usage: sweep-classics.sh [variant-list]
#   variant-list defaults to "classic enhanced ablate-bloom0 ablate-aa0
#   ablate-bloom-aa0".  Use "all" for that list plus classic-half and
#   classic-quarter (which test render-scale).
#
# Each variant is captured for every hack at seed=1, with a 5-second timed
# dump.  The PNGs are normalised locally; metrics are computed on build-host.
set -u

ROOT="/home/jasonperlow/build-tmp/classics-fidelity"
HACKS=(voronoi gravitywell hexstrut cubestorm crackberg cityflow noof geodesic gibson hypertorus klein projectiveplane)
# For the upstream (non-ported) hacks, only classic (default) makes sense; the
# "enhanced" / ablate variants target the shader-engine ports.

VARIANTS_DEFAULT=(classic enhanced ablate-bloom0 ablate-aa0 ablate-bloom-aa0)
VARIANTS_ALL=("${VARIANTS_DEFAULT[@]}" classic-half classic-quarter)
VARIANTS="${1:-${VARIANTS_DEFAULT[*]}}"
read -r -a VARIANT_ARR <<< "$VARIANTS"
echo "variants: ${VARIANT_ARR[*]}"

CAPTURE=/home/jasonperlow/Projects/ncz-screensavers/tools/classics-fidelity/capture-classics.sh
mkdir -p "${ROOT}/captures" "${ROOT}/metrics" "${ROOT}/logs"

start=$(date +%s)
n=0
total=$(( ${#HACKS[@]} * ${#VARIANT_ARR[@]} ))
for hack in "${HACKS[@]}"; do
    for variant in "${VARIANT_ARR[@]}"; do
        # Skip non-applicable variants for the upstream (non-ported) hacks.
        case "$hack" in
            hypertorus|klein|projectiveplane)
                if [ "$variant" != "classic" ]; then continue; fi ;;
        esac
        n=$((n+1))
        printf "[sweep %d/%d] %s / %s / seed=1 ...\n" "$n" "$total" "$hack" "$variant"
        # Tee the output to logs so we have a record even if the script dies
        "$CAPTURE" "$hack" "$variant" 1 2>&1 | tee -a "${ROOT}/logs/sweep.log" > /tmp/cap-stdout
        # short pause between captures (let the compositor breathe)
        sleep 0.5
    done
done
end=$(date +%s)
echo "sweep done in $((end-start))s"
