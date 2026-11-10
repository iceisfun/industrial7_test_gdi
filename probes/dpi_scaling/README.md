# dpi_scaling probe family

Records the DPI dimension of the D12 data. It takes the same core measurements at system DPI 96/120/144/168/192
(100-200 %) and for every DPI-awareness setup a Win32 app can be in. Format: docs/PROBE_FORMAT.md (REC v1). Running:
docs/VM_RUN.md. Conformance tests (DPI-invariant, decomp-verified): `tests/dpi_scaling/`.

## Running it (one boot for the whole sweep)

    tools/vm_run.sh dpi_scaling,dpi_scaling.v6,dpi_scaling.sys,dpi_scaling.pm2,dpi_scaling.v6sys,dpi_scaling.v6pm2 \
        --dpi 96,120,144,168,192 --args "--full" --timeout 400

This is **one boot and one oracle-lock acquisition** (VM_RUN.md section 2, logoff mode). Stage k runs the six exes at
DPI k, then run.cmd writes `HKCU\Control Panel\Desktop\LogPixels` (with `Win8DpiScaling=1`; for 96, LogPixels is
deleted and `Win8DpiScaling=0`), logs off, and auto-logon starts stage k+1. The HKCU values are read back at the start of
each stage (serial `reg query`) and appear in every recording's ENV (`dpi.reg_logpixels`, `dpi.reg_applieddpi`,
`dpi.system`). VM_RUN.md 5.1 verified this mechanism at all five DPIs. If the logoff sweep ever breaks, the fallback
is `--dpi N --dpi-mode boot`: two boots per DPI, so 10 boots for the sweep. That is within the limit of 6 boots per
DPI configuration, but boot mode has not been verified yet.

Output: `data/win10/dpi_scaling/dpi<N>[.<variant>].rec`. The no-manifest exe (`dpi_scaling.exe`, variant `base`) also
re-runs itself once for each API mode after its own recording. Each of those children writes a separate recording,
`dpi<N>.api-<mode>.rec`. So each DPI stage gives 6 + 9 = 15 recordings, and the sweep gives 75.

| variant | process awareness | comctl32 |
|---|---|---|
| base | unaware (no manifest) | 5.82 |
| v6 | unaware | 6 |
| sys / v6sys | system aware (`dpiAware true`) | 5.82 / 6 |
| pm2 / v6pm2 | PerMonitorV2 | 5.82 / 6 |
| api-spda | `SetProcessDPIAware()` | 5.82 |
| api-shc0/1/2 | `SetProcessDpiAwareness(0/1/2)` (shcore) | 5.82 |
| api-pctx1..5 | `SetProcessDpiAwarenessContext(-1..-5)` (unaware, system, pm, pm2, unaware-gdiscaled) | 5.82 |

Each API child records whether its call succeeded (`api` table: ret, last error). The effective awareness is in
ENV and in the `ctx` table.

## What one recording holds

Within one process, measurements are taken in every thread DPI context (`SetThreadDpiAwarenessContext`;
`ctx=proc|unaware|system|pm|pm2|gdis`). `--quick` and the API children use `ctx=proc` only.

| table | key | content |
|---|---|---|
| `api` | mode | the API variant's call: avail, ret (32-bit hex: BOOL for SetProcessDPIAware/SetProcessDpiAwarenessContext, HRESULT for shcore SetProcessDpiAwareness), GetLastError |
| `face` | face | presence of the 12 faces (once per recording): `present` = EnumFontFamiliesExW(DEFAULT_CHARSET) returned at least one entry; `n_enum`, `n_tt` (TrueType entries), `type` (FontType of the first entry, hex), `full` (its elfFullName). Absent faces still get `tm` rows: `got` shows what GDI substituted |
| `ctx` | ctx | thread ctx (packed value, awareness, DPI), GetDpiForSystem, GetSystemDpiForProcess, IsProcessDPIAware, GetProcessDpiAwareness, screen-DC LOGPIXELS/HORZRES/DESKTOPHORZRES, GetDialogBaseUnits, GetDpiForMonitor (effective/angular/raw), monitor/work rects, desktop window DPI |
| `sm` | ctx;name | 58 GetSystemMetrics values |
| `smdpi` | name;dpi | GetSystemMetricsForDpi at 72..384 (manifest exes only) |
| `ncm` / `ncmf` | ctx;src;dpi[;font] | NONCLIENTMETRICS numbers / LOGFONTs (caption, smcaption, menu, status, message, icon, icontitle); `src=spi` in each ctx, `src=fordpi` (SystemParametersInfoForDpi) at 72..384 |
| `stock` | ctx;obj | 7 stock fonts: GetObject LOGFONT, realised face, TEXTMETRIC, extents |
| `ptconv` | ctx | LOGPIXELSY; `muldiv8/9` = MulDiv(8/9, LOGPIXELSY, 72) (positive: em height in px); `dlgrule8/9` = -((pt*LOGPIXELSY+36)/72), the lfHeight user32!CreateDlgFont @0x3ca4 computes for a DS_SETFONT dialog (negative, like an lfHeight); and the lfHeight that DEFAULT_GUI_FONT, NCM message/caption and the icon title font actually hold (negative = em height) |
| `muldiv` | pt;dpi | kernel32 MulDiv grid (5..16 pt x 96..192) |
| `tm` / `cw` / H `cw_ascii`, `abc_ascii` | ctx;face;pt | lfHeight = -MulDiv(pt, LOGPIXELSY, 72): realised face, TEXTMETRIC, extent of a 43-char sentence and of the 52-letter alphabet; `cw` = the 95 GetCharWidth32 widths (ctx=proc); hashes of widths/ABC. 12 faces x 7..14 pt in ctx=proc, 4 faces x 8/9 pt elsewhere |
| `dlg` | ctx;font;pt;style | 100x50 DLU dialog: `style=setfont` (DLGTEMPLATE + DS_SETFONT), `setfont_ex` (DLGTEMPLATEEX + DS_SETFONT, weight 400, charset 1), `shellfont` (DLGTEMPLATEEX + DS_SHELLFONT). user32!CreateDlgFont maps MS Shell Dlg to MS Shell Dlg 2 only for `shellfont`, and a plain DLGTEMPLATE gets weight 700 from FixupDlgLogFont unless the style has DS_3DLOOK (decomp, not yet observed). `pt=32767` (0x7fff) = the message-box font (GetMessageBoxFontForDpi). `font=nofont` = no DS_SETFONT. Values: WM_GETFONT LOGFONT + TEXTMETRIC, MapDialogRect(4,8) and (100,50), client/window size, GetDpiForWindow |
| `ctl` | ctx;font;cls;m | (`m=create` with `!` when a control or the parent could not be created) control heights with font none/dgf/ncm: listview row/item/label/header/spacing, header HDM_LAYOUT, treeview item height/indent, button/checkbox BCM_GETIDEALSIZE, edit EM_GETRECT/margins, combo closed height/item heights, listbox item, status bar, tab item, and the TEXTMETRIC height of each control's font |
| `awr` | ctx;style;dpi | AdjustWindowRectEx (dpi=ctx) and AdjustWindowRectExForDpi (dpi=N, manifest exes) for 7 styles |
| `win` | ctx | a 300x200 window at (100,100): logical rect, then the same rect read from a PMv2 thread (physical), Logical/PhysicalToLogicalPointForPerMonitorDPI |
| `cap` / B `cap_img` | ctx | (an all-`!` row when the window cannot be created) `--full`: a 48x18 topmost popup drawing "Ag Hx" in DEFAULT_GUI_FONT, captured from the screen in physical pixels (4 bpp grey). Shows DWM bitmap stretching (unaware) against GDI scaling (gdis) against native rendering (pm2) |

Units: `:px` columns are in the coordinate space of the measuring context. For a virtualized (unaware) context that is
logical pixels. The `win` and `cap` tables also give the physical values.

Probe version history: v=1 (21b8dee) built the `shellfont` dialogs on a plain DLGTEMPLATE, where DS_SHELLFONT has
no effect, and `ptconv` had a positive `dgf_rule` column. v=2: EX templates, the `setfont_ex` and `pt=32767` rows,
`dlgrule8/9`, the `face` table, the 32-bit `api.ret`, and `!` rows for absent faces, controls and windows. API children
are killed after 30 s each (the vmrunner job object also kills the whole tree at the per-probe timeout).

## Size and time (estimates; replace with measured values once the sweep has run)

- Rows: manifest exe `--full` about 2.1k lines under Wine (157 KB; about 1.6k R rows, 250 H lines) before the window tables Wine
  cannot produce (ctl, dlg values, win, cap: about 0.8k more rows, about 60 KB); API child about 325 lines (20 KB). gzip -9: about 10x.
  Sweep: 5 x (6 x 1.9k + 9 x 0.3k) is about 70k lines, about 8 MB of text uncompressed. Git deltas compress it well,
  because DPI stages differ only in the DPI-dependent rows.
- Runtime under Wine (no windows; plumbing only): under 3 s for exe plus children. On Windows the window/control/dialog
  creation and the 4 captures (about 0.4 s each) add a few seconds per exe. See the validation run notes in the commit log.
