#!/usr/bin/env bash
# run-slice.sh - reproduce exactly one slice recorded by run-long.sh.
#
#   run-slice.sh <slices.tsv> <slice_no> [seconds]
#
# It prints the chosen configuration and the exact command, then runs it with
# the working directory set to this directory (the magmasimplex shader loader
# resolves vendor/magmasimplex/*.frag relative to the cwd). With `seconds`, the
# slice is stopped by exact PID after that long (default: run until interrupted).
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
TSV="${1:-}"
SLICE="${2:-}"
SECONDS_TO_RUN="${3:-0}"

if [ -z "$TSV" ] || [ -z "$SLICE" ]; then
    echo "usage: run-slice.sh <slices.tsv> <slice_no> [seconds]" >&2
    exit 2
fi
[ -r "$TSV" ] || { echo "run-slice.sh: cannot read $TSV" >&2; exit 2; }

line="$(awk -F'\t' -v s="$SLICE" 'NR > 1 && $2 == s { print; exit }' "$TSV")"
if [ -z "$line" ]; then
    echo "run-slice.sh: slice $SLICE not found in $TSV" >&2
    exit 2
fi

cmd="$(printf '%s' "$line" | awk -F'\t' '{ print $NF }')"
fields="$(printf '%s' "$line" | awk -F'\t' '{ for (i = 1; i < NF; i++) printf "%s ", $i }')"

echo "slice $SLICE:"
echo "  $fields"
echo "  cmd: $cmd"

cd "$ROOT" || exit 2
if [ "$SECONDS_TO_RUN" -gt 0 ] 2>/dev/null; then
    exec $cmd &
    pid=$!
    trap 'kill -TERM "$pid" 2>/dev/null; exit 130' INT TERM
    ( sleep "$SECONDS_TO_RUN"; kill -TERM "$pid" 2>/dev/null ) &
    wait "$pid" 2>/dev/null
    exit $?
else
    exec $cmd
fi
