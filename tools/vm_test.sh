#!/usr/bin/env bash
# Run conformance test exes (build/<area>.exe from tests/<area>/*.c) on the local Win10 22H2 VM (w10lab), ONE boot for all areas.
#
#   tools/vm_test.sh AREA [AREA...] [options]
#     AREA               test area (directory under tests/); several may be given, space or comma separated
#   options
#     --args "..."       arguments for every exe (the harness takes one: a substring filter on test names)
#     --timeout S        per-area timeout in the guest (default 300)
#     --tag T            run tag (default t; used in the run id and the default OUTDIR)
#     --outdir DIR       results directory (default build/vmtest/<tag>-<stamp>)
#     --destructive      allow dx_* areas (refused without it)
#     --dry-run          build and print the payload, do not boot
#   env: ORACLE_TIMEOUT (per boot, default 840 s), W10RUN (boot script), W10LAB (lab dir, default ~/w10lab)
# Results: OUTDIR/<area>.log (the exe's stdout: RUN/OK/FAIL/SKIP/NOTE lines + SUMMARY), OUTDIR/<area>.err (stderr, if any),
#   OUTDIR/summary.txt (one line per area + totals), OUTDIR/serial.log (raw COM1), OUTDIR/results.tsv.
# Exit status: 0 every area PASSed (SKIPs allowed); 1 an area FAILed, crashed, timed out or has no result; 2 usage/build/boot error.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
W10LAB="${W10LAB:-$HOME/w10lab}"; W10RUN="${W10RUN:-$W10LAB/tools/win10_run.sh}"
usage(){ sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 1 ] || usage
AREAS=() ARGS="" TMO=300 TAG=t OUT="" DESTR=0 DRY=0
while [ $# -gt 0 ]; do case "$1" in
  -h|--help) usage;;
  --args) ARGS="${2?}"; shift 2;; --timeout) TMO="${2?}"; shift 2;; --tag) TAG="${2?}"; shift 2;;
  --outdir) OUT="${2?}"; shift 2;; --destructive) DESTR=1; shift;; --dry-run) DRY=1; shift;;
  -*) echo "unknown option $1" >&2; usage;;
  *) IFS=, read -r -a a <<< "$1"; AREAS+=("${a[@]}"); shift;; esac; done
[ ${#AREAS[@]} -ge 1 ] || usage
[[ "$TMO" =~ ^[0-9]+$ ]] && [ "$TMO" -gt 0 ] || { echo "bad --timeout" >&2; exit 2; }
[[ "$TAG" =~ ^[A-Za-z0-9_.+-]+$ ]] || { echo "bad --tag (A-Za-z0-9_.+-)" >&2; exit 2; }
[[ "$ARGS" != *[\"\&\|\<\>\^%]* ]] || { echo "--args must not contain cmd metacharacters (\" & | < > ^ %)" >&2; exit 2; }
for a in "${AREAS[@]}"; do
  [[ "$a" =~ ^[A-Za-z0-9_]+$ ]] && [ -d "tests/$a" ] || { echo "unknown area '$a' (no tests/$a)" >&2; exit 2; }
  case "$a" in dx_*) [ $DESTR = 1 ] || { echo "area $a is destructive: pass --destructive (disposable guest only)" >&2; exit 2; };; esac
done

make -s build/vmrunner.exe $(printf 'build/%s.exe ' "${AREAS[@]}") || { echo "build failed" >&2; exit 2; }

STAMP="$(date +%Y%m%d-%H%M%S)"; RUNID="gdi-test-$TAG-$STAMP-$$"
[ -n "$OUT" ] || OUT="build/vmtest/$TAG-$STAMP"
RUN="$W10LAB/runs/$RUNID"; PAY="$ROOT/build/vmpay/$RUNID"; S2="$PAY/s2"
mkdir -p "$S2" "$OUT" "$RUN"; cp build/vmrunner.exe "$S2/"
: > "$S2/plan.txt"
echo "echo id=$RUNID tag=$TAG" >> "$S2/plan.txt"
for a in "${AREAS[@]}"; do
  cp "build/$a.exe" "$S2/"
  echo "run $TMO $a $a.exe${ARGS:+ $ARGS}" >> "$S2/plan.txt"
done
# Single-stage run.cmd. Redirections are prefix style (>COM1 echo ...) so a trailing digit never parses as a handle.
{ echo '@echo off'
  echo 'set R=D:\i7run'
  echo 'for %%d in (D E F G H I J K) do if exist %%d:\i7run\run.cmd set R=%%d:\i7run'
  echo ">COM1 echo I7VMRUN BEGIN id=$RUNID"
  echo 'ver >COM1'
  echo '%R%\vmrunner.exe %R%\plan.txt >COM1'
  echo ">COM1 echo I7VMRUN END id=$RUNID"
  echo 'exit /b 0'; } > "$S2/run.cmd"
if [ $DRY = 1 ]; then ls "$S2"; for f in "$S2"/run.cmd "$S2"/plan.txt; do echo "-- $f"; cat "$f"; done; rmdir "$RUN" 2>/dev/null; exit 0; fi

export ORACLE_TIMEOUT="${ORACLE_TIMEOUT:-840}"
echo "run dir: $RUN  results: $OUT"
T0=$(date +%s)
"$W10RUN" "$S2" "$RUN/stage2"; RC=$?
T1=$(date +%s)
[ $RC = 0 ] || echo "win10_run.sh rc=$RC (no I7VMRUN END, timeout, or boot refused)" >&2
SER="$RUN/stage2/serial.log"
[ -f "$SER" ] || { echo "no serial.log at $SER" >&2; exit 2; }
cp "$SER" "$OUT/serial.log"
python3 -I tools/vm_test_split.py "$OUT/serial.log" "$OUT" "$((T1-T0))" "${AREAS[@]}"
SRC=$?
[ $RC = 0 ] || [ $SRC != 0 ] || SRC=1
exit $SRC
