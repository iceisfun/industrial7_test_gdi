# data/win10/dpi_scaling

DPI scaling recorder (`--full`): system metrics / NONCLIENTMETRICS / stock fonts / dialog fonts / control sizes per thread DPI context (proc, unaware, system, pm, pm2, gdis), a screen-captured window (`cap`), DPI APIs; plus child runs `dpi<N>.api-<mode>.rec` for SetProcessDpiAwareness / SetProcessDpiAwarenessContext modes (spda, shc0-2, pctx1-5).
Files: `dpi96.rec` + 9 `dpi96.api-*.rec`, and the same set at 120/144/168/192 (sweep, 264 s for 5 stages; the sweep's own dpi96 files equal boot A). Boots: quick 1, full 2, sweep 1.
Determinism: IDENTICAL except `ncm src=fordpi,dpi=72` menu_h (592 / 572 / 576 across 3 boots). FACT: SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, dpi 72) returns TRUE with a mostly-zero struct and a varying iMenuHeight, i.e. OS garbage at DPI < 96. Treat that cell as volatile/undefined.
Per-DPI facts (ctx rows): unaware at 120 sees 120 (no virtualisation), at 144/168/192 sees 96 and a virtualised screen 683x512 / 585x439 / 512x384; system/pm/pm2 see the real DPI.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).
