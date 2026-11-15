# data/win10/ui_metrics

UI metrics recorder (`--full`): GetSystemMetrics (+ForDpi), SPI, NONCLIENTMETRICS, dialog fonts, control sizes, message boxes, popup menus, theme sizes, per thread DPI-awareness context.
Files: `dpi96.rec.gz` (13036 lines: R=12584 H=247; this is boot A of the determinism pair), `dpi120/144/168/192.rec.gz` (one boot sweep `--dpi 96,120,144,168,192`, 492 s for 5 stages;
the sweep's own dpi96 stage is not stored, it equals dpi96 apart from the volatile idx=93), `cold144.rec.gz` (separate boot at 144, tag inside the file is `rerun`).
Probe time 70 s per stage. Boots: quick 1, full A 1, full B 1, sweep 1, cold144 1; repeat (2026-11-15): sweep 2, cold144 2, std-user 2 (not stored except `stduser.rec.gz`).
`stduser.rec.gz`: the same probe (`--full`, 96 dpi) run by a non-elevated restricted copy of the session token (tag `stduser`, ENV `host.elevated` 0); see "Standard user" below and docs/STD_USER_RUNS.md.
Determinism: IDENTICAL except sm/sm_dpi idx=93 (`i93`, an undocumented GetSystemMetrics index, 564 vs 588, 14 rows). INFERENCE: it is not a stable setting (maybe a window/session derived value). Treat `i93` as volatile.
All 31 message boxes found (mb_ret seen=1: measured from the HCBT_ACTIVATE hook); no modal hang.
FACT, sweep vs cold boot at 144 (65 diffs): in the sweep, stage 144/168/192 report `dpi desk_dpi=120` (and ftm dax/day=120, sm CYMINSPACING 40) while a cold boot at 144 gives 96/96/39. The desktop DPI keeps the value of the 120 stage across the logoff/logon (INFERENCE: the XP-style scaling state of the 120 stage persists in the session). Use `cold144` as the reference for the unaware desktop-DPI rows at 144, or run 144/168/192 without 120 in front.
FACT (corrected by the repeat below): at 144 GetObjectW on the stock DEVICE_DEFAULT_FONT fails in this probe in some boots and works in others (lf rows `!`, no ftm/ftm_cw rows: 12 rows fewer, 13024/13025 instead of 13036/13037 lines; 6 `lf` rows are `!` in both cases of the `!`-state). It is an intermittent state, not a function of the DPI; see "Repeat of the sweep". dpi_scaling reads the same object fine at 144 in some boots (h20 System bold). Not understood.
Contexts: the exe has no manifest (base). thread awareness is switched by the probe, so every context appears in each recording; process-level DPI is virtualised (96) at 144+, see docs/VM_RUN.md 5.1.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).

## Repeat of the sweep (determinism of the stages other than 96), 2026-11-15

Same probe (`--full`), same DPI list, same tool (`tools/vm_run.sh ui_metrics --dpi 96,120,144,168,192 --args "--full"`), two more fresh boots (repeat 1, repeat 2),
plus two more cold boots at 144 (`--dpi 144 --tag rerun`). The repeats were compared to the committed recordings with `python3 tools/rec.py diff NEW COMMITTED`; they are not stored
(they differ, see below), only the provenance logs are (`data/win10/_runs/repeat1-*`, `repeat2-*`). The probe exe has the same size as the committed one (374170 bytes; the ENV hash differs only through the build timestamp).
`idx=93` (`i93`, 14 rows) differs in every comparison, as documented, and is left out of the counts below; `repeated: diffs=0` means no other row differs.

| stage | repeat 1 vs committed | repeat 2 vs committed |
|---|---|---|
| dpi96 | repeated: diffs=0 | repeated: diffs=0 |
| dpi120 | repeated: diffs=0 | 14 rows: `ftm src=dc_default` (13 rows) + `ftm_cw` hash: the default-font cell height is 16 (as at 96) instead of 20 |
| dpi144 | 103 rows: state A where committed is B (below) | 76 rows: `dax`/`day` values (below) |
| dpi168 | 103 rows: state B where committed is A | 179 rows: state B + `dax`/`day` |
| dpi192 | repeated: diffs=0 | 179 rows: state B + `dax`/`day` |
| cold144 | 103 rows: state A where committed is B | repeated: diffs=0 |

Only the 96 stage reproduces in all three boots (and the 120 stage in two of three). The rows that move are three families, all inside the stages at 144 and above (and one at 120):

1. **State A / B of the unaware desktop DC** (rows that appear together): in state B `GetObjectW` fails on the stock DEVICE_DEFAULT_FONT (the 6 `lf` rows of `src=stock.DEVICE_DEFAULT_FONT` are `!`, and the 6 `ftm` plus 6 `ftm_cw` rows of that source are absent), and
   `GetSystemMetricsForDpi(.., 72)` returns `CYCAPTION 28, CXMIN 136, CYMIN 42, CXSIZE 44, CYSIZE 27, CXMINTRACK 136, CYMINTRACK 42` (7 `sm_dpi dpi=72` rows); in state A the object is readable (`h 20, w 9, weight 700, face System`) and the same 7 cells are
   `31, 143, 45, 49, 30, 143, 45` (sweep stages) or `24, 128, 38, 38, 23, 128, 38` (the 96 and 120 stages and cold144 repeat 1). Like the 72 dpi cells of dpi_scaling, `GetSystemMetricsForDpi` below 96 is not stable and these cells are NOTE-level. Observed state per run (B = DEVICE_DEFAULT_FONT unreadable):

   | run | 144 | 168 | 192 | cold 144 |
   |---|---|---|---|---|
   | committed | B | A | A | B |
   | repeat 1 | A | B | A | A |
   | repeat 2 | B | B | B | B |

   Stages 96 and 120 were state A in all three sweeps.
2. **`dax`/`day`** (the LOGPIXELS of the measurement DC) of 38 `ftm` rows (the five `src=ncm120.*` rows and the `src=dlg/*` rows of a few fonts) take the value 120 or 96 in one run and the stage DPI (144/168/192) or the other of 96/120 in another
   (for example `ncm120.caption` dax 120 in committed and repeat 1, 144 in repeat 2 at 144). The rows measured with the same font in the same recording are not consistent with each other, so these rows describe a transient DC state and must not be asserted.
3. **dc_default at 120** (repeat 2): see the table.

FACT, reproduced in all three sweeps: at stages 144, 168 and 192 the ENV/`dpi` table reports the desktop DPI of the 120 stage (`desk_dpi=120`); a cold boot at 144 reports `desk_dpi=96` (committed, repeat 1, repeat 2). The sweep-vs-cold difference of the committed README is stable.
`cold144` is therefore a valid reference for `desk_dpi`/unaware-DPI rows but, like the sweep stages, not for the families above (the committed cold144 and repeat 2 are state B, repeat 1 is state A).

Consequences: assert only what is identical in the stages that were identical (the 96 stage, plus the cells that never appear in the families above); state A/B and `dax`/`day` rows are NOTE-level data. Not done: a third sweep to count how often each state occurs, and a sweep without the 120 stage in front.

## Standard user (`stduser.rec.gz`)

`tools/vm_run.sh ui_metrics --dpi 96 --args "--full" --plan-extra F` with the plan line `runstd 300 ui_metrics ui_metrics.exe --tag stduser --full` (vmrunner verb `runstd`, docs/STD_USER_RUNS.md): the probe runs with a restricted
copy of the session token (LUA_TOKEN + DISABLE_MAX_PRIVILEGE, Medium integrity, Administrators deny-only; `host.elevated` 0). Same boot as the elevated `dpi96` recording of that run. Two fresh boots: A is stored, B compared.
Determinism over the two boots (`rec.py diff B A`): IDENTICAL except `idx=93` (`i93`, 14 rows). Against the elevated run of the same boot (13 rows): only `src=ncm96.caption` in `ctx=proc` (the first `SystemParametersInfoForDpi` call of the process): `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, 96)` returns `lfCaptionFont.lfHeight` 0
(elevated: -12; in the other five contexts the elevated run also gets 0) and the `ftm`/`ftm_cw` rows follow (restricted: cell height 21 from the lfHeight-0 default font; elevated: 15). All message boxes (27 `mb` / 27 `mb_ret`, same results) and the popup menu rows are identical to the elevated run. Provenance: `data/win10/_runs/stduser-A.ui_metrics.log`, `stduser-B.ui_metrics.log`.
