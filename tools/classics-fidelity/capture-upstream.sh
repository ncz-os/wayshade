#!/bin/bash
# capture-upstream.sh : capture hypertorus / klein / projectiveplane without
# the seed/style options (they don't exist for those hacks).
set -u

WORK="/home/jasonperlow/build-tmp/classics-fidelity"
HACK="${1:?hack}"
VARIANT="${2:?variant}"   # classic | classic-quarter
shift 2
EXTRA="$@"

case "$VARIANT" in
    classic)         ARGS=(--render-scale=1 --render-scale-mode=fixed) ;;
    classic-half)    ARGS=(--render-scale=0.5 --render-scale-mode=fixed) ;;
    classic-quarter) ARGS=(--render-scale=0.25 --render-scale-mode=fixed) ;;
    *) echo "unknown variant $VARIANT"; exit 2 ;;
esac
ARGS+=("$EXTRA")

OUT="${WORK}/captures/${HACK}/${VARIANT}/seed-1"
mkdir -p "$OUT"

SSH="sshpass -p build-host ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 build-host@192.0.2.85"

$SSH "rm -rf /tmp/cap /tmp/cap-* && mkdir -p /tmp/cap" 2>&1 >/dev/null

ENV_CMD='cp=$(pgrep -u $(id -u) -x labwc | head -1); if [ -z "$cp" ]; then cp=$(pgrep -u $(id -u) -x Hyprland | head -1); fi; if [ -z "$cp" ]; then cp=$(pgrep -u $(id -u) -x sway | head -1); fi; if [ -z "$cp" ]; then cp=$(pgrep -u $(id -u) -x weston | head -1); fi; if [ -n "$cp" ] && [ -r /proc/$cp/environ ]; then while IFS= read -r -d "" kv; do case $kv in __EGL_VENDOR_LIBRARY_FILENAMES=*|LD_LIBRARY_PATH=*|MESA_*|GBM_*|LIBGL_*) export "$kv";; esac; done < /proc/$cp/environ; fi'

CMD="cd /tmp/cap && export PATH=/usr/lib/ncz-screensavers:\$PATH && export XDG_RUNTIME_DIR=/run/user/\$(id -u) && export WAYLAND_DISPLAY=wayland-0 && ${ENV_CMD} && NCZ_FRAME_DUMP=/tmp/cap timeout --kill-after=2 5 /usr/lib/ncz-screensavers/${HACK}_gles3 ${ARGS[*]} > /tmp/cap/log.txt 2>&1; echo EXITCODE=\$?"

LOG=$($SSH "$CMD" 2>&1)
EXIT=$(echo "$LOG" | grep -oE 'EXITCODE=[0-9]+' | tail -1 | cut -d= -f2)
printf '%s\n' "$LOG" > "$OUT/log.txt"
$SSH "cd /tmp/cap && tar cf - ." 2>/dev/null | (cd "$OUT" && tar xf -) 2>/dev/null

# Same PNG/RGBA conversion as capture-classics.sh
W=""; H=""
RENDER=$(grep -oE 'render size [0-9]+x[0-9]+' "$OUT/log.txt" | head -1 | sed 's/render size //')
if [ -n "$RENDER" ]; then
    W=${RENDER%x*}; H=${RENDER#*x}
fi
[ -z "$W" ] && W=1920
[ -z "$H" ] && H=1080

N=0
for raw in "$OUT"/frame_*.rgba "$OUT"/frame_*.png; do
    [ -f "$raw" ] || continue
    base=$(basename "$raw")
    name="${base%.*}"
    sz=$(stat -c%s "$raw")
    if [ "$sz" -le 0 ]; then rm -f "$raw"; continue; fi
    HEX3=$(od -An -t x1 -N3 "$raw" 2>/dev/null | head -1 | awk '{print $1$2$3}')
    if [ "$HEX3" = "89504e" ]; then
        [ "$base" != "${name}.png" ] && mv "$raw" "${name}.png"
        N=$((N+1))
        continue
    fi
    if [ "$((W*H*4))" != "$sz" ]; then
        P=$((sz/4))
        H2=$(awk -v p="$P" 'BEGIN{h=int(sqrt(p*9/16/4)); if (h<64) h=540; print h}')
        W2=$((P/H2))
        if [ "$((W2*H2*4))" = "$sz" ]; then W=$W2; H=$H2; fi
    fi
    out="$OUT/${name}.png"
    src="${raw}.raw"
    mv "$raw" "$src" 2>/dev/null
    if ! magick -size ${W}x${H} -depth 8 rgba:"$src" -alpha off "$out" 2>/dev/null; then
        echo "magick fail on $raw (size=$sz w=$W h=$H)" >> "$OUT/summary.txt"
    fi
    rm -f "$src"
    [ -f "$out" ] && N=$((N+1))
done

{
    echo "---- cmd ----"
    echo "/usr/lib/ncz-screensavers/${HACK}_gles3 ${ARGS[*]}"
    echo "---- exit ----"
    echo "${EXIT:-NA}"
    echo "---- detected surface ----"
    echo "${W}x${H}"
    echo "---- harness diag (first 30 lines) ----"
    grep -E '\[diag\]|\[classics\]|\[stats\]|\[guard\]' "$OUT/log.txt" 2>/dev/null | head -30
    echo "---- frames saved ----"
    echo "$N"
} > "$OUT/summary.txt"

echo "rc=${EXIT:-NA}|n=$N|size=${W}x${H}"
