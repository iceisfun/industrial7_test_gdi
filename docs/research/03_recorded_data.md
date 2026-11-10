# 03 Recorded data: what other lanes already measured (Win10 vs Win7), and what is still unmeasured

Role: prior-recorded-data miner. Scope: every font / text-metric / height / row-size fact that exists today
in the bb depot, in bb topics, and in the Industrial7 checkout, with provenance. Nothing here was measured
by this lane: every number is copied from a file or a depot run named next to it. Label legend:

| tag | meaning |
|---|---|
| **W10-ORACLE** | measured on the real Win10 22H2 x64 guest (19045.2965; 1024x768, 96 DPI, English) by laneg/lanef/laned through the oracle runner; depot tarball or committed `*.win10.txt` |
| **W7** | measured on the real Win7 SP1 x64 6.1.7601 VM (the "win7 column", `*.win7.tsv` in industrial7 `userland/wintests/*/data`) |
| **I7** | measured on Industrial7 itself (stand-in 8x16 font, or D12 on). NOT Windows. Listed only because it is the thing being fixed |
| **decomp** | read from Win10 decompilation (not measured) |
| **[inf]** | inference by this lane or by the quoted lane; not measured |

Units: all heights/widths are device pixels at 96 DPI, logical == device (MM_TEXT). `lfHeight` negative = char
height (em) in px; `tmHeight` = cell height = ascent + descent (this INCLUDES internal leading);
`tmHeight - tmInternalLeading` = em. "alphabet" = `GetTextExtentPoint32` of the 52 letters A-Z a-z (px).

## 0. Headline

1. For the Win7 UI families, **Win7 SP1 and Win10 22H2 agree on every metric that matters for layout** (tmHeight,
   ascent, descent, internal leading, average width, 52-letter extent) for Segoe UI -12, MS Shell Dlg -11,
   MS Shell Dlg 2 -11 (alphabet) and the default (Arial) face. The only column that differs is `tmMaxCharWidth`
   (Segoe UI 31 on Win10 vs 23 on Win7; MS Shell Dlg 2 27 vs 23). So the Win7-recorded
   `fonts_sysfont.win7.tsv` / `fonts_map.win7.tsv` data in Industrial7 is a usable proxy for Win10 layout
   numbers, with max-width the known exception (section 5).
2. The 15/13 text heights are **settled for Win10**: Segoe UI -12 -> tmHeight **15** (a 12 d 3 il 3 ave 6),
   MS Shell Dlg -11 -> tmHeight **13** (a 11 d 2 il 2 ave 5). 6/6 boots identical (std == admin).
3. The 19/17 list-view row heights are **recorded on Win10**: rows are **19 px** with Segoe UI -12 (the list view's
   default font when no WM_SETFONT is sent) and **17 px** with MS Shell Dlg -11 (WM_SETFONT(DEFAULT_GUI_FONT)),
   i.e. tmHeight + 4 [inf: formula]; the report header is **24 px in both** (it is not tmHeight-driven at these
   sizes, see 3.6).
4. micross.ttf: Win10 stages `micross.ttf` (876256 bytes) and the FontSubstitutes-level rule "MS Shell Dlg ->
   Microsoft Sans Serif" is what yields 13/11/2 ave 5 alphabet 323 for MS Shell Dlg -11 on both OSes. Arial -11
   is tmHeight **14** on Win7 (and the i7 D12-on fallback), so without micross the MS Shell Dlg -11 height is
   +1 px (14 vs 13). That is the "1 px off even with D12 on" (W7 data + I7 data; the Win10 side is the 13).
5. Large holes remain (section 8): no Win10 stock-object TEXTMETRICs (System, ANSI_VAR, ANSI_FIXED, OEM, FIXEDSYS),
   no Tahoma / Courier / Arial at -11/-13/-16 on Win10, no char-width / ABC arrays, no CreateFontIndirect
   face-substitution table, no non-96 DPI text metrics, no comctl32 control heights other than list view/header.

## 1. Where the data lives (inventory)

### 1.1 bb depot (downloaded to `/home/iceisfun/w10lab/depot/`, extracted under `depot/x/`)

`bb depot ls --all` shows 211 manifests (m11..m217 at the time of the pull). Only the ones below touch fonts,
text metrics or geometry; the rest (winsock, tokens, winsta, xstate, regnotify, wininet, firefox payloads,
media) are unrelated to GDI text. Each Win10 tarball holds 3 std-user + 3 admin boots (`std1..3`, `adm1..3`),
`out.txt` (probe stdout), `serial.log`, `_oracle.txt`, `driver.log`.

| depot | file | what | rows | 6/6 identical? | md5(out.txt) |
|---|---|---|---|---|---|
| m199 | `task60-np-metrics-runs.tar.gz` (sha256 5e4ab28c...) | NPMET: SM_* metrics, SPI NONCLIENTMETRICS/ICONMETRICS fonts, **TEXTMETRIC + 52-letter extents of 6 fonts**, 3 DPI-awareness modes | 220 | yes, std == adm | b8df91a2 |
| m192 | `win10_np_metrics_probe_free.exe` | the probe for m199 | - | - | - |
| m208 | `task66-runs.tar.gz` (3fd7fd5d...) | **CC30 list view geometry**: font LOGFONT, TEXTMETRIC, header/row rects, 6 fixtures | 72 | yes | f240726a |
| m205 | `cc30_lv_geometry_probe.exe` (sha256 1569af77...) | the probe for m208 | - | - | - |
| m188 | `task56-runs.tar.gz` (47992f70...), probe m185 `cc29_lv_autosize_probe.exe` | CC29 LVM_SETCOLUMNWIDTH autosize (-1/-2/90) widths | 96 | yes | 9807791f |
| m196 | `task57-runs.tar.gz` (93588031...) | AS2: autosize widths per text, 5 fixtures x 3 modes x 2 columns | 32 | yes | 374a83c3 |
| m103 | `task-cc28-runs.tar.gz` (02277...) | CC28 header-width sync (PASS/FAIL rows only, no px metrics beyond widths) | 17 | yes | cdc1107a |
| m201 | `task62-np-msgbox-runs.tar.gz` (b4ab1721...) | NPMB: real MessageBox sizes/text rects, 10 cases | 61 | yes | b1684d17 |
| m202 | `task63-np-wndsize-runs.tar.gz` (da6b78b7...) | NPWND: window clamp (maxtrack) + **dialog base units for DS_SETFONT dialogs** | 33 | yes | 67a7760d |
| m200 | `task61-np-icon-runs.tar.gz` | icon sizes (no fonts) | 149 | yes | - |
| m79 | `task-stockobj3-runs.tar.gz` | stock-object handle/type sanity only (NO font metrics) | 26 | - | - |
| m94/m144 | ctxmenu / popup-backdrop runs | no font data | - | - | - |

Not recorded in any depot object: raw GetCharWidth/ABC arrays, kerning, EnumFonts, anything for Tahoma/Courier/System.
`bb depot search` for comctl/font/text/theme/dwrite/caption returned "no manifests": there is no font corpus in the
depot yet; the earlier lanes' text data lives in git (1.2) and bb topic attachments (1.3).

### 1.2 Industrial7 checkout (`/home/iceisfun/work/industrial7`)

| path | what | provenance |
|---|---|---|
| `userland/wintests/gdi32/data/*.win7.tsv` (about 410 tables incl. 20 font/text families) | Win7 SP1 recordings by `gdi32_fonts_*`, `gdi32_text_*`, `gdi32_chrome_sysfont`, `gdi32_chrome_uitext` | **W7**; header line of each file names the exe, OS build 6.1.7601, gdi32 6.1.7601.17514 |
| `.../data/<t>.tsv`, `<t>.fonthost.tsv`, `<t>.ui.tsv` | the same programs on i7 (`.tsv` = 8x16 stand-in, `.fonthost.tsv` = font host, `.ui.tsv` = D12 knob on) | **I7** |
| `userland/wintests/comctl32/data/ui_fonts*.win7.tsv`, `trace_*.win7.tsv` (174 tables) | Win7 SP1 comctl32 5.82/6.0 control traces | **W7** (not mined in depth here) |
| `userland/wintests/win10/win10_fonts_probe.win10.txt` (811 rows) | Win10 `\Windows\Fonts` directory (341 entries), Fonts registry key (154 values), GetFileSize/mapping of each | **W10-ORACLE** (lanef, local QEMU, 2026-11-05) |
| `userland/wintests/win10/win10_dwrite_s3_probe.win10.txt`, `win10_dwrite_rows.win10.txt`, `expected/win10_dwrite_glyph_probe.win10.txt` (1115 rows) | DirectWrite design metrics (upem/asc/desc/cap/xh/underline) for Arial, Courier New, Consolas, Segoe UI, Wingdings, Marlett, Times; per-glyph metrics; alpha textures | **W10-ORACLE** |
| `userland/wintests/win10/data/dlg_paint_probe.win10-oracle*.txt` (4 files, 293..727 rows) | Win10 dialog paint / DLU rows: `MapDialogRect(160x90 DLU)=240x146`, `GetDialogBaseUnits=8,16`, "MS Shell Dlg 8: 6x13 base units, observed" | **W10-ORACLE** |
| `crates/w10-comctl32-lv/tests/data/w10_comctl915_probe.tsv` (68 rows), `pixel_oracle.tsv` (1252 rows), `com_tiles_probe.tsv` | "comctl-subitems" list view with Segoe UI -15 (header 24, row 33), tiles | **W10-ORACLE** (laned Win10 VM, bb #915 attachments 914/923); pixel colours, not text |
| `docs/design/gdi32/D12-TRUETYPE-UI.md`, `docs/fonts/DESIGN.md` D12, `FONT-ENUM.md`, `FONTLINK.md`, `GDI32.md`, `docs/design/comctl32/win7/README.md` | design/consolidation of the W7 and I7 columns | derived from W7/I7 |
| `userland/wintests/win10/expected/win10_text_draw_probe.win10.txt` (commit 6751811b75 `laneg/text-oracle`, not on master) | Win10 `CreateFont` Arial/Segoe UI -16 TEXTMETRIC (coarse), 113 families, DWrite alpha bounds | **W10-ORACLE** |

No Win10 `np-fix-metrics` branch is on disk (bb #1139 refers to `laneb/np-fix-metrics`, `docs/bugs/np-fix-metrics.md`); its
findings are only available through the topic text.

### 1.3 bb topics mined (text pulled to `/home/iceisfun/w10lab/bbtxt/`)

#1139 (Notepad++ / NP probes, i7 vs Win10 metrics diff), #1134 (7zFM list view, CC28..CC33), #915 (comctl-subitems
oracle), #1051 (fonts / text probe), #1077 (text widening pass; gasp tables), #1063 (DWrite single glyph),
#919, #555. Topic text is quoted as evidence only where a depot run or committed file confirms it.

Sibling corpora (`industrial7_test_gdi32_gdifull`, `_user32`, cloned read-only): their tests assert **relations**
(extent == sum of widths, `cy == tmHeight`, stock-object width == `tmAveCharWidth`) verified on Win10 (user32
ORACLE-NOTES: 3 identical runs, 6814 tests) but contain no absolute metric tables. They tell us nothing numeric.

## 2. Win10 environment of every run above

1024x768 screen, work area 1024x728, 1 monitor, LOGPIXELSX/Y 96, BITSPIXEL 32, system DPI 96,
`GetDialogBaseUnits` = 8x16, `GetDpiForWindow(NULL)` = 0 (gle 87), probe images are mingw builds with
subsystem version 5.2 (this affects the padded-border rule, section 3.4). Image 10.0.19045.2965, no updates. English (US).

## 3. Win10 22H2 facts (W10-ORACLE)

### 3.1 TEXTMETRIC and alphabet extent (NPMET, m199, identical in unaware / system-aware / per-monitor-v2 contexts)

| probe name | request | height | ascent | intlead | ave | max | alphabet cx (52 letters) | alpha cy | '0' width | face reported |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `message` = `caption` = `segoeui_m12` | NCM message font, lfHeight -12 Segoe UI (also CreateFont -12) | **15** | 12 | 3 | 6 | 31 | **347** | 15 | 6 | Segoe UI |
| `msshelldlg_m11` | -11 "MS Shell Dlg" | **13** | 11 | 2 | 5 | 23 | **323** | 13 | 6 | MS Shell Dlg |
| `msshelldlg2_m11` | -11 "MS Shell Dlg 2" | **13** | 11 | 2 | 5 | 27 | **309** | 13 | 6 | MS Shell Dlg 2 |
| `default_h0` | lfHeight 0, empty face | **18** | 15 | 2 | 7 | 43 | **472** | 18 | 9 | Arial |
| `screen_dc_default` | GetTextMetrics on the screen DC with no select | 16 | - | - | 7 | - | - | - | - | (SYSTEM_FONT, see 8) |

"avg" in the probe line (7, 6, 9) is a separate `GetCharWidth`/extent-derived average the probe prints; the `ave=`
column is `tmAveCharWidth`. Descent is not printed by NPMET but is `height - ascent` (Segoe 3, Shell Dlg 2, Arial 3).
`GetDialogBaseUnits` = 8x16 (system font, `dpi.dialogbaseunits`).

### 3.2 CC30 TEXTMETRIC (m208): the same fonts through the list view (identical to 3.1)

Row format `text=name,ok,tmHeight,tmAscent,tmDescent,tmExternalLeading,tmInternalLeading,tmAveCharWidth,dpiY`:

| fixture | font the control used | LOGFONT (`GetObject`, size 92) | tm: h/a/d/el/il/ave | dpiY |
|---|---|---|---|---:|
| base, smallil, nohdr, fullrow (no WM_SETFONT) | **Segoe UI -12**, weight 400, charset 1 (DEFAULT) | `-12 Segoe UI` | **15/12/3/0/3/6** | 96 |
| guifont, guifont+smallil (`WM_SETFONT(GetStockObject(DEFAULT_GUI_FONT))`) | **MS Shell Dlg -11**, weight 400, charset 0 | `-11 MS Shell Dlg` | **13/11/2/0/2/5** | 96 |

So on Win10 `GetStockObject(DEFAULT_GUI_FONT)` is `MS Shell Dlg -11` charset 0 (same as Win7, row W7 `GFNT.SYSFONT.003`), while a
list view with no font message sends itself to the **message font** (Segoe UI -12), not to DEFAULT_GUI_FONT.

### 3.3 Other Win10 TEXTMETRIC rows (older / coarser)

| source | font | result |
|---|---|---|
| bb #915 attachment 923 (`w10_comctl915_probe.tsv`, laned Win10 VM) | Segoe UI **-15** (lfQuality 5) | height **20**, ascent 16, descent 4, intlead 5, extleading 0, ave **8**, max **39**, weight 400, charset 0 |
| `win10_text_draw_probe` (laneg #12372; bucketed) | Arial -16 / Segoe UI -16 | Arial: height **18**, avgw **7**, first 32, last 65532; Segoe UI: height **21**, avgw **9**, first 13, last 65533 |
| NPMB message-box static (m201) | font of the MessageBox text control, `WM_GETFONT` | `lf_h=-11 tm_h=13 tm_ave=6` while the text rect is 15 px high and NCM message font is Segoe -12 (tmHeight 15): **unexplained discrepancy** (face name not printed; this probe selected a different font than the one that drew). Do not use `tm_h=13` as a Segoe UI datum |

### 3.4 NONCLIENTMETRICS / ICONMETRICS fonts (m199)

All five NCM LOGFONTs (caption, smcaption, menu, status, message) and ICONTITLELOGFONT: `lfHeight -12, weight 400,
charset 1, Segoe UI` at 96 DPI; **`-18` at 144 DPI** (SystemParametersInfoForDpi cb 504). Metrics at 96 (mingw 5.2 image
view): border 5, scroll 17, caption 36x22, smcaption h 22, menu h 19, padded 0. With the 6.0+ view (`ncm96` row): border 1, padded 4, same scroll/caption/menu.
At 144: border 2, scroll 26, caption 54x33, menu 29, padded 6.

### 3.5 System metrics at 96 DPI (m199, all three contexts equal)

CXICON/CYICON 32, SMICON 16, ICONSPACING 75x75, CYCAPTION **23**, CYSMCAPTION **23**, CYMENU **20**, CXSIZE 36, CYSIZE 22,
CXBORDER 1, CXEDGE 2, CXFIXEDFRAME/CY 3, CXSIZEFRAME/CY 8, CXPADDEDBORDER 0 (5.2 view; 4/4/4 in the 6.0 view), MINTRACK 136x39.
GetSystemMetricsForDpi 96/120/144/192: CXICON 32/40/48/64, CXSMICON 16/20/24/32, CYCAPTION 23/29/34/45,
CXSIZEFRAME 4/4/5/5, CXPADDEDBORDER 4/5/6/8.

### 3.6 List view and header geometry (CC30 m208, CC29 m188, AS2 m196, bb #915)

CC30 geometry rows (all 96 DPI, 500x180 list view, 2 columns 200/80, rows `hdr=`, `irow=`):

| fixture | list font | header rect | header h | row 0 / 1 / 2 rect (y) | row h | label x |
|---|---|---|---:|---|---:|---:|
| base | Segoe UI -12 (tm 15) | 0,0,500,24 | **24** | 24-43 / 43-62 / 62-81 | **19** | 4 |
| guifont (MS Shell Dlg -11, tm 13) | | 0,0,500,24 | **24** | 24-41 / 41-58 / 58-75 | **17** | 4 |
| smallil (16 px small image list, Segoe) | | 0,0,500,24 | 24 | 24-43 / 43-62 / 62-81 | 19 | 20 |
| guifont+smallil | | 0,0,500,24 | 24 | 24-41 / 41-58 / 58-75 | 17 | 20 |
| nohdr (LVS_NOCOLUMNHEADER) | Segoe | 0,0,500,0 | 0 | 0-19 / 19-38 / 38-57 | 19 | 4 |
| fullrow | Segoe | 0,0,500,24 | 24 | as base | 19 | 4 |

sysmetrics row (same in all fixtures, `name,cyborder,cxborder,cyedge,cxedge,cxsmicon,cysmicon,cyfixedframe,0`): `1|1|2|2|16|16|3|0` = CYBORDER 1, CXBORDER 1, CYEDGE 2, CXEDGE 2, small icon 16x16, CYFIXEDFRAME 3 (all agree with 3.5).
Row rect widths: data rows span the full header-column total (0..280), label rect starts at x=4 (20 with the small image list).
Header width: 3 header-item rects `0,0,200,24` and `200,0,280,24`.

Inference [inf, lanea/laneg in bb #1134 and #915]: row height = tmHeight + 4 (15 -> 19, 13 -> 17; with a 32 px image list
(bb #915, tmHeight 20) the row is 33 = image + 1, i.e. max(text + 4, image + 1)); header height = max(tmHeight + 2*CYEDGE
= tmHeight + 4, themed minimum 24) because Segoe -12 (15 + 4 = 19) and MS Shell Dlg -11 (13 + 4 = 17) both give 24, while
Segoe -15 (20 + 4) also gives 24 (bb #915: "24 px for Segoe UI -15 (text height 20 + 4)"). Header height therefore cannot discriminate
between the two fonts; the row height can (19 vs 17).

Autosize widths in the default (Segoe -12) font (AS2 m196 / CC29 m188), column widths in px with 25 px minimum:
-1 AUTOSIZE (fits cells only): cell 'x' -> 25; 'a long cell text' -> 85; '2024-01-01 12:34' -> 96; 'short'/'a much longer second item text' -> 176;
'y' -> 25; '1,234' -> 39; empty list + header 'Header text' -> 25. -2 AUTOSIZE_USEHEADER (non-final): header 'Header text' vs cell 'x' -> 73;
final column -2 always fills 500-col0 = 300. These are text extents + padding for Segoe -12 [inf: the strings' own extents are not printed].

bb #915 (laned, comctl-subitems, Segoe UI -15): list+header Segoe -15, header **24**, row **33** (32 px icon + 1), column widths as set; useful
second data point for the "header 24" rule.

### 3.7 MessageBox (NPMB m201) and dialogs (NPWND m202)

NCM message font Segoe -12, `GetDialogBaseUnits` 8x16, work area 1024x728. Short text MessageBox: outer **123x130**, client 117x101, frame 6x29, static
text 77x15 at (14,49), button 75x23, bottom gap 13; with icon 125x149; multi-line CRLF 176x182; 300-char no-space string 413x182 (text 366x67); long
words 416x273. Per-monitor-v2 gives identical sizes at 96 DPI.
Dialog templates (DS_SETFONT, MS Shell Dlg 8 / Segoe UI 8): both give **base units 6x13** with a realised font `lf_h = -11`, window 306x192,
client 300x163 (`dlg.msshelldlg8_setfont`, `dlg.segoeui8_setfont`, same at pmv2). bb #1139 notes the 9pt Segoe UI (-12) DS_SETFONT prediction 7x15 is **not recorded**.
Also from `dlg_paint_probe.win10-oracle-19045.txt`: `MapDialogRect(160x90 DLU)=240x146` for the MS Shell Dlg 8 dialog.

### 3.8 Fonts present on the Win10 guest, DirectWrite design metrics

`win10_fonts_probe.win10.txt` (lanef): `C:\Windows\Fonts` 341 entries, 154 Fonts-key values. Relevant files and sizes: `segoeui.ttf` 955804,
`segoeuib.ttf` 951724, `tahoma.ttf` 939280, `tahomabd.ttf` 867988, **`micross.ttf` 876256** ("Microsoft Sans Serif (TrueType)"), `arial.ttf` 1036584,
`cour.ttf` 806504, `times.ttf` 1195688, `marlett.ttf` 27724, `consola.ttf` 459180, `lucon` and `verdana` present, plus the bitmap `.fon` set:
`vgasys.fon` 7280 (System), `vgafix.fon` 5360 (Fixedsys), `vgaoem.fon` 5168 (Terminal), `sserife.fon` 64656 / `sseriff.fon` 89856 (MS Sans Serif 96/120),
`serife.fon` 57936 (MS Serif), `smalle.fon` 26112 (Small Fonts), `coure.fon`/`courf.fon` (Courier), `8514*`, `cga*`, `ega*`, `app*`, `dos*`. Registry names:
`MS Sans Serif 8,10,12,14,18,24` = SSERIFE.FON, `... (120)` = SSERIFF.FON, `Small Fonts` = SMALLE.FON, `MS Serif 8,10,12,14,18,24` = SERIFE.FON.
(The text-draw oracle run lists 113 GDI families and 72 DirectWrite families on the same class of guest.)

DirectWrite `GetMetrics` (design units, upem 2048; `win10_dwrite_s3_probe.win10.txt`): Arial asc 1854 desc 434 gap 67 cap 1467 xh 1062; Segoe UI asc **2210**
desc **514** gap 0 cap 1434 xh 1024; Courier New asc 1705 desc 615 gap 0; Consolas asc 1884 desc 514; Times asc 1825 desc 443 gap 87; Marlett asc 2048 desc 0; Wingdings asc 1841 desc 432.
(Segoe UI at -12 px: (2210+514)/2048*12 = 15.96 -> GDI cell 15 [inf]; this is the metric source the GDI table must reproduce.)
Glyph design advances (`win10_dwrite_glyph_probe`): Arial A 1366, g 1139, j 455, Q 1593, W 1933; Segoe UI A 1321, g 1206, j 496, Q 1544, W 1913 (upem 2048).
gasp (bb #1077 from laneg #12720, oracle): Arial `[(8,0xA),(17,5),(65535,0xF)]`, Segoe UI `[(8,0xA),(19,7),(65535,0xF)]`, arialbd v0 `[(8,2),(10,1),(65535,3)]`.

### 3.9 Window/frame constants from the same probes (for completeness)

Overlapped windows clamp to maxtrack = work/screen + 20; WM_GETMINMAXINFO maxsize = screen + 16, maxpos (-8,-8), mintrack 136x39 (NPWND m202).

## 4. Win7 SP1 facts (W7) relevant to D12

All from `userland/wintests/gdi32/data/*.win7.tsv` (VM 6.1.7601, gdi32 6.1.7601.17514, 96 DPI). Columns as in the file header.

### 4.1 Stock objects and UI LOGFONTs (`fonts_sysfont.win7.tsv`, GFNT.SYSFONT)

| object | LOGFONT (GetObject) | tmHeight / asc / desc / intlead / extlead / ave / max | pf (pitch+family) | alphabet extent | truetype |
|---|---|---|---|---|---|
| NCM caption/smcaption/menu/status/message, icon title | -12 Segoe UI 400, cs 1, q 5 | **15 / 12 / 3 / 3 / 0 / 6 / 23** | 0x27 | **347 x 15** | yes |
| DEFAULT_GUI_FONT | -11 MS Shell Dlg 400, cs 0 | **13 / 11 / 2 / 2 / 0 / 5 / 23** | 0x27 | **323 x 13** | yes |
| SYSTEM_FONT | h 16 w 7 wt 700 "System" | 16 / 13 / 3 / 3 / 0 / 7 / 14 | 0x21 | 429 x 16 | no (bitmap) |
| DEVICE_DEFAULT_FONT | same as SYSTEM_FONT | 16 / 13 / 3 / 3 / 0 / 7 / 14 | 0x21 | 429 x 16 | no |
| ANSI_VAR_FONT | h 12 w 9 "MS Sans Serif" | 13 / 11 / 2 / 2 / 0 / 5 / 11 | 0x21 | 323 x 13 | no |
| ANSI_FIXED_FONT | h 12 w 9 "Courier" | 13 / 11 / 2 / 0 / 0 / 8 / 8 | 0x30 | 416 x 13 | no |
| OEM_FIXED_FONT | h 12 w 8 cs 255 "Terminal" | 12 / 10 / 2 / 0 / 0 / 8 / 8 | 0x30 | 416 x 12 | no |
| SYSTEM_FIXED_FONT | h 15 w 8 "Fixedsys" | 15 / 12 / 3 / 3 / 0 / 8 / 8 | 0x30 | 416 x 15 | no |
| NCM sizes | border 5, scroll 17, caption 35x21, sm caption 17x17, menu 19x19 | | | | |

### 4.2 Face / height matrix (`fonts_metrics_dim.win7.tsv`, GFNT.METRICS.005; rows: face, lfHeight, GdiGetCharDimensions ret, height_out = tmHeight, extent52)

| face | -11: tmH / ave-dim / alphabet | -13 | -16 |
|---|---|---|---|
| MS Sans Serif | 13 / 6 / 323 | 16 / 8 / 405 | 20 / 9 / 483 |
| MS Shell Dlg | 13 / 6 / 323 | 16 / 8 / 405 | 20 / 9 / 483 |
| Tahoma | 13 / 6 / 309 | 16 / 7 / 363 | 19 / 9 / 463 |
| Arial | 14 / 6 / 322 | 16 / 7 / 389 | 18 / 9 / 472 |
| Courier New | 14 / 7 / 364 | 16 / 8 / 416 | 18 / 10 / 520 |

(`ret` = GdiGetCharDimensions width = the dialog-unit base; the file also has fixture faces "G32B Fixed/Prop".) MS Shell Dlg is metric-identical to MS Sans Serif
bitmap-sized -11 here (323) and to micross.ttf [inf: Win7's MS Shell Dlg -> Microsoft Sans Serif substitution; `pf 0x27` = TrueType]. MS Sans Serif (real bitmap)
at -11 gives tm 13/11/2/2 ave 5 max 11; as TrueType MS Shell Dlg max is 23.

### 4.3 Per-height TEXTMETRIC rows (`fonts_metrics.win7.tsv` GFNT.METRICS.001, `fonts_map.win7.tsv` GFNT.MAP.002, `fonts_vdmx_walk.win7.tsv`)

Format `h/a/d/il/el/ave/max` at lfHeight -8, -11, -12, -16, -24 (GFNT.METRICS.001):

| face | -8 | -11 | -12 | -16 | -24 |
|---|---|---|---|---|---|
| MS Shell Dlg | 9/7/2/1/0/4/17 | 13/11/2/2/0/5/23 | 13/11/2/2/0/5/23 | 20/16/4/4/0/7/33 | 29/23/6/5/0/11/50 |
| Tahoma | 10/8/2/2/0/4/17 | 13/11/2/2/0/5/23 | 14/12/2/2/0/5/25 | 19/16/3/3/0/7/34 | 29/24/5/5/0/11/51 |
| Arial | 10/8/2/2/0/4/21 | 14/11/3/3/0/5/29 | 15/12/3/3/0/5/32 | 18/15/3/2/1/7/43 | 27/21/6/3/1/11/64 |
| Courier New | 8/6/2/0/0/5/6 | 14/11/3/3/0/7/8 | 15/12/3/3/0/7/9 | 18/14/4/2/0/10/12 | 27/20/7/3/0/14/18 |
| MS Sans Serif (bitmap) | 13/11/2/2/0/5/11 | 13/11/2/2/0/5/11 | 13/11/2/2/0/5/11 | 20/16/4/4/0/8/16 | 29/23/6/5/0/12/24 |

`fonts_vdmx_walk.win7.tsv` has Arial and Courier New at **every** lfHeight from -30 to 40 (3 rows per size); Arial -9..-18:
12/9/3/3/0/4/24, 13/10/3/3/0/4/27, 14/11/3/3/0/5/29, 15/12/3/3/0/5/32, 16/13/3/3/0/6/35, 16/13/3/2/0/6/37, 17/14/3/2/0/7/40, 18/15/3/2/1/7/43,
19/15/4/2/1/8/45, 21/17/4/3/1/8/48 (columns h/a/d/il/el/ave/max). Lucida Console sweep -8..-400 (tm and glyph rows): -12 -> 12/9/3/0/0/7/8; -16 -> 16/13/3/0/0/10/10.
Tahoma/Lucida/Arial/Verdana have thousands of `native`/`hdmx` glyph rows in `fonts_metrics.win7.tsv` (GFNT.METRICS.010).

### 4.4 CreateFontIndirect face mapping (W7: `fonts_map.win7.tsv` realised face; `fonts_create_tm.win7.tsv`)

Requested -> realised face name (the Win7 mapper), all W7:
`NoSuchFace` + FF_ROMAN fixed -> **BatangChe**; FF_ROMAN variable -> **Times New Roman**; FF_SWISS fixed / FF_MODERN fixed -> **Courier New**; FF_SWISS/FF_MODERN
variable and DONTCARE -> **Arial**; empty face -> Arial; "Wingdings" with SYMBOL_CHARSET -> Wingdings, with ANSI_CHARSET -> Arial; "Arial" with SYMBOL_CHARSET ->
**Wingdings**; empty face + SYMBOL -> Wingdings; "MS Sans Serif" with OUT_TT_ONLY (q 7) -> **Arial**; "Courier" with OUT_TT_ONLY -> **Arial**, raster "Courier" keeps Courier;
"Helv" -> keeps name, tm = MS Sans Serif 13/11/2; "Helvetica" -> keeps name, tm = Arial -11 (14/11/3 max 29); "Times" -> tm 14/11/3 ave 4 max 28; "MS Shell Dlg 2" -> tm = Tahoma -11 (13/11/2 ave 5 max 23).
Win7 `-12` request metrics, rows `GFNT.MAP.001`: Shell Dlg 13/11/2/2/0/5/23; Tahoma 14/12/2/2/0/5/25; Arial 15/12/3/3/0/5/32; Courier New 15/12/3/3/0/7/9; NoSuchFace (any) -> Arial 15/12/3/3/0/5/32.

## 5. Win7 vs Win10 on the keys both recorded (same request, same unit)

| key | Win10 (W10-ORACLE) | Win7 (W7) | equal? |
|---|---|---|---|
| Segoe UI -12: tm h/a/d/il/el/ave | 15/12/3/3/0/6 (m199, m208) | 15/12/3/3/0/6 | yes |
| Segoe UI -12: max char width | **31** | **23** | **no** (+8) |
| Segoe UI -12: alphabet | 347 x 15 | 347 x 15 | yes |
| MS Shell Dlg -11: tm h/a/d/il/el/ave/max | 13/11/2/2/0/5/23 | 13/11/2/2/0/5/23 | yes |
| MS Shell Dlg -11: alphabet | 323 x 13 | 323 x 13 | yes |
| MS Shell Dlg 2 -11: tm h/a/d/il/ave | 13/11/2/2/5 | 13/11/2/2/5 | yes |
| MS Shell Dlg 2 -11: max | 27 | 23 | no (+4) |
| MS Shell Dlg 2 -11: alphabet | 309 x 13 | 309 (= Win7 Tahoma -11 309; no direct Shell Dlg 2 extent row) | yes [inf: same substitution target] |
| Default face, lfHeight 0 / -16 | Arial 18/15/2(il)/7/max 43, alphabet 472 | Arial -16: 18/15/3/il 2/el 1/7/43, alphabet 472 | yes [inf: default request is -16-class] |
| `GetStockObject(DEFAULT_GUI_FONT)` | MS Shell Dlg -11 cs 0 | same | yes |
| NCM fonts / icon title | all Segoe UI -12, cs 1 | all Segoe UI -12, cs 1, q 5 | yes |
| NCM menu / scroll | menu 19, scroll 17 | menu 19, scroll 17 | yes |
| NCM caption | 36 x 22 (96) | 35 x 21 | no (+1) |
| NCM small caption | 22 (and SM_CYSMCAPTION 23) | 17 x 17 | no |
| NCM border | 5 (5.2 view) / 1 + padded 4 (6.0 view) | 5 | by view |
| Segoe UI -16 tmHeight | 21, avgw 9 (bucket row) | not recorded | - |
| Segoe UI -15 | 20/16/4/5/0/8/39 | not recorded | - |
| `GetDialogBaseUnits` | 8x16 | system-font based (not recorded as 8x16 text here) | - |
| MS Shell Dlg 8 DS_SETFONT dialog base units | 6x13 | W7 `dlu_base_x=7` for Segoe -12 chrome row | - |

Differences are limited to max char width and some NC metrics. Reason for max width [inf, bb #1139]: the Win7 `segoeui.ttf` staged on i7 is an older font revision than
Win10's 955804-byte file; i7 D12-on reports max 23 = the Win7 number.

## 6. Industrial7's own state (I7; context, not Windows)

| item | D12 off (stand-in) | D12 on, font host (`*.ui.tsv`) | Win10 oracle |
|---|---|---|---|
| Segoe UI -12 tm | 16/12/4/0/0/8/8, alphabet 416 | 15/12/3/3/0/6/23, alphabet 347 | 15/12/3/3/0/6/31, 347 |
| MS Shell Dlg -11 / DEFAULT_GUI_FONT | 8x16 | **Arial -11: 14/11/3/3/0/5/29** (micross missing) | 13/11/2/2/0/5/23 |
| SYSTEM_FONT, ANSI_*, OEM, SYSTEM_FIXED | 16/12/4/0/0/8/8 (all identical) | same stand-in | Win10 not recorded; Win7: 16/13/3/3, 13/11/2, 13/11/2/0, 12/10/2, 15/12/3/3 |
| NCM (I7) | border 1, caption 18, menu 18 | same | border 5/1, caption 22, menu 19 |
| list view (CC30/CC32 diff, bb #1134) | text 16/12/4/0/0/8, header 20, row 20 | not run | header 24, row 19 / 17 |
| MessageBox | 3344 px wide for a short text (8x font inflation) | not run | 123 x 130 |

(Quoted from D12-TRUETYPE-UI.md, `fonts_sysfont.ui.tsv`, bb #1134 and #1139; the I7 rows are measured but are not Windows.)

## 7. What the data already settles, and what it only suggests

Settled (Win10 measured, 6/6 boots identical):
- Segoe UI -12 -> 15/12/3/3, ave 6, alphabet 347; MS Shell Dlg -11 -> 13/11/2/2, ave 5, alphabet 323; MS Shell Dlg 2 -11 alphabet 309; default-face 18/15 alphabet 472.
- List view row 19 (Segoe -12) / 17 (MS Shell Dlg -11); header 24 in both; label x offset 4 (20 with a 16 px image list).
- DEFAULT_GUI_FONT is MS Shell Dlg -11 cs 0; a list view without WM_SETFONT uses Segoe UI -12; NCM fonts Segoe UI -12 (-18 at 144 DPI).
- Real MessageBox is 123x130 for short text; DS_SETFONT 8pt dialogs get base units 6x13.
- Win10 has micross.ttf and the .fon bitmap families on disk.

Settled by Win7 and equal on the overlap (use as the proxy only where flagged): the whole face x {-11,-13,-16} matrix in 4.2, the per-height Arial/Courier New/Lucida rows, the stock-object table in 4.1, and the CreateFontIndirect mapping table in 4.4.

D12 reading of the data: with D12 ON and Win10-compatible font files, Segoe UI matches Win10 on every column except `tmMaxCharWidth` (needs Win10's segoeui.ttf, not Win7's);
MS Shell Dlg -11 needs micross.ttf (otherwise Arial -11 gives tmHeight 14, a 1 px error that propagates to list rows 18 instead of 17 [inf: tmHeight + 4]); the bitmap System/Terminal/Fixedsys/MS Sans Serif/Courier families
must stay bitmap `.fon` (Win10 still ships them and Win7 numbers show they are the non-TrueType 16/13, 12/10, 15/12, 13/11 cells) - the stand-in (16/12/4) is wrong for them and for everything else, but those numbers have not been taken from Win10.

## 8. Still unmeasured on Win10 (prioritised; each can go into one boot)

Priority A (needed to decide D12 and to close the stock-object question):
1. TEXTMETRIC + GetObject LOGFONT + 52-letter extent for **all 7 stock fonts** (SYSTEM_FONT, OEM_FIXED, ANSI_FIXED, ANSI_VAR, SYSTEM_FIXED, DEVICE_DEFAULT, DEFAULT_GUI). Only DEFAULT_GUI is known; SYSTEM_FONT only as "height 16, ave 7" from NPMET `screen_dc_default`.
2. The **face x height matrix** (MS Shell Dlg, MS Shell Dlg 2, MS Sans Serif, Tahoma, Segoe UI, Arial, Courier, Courier New, Terminal, Fixedsys, System, Verdana, Lucida Console, Marlett) x lfHeight {-8,-9,-10,-11,-12,-13,-14,-15,-16,-18,-20,-24,-32} x weight {400,700} x italic: Win10 has only the 3 rows in section 3.1 + 2 coarse ones. (The Win7 shape of this matrix is `fonts_vdmx_walk` / `fonts_metrics`; a Win10 twin is the missing half.)
3. Per-character widths (`GetCharWidth32`, `GetCharABCWidths`, `GetCharWidthI`) for printable ASCII + Latin-1 for the UI faces at 11/12/13/15/16 px, and the same strings via `GetTextExtentPoint32` / `GetTextExtentExPoint` / `DrawText(DT_CALCRECT)`. Win10 has only the 52-letter extent of 4 faces at 1 height, and DWrite design advances.
4. **CreateFontIndirect substitution table on Win10**: realised `GetTextFace` and TEXTMETRIC for NoSuchFace x pitch/family combos, empty face, "MS Shell Dlg"/"MS Shell Dlg 2"/"Helv"/"Helvetica"/"Times"/"Tms Rmn"/"Courier", SYMBOL/OEM/DEFAULT charset, OUT_TT_ONLY/OUT_RASTER, quality flags; and the live `HKLM\...\FontSubstitutes` and `Fonts` / `FontLink\SystemLink` values (the existing Win10 fonts probe read only the Fonts key).
5. **GetTextMetrics max-width** for Segoe UI at other sizes (31 at -12 is the only point) - needed because staged Win7 fonts disagree.

Priority B (comctl32 / user32 consumers):
6. Heights/widths of the other controls at the two fonts: edit, button, combobox (closed/dropped item height), listbox (LB item height), tree-view, tab (`CalcTabHeight`), toolbar (button height), status bar, rebar, tooltip, menu item height / separator, DrawCaption, dialog frame - currently only list view + header + MessageBox + dialog base units.
7. `GetDialogBaseUnits` and `MapDialogRect` for DS_SETFONT dialogs at **9pt Segoe UI (-12)**, 8pt/9pt/10pt MS Shell Dlg 2 / Tahoma / Segoe UI (only 8pt MS Shell Dlg and 8pt Segoe UI are recorded: 6x13); the 7x15 prediction in bb #1139 is untested.
8. List view with explicit fonts at -11/-12/-13/-15/-16 and Tahoma/Arial/Courier (only Segoe -12, MS Shell Dlg -11, Segoe -15); LVS_REPORT, LVS_LIST, small icon, tile view row heights; `LVM_APPROXIMATEVIEWRECT`.
9. Non-96 DPI: TEXTMETRIC of the DPI-scaled NCM fonts (Segoe -18 at 144) at 120/144/192 and system-aware / per-monitor modes; only SM_* and the LOGFONT heights are known.

Priority C (rendering and long tail):
10. Bitmap-font behaviour: GetTextMetrics/extent of MS Sans Serif / Small Fonts / Terminal / Fixedsys / Courier at all their strike sizes, and the size-snapping rule for a requested size with no strike.
11. Kerning pairs, GetFontData table sizes (`hdmx`, `VDMX`, `gasp`, `head`) per UI face; glyph indices (`GetGlyphIndices` with the missing-char rule); `GetOutlineTextMetrics` (otmTextMetrics, otmEMSquare, otmAscent, otmsXHeight).
12. ExtTextOut / DrawText pixel results (greyscale small bitmaps / hashes) for the UI faces at 3 heights with ClearType off/on (`lfQuality`), alignment and clipping, `TA_*`, `SetTextCharacterExtra`, `SetTextJustification`, and the system text smoothing SPIs (the W7 values 1/2/1200/1/1 are recorded; Win10's are not).
13. The unexplained MessageBox `tm_h=13 tm_ave=6 lf_h=-11` datum: re-record with the `GetTextFace` name of that static control's font.
14. Font enumeration order/charsets on Win10 (113 GDI families known by count only; per-face names/styles/charsets not recorded), FontLink chains on Win10 (all W7).
15. Ability to cross-check the W7 proxy: record the Win10 twin of `fonts_sysfont`, `fonts_map`, `fonts_metrics_dim`, `fonts_widths`, `text_extent`, `fonts_create_tm` (same programs, same row format) so each Win7 row has a Win10 neighbour.

## 9. Reproduction notes (everything above is re-derivable)

- Depot: `cd ~/w10lab/depot && for m in 199 208 188 196 103 201 202 200 79; do bb depot get m$m; done` (tarballs; `tar xzf X -C x/X`). The probe exes are `m192 m205 m185 m189 m101 m194 m195 m193`.
- NPMET rows: `grep '\.font\.' x/task60-np-metrics-runs/std1/out.txt`; CC30: `grep 'CC32REC lv_geometry_v6' x/task66-runs/std1/out.txt` (text rows `|base|1|15|12|3|0|3|6|96`, field order in the header row `lv_geometry_v6#`).
- Win7: `userland/wintests/gdi32/data/{fonts_sysfont,fonts_metrics_dim,fonts_metrics,fonts_map,fonts_vdmx_walk,fonts_width,fonts_create_tm,text_extent,text_charset_font}.win7.tsv` and `userland/wintests/comctl32/data/ui_fonts*.win7.tsv`.
- Win10 committed oracle files: `userland/wintests/win10/{win10_fonts_probe.win10.txt,win10_dwrite_s3_probe.win10.txt,win10_dwrite_rows.win10.txt,expected/win10_dwrite_glyph_probe.win10.txt}`, `data/dlg_paint_probe.win10-oracle-*.txt`, `crates/w10-comctl32-lv/tests/data/w10_comctl915_probe.tsv`.
- Local copies of the bb topic texts: `/home/iceisfun/w10lab/bbtxt/{1139,1134,915,1051,1077,1063,919,555}.txt`.
- This lane ran no VM, no Wine and no probe; the lab was ready (`LAB_READY` present) but unused by this role.
