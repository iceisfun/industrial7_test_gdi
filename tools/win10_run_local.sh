#!/usr/bin/env bash
# tools/win10_run_local.sh - copy of /storage/lanec/w10lab/tools/win10_run.sh (lanec local oracle, bb depot m2).
# Install: ln -s /storage/lanec/w10lab ~/w10lab ; this file is the script tools/vm_run.sh calls as ~/w10lab/tools/win10_run.sh.
# win10_run.sh PAYLOAD_DIR [OUTDIR]  - boot the local Win10 22H2 oracle (bb depot m2) on a throw-away overlay,
# with PAYLOAD_DIR (must hold run.cmd) mounted as a CD whose root has i7run/ (guest hook C:\i7\boot.cmd runs i7run\run.cmd,
# powers off when i7run\shutdown exists). Serial -> OUTDIR/serial.log, QMP -> OUTDIR/qmp.sock, pid -> OUTDIR/qemu.pid.
# env: ORACLE_TIMEOUT (s, default 840), W10_KEEPDISK=1 (keep OUTDIR/disk.qcow2), W10_OVERLAY=file (boot this overlay, kept),
#      W10_BASE, W10_LAB (default /storage/lanec/w10lab, also reachable as ~/w10lab)
# exit: 0 ok, 65 no "I7VMRUN END" marker in serial.log, 124 timeout, 2 usage/setup.
set -uo pipefail
LAB="${W10_LAB:-/storage/lanec/w10lab}"; BASE="${W10_BASE:-$LAB/win10base.qcow2}"
PAY="${1:-}"; OUT="${2:-$LAB/runs/adhoc-$(date +%Y%m%d-%H%M%S)-$$}"
[ -f "$PAY/run.cmd" ] || { echo "usage: $0 PAYLOAD_DIR [OUTDIR] (PAYLOAD_DIR/run.cmd required)" >&2; exit 2; }
mkdir -p "$OUT" "$LAB/tmp"; OUT="$(cd "$OUT" && pwd)"
STG="$(mktemp -d "$LAB/tmp/iso.XXXXXX")"; trap 'rm -rf "$STG"' EXIT
mkdir -p "$STG/i7run"; cp -r "$PAY"/. "$STG/i7run/"
sed -i 's/\r$//; s/$/\r/' "$STG/i7run/run.cmd"; : > "$STG/i7run/shutdown"
xorriso -as mkisofs -quiet -iso-level 3 -J -r -V I7RUN -o "$OUT/payload.iso" "$STG" || { echo "iso build failed" >&2; exit 2; }
exec 9>"$LAB/oracle.lock"; echo "win10_run: waiting for oracle.lock" >&2; flock 9
if [ -n "${W10_OVERLAY:-}" ]; then DISK="$W10_OVERLAY"; KEEP=1
else DISK="$OUT/disk.qcow2"; rm -f "$DISK"; qemu-img create -q -f qcow2 -b "$BASE" -F qcow2 "$DISK" || exit 2
  KEEP="${W10_KEEPDISK:-0}"; fi
: > "$OUT/serial.log"; rm -f "$OUT/qmp.sock"
T0=$(date +%s)
timeout "${ORACLE_TIMEOUT:-840}" qemu-system-x86_64 -enable-kvm -machine q35 -cpu host -smp 2 -m 4G \
  -drive file="$DISK",if=ide,format=qcow2 -drive file="$OUT/payload.iso",media=cdrom,readonly=on,if=ide \
  -vga std -display none -serial file:"$OUT/serial.log" -qmp unix:"$OUT/qmp.sock",server,nowait \
  -nic none -rtc base=localtime -no-reboot -pidfile "$OUT/qemu.pid"
RC=$?
echo "win10_run: qemu rc=$RC after $(( $(date +%s)-T0 )) s" >&2
[ "$KEEP" = 1 ] || rm -f "$DISK"
rm -f "$OUT/qmp.sock" "$OUT/payload.iso"
[ $RC = 124 ] && exit 124
grep -a -q "I7VMRUN END" "$OUT/serial.log" || { echo "win10_run: no I7VMRUN END marker" >&2; exit 65; }
exit 0
