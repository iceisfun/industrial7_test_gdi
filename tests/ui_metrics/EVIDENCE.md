# ui_metrics: evidence

Area: `GetSystemMetricsForDpi`, `SystemParametersInfoW/A` and `SystemParametersInfoForDpi` (non-client metrics, icon metrics,
icon-title font), `AdjustWindowRectEx` / `AdjustWindowRectExForDpi`, `MapDialogRect`, `GetDialogBaseUnits`, `GetStockObject` (fonts),
`GetObjectW/A` on fonts, `GetTextMetricsW`, `CreateDialogIndirectParamW` (in-memory templates).
File: `ui_metrics_test.c`, built to `build/ui_metrics.exe`. This area asserts relations between metrics, not recorded numbers; the
recorded numbers are in `data/win10/ui_metrics/`.

Oracle: Windows 10 Home 22H2, 10.0.19045.2965, en-US (ACP 1252), 1024x768 32 bpp, QEMU guest, elevated Administrator, 96 DPI.
Setup: the process has no manifest (so it is DPI-unaware), the screen DC, `CreateDialogIndirectParamW` with a generated template
(no controls, `DS_SETFONT` when a face is given, caption + system menu + modal frame, 200x100 dialog units).

Sources: **D** = documentation, **O** = observed on Win10 22H2 19045.2965 (asserted only because stable), **C** = reading of the
decompiled code of `user32.dll` / `win32kfull.sys` / `gdi32full.dll` 19041.2965 in the reference corpus (the `DECOMP(...)` lines in the
source), **N** = NOTE only.

Stability: two boots of the final build, each: 18 tests, 5677 checks, 0 failed, 0 skipped, 2 NOTE; the `ui_metrics.log` of both boots
is identical line for line.

## Expectations

| # | Test | Expectation | Source | Stable |
|---|------|-------------|--------|--------|
| 1 | tm_descent_is_height_minus_ascent | `tmDescent == tmHeight - tmAscent` for 12 face names (including an unknown face) x 12 heights, the 7 stock fonts, and the 5 `NONCLIENTMETRICS` fonts; more than 100 metric sets were checked | D (TEXTMETRIC: height = ascent + descent); C (`vIFIMetricsToTextMetricWStrict`) | yes |
| 2 | tm_extent_vs_char_widths_observed | N: sum of per-character widths vs `GetTextExtentPoint32W` of a 25-char string for 5 faces x 3 heights is printed only on a mismatch (none printed) | O | yes |
| 3 | getobject_font_buffer_protocol | for 3 stock fonts (DEFAULT_GUI, SYSTEM, ANSI_VAR) and a Segoe UI -12 font: `GetObjectW(f, 0, NULL)` is `sizeof(LOGFONTW)` (92); `GetObjectA(f, 0, NULL)` is `sizeof(LOGFONTA)` (60); `GetObjectA(f, 92, big)` returns 60 and writes only 60 bytes; A and W agree on height, width, weight, italic, charset, pitch-and-family, quality | D (GetObject: NULL buffer returns the size needed); the A-with-larger-count behaviour is C and O | yes |
| 4 | same | N: a differing A vs W face name is printed only on mismatch (none printed) | O | yes |
| 5 | stock_font_identity | the 7 stock font ids give a non-NULL handle that is identical on two calls; `GetStockObject(0x16)`, `(-1)` and `(1000)` return NULL | D (GetStockObject: NULL on invalid index); C. The handle identity holds within one thread DPI context only (C) | yes |
| 6 | same | N: `GetObjectType` of a stock font other than `OBJ_FONT` is printed (none printed) | O | yes |
| 7 | mapdialogrect_rounding | for dialogs with MS Shell Dlg 8, Segoe UI 9, Tahoma 8, Courier New 10 and no font: `MapDialogRect` of a (0,0,4,8) rectangle yields the base units (x > 0, y > 0); for 20 values v (0..1000 and negatives) a rectangle of four v's maps to x = `(baseX * v + 2) / 4`, y = `(baseY * v + 4) / 8` with C integer division | D (MapDialogRect: dialog units -> pixels, "base units / 4" and "/ 8"); the rounding term (+2, +4) and truncation toward zero for negatives are C and O | yes |
| 8 | mapdialogrect_not_a_dialog | `MapDialogRect` for a STATIC popup window or NULL returns FALSE and leaves the rectangle untouched | D (returns zero on failure); C | yes |
| 9 | dialog_base_units_stable | `GetDialogBaseUnits()` returns the same value on two calls | D | yes |
| 10 | same | N: the value on this guest is 8 x 16 (system font, 96 DPI) | O | yes |
| 11 | adjustwindowrectexfordpi_decomposition | for 14 window styles x 9 extended styles x menu / no menu x DPI 96, 120, 144, 192: the left, right and bottom inflation are equal (`n`); the top inflation is `n` + `SM_CYMENU` (if a menu) + `SM_CYCAPTION` (or `SM_CYSMCAPTION` with `WS_EX_TOOLWINDOW`) when the style has the full `WS_CAPTION` bits, both from `GetSystemMetricsForDpi` at the same DPI; adding `WS_EX_CLIENTEDGE` grows every side by 2 | D (AdjustWindowRectEx computes the window rectangle for a client rectangle); the decomposition and the +2 are C and O. The menu and caption heights are taken from `GetSystemMetricsForDpi`, whose own values are recorded in data/win10/ui_metrics, not asserted here | yes |
| 12 | adjustwindowrectexfordpi_borderless_unchanged | `WS_CHILD` / `WS_POPUP` styles (with clip, visible and scroll-bar bits) with extended style 0 and no menu leave the rectangle unchanged at DPI 96, 144, 192 | D (no non-client area to add); C | yes |
| 13 | adjustwindowrectexfordpi_dpi0 | `AdjustWindowRectExForDpi(.., dpi 0)` fails with `ERROR_INVALID_PARAMETER` and leaves the rectangle untouched | C (argument check); O. Not in the documentation | yes |
| 14 | adjustwindowrectex_uses_system_dpi | `AdjustWindowRectEx` follows the same decomposition as row 11 with the DPI of `GetDpiForSystem()` (14 x 9 x 2 combinations) | D (AdjustWindowRectEx is the system-DPI form of the ForDpi function); C | yes (96 DPI here; not run at another system DPI) |
| 15 | getsystemmetricsfordpi_bad_params | `GetSystemMetricsForDpi` with dpi 0, index -1, or dpi `(UINT)-96` returns 0 with `ERROR_INVALID_PARAMETER` | C; O (not in the documentation) | yes |
| 16 | getsystemmetricsfordpi_dpi_independent_indices | for `SM_CXSCREEN`, `SM_CYSCREEN`, `SM_MOUSEPRESENT`, `SM_SWAPBUTTON`, `SM_CMOUSEBUTTONS`, `SM_CMONITORS`, `SM_DBCSENABLED`, `SM_IMMENABLED`, `SM_MIDEASTENABLED`, `SM_SAMEDISPLAYFORMAT`: `GetSystemMetricsForDpi(i, dpi) == GetSystemMetrics(i)` for dpi 72, 96, 120, 144, 192, 480 | D (the ForDpi variant scales DPI-dependent metrics; the others are unaffected); the index set is C and O | yes |
| 17 | getsystemmetrics_scaled_observed | N: monotonic growth of 8 scaled indices with DPI (96 <= 144 <= 192) is printed only when violated (never) | O | yes |
| 18 | spifordpi_protocol | `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof, &n, 0, 96)` succeeds, keeps `cbSize`, returns a message-font face name; with `pvParam == NULL`, `uiParam == 500`, `cbSize == 500` (all with the 504-byte struct) or `SPI_GETWORKAREA` it fails with `ERROR_INVALID_PARAMETER`; `SPI_GETICONTITLELOGFONT` succeeds with a face name and non-zero height, and fails for `sizeof(LOGFONTA)`; `SPI_GETICONMETRICS` succeeds with `cbSize` kept and fails with `cbSize` smaller by 4 | D (SystemParametersInfoForDpi: only the listed actions); the exact size checks are C and O | yes |
| 19 | same | `SystemParametersInfoForDpi(.., dpi 0)` returns TRUE on Windows 10 (the decompilation suggested failure; this was corrected after the guest run, see Corrections). The values returned for dpi 0 are NOT asserted; N: on this guest iBorderWidth 5, iCaptionHeight 22 | O | yes |
| 20 | spi_ncm_size_protocol | `SPI_GETNONCLIENTMETRICS` through `SystemParametersInfoW`: succeeds with `uiParam == sizeof` and with `uiParam == 0`, giving identical structures; succeeds with `cbSize == 500` giving the same caption/menu/scroll sizes and message font height as the 504-byte call; fails with `cbSize == 340`, and with `pvParam == NULL`; `SystemParametersInfoA` with the 344-byte `NONCLIENTMETRICSA` agrees with W on all nine size members and the message, caption and menu font heights and weight; with 340 it succeeds and agrees on caption height; with 500 it fails | D (NONCLIENTMETRICS: cbSize must be set; the pre-Vista size is 500); the uiParam-0 and the 340 / 500 acceptance matrix are C and O | yes |
| 21 | spi_iconmetrics_size_protocol | `SPI_GETICONMETRICS` through `SystemParametersInfoW`: succeeds with `cbSize == sizeof` and a font face name; fails with `cbSize` smaller by 4 and with a NULL buffer | D (ICONMETRICS cbSize); C | yes |
| 22 | spifordpi_matches_spi_at_system_dpi_observed | N: `SystemParametersInfoW` and `SystemParametersInfoForDpi(.., GetDpiForSystem())` caption height / message font height compared; printed only when different (never printed) | O | yes |

## Known limits

- Rows 11 and 14 check the structure of the result at the guest's 96 DPI system DPI and at the explicit ForDpi values; they do not assert absolute pixel sizes (that is what the recordings are for).
- The test process is unaware of DPI. Behaviour for system-aware and per-monitor-aware processes at DPI above 96 is not exercised by this area (STATE.md "Not done").
- Row 10 and row 19's numbers describe the guest only and are not asserted.
- Face-dependent rows (1, 7) assume Segoe UI, MS Shell Dlg, Tahoma, Courier New and the raster faces exist as on a stock install; an uninstalled face still gets the mapper's fallback and the invariants hold.

## Corrections

Found by the first conformance run of this area on Windows 10 and fixed before the final runs: `SystemParametersInfoForDpi` with dpi 0
returns TRUE (row 19); the earlier assertion that it fails was based on the decompilation and was corrected to the observed behaviour,
with the values left unasserted. No assertion was weakened for this document.
