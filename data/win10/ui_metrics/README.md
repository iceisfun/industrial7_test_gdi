# data/win10/ui_metrics

UI metrics recorder (`--full`): GetSystemMetrics (+ForDpi), SPI, NONCLIENTMETRICS, dialog fonts, control sizes, message boxes, popup menus, theme sizes, per thread DPI-awareness context.
Files: `dpi96.rec.gz` (13036 lines: R=12584 H=247; this is boot A of the determinism pair), `dpi120/144/168/192.rec.gz` (one boot sweep `--dpi 96,120,144,168,192`, 492 s for 5 stages;
the sweep's own dpi96 stage is not stored, it equals dpi96 apart from the volatile idx=93), `cold144.rec.gz` (separate boot at 144, tag inside the file is `rerun`).
Probe time 70 s per stage. Boots: quick 1, full A 1, full B 1, sweep 1, cold144 1.
Determinism: IDENTICAL except sm/sm_dpi idx=93 (`i93`, an undocumented GetSystemMetrics index, 564 vs 588, 14 rows). INFERENCE: it is not a stable setting (maybe a window/session derived value). Treat `i93` as volatile.
All 31 message boxes found (mb_ret seen=1: measured from the HCBT_ACTIVATE hook); no modal hang.
FACT, sweep vs cold boot at 144 (65 diffs): in the sweep, stage 144/168/192 report `dpi desk_dpi=120` (and ftm dax/day=120, sm CYMINSPACING 40) while a cold boot at 144 gives 96/96/39. The desktop DPI keeps the value of the 120 stage across the logoff/logon (INFERENCE: the XP-style scaling state of the 120 stage persists in the session). Use `cold144` as the reference for the unaware desktop-DPI rows at 144, or run 144/168/192 without 120 in front.
FACT: at 144 (both boots) GetObjectW fails for the stock DEVICE_DEFAULT_FONT in this probe (stock.DEVICE_DEFAULT_FONT `!` in lf, no ftm row: 6 rows fewer), while dpi_scaling at 144 reads it fine (h20 System bold). Not understood.
Contexts: the exe has no manifest (base). thread awareness is switched by the probe, so every context appears in each recording; process-level DPI is virtualised (96) at 144+, see docs/VM_RUN.md 5.1.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).
