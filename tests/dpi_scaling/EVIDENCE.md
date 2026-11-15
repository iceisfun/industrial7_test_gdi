# dpi_scaling: evidence

Area: DPI awareness contexts, `GetDpiForSystem`, `GetSystemMetricsForDpi`, `SystemParametersInfoForDpi`, `AdjustWindowRectExForDpi`, dialog font
creation, and GDI font/metric invariants under each thread DPI context. Built into `build/dpi_scaling.exe`; run with `tools/vm_test.sh dpi_scaling`.

Oracle: Windows 10 22H2, 10.0.19045.2965, en-US, 1024x768, one monitor, Administrator session, **96 DPI**. Two fresh boots; the logs agree line for line
(16 tests, 1603 checks, 0 failed, 0 skipped, 1 NOTE line). Only 96 DPI can be run live. Every expectation that depends on a DPI other than 96 is
therefore checked against the recordings in `data/win10/dpi_scaling/` (96/120/144/168/192 sweep, two boots at 96 identical) and cited as **R** rows
below. The test itself evaluates the rule at several DPIs where the API takes the DPI as an argument (`...ForDpi`), and uses the live DPI otherwise.
"Every enterable thread context" below means the process default plus unaware (-1), system (-2) and per-monitor v2 (-4).

Source key: **doc** = Microsoft Learn; **decomp** = Win10 22H2 (19041.2965) decompilation of the named function (addresses are in the test header,
tags in brackets such as [VDAC]); **obs** = observed on the oracle, stable in both boots; **R** = a row of `data/win10/dpi_scaling/dpi<N>.rec`
(table `ncm`, `smdpi`, `awr`, `dlg`, `ptconv`, `ctx`); **hdr** = rule written in the test header and verified by the run.

Docs: `GetDpiForSystem`, `GetSystemMetricsForDpi`, `SystemParametersInfoForDpi`, `AdjustWindowRectExForDpi`, `SetThreadDpiAwarenessContext`,
`AreDpiAwarenessContextsEqual`, `IsValidDpiAwarenessContext`, `GetAwarenessFromDpiAwarenessContext`, `GetDpiFromDpiAwarenessContext`,
`GetObject`, `GetTextMetrics`, `GetCharABCWidths`, `DLGTEMPLATEEX` (all under `learn.microsoft.com/windows/win32/api/`).

## Expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| dpictx_pseudo_handles_valid | The five pseudo handles (-1..-5) are valid; awareness 0,1,2,2,0 (unaware, system, per-monitor, per-monitor v2, unaware-gdiscaled); DPI of -1 and -5 is 96, of -3 and -4 is 0; the system context DPI is in 96..511 | doc (return values) + decomp [VDAC][AWARE]; R `ctx` rows: awareness column 0/1/2/2/0 for unaware/system/pm/pm2/gdis, packed values 0x6010, 0x6011, 0x12, 0x22, 0x40006010 | 2 boots; R at 5 DPIs |
| dpictx_invalid_values | NULL, -6 and 0x6012 are invalid with `ERROR_INVALID_PARAMETER`; awareness of NULL is -1 (`DPI_AWARENESS_INVALID`), DPI of NULL is 0; equality with NULL is false | doc + decomp [VDAC][AWARE] | 2 boots |
| dpictx_packed_system_value | A packed system-aware value with any 9-bit DPI (`0x1ff<<8 \| 0x11`) validates and decodes (awareness 1, DPI 511), but `SetThreadDpiAwarenessContext` rejects it (NULL, error 87) because set semantics require the process DPI | decomp [VDAC][STDAC] (the validate-versus-set split is not stated in the docs) | 2 boots |
| dpictx_equality | Each handle equals itself; -3 differs from -4, -1 from -5 (bit 30 counts), -1 from -4; a packed value equals its pseudo handle (-1 vs 0x6010, -4 vs 0x22) | doc + decomp [AWARE]; R `ctx` packed values | 2 boots |
| dpictx_thread_roundtrip | Setting each pseudo handle returns the previous context, the getter reports the new one, restoring returns the one just set; `Set(NULL)` returns NULL with error 87 and changes nothing | doc (return value = old context) + obs (failed set changes nothing) | 2 boots |
| dpiforsystem_unaware_thread_is_96 | In an unaware (-1) and gdi-scaled (-5) thread `GetDpiForSystem` is 96 (also on a system whose DPI is not 96) | doc (unaware threads see 96) + decomp [GDFS]; R `ctx` rows `unaware` and `gdis`: `sysdpi` column = 96 at all five DPIs (96/120/144/168/192), while `procsysdpi` is the real DPI | 2 boots; R at 5 DPIs |
| smfordpi_bad_params | dpi 0, a negative index and a dpi whose signed value is <= 0 give 0 with error 87 | decomp [GSMD]; obs | 2 boots |
| smfordpi_unscaled_index_passthrough | For indexes that do not scale (`SM_CXSCREEN`, `SM_CYSCREEN`, `SM_SWAPBUTTON`, `SM_CMOUSEBUTTONS`, `SM_CXVIRTUALSCREEN`, `SM_CMONITORS`) `GetSystemMetricsForDpi` equals `GetSystemMetrics` at 96/144/192 in every enterable thread context | decomp [GSMD]; R `smdpi` (recorded in the process context): CXSCREEN and CXVIRTUALSCREEN are the same for every `dpi` argument within one recording (1024 at 96/120; 683/585/512 at 144/168/192, where the process context is unaware and the screen is virtualised), i.e. the DPI argument is ignored for these indexes | 2 boots; R at 5 DPIs |
| smfordpi_96_is_thread_independent | The 8 scaled metrics (scroll width/height, caption height, menu height, small and large icon, frame) are > 0 and have the same value at dpi 96 in every thread context | decomp [GSMD] (dpi 96 reads a global table) + R `smdpi` (CXVSCROLL 17, CYCAPTION 23, CYMENU 20, CXICON 32, CXSMICON 16, CXFRAME 4 at 96) | 2 boots |
| spifordpi_param_protocol | `SPI_GETNONCLIENTMETRICS` / `ICONMETRICS` / `ICONTITLELOGFONT` with a NULL buffer, a wrong `uiParam` or a wrong `cbSize` fail with 87; an unsupported action (`SPI_GETWORKAREA`, `SPI_GETFONTSMOOTHING`) fails with 87 | decomp [SPID]; obs | 2 boots |
| spifordpi_param_protocol (dpi 0) | `SystemParametersInfoForDpi(NCM, dpi 0)` returns TRUE (asserted); the values are only reported by NOTE (iBorderWidth 5, caption 22 in both boots; they differ from the dpi 96 result). The decompiled argument check suggests failure, the oracle disagrees, so the decompilation is not the authority here | obs only | 2 boots |
| spifordpi_ncm_matches_smfordpi | At 96/120/144/168/192 each NONCLIENTMETRICS size (scroll w/h, caption w/h, small caption w/h, menu w/h, padded border) equals `GetSystemMetricsForDpi` of the corresponding metric (`SM_CXVSCROLL`, `SM_CYVSCROLL`, `SM_CXSIZE`, `SM_CYSIZE`, `SM_CXSMSIZE`, `SM_CYSMSIZE`, `SM_CXMENUSIZE`, `SM_CYMENUSIZE`, `SM_CXPADDEDBORDER`); `iBorderWidth` at 192 is twice that at 96 | decomp [NCMD][GSMD] + R `ncm src=fordpi` vs `smdpi`: e.g. dpi 120 scroll 21, caption 46x28, menu 24, padded 5; dpi 192 scroll 34, caption 72x44, menu 38, padded 8, border 2 (border 1 at 96) | 2 boots; R at 5 DPIs |
| awrfordpi_bad_dpi | dpi 0 returns FALSE with error 87 and leaves the rectangle untouched | doc (failure) + decomp [AWRD] | 2 boots |
| awrfordpi_frame_composition | For 8 style/ex-style sets, at 96/120/144/168/192, with and without a menu: all four sides get the same frame width n, the top additionally gets caption (`SM_CYCAPTION`, or `SM_CYSMCAPTION` with `WS_EX_TOOLWINDOW`) plus the menu height (`SM_CYMENU`) at that DPI, both ForDpi; `WS_EX_CLIENTEDGE` adds 2 on every side | decomp [AWRD] + R `awr` rows: overlapped 96 = (-8,-31,108,108), 120 = (-9,-38,109,109), 192 = (-13,-58,113,113); with menu 96 top -51, 120 -63, 192 -97; dialog 96 (-3,-26), 120 (-3,-32), 192 (-3,-48). Each equals frame + `smdpi` caption (+ menu) for the same DPI (for example 192: 13 + 45 = 58, 13 + 45 + 39 = 97) | 2 boots; R at 5 DPIs |
| font_invariants_every_ctx | For 8 faces (Segoe UI, Tahoma, Arial, MS Shell Dlg, MS Shell Dlg 2, Microsoft Sans Serif, Courier New, System) at 8/9/12 pt (`lfHeight = -MulDiv(pt, logpixelsy, 72)`), in every enterable thread context: `GetObjectW` returns the stored LOGFONT; `tmHeight == tmAscent + tmDescent`; TrueType `tmOverhang == 0`; `GetTextMetricsA` equals `GetTextMetricsW` field by field; `GetTextExtentPoint32` cy equals `tmHeight`; cx is within rounding of the sum of `GetCharWidth32W` (tolerance n/2); for TrueType A+B+C equals the advance; the font is selected and restored. A-versus-W extent differences and fractional-advance differences are NOTE only (none arose) | doc (`GetTextMetrics`, `GetCharABCWidths`) + decomp [TM][EXT][TMA][GOBJ]; R `tm`/`ptconv`: pt-to-pixel conversion at 96 = 11/12 (8/9 pt), at 192 = 21/24, matching `-MulDiv` | 2 boots |
| getobject_font_size_protocol | `GetObjectW(font, 0, NULL)` and `(font, 1000, NULL)` return 92; a full copy returns 92 with the stored height; a DC handle returns 0 with `ERROR_INVALID_HANDLE`. The stock `DEFAULT_GUI_FONT` height rule `-((8*dpi+36)/72)` is reported by NOTE only if it does not hold (it held, no NOTE) | doc + decomp [GOBJ]; obs; R `ptconv` `dgf_lfh` -11 at 96 and -21 at 192 (rule gives -11 and -21) | 2 boots |
| dlgfont_template_kind_and_dpi_rule | A dialog created from a template with `DS_SETFONT`: `DLGTEMPLATEEX` + `DS_FIXEDSYS` (shell font) with face "MS Shell Dlg" gets "MS Shell Dlg 2"; DIALOGEX with `DS_SETFONT` only keeps "MS Shell Dlg"; a plain `DLGTEMPLATE` with `DS_FIXEDSYS` keeps "MS Shell Dlg"; Segoe UI and Tahoma are kept; height is `-((pt*dpi+36)/72)` with the window DPI; DIALOGEX keeps weight 400. Run in each thread context | doc (`DS_SHELLFONT`, DIALOGEX) + decomp [DLGF]; R `dlg` rows at 96: shellfont MS Shell Dlg -> MS Shell Dlg 2, `setfont_ex` MS Shell Dlg stays, lfh -11 (8 pt) / -12 (9 pt Segoe UI); at 192 system and pm2 contexts lfh -21 / -24, unaware -11 / -12 | 2 boots; R at 5 DPIs |

## R cross-checks that the 96 DPI guest cannot run

The test code is DPI-independent, but these numbers are the oracle's answer at other DPIs and were compared by hand with the rules the test applies
(the comparison is not repeated by the harness):

- `ncm src=fordpi` iBorderWidth is 1, 1, 2, 2, 2 at 96/120/144/168/192; scroll width 17/21/26/30/34; padded border 4/5/6/7/8. The test asserts only
  border(192) == 2 * border(96) (1 and 2 in R), because other DPIs round differently.
- `awr` (process context, 7 styles at each `dpi` argument 96..192) and `smdpi` agree by the composition rule above in all 5 recordings.
- Dialog fonts at 120/144/168/192 follow `-((pt*dpi+36)/72)` with the real DPI in the system, pm and pm2 contexts. The unaware context sees 96 at every
  DPI (`dlg` `wdpi` 96, lfh -11 / -12; the screen is virtualised, 819x614 at 120): the test uses `GetDpiForWindow` of the dialog, so it needs no
  special case.

## Oracle surprises

- `SystemParametersInfoForDpi(NONCLIENTMETRICS, dpi 0)` returns TRUE (see above), contrary to the argument check in the decompilation. At dpi 72 the
  function also returns TRUE with a mostly-zero struct and a boot-dependent `iMenuHeight` (R `ncm src=fordpi;dpi=72`: 592, 7672, 4072, 2020, 1316 across
  the sweep stages); the test does not touch DPIs below 96.
- `ncm` rows for the thread-DPI-dependent source (`src=spi`) are context dependent (system context at 120 gives iBorderWidth 6, at 144 gives 8); the test
  does not compare `SystemParametersInfo` with anything for this reason.

## Skips

None in either boot. The test skips only if an API is missing (`REQUIRE`), the dialog test skips when no dialog can be created.

## Not covered

- A live run at a DPI other than 96 (per-DPI numbers rely on R rows above), per-monitor DPI change notification, `GetDpiForWindow` on a moved window,
  `SetProcessDpiAwarenessContext` modes (R `api-*` files record these, the test does not call them), and `EnableNonClientDpiScaling`.
- `GetSystemMetricsForDpi` for the full list of scaled indexes (only the 8 in the test plus the pass-through set).
