# Running probes on the Win10 VM (w10lab)

    make probes                                   # optional; vm_run.sh builds what it needs
    tools/vm_run.sh hello                         # 96 dpi, one boot -> data/win10/hello/dpi96.rec
    tools/vm_run.sh tm_basic,tm_basic.v6pm2 --dpi 144 --args "--faces ui"   # two-stage boot
    tools/vm_run.sh hello --res 1280x1024 --shot desktop                    # display mode + QMP screendump

## 1. The chain

1. **Build** (`make`): `build/probes/<spec>.exe` for each `PROBE[.variant]` and `build/vmrunner.exe`.
2. **Payload** (`build/vmpay/<runid>/s2/`, plus `s1/` for a DPI change): the exes, `vmrunner.exe`, `plan.txt`, `run.cmd`.
   `run.cmd` locates its own drive (`D..K:\i7run`), prints `I7VMRUN BEGIN`, `ver` and the HKCU DPI registry values to
   COM1, runs `vmrunner.exe plan.txt >COM1`, prints `I7VMRUN END rc=`. (cmd note: redirections are written prefix
   style, `>COM1 echo ...`, because `...=144>COM1` would be parsed as handle redirection `4>`.)
3. **Runner** (`tools/vmrunner/vmrunner.c`): executes the plan. Each probe inherits the runner's stdout (the COM1
   handle cmd opened), so probe output goes straight to the serial port; stderr goes to `%TEMP%` and is relayed as
   `I7P ERR name=... | <escaped line>`. Each step is bracketed by `I7P BEGIN name= seq= timeout=` /
   `I7P END name= seq= rc= ms= timeout=0|1`; a step that exceeds its timeout is killed with its whole process tree
   (job object) and its recording has no `END` trailer. Verbs: `run <s> <name> <exe> [args]`, `cmd <s> <name> <cmdline>`,
   `setres WxH [persist]`, `shot <name> [ms]`, `sleep <ms>`, `echo <text>`; `I7P DONE n= failed=` at the end.
4. **Boot**: `~/w10lab/tools/win10_run.sh PAYLOAD OUTDIR` (never qemu directly; it holds `~/w10lab/oracle.lock`, one guest
   per host; it blocks while other workflows hold it - the lock is not FIFO, so waits of tens of minutes happen when
   many lanes are busy). OUTDIR = `~/w10lab/runs/gdi-<tag>-<stamp>-<pid>/stage2` (and `stage1`). `ORACLE_TIMEOUT`
   defaults to 840 s per boot in vm_run.sh.
5. **Split**: `tools/rec.py split serial.log --outdir data/win10 --tag <tag>` writes each recording whose END
   trailer validates (row count + CRC-32 + `status=ok`) to `data/win10/<probe>/<tag>[.<variant>].rec`; invalid ones are
   reported and not written (`--keep-bad` writes `*.rec.bad`, gitignored). `data/win10/_runs/<tag>.<probes>.log` keeps
   the serial.log SHA-256 and the runner markers. vm_run.sh exits 1 if any requested probe produced no valid recording.

Options: `--args "..."` (all probes), `--timeout S` (per probe, default 300), `--tag T` (default `dpi<N>[-WxH]`),
`--data DIR`, `--no-split`, `--keep-bad`, `--dry-run` (print the payloads), `--shot NAME`, `--screendump`.

## 2. DPI (two-stage boot)

`--dpi N` with N != 96:
- **stage 1** payload (`s1`): `reg add "HKCU\Control Panel\Desktop" /v LogPixels /t REG_DWORD /d N` and
  `Win8DpiScaling=1` (the "custom scaling" pair; Administrator's hive, the auto-logon user), optional
  `setres WxH persist`; boots with `W10_KEEPDISK=1` so the overlay survives.
- **stage 2** payload (`s2`): boots the same overlay (`W10_OVERLAY=<stage1>/disk.qcow2`), so the new value is read at
  logon; runs the probes; vm_run.sh deletes the overlay afterwards.
- Verification is in every recording: `ENV dpi.system`, `dpi.logpixels`, `dpi.reg_logpixels`, `dpi.reg_applieddpi`,
  `sm.*`, `ncm.*` (and the stage-2 serial has `reg query` output). Results of trying it: section 5.

## 3. Display mode

`--res WxH` adds `setres WxH` (ChangeDisplaySettings, no reboot) before the probes; with a DPI change it is also
persisted in stage 1 (`CDS_UPDATEREGISTRY`) so logon happens at that mode. The VM uses `-vga std` (the Microsoft
Basic Display Adapter in the guest); the mode list is what that driver offers (section 5). `I7P SETRES want= rc= now=`
records the outcome (rc 0 = DISP_CHANGE_SUCCESSFUL).

## 4. Screendumps (QMP)

`win10_run.sh` gives qemu `-qmp unix:OUTDIR/qmp.sock`. With `--shot NAME` / `--screendump`, vm_run.sh starts
`tools/screendump.py watch OUTDIR` next to the boot; for every serial line `I7P SHOT name=X` (vmrunner `shot` verb, or a
probe printing that line on stdout and sleeping ~2 s) it saves `OUTDIR/shots/X.png` (QMP `screendump`, PNG format).
One-off: `tools/screendump.py shot OUTDIR/qmp.sock out.png`. Screenshots stay in the run dir (not in git) unless a
probe's data needs one; then commit a cropped/derived artefact plus its hash.

## 5. What was verified on this image (Win10 22H2 19045.2965, 2026-11-09)

(filled in from the real runs below; every item names the run dir)

## 6. Wine (plumbing only)

`tools/wine_probe.sh PROBE[.variant] [args]` builds and runs one probe under headless Wine (`tools/wine_run.sh`, DISPLAY
unset, throw-away prefix) into `build/wine/<probe>.rec` and validates the trailer. It proves the probe compiles, runs,
emits a well-formed recording and does not crash. Its numbers are Wine's (FreeType, Wine/Liberation fonts) and must
never go into `data/` (docs/research/04).
