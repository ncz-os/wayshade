#!/usr/bin/env bash
# contact-sheet.sh - montage the screenshots of one run into a contact sheet.
#
#   contact-sheet.sh <runid> [tile]
#
# Uses ImageMagick (magick/montage) when present; otherwise it writes an index
# listing the shots and prints the exact montage command to run elsewhere.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
RUNID="${1:-}"
TILE="${2:-4x}"
SHOTDIR="$ROOT/shots/$RUNID"

if [ -z "$RUNID" ]; then
    echo "usage: contact-sheet.sh <runid> [tile]" >&2
    echo "runids:"; ls -1 "$ROOT/shots" 2>/dev/null
    exit 2
fi
[ -d "$SHOTDIR" ] || { echo "contact-sheet.sh: no such shots dir: $SHOTDIR" >&2; exit 2; }

shopt -s nullglob
shots=()
for f in "$SHOTDIR"/s*-t*.png; do
    case "$f" in (*.1600.png) ;; (*) shots+=("$f") ;; esac
done

if [ "${#shots[@]}" -eq 0 ]; then
    echo "contact-sheet.sh: no shots in $SHOTDIR" >&2
    exit 2
fi

out="$SHOTDIR/contact-sheet.png"
if command -v montage >/dev/null 2>&1; then
    montage "${shots[@]}" -tile "$TILE" -geometry 480x270+6+6 \
        -background '#111111' -fill white -label '%f' "$out" && \
        echo "wrote $out (tile $TILE, ${#shots[@]} shots)"
elif command -v magick >/dev/null 2>&1; then
    magick montage "${shots[@]}" -tile "$TILE" -geometry 480x270+6+6 \
        -background '#111111' -fill white -label '%f' "$out" && \
        echo "wrote $out (tile $TILE, ${#shots[@]} shots)"
else
    echo "ImageMagick not present; shots (${#shots[@]}) are:"
    printf '  %s\n' "${shots[@]}"
    echo "make the sheet elsewhere with:"
    echo "  montage ${shots[*]} -tile $TILE -geometry 480x270+6+6 -label '%f' contact-sheet.png"
fi
