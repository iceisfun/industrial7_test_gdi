#!/usr/bin/env bash
# Run probes on the local Win10 22H2 VM (w10lab) and file their recordings under data/win10/<probe>/<tag>.rec.
#
#   tools/vm_run.sh PROBE[,PROBE...] [options]
#     PROBE        probe name (probes/<name>) optionally with a manifest variant: hello, hello.v6, hello.v6pm2, hello.x86 ...
#   options
#     --dpi N            96 (default) | 120 | 144 | 168 | 192 ... ; != 96 uses a two-stage boot (see docs/VM_RUN.md)
#     --res WxH          guest display mode (vmrunner setres; persisted in stage 1 when two-stage)
#     --args "..."       extra arguments for every probe
#     --tag T            recording tag (default dpi<N>[-<W>x<H>]); file name data/win10/<probe>/<T>[.<variant>].rec
#     --timeout S        per-probe timeout in the guest (default 300)
#     --shot NAME        take a QMP screendump after the probes (repeatable); implies --screendump
#     --screendump       run tools/screendump.py watch (probes may print 'I7P SHOT name=X' and sleep)
#     --data DIR         destination (default data/win10); --no-split leaves the serial.log only
#     --keep-bad         also write recordings that fail validation (as *.rec.bad)
#     --dry-run          build the payload(s) and print them, do not boot
#   env: ORACLE_TIMEOUT (per boot, default 840 s), W10_* pass through to ~/w10lab/tools/win10_run.sh
# Exit status: 0 all recordings valid; 1 some recording bad/missing; 2 usage/build/boot error.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
W10RUN="${W10RUN:-$HOME/w10lab/tools/win10_run.sh}"
usage(){ sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 1 ] || usage
case "$1" in -h|--help) usage;; esac
SPECS="$1"; shift
DPI=96 RES="" ARGS="" TAG="" TMO=300 SHOTS=() SCREENDUMP=0 DATA="data/win10" SPLIT=1 KEEPBAD="" DRY=0
while [ $# -gt 0 ]; do case "$1" in
  --dpi) DPI="$2"; shift 2;; --res) RES="$2"; shift 2;; --args) ARGS="$2"; shift 2;; --tag) TAG="$2"; shift 2;;
  --timeout) TMO="$2"; shift 2;; --shot) SHOTS+=("$2"); SCREENDUMP=1; shift 2;; --screendump) SCREENDUMP=1; shift;;
  --data) DATA="$2"; shift 2;; --no-split) SPLIT=0; shift;; --keep-bad) KEEPBAD="--keep-bad"; shift;; --dry-run) DRY=1; shift;;
  *) echo "unknown option $1" >&2; usage;; esac; done
[[ "$DPI" =~ ^[0-9]+$ ]] || { echo "bad --dpi" >&2; exit 2; }
[ -z "$RES" ] || [[ "$RES" =~ ^[0-9]+x[0-9]+$ ]] || { echo "bad --res" >&2; exit 2; }
[ -n "$TAG" ] || TAG="dpi$DPI${RES:+-$RES}"
[[ "$TAG" =~ ^[A-Za-z0-9_.+-]+$ ]] || { echo "bad --tag (A-Za-z0-9_.+-)" >&2; exit 2; }
IFS=, read -r -a PROBES <<< "$SPECS"

# ---- build
TARGETS=(build/vmrunner.exe); for p in "${PROBES[@]}"; do TARGETS+=("build/probes/$p.exe"); done
make -s "${TARGETS[@]}" || { echo "build failed" >&2; exit 2; }

STAMP="$(date +%Y%m%d-%H%M%S)"; RUNID="gdi-$TAG-$STAMP-$$"
RUN="$HOME/w10lab/runs/$RUNID"; PAY="$ROOT/build/vmpay/$RUNID"; mkdir -p "$RUN" "$PAY"
HEAD='@echo off
set R=D:\i7run
for %%d in (D E F G H I J K) do if exist %%d:\i7run\run.cmd set R=%%d:\i7run'
TWO=0; [ "$DPI" != 96 ] && TWO=1

# ---- stage 1 (only for a DPI change): write the per-user scaling values, persist the display mode, power off
if [ $TWO = 1 ]; then
  mkdir -p "$PAY/s1"; cp build/vmrunner.exe "$PAY/s1/"
  { echo "echo stage1 dpi=$DPI res=${RES:-default}"
    [ -n "$RES" ] && echo "setres $RES persist"
    echo "cmd 60 regq reg query \"HKCU\\Control Panel\\Desktop\" /v LogPixels"; } > "$PAY/s1/plan.txt"
  cat > "$PAY/s1/run.cmd" <<EOF
$HEAD
>COM1 echo I7VMRUN BEGIN id=$RUNID stage=1 dpi=$DPI
reg add "HKCU\\Control Panel\\Desktop" /v LogPixels /t REG_DWORD /d $DPI /f >COM1 2>&1
reg add "HKCU\\Control Panel\\Desktop" /v Win8DpiScaling /t REG_DWORD /d 1 /f >COM1 2>&1
%R%\\vmrunner.exe %R%\\plan.txt >COM1
>COM1 echo I7VMRUN END stage=1 rc=%ERRORLEVEL%
EOF
fi

# ---- stage 2 (or the only stage): run the probes
S2="$PAY/s2"; mkdir -p "$S2"; cp build/vmrunner.exe "$S2/"
{ echo "echo id=$RUNID tag=$TAG dpi=$DPI res=${RES:-default}"
  [ -n "$RES" ] && echo "setres $RES"
  for p in "${PROBES[@]}"; do cp "build/probes/$p.exe" "$S2/"; echo "run $TMO $p $p.exe --tag $TAG $ARGS"; done
  for s in "${SHOTS[@]+"${SHOTS[@]}"}"; do echo "shot $s 3000"; done; } > "$S2/plan.txt"
cat > "$S2/run.cmd" <<EOF
$HEAD
>COM1 echo I7VMRUN BEGIN id=$RUNID stage=2 tag=$TAG
ver >COM1
reg query "HKCU\\Control Panel\\Desktop" /v LogPixels >COM1 2>&1
reg query "HKCU\\Control Panel\\Desktop\\WindowMetrics" /v AppliedDPI >COM1 2>&1
%R%\\vmrunner.exe %R%\\plan.txt >COM1
>COM1 echo I7VMRUN END stage=2 rc=%ERRORLEVEL%
EOF
if [ $DRY = 1 ]; then
  for d in "$PAY"/s*; do echo "== $d"; ls -la "$d"; cat "$d/run.cmd" "$d/plan.txt"; done; rmdir "$RUN" 2>/dev/null; exit 0; fi

export ORACLE_TIMEOUT="${ORACLE_TIMEOUT:-840}"
boot(){ # boot <payload> <outdir>; screendump watcher alongside when asked
  local wp=""
  if [ $SCREENDUMP = 1 ]; then mkdir -p "$2"; python3 tools/screendump.py watch "$2" > "$2/screendump.log" 2>&1 & wp=$!; fi
  "$W10RUN" "$1" "$2"; local rc=$?
  [ -n "$wp" ] && { sleep 1; kill "$wp" 2>/dev/null; wait "$wp" 2>/dev/null; }
  return $rc
}
echo "run dir: $RUN"
if [ $TWO = 1 ]; then
  W10_KEEPDISK=1 boot "$PAY/s1" "$RUN/stage1" || { echo "stage 1 boot failed" >&2; exit 2; }
  grep -a -q "I7VMRUN END stage=1" "$RUN/stage1/serial.log" || { echo "stage 1 did not finish (see $RUN/stage1/serial.log)" >&2; rm -f "$RUN/stage1/disk.qcow2"; exit 2; }
  W10_OVERLAY="$RUN/stage1/disk.qcow2" boot "$S2" "$RUN/stage2"; RC=$?
  rm -f "$RUN/stage1/disk.qcow2"
  SER="$RUN/stage2/serial.log"
else
  boot "$S2" "$RUN/stage2"; RC=$?
  SER="$RUN/stage2/serial.log"
fi
[ $RC = 0 ] || echo "win10_run.sh rc=$RC" >&2
grep -a -E '^(I7VMRUN|I7P (BEGIN|END|SETRES|SHOT|DONE|ERR))' "$SER" | tr -d '\r' | cut -c1-200
[ $SPLIT = 1 ] || { echo "serial: $SER"; exit 0; }
mkdir -p "$DATA/_runs"
python3 tools/rec.py split "$SER" --outdir "$DATA" --tag "$TAG" $KEEPBAD --runlog "$DATA/_runs/$TAG.$(echo "$SPECS" | tr ',' '+').log"
SRC=$?
# every requested probe must have produced a recording
for p in "${PROBES[@]}"; do
  base="${p%%.*}"; var="${p#"$base"}"; f="$DATA/$base/$TAG$var.rec"
  [ -f "$f" ] || [ -f "$f.gz" ] || { echo "MISSING recording for $p ($f)" >&2; SRC=1; }
done
echo "serial: $SER"
exit $SRC
