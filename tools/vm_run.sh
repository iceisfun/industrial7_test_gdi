#!/usr/bin/env bash
# Run probes on the local Win10 22H2 VM (w10lab) and file their recordings under data/win10/<probe>/<tag>.rec.
#
#   tools/vm_run.sh PROBE[,PROBE...] [options]
#     PROBE        probe name (probes/<name>) optionally with a variant: hello, hello.v6, hello.v6pm2, hello.x86 ...
#   options
#     --dpi N[,N...]     96 (default); a list sweeps DPIs in ONE boot (logoff + auto-logon between them, see docs/VM_RUN.md)
#     --dpi-mode M       logoff (default) | boot (two boots per DPI: set, power off, boot the same overlay; single DPI only)
#     --res WxH          guest display mode (vmrunner setres before the probes; persisted in boot mode)
#     --args "..."       extra arguments for every probe
#     --tag T            tag (default dpi<N>[-<W>x<H>]; with a DPI list T is used as a prefix: T-dpi<N>)
#     --timeout S        per-probe timeout in the guest (default 300)
#     --shot NAME        QMP screendump after the probes of every DPI stage (NAME-dpi<N>); implies --screendump
#     --screendump       run tools/screendump.py watch (probes may print 'I7P SHOT name=X' and sleep)
#     --data DIR         destination (default data/win10); --no-split leaves the serial.log only
#     --keep-bad         also write recordings that fail validation (as *.rec.bad, gitignored)
#     --plan-extra F     append the vmrunner plan lines in F to the last DPI stage (e.g. setres + more runs)
#     --dry-run          build the payload(s) and print them, do not boot
#   env: ORACLE_TIMEOUT (per boot, default 840 s), W10_* pass through to ~/w10lab/tools/win10_run.sh
# Exit status: 0 all recordings valid; 1 some recording bad/missing; 2 usage/build/boot error.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
W10RUN="${W10RUN:-$HOME/w10lab/tools/win10_run.sh}"
usage(){ sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 1 ] || usage
case "$1" in -h|--help) usage;; esac
SPECS="$1"; shift
DPIS=96 MODE=logoff RES="" ARGS="" TAG="" TMO=300 SHOTS=() SCREENDUMP=0 DATA="data/win10" SPLIT=1 KEEPBAD="" DRY=0 EXTRA=""
while [ $# -gt 0 ]; do case "$1" in
  --dpi) DPIS="$2"; shift 2;; --dpi-mode) MODE="$2"; shift 2;; --res) RES="$2"; shift 2;; --args) ARGS="$2"; shift 2;;
  --tag) TAG="$2"; shift 2;; --timeout) TMO="$2"; shift 2;; --shot) SHOTS+=("$2"); SCREENDUMP=1; shift 2;;
  --screendump) SCREENDUMP=1; shift;; --data) DATA="$2"; shift 2;; --no-split) SPLIT=0; shift;;
  --keep-bad) KEEPBAD="--keep-bad"; shift;; --dry-run) DRY=1; shift;; --plan-extra) EXTRA="$2"; shift 2;;
  *) echo "unknown option $1" >&2; usage;; esac; done
[[ "$DPIS" =~ ^[0-9]+(,[0-9]+)*$ ]] || { echo "bad --dpi" >&2; exit 2; }
[ -z "$RES" ] || [[ "$RES" =~ ^[0-9]+x[0-9]+$ ]] || { echo "bad --res" >&2; exit 2; }
[ -z "$TAG" ] || [[ "$TAG" =~ ^[A-Za-z0-9_.+-]+$ ]] || { echo "bad --tag (A-Za-z0-9_.+-)" >&2; exit 2; }
case "$MODE" in logoff|boot) ;; *) echo "bad --dpi-mode" >&2; exit 2;; esac
IFS=, read -r -a PROBES <<< "$SPECS"
IFS=, read -r -a DPI <<< "$DPIS"
[ "$MODE" = boot ] && [ ${#DPI[@]} -gt 1 ] && { echo "--dpi-mode boot takes one DPI" >&2; exit 2; }
tag_for(){ # tag_for <dpi>
  local t; if [ ${#DPI[@]} -gt 1 ]; then t="${TAG:+$TAG-}dpi$1${RES:+-$RES}"; else t="${TAG:-dpi$1${RES:+-$RES}}"; fi; echo "$t"; }

# ---- build
TARGETS=(build/vmrunner.exe); for p in "${PROBES[@]}"; do TARGETS+=("build/probes/$p.exe"); done
make -s "${TARGETS[@]}" || { echo "build failed" >&2; exit 2; }

STAMP="$(date +%Y%m%d-%H%M%S)"; RUNID="gdi-$(tag_for "${DPI[0]}")$([ ${#DPI[@]} -gt 1 ] && echo "-sweep")-$STAMP-$$"
RUN="$HOME/w10lab/runs/$RUNID"; PAY="$ROOT/build/vmpay/$RUNID"; mkdir -p "$RUN" "$PAY"
HEAD='@echo off
set R=D:\i7run
for %%d in (D E F G H I J K) do if exist %%d:\i7run\run.cmd set R=%%d:\i7run'
# cmd fragments. Redirections are written prefix-style (>COM1 echo ...): "x=144>COM1" would parse as handle 4.
setdpi(){ # setdpi <dpi>: per-user custom scaling (applied at the next logon)
  if [ "$1" = 96 ]; then
    printf '%s\n' 'reg delete "HKCU\Control Panel\Desktop" /v LogPixels /f >nul 2>&1' \
                  'reg add "HKCU\Control Panel\Desktop" /v Win8DpiScaling /t REG_DWORD /d 0 /f >nul'
  else
    printf '%s\n' "reg add \"HKCU\\Control Panel\\Desktop\" /v LogPixels /t REG_DWORD /d $1 /f >nul" \
                  'reg add "HKCU\Control Panel\Desktop" /v Win8DpiScaling /t REG_DWORD /d 1 /f >nul'
  fi
  printf '%s\n' ">COM1 echo I7DPI set LogPixels=$1"; }
regq='reg query "HKCU\Control Panel\Desktop" /v LogPixels >COM1 2>&1
reg query "HKCU\Control Panel\Desktop" /v Win8DpiScaling >COM1 2>&1
reg query "HKCU\Control Panel\Desktop\WindowMetrics" /v AppliedDPI >COM1 2>&1'
logoff(){ # logoff <next stage>: record the next stage, make sure auto-logon follows a logoff, log off and wait.
  # ForceAutoLogon=1 (HKLM Winlogon, throw-away overlay) re-logs Administrator on after a logoff. The wait is long
  # (logoff kills this cmd within seconds); if it ever returns, the hook powers the VM off and the sweep is cut short.
  printf '%s\n' ">C:\\i7gdi\\stage.txt echo $1" \
    'reg add "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon" /v ForceAutoLogon /t REG_SZ /d 1 /f >nul 2>&1' \
    ">COM1 echo I7VMRUN LOGOFF next=$1" 'shutdown /l' 'ping -n 300 127.0.0.1 >nul' '>COM1 echo I7VMRUN LOGOFF-FAILED' 'exit /b 0'; }
plan_for(){ # plan_for <dpi> <file>
  local t; t="$(tag_for "$1")"
  { echo "echo id=$RUNID tag=$t dpi=$1 res=${RES:-default}"
    case "$2" in */plan0.txt) echo "modes";; esac       # display modes the guest driver offers, once per boot
    [ -n "$RES" ] && echo "setres $RES"
    for p in "${PROBES[@]}"; do echo "run $TMO $p $p.exe --tag $t $ARGS"; done
    for s in "${SHOTS[@]+"${SHOTS[@]}"}"; do echo "shot $s-dpi$1 3000"; done; } > "$2"; }

S2="$PAY/s2"; mkdir -p "$S2"; cp build/vmrunner.exe "$S2/"
for p in "${PROBES[@]}"; do cp "build/probes/$p.exe" "$S2/"; done
if [ "$MODE" = logoff ]; then
  # One boot. Logon stage k (state in C:\i7gdi\stage.txt on the throw-away overlay) runs the probes at DPI[k'] and then
  # writes the next DPI and logs off; AutoAdminLogon logs Administrator back on and the HKLM Run hook (C:\i7\boot.cmd)
  # calls run.cmd again. The last stage returns normally, so the hook powers the VM off.
  STEPS=(); cur=96                      # logon stages: each = "RUN <dpi>" optionally followed by "SET <dpi>"
  {
    echo "$HEAD"
    echo 'if not exist C:\i7gdi mkdir C:\i7gdi'
    echo 'set S=0'
    echo 'if exist C:\i7gdi\stage.txt set /p S=<C:\i7gdi\stage.txt'
    echo ">COM1 echo I7VMRUN BEGIN id=$RUNID logon=%S%"
    echo 'goto L%S%'
    k=0; i=0
    # stage 0 may need a DPI change before any probe runs
    if [ "${DPI[0]}" != 96 ]; then
      echo ":L$k"; setdpi "${DPI[0]}"; k=$((k+1))
      logoff "$k"
    fi
    for d in "${DPI[@]}"; do
      plan_for "$d" "$S2/plan$i.txt"
      echo ":L$k"; echo "$regq"; echo ">COM1 echo I7VMRUN STAGE dpi=$d plan=plan$i.txt"
      echo "%R%\\vmrunner.exe %R%\\plan$i.txt >COM1"
      i=$((i+1))
      if [ $i -lt ${#DPI[@]} ]; then
        setdpi "${DPI[$i]}"; k=$((k+1))
        logoff "$k"
      fi
    done
    echo ">COM1 echo I7VMRUN END id=$RUNID"
    echo 'exit /b 0'
  } > "$S2/run.cmd"
else
  # boot mode: stage 1 sets the DPI (and persists the display mode), powers off; stage 2 boots the same overlay.
  mkdir -p "$PAY/s1"; cp build/vmrunner.exe "$PAY/s1/"
  { echo "echo stage1 dpi=${DPI[0]} res=${RES:-default}"; [ -n "$RES" ] && echo "setres $RES persist"; } > "$PAY/s1/plan.txt"
  { echo "$HEAD"; echo ">COM1 echo I7VMRUN BEGIN id=$RUNID stage=1"; setdpi "${DPI[0]}"; echo "$regq"
    echo '%R%\vmrunner.exe %R%\plan.txt >COM1'; echo ">COM1 echo I7VMRUN END stage=1"; } > "$PAY/s1/run.cmd"
  plan_for "${DPI[0]}" "$S2/plan0.txt"
  { echo "$HEAD"; echo ">COM1 echo I7VMRUN BEGIN id=$RUNID stage=2"; echo "$regq"
    echo '%R%\vmrunner.exe %R%\plan0.txt >COM1'; echo ">COM1 echo I7VMRUN END id=$RUNID"; } > "$S2/run.cmd"
fi
if [ -n "$EXTRA" ]; then last="$(ls "$S2"/plan*.txt | sort -V | tail -1)"; cat "$EXTRA" >> "$last"; fi
if [ $DRY = 1 ]; then
  for d in "$PAY"/s*; do echo "== $d"; ls "$d"; for f in "$d"/*.cmd "$d"/*.txt; do echo "-- $f"; cat "$f"; done; done
  rmdir "$RUN" 2>/dev/null; exit 0; fi

export ORACLE_TIMEOUT="${ORACLE_TIMEOUT:-840}"
boot(){ # boot <payload> <outdir>; screendump watcher alongside when asked
  local wp=""
  if [ $SCREENDUMP = 1 ]; then mkdir -p "$2"; python3 tools/screendump.py watch "$2" > "$2/screendump.log" 2>&1 & wp=$!; fi
  "$W10RUN" "$1" "$2"; local rc=$?
  [ -n "$wp" ] && { sleep 1; kill "$wp" 2>/dev/null; wait "$wp" 2>/dev/null; }
  return $rc
}
echo "run dir: $RUN"
if [ "$MODE" = boot ]; then
  W10_KEEPDISK=1 boot "$PAY/s1" "$RUN/stage1" || { echo "stage 1 boot failed" >&2; exit 2; }
  grep -a -q "I7VMRUN END stage=1" "$RUN/stage1/serial.log" || { echo "stage 1 did not finish (see $RUN/stage1/serial.log)" >&2; rm -f "$RUN/stage1/disk.qcow2"; exit 2; }
  W10_OVERLAY="$RUN/stage1/disk.qcow2" boot "$S2" "$RUN/stage2"; RC=$?
  rm -f "$RUN/stage1/disk.qcow2"
else
  boot "$S2" "$RUN/stage2"; RC=$?
fi
SER="$RUN/stage2/serial.log"
[ $RC = 0 ] || echo "win10_run.sh rc=$RC" >&2
grep -a -E '^(I7VM|I7DPI|I7P (BEGIN|END|SETRES|SHOT|DONE|ERR))' "$SER" | tr -d '\r' | cut -c1-200
[ $SPLIT = 1 ] || { echo "serial: $SER"; exit 0; }
mkdir -p "$DATA/_runs"
python3 tools/rec.py split "$SER" --outdir "$DATA" $KEEPBAD --runlog "$DATA/_runs/$(tag_for "${DPI[0]}")$([ ${#DPI[@]} -gt 1 ] && echo "-sweep").$(echo "$SPECS" | tr ',' '+').log"
SRC=$?
for d in "${DPI[@]}"; do t="$(tag_for "$d")"            # every requested (probe, DPI) must have a recording
  for p in "${PROBES[@]}"; do base="${p%%.*}"; var="${p#"$base"}"; f="$DATA/$base/$t$var.rec"
    [ -f "$f" ] || [ -f "$f.gz" ] || { echo "MISSING recording for $p at dpi $d ($f)" >&2; SRC=1; }; done; done
echo "serial: $SER"
exit $SRC
