#!/usr/bin/env bash
# Plumbing check of a probe under headless Wine (NOT a source of Windows data; Wine's fonts and metrics differ).
#   tools/wine_probe.sh PROBE[.variant] [probe args...]
# Builds build/probes/PROBE.exe, runs it via tools/wine_run.sh (DISPLAY unset, throw-away prefix, timeout),
# writes build/wine/PROBE.rec and validates its END trailer with tools/rec.py. Exit status = validation status.
# Recordings from here must never be copied into data/win10.
set -uo pipefail
cd "$(dirname "$0")/.."
P="${1:?probe name, e.g. hello or hello.v6pm2}"; shift
make -s "build/probes/$P.exe" || exit 2
mkdir -p build/wine
OUT="build/wine/$P.rec"
TP_TIMEOUT="${TP_TIMEOUT:-300}" tools/wine_run.sh "build/probes/$P.exe" --tag wine "$@" > "$OUT" 2> "build/wine/$P.err"
RC=$?
echo "wine exit $RC; $(wc -l < "$OUT") lines -> $OUT (stderr: build/wine/$P.err)"
python3 tools/rec.py validate "$OUT"
