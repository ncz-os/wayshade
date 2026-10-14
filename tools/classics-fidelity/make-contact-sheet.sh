#!/bin/bash
# make-contact-sheet.sh : build a 6-row contact sheet per hack:
#   row 1: legacy GL1 reference (3 timestamps)
#   row 2: classic style (3 timestamps)
#   row 3: enhanced style (3 timestamps)
#   row 4: enhanced with --bloom=0 (isolated bloom effect)
#   row 5: enhanced with --antialias=false (isolated FXAA effect)
#   row 6: classic-quarter (render-scale=0.25)
set -u

ROOT="${1:-/home/jasonperlow/build-tmp/classics-fidelity}"
OUT="${ROOT}/contact-sheets"
REF="${ROOT}/ref-legacy"

mkdir -p "$OUT"

HACKS=(voronoi gravitywell hexstrut cubestorm crackberg cityflow noof geodesic gibson hypertorus klein projectiveplane)

SSH="sshpass -p build-host ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 build-host@192.0.2.85"

# Make sure the audit dir exists on the host and the references are pushed.
$SSH "mkdir -p /tmp/audit/ref /tmp/audit/captures /tmp/audit/legend" 2>&1 >/dev/null
$SSH "rm -rf /tmp/audit/ref/* /tmp/audit/captures/* /tmp/audit/legend/*" 2>&1 >/dev/null

# Push the reference frames
for hack in "${HACKS[@]}"; do
    mkdir -p "/tmp/audit-staging/ref/$hack"
    if [ -d "${REF}/$hack" ]; then
        cp "${REF}/$hack"/*.png "/tmp/audit-staging/ref/$hack/" 2>/dev/null
    fi
    mkdir -p "/tmp/audit-staging/captures/$hack"
    for variant in classic enhanced ablate-bloom0 ablate-aa0 classic-quarter; do
        mkdir -p "/tmp/audit-staging/captures/$hack/$variant"
        cp "${ROOT}/captures/$hack/$variant/seed-1"/*.png "/tmp/audit-staging/captures/$hack/$variant/" 2>/dev/null
    done
done

# Push legend labels (PNG images for each label)
LABELS=("legacy GL1" "classic" "enhanced" "enhanced --bloom=0" "enhanced --aa=0" "classic --scale=0.25")
for i in "${!LABELS[@]}"; do
    r=$i
    timeout 30 $SSH "magick -size 200x250 xc:'#1a1a1a' -fill white -gravity center -font Helvetica -pointsize 22 -annotate +0+0 '${LABELS[$r]}' /tmp/audit/legend/lab_${r}.png" 2>&1 >/dev/null
done

# Push the captures
tar -C /tmp/audit-staging -cf - . | \
    $SSH "tar -C /tmp/audit -xf -" 2>&1 | tail -3

# Now build the sheets on the host.  For each hack, we build a single magick
# command that:
#   - picks frame_0000003c, frame_000000b4, frame_00000012c if present
#   - resizes to 400x250 and tiles 3-across
#   - pastes the legend on the left
#   - stacks the 6 rows vertically
SHEET_CMD=""
for hack in "${HACKS[@]}"; do
    SHEET_CMD+="echo Building ${hack}...; "
    SHEET_CMD+="cd /tmp/audit; rm -f row_${hack}_*.png sheet_${hack}.png 2>/dev/null; "
    ROWS_REF=(ref classic enhanced ablate-bloom0 ablate-aa0 classic-quarter)
    for r in 0 1 2 3 4 5; do
        v="${ROWS_REF[$r]}"
        # frames
        SHEET_CMD+="F=; for t in 0x3c 0xb4 0x12c; do HEX=\$(printf '%08x' \$t); "
        if [ "$v" = "ref" ]; then
            SHEET_CMD+="PF=/tmp/audit/ref/${hack}/frame_\${HEX}.png; "
        else
            SHEET_CMD+="PF=/tmp/audit/captures/${hack}/${v}/frame_\${HEX}.png; "
        fi
        SHEET_CMD+="if [ -f \"\$PF\" ]; then F=\"\$F \$PF\"; fi; done; "
        if [ "$v" = "ref" ]; then
            SHEET_CMD+="if [ -z \"\$F\" ]; then EXISTING=(\$(ls /tmp/audit/ref/${hack}/frame_*.png 2>/dev/null | sort)); N=\${#EXISTING[@]}; if [ \$N -ge 3 ]; then F=\"\${EXISTING[\$((N/4))]} \${EXISTING[\$((N/2))]} \${EXISTING[\$((3*N/4))]}\"; elif [ \$N -ge 1 ]; then F=\"\${EXISTING[@]}\"; fi; fi; "
        else
            SHEET_CMD+="if [ -z \"\$F\" ]; then EXISTING=(\$(ls /tmp/audit/captures/${hack}/${v}/frame_*.png 2>/dev/null | sort)); N=\${#EXISTING[@]}; if [ \$N -ge 3 ]; then F=\"\${EXISTING[\$((N/4))]} \${EXISTING[\$((N/2))]} \${EXISTING[\$((3*N/4))]}\"; elif [ \$N -ge 1 ]; then F=\"\${EXISTING[@]}\"; fi; fi; "
        fi
        SHEET_CMD+="if [ -n \"\$F\" ]; then magick \$F -resize 400x250 +append /tmp/audit/row_${hack}_${r}_strip.png; magick /tmp/audit/legend/lab_${r}.png /tmp/audit/row_${hack}_${r}_strip.png +append /tmp/audit/row_${hack}_${r}.png; fi; "
    done
    SHEET_CMD+="magick /tmp/audit/row_${hack}_0.png /tmp/audit/row_${hack}_1.png /tmp/audit/row_${hack}_2.png /tmp/audit/row_${hack}_3.png /tmp/audit/row_${hack}_4.png /tmp/audit/row_${hack}_5.png -append /tmp/audit/sheet_${hack}.png; "
done

# Run on host
$SSH "$SHEET_CMD" 2>&1 | tail -5

# Pull sheets back
for hack in "${HACKS[@]}"; do
    sshpass -p build-host scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes \
        -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 \
        build-host@192.0.2.85:/tmp/audit/sheet_${hack}.png "${OUT}/${hack}-sheet.png" 2>&1 | tail -1
done

ls -la "$OUT/"
