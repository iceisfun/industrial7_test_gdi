# data/win10/font_metrics

Font metrics recorder (`--full`): 21 faces x 71 heights, TEXTMETRIC / OUTLINETEXTMETRIC rows, stock/UI fonts.
File: `dpi96.rec.gz` (35189 lines: R=33455 H=1547). Probe run time 191 s (one boot ~4.2 min). Boots: quick 1, full 2 (A + determinism B).
Determinism: IDENTICAL (diff 0 rows, ENV differs only in time.local/utc/uptime_ms).
Sanity: tmHeight = tmAscent + tmDescent in every tm/ui_tm/stock_tm row; Segoe UI -12 regular = h15 a12 d3 il3, file segoeui.ttf.
Known limits: DPI 96 only (pixel-height rows do not depend on DPI for the unaware exe).

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).
