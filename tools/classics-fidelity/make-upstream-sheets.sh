#!/bin/bash
# make-upstream-sheets.sh : for the 3 upstream (non-ported) classics, build
# a 3-row sheet: classic (this build), legacy reference (if available),
# classic-quarter (render-scale=0.25).
set -u

ROOT="${1:-/home/jasonperlow/build-tmp/classics-fidelity}"
OUT="${ROOT}/contact-sheets"
REF="${ROOT}/ref-legacy"

mkdir -p "$OUT"

HACKS=(hypertorus klein projectiveplane)
LABELS=("this build (classic)" "legacy ref (if any)" "classic --scale=0.25")

SSH="sshpass -p build-host ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 build-host@192.0.2.85"

$SSH "mkdir -p /tmp/audit/ref /tmp/audit/captures /tmp/audit/legend" 2>&1 >/dev/null

# Push legends
for i in 0 1 2; do
    timeout 30 $SSH "magick -size 200x250 xc:'#1a1a1a' -fill white -gravity center -font Helvetica -pointsize 22 -annotate +0+0 '${LABELS[$i]}' /tmp/audit/legend/lab_${i}.png" 2>&1 >/dev/null
done

# Push ref + captures
rm -rf /tmp/audit-staging-up
mkdir -p /tmp/audit-staging-up
for hack in "${HACKS[@]}"; do
    mkdir -p "/tmp/audit-staging-up/ref/$hack"
    [ -d "${REF}/$hack" ] && cp "${REF}/$hack"/*.png "/tmp/audit-staging-up/ref/$hack/" 2>/dev/null
    mkdir -p "/tmp/audit-staging-up/captures/$hack/classic/seed-1"
    cp "${ROOT}/captures/$hack/classic/seed-1"/*.png "/tmp/audit-staging-up/captures/$hack/classic/seed-1/" 2>/dev/null
    mkdir -p "/tmp/audit-staging-up/captures/$hack/classic-quarter/seed-1"
    cp "${ROOT}/captures/$hack/classic-quarter/seed-1"/*.png "/tmp/audit-staging-up/captures/$hack/classic-quarter/seed-1/" 2>/dev/null
done

tar -C /tmp/audit-staging-up -cf - . | $SSH "tar -C /tmp/audit -xf -" 2>&1 | tail -3

SHEET_CMD=""
for hack in "${HACKS[@]}"; do
    SHEET_CMD+="echo Building ${hack}...; "
    SHEET_CMD+="cd /tmp/audit; rm -f row_${hack}_*.png sheet_${hack}.png 2>/dev/null; "
    ROWS=(classic ref classic-quarter)
    for r in 0 1 2; do
        v="${ROWS[$r]}"   # expand locally so the host sees a literal value
        SHEET_CMD+="F=; "
        # Always try both sources
        if [ "$v" = "ref" ]; then
            SHEET_CMD+="for t in 0x3c 0xb4 0x12c; do HEX=\$(printf '%08x' \$t); PF=/tmp/audit/ref/${hack}/frame_\${HEX}.png; if [ -f \"\$PF\" ]; then F=\"\$F \$PF\"; fi; done; "
            SHEET_CMD+="if [ -z \"\$F\" ]; then EXISTING=(\$(ls /tmp/audit/ref/${hack}/frame_*.png 2>/dev/null | sort)); N=\${#EXISTING[@]}; if [ \$N -ge 3 ]; then F=\"\${EXISTING[\$((N/4))]} \${EXISTING[\$((N/2))]} \${EXISTING[\$((3*N/4))]}\"; elif [ \$N -ge 1 ]; then F=\"\${EXISTING[@]}\"; fi; fi; "
        else
            SHEET_CMD+="for t in 0x3c 0xb4 0x12c; do HEX=\$(printf '%08x' \$t); PF=/tmp/audit/captures/${hack}/${v}/frame_\${HEX}.png; if [ -f \"\$PF\" ]; then F=\"\$F \$PF\"; fi; done; "
            SHEET_CMD+="if [ -z \"\$F\" ]; then EXISTING=(\$(ls /tmp/audit/captures/${hack}/${v}/frame_*.png 2>/dev/null | sort)); N=\${#EXISTING[@]}; if [ \$N -ge 3 ]; then F=\"\${EXISTING[\$((N/4))]} \${EXISTING[\$((N/2))]} \${EXISTING[\$((3*N/4))]}\"; elif [ \$N -ge 1 ]; then F=\"\${EXISTING[@]}\"; fi; fi; "
        fi
        SHEET_CMD+="if [ -n \"\$F\" ]; then magick \$F -resize 400x250 +append /tmp/audit/row_${hack}_${r}_strip.png; magick /tmp/audit/legend/lab_${r}.png /tmp/audit/row_${hack}_${r}_strip.png +append /tmp/audit/row_${hack}_${r}.png; else magick -size 1200x250 xc:'#111' /tmp/audit/row_${hack}_${r}_strip.png; magick /tmp/audit/legend/lab_${r}.png /tmp/audit/row_${hack}_${r}_strip.png +append /tmp/audit/row_${hack}_${r}.png; fi; "
    done
    SHEET_CMD+="magick /tmp/audit/row_${hack}_0.png /tmp/audit/row_${hack}_1.png /tmp/audit/row_${hack}_2.png -append /tmp/audit/sheet_${hack}.png; "
done
$SSH "$SHEET_CMD" 2>&1 | tail -5

for hack in "${HACKS[@]}"; do
    sshpass -p build-host scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes \
        -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 \
        build-host@192.0.2.85:/tmp/audit/sheet_${hack}.png "${OUT}/${hack}-sheet.png" 2>&1 | tail -1
done

ls -la "$OUT/" | grep -E "hypertorus|klein|projective"
