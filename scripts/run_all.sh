#!/usr/bin/env bash
# Run every probe exe and collect recs; counterpart of run_all.ps1 for Git-Bash/MSYS2/Cygwin on Windows,
# or for Linux with a wrapper (PROBE_RUNNER=tools/wine_run.sh) -- Wine output is plumbing only, never a reference.
# usage: scripts/run_all.sh [-d DIR]... [-o OUTDIR] [-t SECONDS] [-i GLOB]... [-x GLOB]... [-D] [-- probe args]
#   -d  directory holding *.exe (repeatable; default build/ next to this script)   -o  output dir (default recs/<host>-<stamp>)
#   -t  per-probe timeout, default 300   -i/-x include/exclude name globs   -D  also run dx_* (destructive) areas
# Env: PROBE_RUNNER=<command prefix to launch an exe> (NOT named WINE: tools/wine_run.sh reads $WINE as the wine binary)
set -u
here="$(cd "$(dirname "$0")" && pwd)"
dirs=(); out=""; tmo=300; inc=(); exc=(); destr=0
while getopts "d:o:t:i:x:Dh" o; do case $o in
  d) dirs+=("$OPTARG");; o) out="$OPTARG";; t) tmo="$OPTARG";; i) inc+=("$OPTARG");; x) exc+=("$OPTARG");; D) destr=1;;
  *) sed -n '2,7p' "$0"; exit 2;; esac; done
shift $((OPTIND-1)); [ "${1:-}" = "--" ] && shift
[ ${#dirs[@]} -eq 0 ] && dirs=("$here/../build"); [ ${#inc[@]} -eq 0 ] && inc=("*")
for i in "${!dirs[@]}"; do dirs[$i]="$(cd "${dirs[$i]}" && pwd)"; done
hostn="${COMPUTERNAME:-$(hostname)}"; stamp="$(date +%Y%m%d-%H%M%S)"
[ -z "$out" ] && out="$here/../recs/$hostn-$stamp"
mkdir -p "$out"; out="$(cd "$out" && pwd)"
sha() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1 || certutil -hashfile "$1" SHA256 | sed -n 2p; }
matchany() { local n="$1"; shift; local g; for g in "$@"; do case "$n" in $g) return 0;; esac; done; return 1; }
{
  echo "RUNALL host=$hostn user=${USERNAME:-${USER:-?}} uname=$(uname -sr | tr ' ' '_') runner=${PROBE_RUNNER:-none}"
  if command -v cmd.exe >/dev/null 2>&1; then echo "RUNALL ver=$(cmd.exe /c ver 2>/dev/null | tr -d '\r' | grep -i version | sed 's/ /_/g')"; fi
} > "$out/_env.rec"
: > "$out/_status.rec"; nfail=0
for d in "${dirs[@]}"; do
  for e in "$d"/*.exe; do
    [ -e "$e" ] || continue
    n="$(basename "$e" .exe)"
    matchany "$n" "${inc[@]}" || continue
    if [ ${#exc[@]} -gt 0 ] && matchany "$n" "${exc[@]}"; then continue; fi
    case "$n" in dx_*) if [ $destr -eq 0 ]; then echo "RUNALL rc name=$n rc=skipped reason=destructive" >> "$out/_status.rec"; continue; fi;; esac
    echo "=== $n"
    t0=$(date +%s)
    ( cd "$out" && timeout "$tmo" ${PROBE_RUNNER:-} "$e" "$@" > "$n.rec" 2> "$n.err" ); rc=$?
    [ $rc -eq 124 ] && rc=timeout
    [ "$rc" != 0 ] && nfail=$((nfail+1))
    tr -d '\r' < "$out/$n.rec" > "$out/$n.rec.tmp" && mv "$out/$n.rec.tmp" "$out/$n.rec"
    echo "RUNALL rc name=$n rc=$rc seconds=$(( $(date +%s) - t0 )) lines=$(wc -l < "$out/$n.rec" | tr -d ' ') exe_sha256=$(sha "$e")" >> "$out/_status.rec"
    echo "    rc=$rc"
  done
done
( cd "$out" && for f in $(ls | grep -v '^MANIFEST.txt$'); do printf '%s  %9s  %s\n' "$(sha "$f")" "$(wc -c < "$f" | tr -d ' ')" "$f"; done > MANIFEST.txt )
echo "recs in $out ($nfail failed/timeout)"
[ $nfail -eq 0 ]
