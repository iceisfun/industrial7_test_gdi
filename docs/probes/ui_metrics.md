# Probe `ui_metrics`: everything a dialog / control layout depends on

Source: `probes/ui_metrics/ui_metrics.c` (also built as `ui_metrics_ss6` through a symlink, linked with PE subsystem
version 6.0 by `probes/ui_metrics_ss6/LIBS`). Conformance tests: `tests/ui_metrics/ui_metrics_test.c`.
Format: REC v1 (docs/PROBE_FORMAT.md). Probe version `v=2` (v1: `ctl` column `font`, no `face` table, popup item rects
taken with the owner hwnd, message-box text cut at 511 chars, no `mb` row for a box that was never measured).

    ui_metrics.exe [--out F | --com1] [--tag T] [--full | --quick] [--no-msgbox] [--no-popup]

`--full` is the default and is what the VM runs (about 12.6k R rows, 240 H, about 1.4 MB raw / 100 KB gzipped). `--quick`
does the process context only, 4 dialog fonts, 3 message boxes, no popup menu (plumbing check).

## Contexts

`ctx=proc` is the process default given by the manifest variant (`base`/`u` unaware, `sys`, `pm2`, `v6*`). `--full` then
switches the thread with SetThreadDpiAwarenessContext to `unaware`, `system`, `pm`, `pm2`, `gdiscaled` and repeats: dpi,
devcaps, mon, stock fonts, sm, dbu, NCM/ICM/icon-title LOGFONTs, SystemParametersInfoForDpi, awr, win, 2-font dialogs,
menu bar; message boxes (4) in `unaware` and `pm2` only. Tables without `ctx` in the key (`sm_dpi`, `awr_dpi`, `face`)
are recorded once, in the process context.

PE subsystem version matters (user32 reads it, e.g. AdjustWindowRectExForDpi and the padded border): ENV
`um.subsystem_version` says which ran. mingw defaults: x64 5.2, **x86 4.0**; `ui_metrics_ss6` = 6.0. So an x86 vs x64
difference can come from the subsystem version, not the bitness.

## Units and signs

All `:px` values are device pixels of the context they were measured in; `:du` = dialog units (the template inputs);
`lf.h:lu` = LOGFONT lfHeight as returned (negative = character height). `ftm` is the font realised on the screen DC
(MM_TEXT): `h` = tmHeight (cell height), `il` = internal leading, `gcd_cx/gcd_cy` = GdiGetCharDimensions (the average
width the dialog manager uses), `ext52_cx` = extent of A-Za-z.

- `awr`/`awr_dpi` `dl dt dr db`: how far AdjustWindowRectEx(ForDpi) grew each side of 0,0-100,100 (positive = outward).
- `win`: real CreateWindowEx frame of a 300x200 window; `cli_x/cli_y` = client origin minus window origin.
- `dlg`: `bu_x/bu_y` = MapDialogRect(0,0,4,8) = the base units the dialog actually uses; `map100_*`, `map_7_7_*` likewise.
- `ctl`: `x/y` = control origin in dialog **client** coordinates, `w/h` window size, `cli_*` client size; `font_kind`
  0 NULL, 1 the dialog font, 2 DEFAULT_GUI_FONT of this context, 3 another font.
- `menu`: `bar_*` and `menu_item` relative to the window rect top-left; `rows` = distinct item tops (wrapping).
- `popup_item`: relative to the popup window top-left; `popup.win_dx/win_dy` = popup origin minus the track point.
- `mb`: `x/y` screen position; `mb_ch` child rects in client coordinates, with class, id and text (button captions are
  localised: see ENV `locale.ui_*`). `mb_ret.seen`: 0 never found (the `mb` row is then `0 ! ...`), 1 measured from the
  WH_CBT hook, 2 from the fallback timer; `mb_ret.ret` 32000 = MB_TIMEDOUT.
- `face`: EnumFontFamiliesExW(DEFAULT_CHARSET) for each dialog face: number of entries, FontType bits, number of
  charsets, first full name and style. An absent face has `n=0` (it is never skipped).

## Safety

Read-only on the system: only SPI_GET* actions (plus the deliberately invalid sizes in `spi_err`, which fail or read),
no display or registry changes. Every window, menu and dialog is destroyed; dialogs are created hidden. Message boxes and
the popup menu appear for about 0.1-0.3 s each and are closed by the probe: posted click / EndMenu, then WM_CLOSE or
WM_CANCELMODE + Esc, then EndDialog; MessageBoxTimeoutW (5 s) and a 4 s give-up for a popup that never appears bound it.

## Golden-generator hooks

- `dlg.bu_x/bu_y` per face/pt: the 6x13 vs 7x15 question. `ctl`/`ctl_cb` (`ih_sel`, `ih_item`): control heights.
- `ftm` of `dlg/...MS Shell Dlg...` and `face` rows: whether MS Shell Dlg resolves to Microsoft Sans Serif (micross.ttf).
- `stock` + `lf src=stock.DEFAULT_GUI_FONT` per context: DEFAULT_GUI_FONT identity.
- `spi_n` of `ui_metrics` vs `ui_metrics_ss6`: padded border for 5.2 vs 6.0 images.
- `mb`: message box sizes (cross-check of m201's 123x130).
