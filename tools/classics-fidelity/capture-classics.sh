#!/bin/bash
# capture-classics.sh : capture full-resolution frames from a classics hack on a
# shared test host.  Frames are dumped to NCZ_FRAME_DUMP by the harness at full
# window size; the harness writes raw RGBA with no header so we strip alpha and
# save as real PNGs.
#
# Usage: capture-classics.sh <hack> <variant> <seed> [extra-args...]
#   variant: classic | enhanced | ablate-bloom0 | ablate-aa0 | ablate-bloom-aa0
#            | classic-half | classic-quarter
#   extra-args: forwarded to <hack>_gles3 (e.g. --max-render-height=540).
#
# Layout under WORK (default /home/jasonperlow/build-tmp/classics-fidelity):
#   captures/<hack>/<variant>/seed-<seed>/{frame_*.png, log.txt, summary.txt}
set -u

HACK="${1:?hack}"; VARIANT="${2:?variant}"; SEED="${3:?seed}"; shift 3
EXTRA="$@"

WORK="${WORK:-/home/jasonperlow/build-tmp/classics-fidelity}"
OUT="${OUT:-${WORK}/captures/${HACK}/${VARIANT}/seed-${SEED}}"
mkdir -p "$WORK"/{captures,contact-sheets,logs,metrics}
mkdir -p "$OUT"

REMOTE_USER="${REMOTE_USER:?set REMOTE_USER}"
REMOTE_HOST="${REMOTE_HOST:?set REMOTE_HOST}"
: "${SSHPASS:?set SSHPASS in the environment}"; export SSHPASS
SSH="sshpass -e ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 ${REMOTE_USER}@${REMOTE_HOST}"

ARGS=(--seed="$SEED" --render-scale=1 --render-scale-mode=fixed)
case "$VARIANT" in
    classic)               ARGS+=(--style=classic)  ;;
    enhanced)              ARGS+=(--style=enhanced) ;;
    ablate-bloom0)         ARGS+=(--style=enhanced --bloom=0)  ;;
    ablate-aa0)            ARGS+=(--style=enhanced --antialias=false) ;;
    ablate-bloom-aa0)      ARGS+=(--style=enhanced --bloom=0 --antialias=false) ;;
    classic-half)          ARGS+=(--style=classic  --render-scale=0.5) ;;
    classic-quarter)       ARGS+=(--style=classic  --render-scale=0.25) ;;
    *) echo "unknown variant $VARIANT" | tee "$OUT/log.txt"; exit 2 ;;
esac
ARGS+=("$EXTRA")

# Clean /tmp on the host before each capture (the harness's tmpfs fills fast).
$SSH "rm -rf /tmp/cap /tmp/cap-* && mkdir -p /tmp/cap && df -h /tmp" 2>&1 >/dev/null

# Pull the compositor's EGL/Mesa env (NEVER software rendering)
ENV_CMD='cp=$(pgrep -u $(id -u) -x labwc | head -1); if [ -z "$cp" ]; then cp=$(pgrep -u $(id -u) -x Hyprland | head -1); fi; if [ -z "$cp" ]; then cp=$(pgrep -u $(id -u) -x sway | head -1); fi; if [ -z "$cp" ]; then cp=$(pgrep -u $(id -u) -x weston | head -1); fi; if [ -n "$cp" ] && [ -r /proc/$cp/environ ]; then while IFS= read -r -d "" kv; do case $kv in __EGL_VENDOR_LIBRARY_FILENAMES=*|LD_LIBRARY_PATH=*|MESA_*|GBM_*|LIBGL_*) export "$kv";; esac; done < /proc/$cp/environ; fi; echo COMP=\$cp'

CMD="cd /tmp/cap && export PATH=/usr/lib/ncz-screensavers:\$PATH && export XDG_RUNTIME_DIR=/run/user/\$(id -u) && export WAYLAND_DISPLAY=wayland-0 && ${ENV_CMD} && NCZ_FRAME_DUMP=/tmp/cap timeout --kill-after=2 5 /usr/lib/ncz-screensavers/${HACK}_gles3 ${ARGS[*]} > /tmp/cap/log.txt 2>&1; echo EXITCODE=\$?"

START=$(date +%s)
LOG=$($SSH "$CMD" 2>&1)
END=$(date +%s)
ELAPSED=$((END-START))
EXIT=$(echo "$LOG" | grep -oE 'EXITCODE=[0-9]+' | tail -1 | cut -d= -f2)

printf '%s\n' "$LOG" > "$OUT/log.txt"

# Pull the raw dump
$SSH "cd /tmp/cap && tar cf - ." 2>/dev/null | (cd "$OUT" && tar xf -) 2>/dev/null

# Convert RGBA dumps to PNGs.  Determine W x H from the harness stat line
# (e.g. "frame=#0 pixels=1474560 nonblack=...").
W=""; H=""
for px in $(grep -oE 'pixels=[0-9]+' "$OUT/log.txt" | head -1 | cut -d= -f2); do
    P=$px
done
if [ -n "${P:-}" ] && [ "$P" -gt 0 ]; then
    # Try the harness log line "render size WxH"
    RENDER=$(grep -oE 'render size [0-9]+x[0-9]+' "$OUT/log.txt" | head -1 | sed 's/render size //')
    if [ -n "$RENDER" ]; then
        W=${RENDER%x*}; H=${RENDER#*x}
    else
        # Default to 1536x960 (matches the original build-host capture script)
        H=960; W=1536
        if [ $((W*H*4)) -ne "$P" ]; then
            # try 1920x1080
            H=1080; W=1920
            if [ $((W*H*4)) -ne "$P" ]; then
                # derive from pixels
                H=$(awk -v p="$P" 'BEGIN{h=int(sqrt(p*9/16/4)); print h}')
                W=$(awk -v p="$P" 'BEGIN{w=int(p*4/h); print w}')
            fi
        fi
    fi
fi
[ -z "$W" ] && W=1536
[ -z "$H" ] && H=960

N=0
for raw in "$OUT"/frame_*.rgba "$OUT"/frame_*.png; do
    [ -f "$raw" ] || continue
    base=$(basename "$raw")
    name="${base%.*}"
    sz=$(stat -c%s "$raw")
    if [ "$sz" -le 0 ]; then rm -f "$raw"; continue; fi
    # Check if the file is actually a PNG by reading the magic bytes.  If so,
    # just symlink/copy it.  Otherwise convert from raw RGBA.  The PNG magic
    # is 89 50 4E 47 0D 0A 1A 0A; in `od -An -c` the first byte prints as 211
    # (octal of 0x89).  Use xxd-style hex via od -t x1 for a stable check.
    HEX3=$(od -An -t x1 -N3 "$raw" 2>/dev/null | head -1 | awk '{print $1$2$3}')
    if [ "$HEX3" = "89504e" ]; then
        [ "$base" != "${name}.png" ] && mv "$raw" "${name}.png"
        N=$((N+1))
        continue
    fi
    # Not a real PNG -> raw RGBA.  Size check.
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

# Remove the size=$W x $H guess from any later parsing: it lives in summary
{
    echo "---- cmd ----"
    echo "/usr/lib/ncz-screensavers/${HACK}_gles3 ${ARGS[*]}"
    echo "---- exit ----"
    echo "${EXIT:-NA}"
    echo "---- elapsed ----"
    echo "${ELAPSED}s"
    echo "---- detected surface ----"
    echo "${W}x${H}"
    echo "---- harness diag (first 30 lines) ----"
    grep -E '\[diag\]|\[classics\]|\[stats\]|\[guard\]' "$OUT/log.txt" 2>/dev/null | head -30
    echo "---- frames saved ----"
    echo "$N"
} > "$OUT/summary.txt"

echo "rc=${EXIT:-NA}|n=$N|size=${W}x${H}"
