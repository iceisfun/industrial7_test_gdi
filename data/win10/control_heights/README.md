# data/win10/control_heights

Common-control heights recorder (`--full`): buttons, edit, combo, listview, header, treeview, toolbar, statusbar, rebar, tab, tooltips, menus, message box, themes parts.
Files: `dpi96.rec` (5169 lines: R=4779), `dpi120/144/168/192.rec` (sweep, 317 s for 5 stages), `dpi96.v6.rec` (comctl32 6.16 manifest variant, 9625 lines, R=9092; boot 157 s).
The base files are the unaware (no dpiAware) exes: at 144+ they see the virtualised 96 DPI view (see docs/VM_RUN.md 5.1), so dpi144/168/192 differ from 96 only in a few sys rows. The awareness variants below give real scaled controls.
Awareness variants (`--full`, DPI 96,120,144,168,192, one boot per probe pair): `dpi<N>.sys.rec`, `dpi<N>.pm2.rec` (comctl32 5.82; system-aware / PerMonitorV2 manifests, 5168-5170 lines, R=4779-4780; the extra row at 192 is `theme_int cls=Button;part=4;state=1;prop=MINDPI1`) and `dpi<N>.v6sys.rec.gz`, `dpi<N>.v6pm2.rec.gz` (comctl32 6.16; 9624-9626 lines, R=9092-9093; gzip like `dpi96.v6.rec.gz`).
Boots: quick 3 (one crash, one diagnostic), full A 1, full B 1, sweep 1, v6 1; sys+pm2 sweep 2 (stored: boot 1), v6sys+v6pm2 sweep 2 (stored: boot 1; second boots are not stored, logs `_runs/*.boot2.log`).
Determinism: IDENTICAL (dpi96 vs boot B vs the sweep's 96 stage: diff 0). Awareness variants: `rec.py diff` of boot 1 vs boot 2 is diffs=0 for 14 of the 20 recordings; 6 differ (sys and pm2 at 168 and 192 DPI, v6sys and v6pm2 at 120 DPI) by exactly 35 values each (`rec.py diff` diffs=35, in the `font` and `dt` rows) plus one `font_cw_ascii` row present on one side only, all `font=devdefault` (exception below). With `font=devdefault` rows dropped from both sides, all 20 are diffs=0. A third fresh boot running all four exes in each stage (not stored) gave the same: filtered diffs=0 against all 20 stored files, `devdefault` absent at 120 and 144 for all four exes alike.
Awareness at 96 DPI is a no-op for these controls: `dpi96.sys/pm2` equal `dpi96.rec` and `dpi96.v6sys/v6pm2` equal `dpi96.v6.rec.gz` (diffs=0).
Many `!` values in `btn`, `tb`, `lv`, `tv`, `tt` etc. in the base exe: comctl32 5.82 does not implement BCM_GETIDEALSIZE/BCM_GETTEXTMARGIN/TTM_GETBUBBLESIZE (v6 messages); the v6 file has them.
Probe bug fixed (636e974): TTM_GETBUBBLESIZE crashes comctl32 5.82 with an access violation; only sent when v6 active now.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).

## Exception: the `devdefault` font is intermittently unavailable

The `devdefault` font (`GetStockObject(DEVICE_DEFAULT_FONT)`) is sometimes not obtainable: its `font` row reads `3 0 ! ! ...` (req_h 0), its `dt` row is all 0 and its `font_cw_ascii` row is missing; other times it is the bold `System` font, 16 px at 96 and 120 DPI and 20 px at 144 and above (its size, when present, also comes from the logon stage: the unaware `dpi144.rec` has 20 px although that exe sees 96 DPI). Observation (FACT): the state belongs to one logon stage, not to the exe: `sys` and `pm2` (and `v6sys`/`v6pm2`) of the same stage always agree, and neither DPI nor manifest predicts it. Absent in: base sweep 120; sys/pm2 boot 1 at 144 and 168, boot 2 at 144 and 192; v6sys/v6pm2 boot 1 at 144, boot 2 at 120 and 144. DPI 96 always had it. Stored files are as observed. Every other row of those stages is identical across boots. To compare two boots drop this font on both sides first: `grep -v font=devdefault A.rec > a; grep -v font=devdefault B.rec > b; python3 tools/rec.py diff a b` (the trailer no longer matches, which diff tolerates). Treat any `devdefault` row as NOTE-grade (not stable across runs) in tests.

## What the awareness changes (data from the stored recordings, boot 1)

Selected rows. `font=default` means no WM_SETFONT: the control keeps its own default font, which the `ctlfont` rows show is the raster bold `System` font (16 px at 96, 20 px at 120 and above) for button, edit, combobox and tab, and Segoe UI for the statusbar. `font=guifont` is DEFAULT_GUI_FONT, `font=ncm-message` the NONCLIENTMETRICS message font. The value is one named column of the row: edit, combobox, statusbar `win_h`; button `dt_cx` (DrawText DT_CALCRECT width of the caption "OK" in the button's font, not a button size: comctl32 5.82 has no BCM_GETIDEALSIZE); tab `it0_b`. Each cell lists DPI 96 / 120 / 144 / 168 / 192:

| row | unaware (`dpi<N>.rec`) | sys = pm2 | v6sys = v6pm2 |
|---|---|---|---|
| LOGPIXELSY | 96 / 120 / 96 / 96 / 96 | 96 / 120 / 144 / 168 / 192 | same as sys |
| SM_CYCAPTION | 23 / 29 / 23 / 23 / 23 | 23 / 29 / 34 / 40 / 45 | same |
| SM_CYMENU | 20 / 25 / 20 / 20 / 20 | 20 / 25 / 30 / 34 / 39 | same |
| SM_CXVSCROLL | 17 / 21 / 17 / 17 / 17 | 17 / 21 / 26 / 30 / 34 | same |
| GetDialogBaseUnits.y | 16 / 20 / 16 / 16 / 16 | 16 / 20 / 20 / 20 / 20 | same |
| button caption "OK" width, font=default | 19 / 24 / 19 / 19 / 19 | 19 / 24 / 24 / 24 / 24 | same |
| button caption "OK" width, font=ncm-message | 16 / 20 / 16 / 16 / 16 | 16 / 20 / 24 / 28 / 32 | same |
| combobox dropdown, font=default | 24 / 28 / 24 / 24 / 24 | 24 / 28 / 28 / 28 / 28 | same |
| combobox dropdown, font=guifont | 21 / 24 / 21 / 21 / 21 | 21 / 24 / 28 / 32 / 33 | same |
| combobox dropdown, font=ncm-message | 23 / 28 / 23 / 23 / 23 | 23 / 28 / 33 / 38 / 40 | same |
| statusbar simple (both fonts) | 22 / 29 / 22 / 22 / 22 | 22 / 29 / 36 / 43 / 44 | 23 / 29 / 36 / 43 / 44 |
| tab single row, font=default | 23 / 27 / 23 / 23 / 23 | 23 / 27 / 27 / 27 / 27 | 23 / 27 / 27 / 27 / 34 |
| single-line edit `win_h`, both fonts | 23 at every DPI | 23 at every DPI | 23 at every DPI |

(The edit is created 23 px tall and EDIT does not resize itself, so its `win_h` is the requested height, not a measured size.)

Observed (FACT, from these rows and whole-file `rec.py diff`):
- System-aware and PerMonitorV2 are indistinguishable in this family: `sys` vs `pm2` and `v6sys` vs `v6pm2` are diffs=0 at every DPI (one monitor, no DPI change after start), although the manifests took effect (ENV `dpi.awareness` 1 vs 2).
- Unaware vs sys: equal at 96 and at 120 (the unaware exe is not virtualised at 120; the only diffs there are the intermittent `devdefault` rows); at 144, 168 and 192 about 14.8-15.1 thousand values differ (`rec.py diff` diffs).
- Controls left on their default font (`font=default`, the raster `System` font) stay at their 120-DPI size above 120 (caption width 24, combobox 28), because that font is 20 px at 120 and above (`ctlfont` and `font font=system` rows). System metrics (caption, menu, scrollbar) and controls given DEFAULT_GUI_FONT or the message font scale with the DPI. These come from a logoff DPI sweep (docs/VM_RUN.md 5.1); a fresh boot at each DPI was not recorded.
- comctl32 6 vs 5.82 under the same awareness: about 32.7-35.4 thousand values differ and the v6 files hold about 4456 more rows; at 192 DPI the v6 tab strip is 34 instead of 27, and the v6 statusbar is 1 px taller at 96 DPI.
