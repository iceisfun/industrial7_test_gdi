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
   `setres WxH [persist]`, `modes` (lists EnumDisplaySettings modes as `I7P MODE WxHxBPP@Hz`), `shot <name> [ms]`,
   `sleep <ms>`, `echo <text>`; `I7P DONE n= failed=` at the end.
4. **Boot**: `~/w10lab/tools/win10_run.sh PAYLOAD OUTDIR` (never qemu directly; it holds `~/w10lab/oracle.lock`, one guest
   per host; it blocks while other workflows hold it - the lock is not FIFO, so waits of tens of minutes happen when
   many lanes are busy). OUTDIR = `~/w10lab/runs/gdi-<tag>-<stamp>-<pid>/stage2` (and `stage1`). `ORACLE_TIMEOUT`
   defaults to 840 s per boot in vm_run.sh.
5. **Split**: `tools/rec.py split serial.log --outdir data/win10` (file tag = the `tag=` of each REC line, which
   vm_run.sh passes to every probe as `--tag`) writes each recording whose END
   trailer validates (row count + CRC-32 + `status=ok`) to `data/win10/<probe>/<tag>[.<variant>].rec`; invalid ones are
   reported and not written (`--keep-bad` writes `*.rec.bad`, gitignored). `data/win10/_runs/<tag>.<probes>.log` keeps
   the serial.log SHA-256 and the runner markers. vm_run.sh exits 1 if any requested probe produced no valid recording.

Options: `--args "..."` (all probes), `--timeout S` (per probe, default 300), `--tag T` (default `dpi<N>[-WxH]`; with a DPI list a prefix: `T-dpi<N>`),
`--data DIR`, `--no-split`, `--keep-bad`, `--dry-run` (print the payloads), `--shot NAME`, `--screendump`,
`--plan-extra FILE` (vmrunner plan lines appended to the last stage, e.g. `setres` + extra `run` lines with their own
`--tag`). Every plan starts with `modes`.

## 2. DPI

How the guest runs payloads (FACT, read from the base image `C:\i7\boot.cmd`, extracted read-only with
`qemu-img convert -U` + `7z`): *"runs at every logon (HKLM Run key)"*; it scans D..K for `\i7run\run.cmd`, `call`s it,
then `shutdown /s /t 0` if `\i7run\shutdown` exists. The SOFTWARE hive has `AutoAdminLogon`, `DefaultUserName`,
`DefaultPassword` and no `ForceAutoLogon`/`AutoLogonCount` value (`strings` over the hive; value names, not values).

**`--dpi 96,120,144,...` (default mode `logoff`, ONE boot, one lock acquisition)**: run.cmd is a state machine keyed by
`C:\i7gdi\stage.txt` on the throw-away overlay. Logon stage k: query the HKCU DPI values to COM1, run the probes of
DPI k (`plan<k>.txt`, tag `dpi<N>`), write the next DPI (`reg add "HKCU\Control Panel\Desktop" /v LogPixels /d N` +
`Win8DpiScaling=1`; for 96: delete LogPixels, `Win8DpiScaling=0`), write k+1 to stage.txt, `shutdown /l` (log off) and
wait. Auto-logon logs Administrator back on, the Run hook calls run.cmd again, which jumps to stage k+1. The last stage
returns normally, so the hook powers the VM off. If the first DPI is not 96 there is an extra set-and-logoff stage first.
Serial markers: `I7VMRUN BEGIN logon=<k>`, `I7VMRUN STAGE dpi=<N>`, `I7DPI set LogPixels=<N>`, `I7VMRUN LOGOFF next=<k>`.
Rationale: the oracle lock is shared by every workflow on the host and is not FIFO (49 waiters were queued at one point
on 2026-11-09), so a sweep costs one wait instead of 2 x N.

**`--dpi N --dpi-mode boot`** (fallback, two boots): stage 1 sets the values (and `setres WxH persist`) and powers off
with `W10_KEEPDISK=1`; stage 2 boots the same overlay (`W10_OVERLAY=<stage1>/disk.qcow2`) and runs the probes; the
overlay is deleted afterwards.

Verification is in every recording: `ENV dpi.system`, `dpi.logpixels`, `dpi.reg_logpixels`, `dpi.reg_applieddpi`,
`sm.*`, `ncm.*`. Results of trying it: section 5.

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

Every item below is FACT from a real run unless marked *inference*. Runs live in `~/w10lab/runs/<id>/stage2/` (serial.log,
shots/); the provenance logs are in `data/win10/_runs/`.

### 5.1 One-boot DPI sweep (logoff mode) - works at 96/120/144/168/192

Run `gdi-dpi96-sweep-20261110-050155-1758077` (`tools/vm_run.sh hello,hello.v6,hello.sys,hello.v6pm2,hello.x86 --dpi
96,120,144,168,192 --shot desk`): 5 logon stages in one boot, 25/25 recordings valid (`data/win10/hello/dpi<N>[.<v>].rec`).
Lock acquired 05:20:36, VM powered off at about 05:22:45 host time, so roughly 2 min for boot plus 5 stages. Guest uptime at the
first probe of each stage (ENV `time.uptime_ms`): 30.6 s, 46.9 s, 62.6 s, 78.5 s, 106.4 s, so a logoff and auto-logon cycle takes about 15-30 s.
After each logoff, auto-logon brought Administrator back and the HKLM Run hook re-ran run.cmd (`I7VMRUN BEGIN logon=1..4`).
`ForceAutoLogon=1` is written before every `shutdown /l`. *Inference*: it may not be required, but this run did not test without it.

HKCU values read back at the start of each stage (`reg query` in serial.log) and in ENV: `LogPixels` = the requested value,
`WindowMetrics\AppliedDPI` = the same value (0x78/0x90/0xa8/0xc0); at 96 LogPixels is absent, Win8DpiScaling=0, AppliedDPI=96.
`dpi.reg_hw_logpixels` (HKLM hardware profile) stays 96 throughout.

What each manifest variant sees (ENV `dpi.system` / `dpi.awareness` / `dpi.logpixels` / `display.res`):

| LogPixels | base, v6, x86 (no dpiAware) | sys (system aware) | v6pm2 (PerMonitorV2) |
|---|---|---|---|
| 96  | 96 / 0 / 96 / 1024x768 | 96 / 1 / 96 / 1024x768 | 96 / 2 / 96 / 1024x768 |
| 120 | **120 / 2 / 120 / 1024x768** | 120 / 1 / 120 / 1024x768 | 120 / 2 / 120 / 1024x768 |
| 144 | 96 / 0 / 96 / 683x512 (virtualized) | 144 / 1 / 144 / 1024x768 | 144 / 2 / 144 / 1024x768 |
| 168 | 96 / 0 / 96 / 585x439 (virtualized) | 168 / 1 / 168 / 1024x768 | 168 / 2 / 168 / 1024x768 |
| 192 | 96 / 0 / 96 / 512x384 (virtualized) | 192 / 1 / 192 / 1024x768 | 192 / 2 / 192 / 1024x768 |

At 120 DPI (125%) the unaware exes are NOT virtualized: they see 120 DPI and report awareness 2. *Inference*: this is the
"XP-style DPI scaling" that Windows uses at or below 120 DPI. Consequence for probes: **to measure at a real DPI above 120,
use the `sys` or `pm2`/`v6pm2` variants.** Unaware variants at 144+ measure the 96-DPI virtualized view, which is also a
useful datum because it is what an unaware app (the default i7 case) sees.
`SM_CYCAPTION` (sys/pm2): 23/29/34/40/45 and `SM_CYMENU` 20/25/30/34/39 at 96/120/144/168/192. `ncm.message` lfHeight
(sys): -12/-15/-18/-21/-24. comctl32: `5.82.10586` without the v6 manifest, `6.16.10586` with it.

hello's pixel-height rows (lfHeight < 0, MM_TEXT, screen DC: tm, cw_ascii hashes, glyph bitmaps) are byte-identical
across all 5 DPIs and all 5 variants (`rec.py diff`, 0 diffs), and between the x64 and x86 builds.

Screendumps (QMP, `shots/desk-dpi<N>.png`, 1024x768) show the desktop and console window at the expected scale.

### 5.2 Guest facts recorded in ENV (same run)

Windows 10 Home (EditionID Core) 22H2, 10.0.19045 UBR 2965; computer `I7-WIN10`, user `Administrator`, session 1,
elevated 1; time zone Pacific Standard Time (bias 480, `tz_state` 1). Guest clock: `time.local` 04:21:04.993 while the
host (America/Los_Angeles, PST) wrote the matching screendump at 05:21:07, so the guest runs 1 h behind the host
(qemu `-rtc base=localtime`; cause not investigated). Do not use guest timestamps for anything but ordering. Locale en-US, LCID 0x0409, ACP 1252,
OEMCP 437, keyboard 00000409; theme active; font smoothing on, type 2 (ClearType), contrast 1200, orientation 1 (RGB);
`%WINDIR%\Fonts`: 339 files, 360823736 bytes; `micross.ttf` present (876256 bytes); FontSubstitutes: `MS Shell Dlg` ->
`Microsoft Sans Serif`, `MS Shell Dlg 2` -> `Tahoma`; DEFAULT_GUI_FONT = MS Shell Dlg, lfHeight -11; display adapter
mode 1024x768x32 at 64 Hz, `HORZSIZE x VERTSIZE` 325x203 mm, 1 monitor.

### 5.3 Display modes and `--dpi-mode boot`

**Display modes** (run `gdi-resA-dpi96-1280x1024-sweep-20261110-052317-1830258`, `--dpi 96,144 --res 1280x1024
--plan-extra` with `setres 1920x1080` / `setres 800x600` + hello.sys, 8/8 recordings valid, `data/win10/hello/resA-*.rec`):
- `-vga std` (Microsoft Basic Display Adapter) offers 11 modes, all 32 bpp at 64 Hz: 640x480, 800x600, 1024x768 (boot
  default), 1280x1024, 1600x1200, 1280x800, 1280x960, 1440x900, 1680x1050, 1920x1200, 1920x1080 (`I7P MODE`, in that order).
- `setres` (ChangeDisplaySettings, no reboot) returned 0 (DISP_CHANGE_SUCCESSFUL) for 1280x1024, 1920x1080 and 800x600, and
  ENV `display.mode`/`display.res`/`sm.CXSCREEN` follow it; `display.size_mm` stays 325x203.
- The DPI does not follow the mode: at LogPixels 144, `hello.sys` reports `dpi.system` 144 at 800x600, 1280x1024 and
  1920x1080 alike; the unaware exe at 144 sees the virtualized 853x683 (= 1280x1024 / 1.5).
- hello's rows at 144 DPI are identical at 1024x768 and 800x600 (`rec.py diff`, 0 diffs).
- Not shown by this run: whether a non-persisted mode survives the logoff (vm_run.sh repeats `setres` in every stage).

**`--dpi-mode boot` is NOT verified.** A run (`--dpi 120 --dpi-mode boot --res 1600x1200`) waited 50 min for the
oracle lock (about 45 other runs were queued) before its first stage and was cancelled; boot mode needs two lock
acquisitions. Use the default logoff mode, which section 5.1 shows works for every DPI. Boot mode stays as a fallback
(for example if a setting turns out to need a full reboot) until someone runs it.

## 6. Wine (plumbing only)

`tools/wine_probe.sh PROBE[.variant] [args]` builds and runs one probe under headless Wine (`tools/wine_run.sh`, DISPLAY
unset, throw-away prefix) into `build/wine/<probe>.rec` and validates the trailer. It proves the probe compiles, runs,
emits a well-formed recording and does not crash. Its numbers are Wine's (FreeType, Wine/Liberation fonts) and must
never go into `data/` (docs/research/04).

## Running on lanec: local oracle from bb depot m2

`~/w10lab` is a symlink to `/storage/lanec/w10lab`, which holds `win10base.qcow2` (`bb depot get m2`, sha256 991856e1...c4e, read-only),
`oracle.lock` and `runs/`. `~/w10lab/tools/win10_run.sh` (copy in the repo: `tools/win10_run_local.sh`) implements the contract above:
flock on `oracle.lock`, qcow2 overlay per boot (deleted unless `W10_KEEPDISK=1`/`W10_OVERLAY`), payload ISO with `i7run/` (+ empty `shutdown`,
CRLF run.cmd), qemu/KVM q35, 2 vCPU, 4 GiB, IDE disk + IDE CD, std VGA, no NIC, `-rtc base=localtime`, `-no-reboot`, serial to
`OUTDIR/serial.log`, QMP at `OUTDIR/qmp.sock`, pid in `OUTDIR/qemu.pid`. Exit 0 ok, 65 no `I7VMRUN END`, 124 timeout.
Measured 2026-11-10: `vm_run.sh hello` 59 s, `--dpi 96,144` sweep 72 s; hello recordings equal the committed ones (diffs=0).
