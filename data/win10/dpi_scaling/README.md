# data/win10/dpi_scaling

DPI scaling recorder (`--full`): system metrics / NONCLIENTMETRICS / stock fonts / dialog fonts / control sizes per thread DPI context (proc, unaware, system, pm, pm2, gdis), a screen-captured window (`cap`), DPI APIs; plus child runs `dpi<N>.api-<mode>.rec` for SetProcessDpiAwareness / SetProcessDpiAwarenessContext modes (spda, shc0-2, pctx1-5).
Files: `dpi96.rec` + 9 `dpi96.api-*.rec`, and the same set at 120/144/168/192 (sweep, 264 s for 5 stages; the sweep's own dpi96 files equal boot A). Boots: quick 1, full 2, sweep 1; repeat (2026-11-15): sweep 2 (not stored, see below).
Determinism: IDENTICAL except `ncm src=fordpi,dpi=72` menu_h (592 / 572 / 576 across 3 boots). FACT: SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, dpi 72) returns TRUE with a mostly-zero struct and a varying iMenuHeight, i.e. OS garbage at DPI < 96. Treat that cell as volatile/undefined.
Per-DPI facts (ctx rows): unaware at 120 sees 120 (no virtualisation), at 144/168/192 sees 96 and a virtualised screen 683x512 / 585x439 / 512x384; system/pm/pm2 see the real DPI.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).

## Repeat of the sweep (determinism of the stages other than 96), 2026-11-15

Same probe (`--full`, children included), same DPI list, `tools/vm_run.sh dpi_scaling --dpi 96,120,144,168,192 --args "--full"`, two more fresh boots (repeat 1, repeat 2); each of the 50 recordings compared to the committed one with
`python3 tools/rec.py diff NEW COMMITTED`. The repeats are not stored (they differ); provenance logs: `data/win10/_runs/repeat1-dpi96-sweep.dpi_scaling.log`, `repeat2-...`. The probe exe has the same size as the committed one (354375 bytes; only the build timestamp differs in the ENV hash).
`repeated: diffs=0` below means no row differs except the documented `ncm src=fordpi,dpi=72` `menu_h` cell (1 row, in the main recording of every stage, every run).

| stage | repeat 1 vs committed | repeat 2 vs committed |
|---|---|---|
| dpi96 (main + 9 children) | repeated: diffs=0 | repeated: diffs=0 |
| dpi120 | `stock obj=DEVICE_DEFAULT_FONT` unavailable (150 cells in the main recording, 25 in each of the 9 children) | repeated: diffs=0 |
| dpi144 | same 150 / 25 cells | the same cells, plus 40 `digx`/`digy` cells (main) and 16 (children `api-shc0`, `api-pctx1`) |
| dpi168 | `ctl font=none cls=header m=layout_cy` 24 instead of 20 (1 row); children: repeated: diffs=0 | the 150 / 25 `DEVICE_DEFAULT_FONT` cells plus 40 / 16 `digx`/`digy` cells |
| dpi192 | repeated: diffs=0 (children too) | 40 `digx`/`digy` cells (main) and 16 (children `api-shc0`, `api-pctx1`); the other children: repeated: diffs=0 |

The cells that move, all in stages at 120 and above:

1. **Stock DEVICE_DEFAULT_FONT**: the 25 cells of `stock obj=DEVICE_DEFAULT_FONT` are NA (`!`) in some stages and have values (`lfh 16, lfw 7, weight 700, face System`, `ext43 290x16`) in others. Observed unavailable: repeat 1 at 120 and 144, repeat 2 at 144 and 168; never in the committed sweep, never at 96. The same intermittent
   state as in `data/win10/ui_metrics/README.md` ("state B"); it hits all six contexts of a recording at once.
2. **`digx`/`digy`** (tmDigitizedAspectX/Y of `tm` and `dlg` rows) is the LOGPIXELS of the measurement DC: the committed stages at 144/168/192 report 120 for some faces and 96 for others (the unaware 120-state leaks into the stage, see the ui_metrics README), repeat 2 reports the stage DPI (192 for `tm face=Segoe UI pt=11` at 192) or the other value for the same faces.
   Repeat 1 reproduces the committed values. Not stable across boots at 144 and above: NOTE-level data.
3. **`ctl header layout_cy`** at 168: 24 (repeat 1) versus 20 (committed and repeat 2); one row, one boot.

Consequence: the 96 stage is deterministic over three boots (committed + 2 repeats, all 10 files); the 120 stage over two of three; 144 and above not. Do not assert rows from the three families above at 120 and above; everything else in those stages was identical in all three boots
(the per-DPI facts listed above: awareness, virtualised screen, system metrics, NCM, control sizes). Not done: a sweep without the 120 stage in front of 144/168/192, and a count of how often each state occurs.
