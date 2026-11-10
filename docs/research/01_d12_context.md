# 01 - D12 context: what Industrial7 has decided, what it measured, what this suite must record

Role: D12 CONTEXT. Sources are cited inline. Every number below is labelled:

- **[W10]** measured on Windows 10 22H2 (bb depot run, or the Win10 decomp corpus for code facts);
- **[W7]** measured on the Win7 SP1 reference VM (Industrial7 `*.win7.tsv` recordings);
- **[i7]** measured on Industrial7 (recordings or bb runs);
- **[doc]** stated by an Industrial7 design doc (not re-measured here);
- **[inf]** inference. Not a measurement; something this suite has to confirm.

Industrial7 tree read: `/home/iceisfun/work/industrial7` at `25af4cbccd`. Win10 decomp corpus:
`http://docker1.dev.moonlightcompanies.com:8765` (Win10 22H2 19041.2965). Depot runs fetched and read
for this note: `m199` (task60-np-metrics-runs.tar.gz, sha256 5e4ab28c...a74ee) and `m208`
(task66-runs.tar.gz, sha256 3fd7fd5d...9c1b7b1). bb topics: #1134 (7zFM list view, CC28-CC33),
#1139 (Notepad++ NP probes).

Units in this note: all heights and widths are **device pixels at 96 DPI** unless stated.
`lfHeight -12` is a *character (em) height* request (negative = em/ppem); `tmHeight` is the *cell
height* (ascent + descent = em + internal leading). Row and header heights are rectangle heights in
the list view's client coordinates.

---

## 1. The knob (what D12 is, exactly)

**D12** = `\industrial7\gdi-truetype-ui` (ESP/registry knob; xtask env `I7_GDI_TRUETYPE_UI=on`).
Source: `docs/fonts/DESIGN.md` line 161 (D12 row), `docs/design/gdi32/D12-TRUETYPE-UI.md`,
`kernel/kmain/src/win32k/font/mod.rs` 621-633 and 742-747.

- **Off (the default, current state).** `font::realise` returns `None` (= the built-in stand-in)
  when `!TRUETYPE_UI && is_ui_family(face)` (mod.rs 744-746). The UI family list is
  `i7font_abi::map::UI_FAMILIES` = System, MS Shell Dlg, MS Shell Dlg 2, MS Sans Serif, Tahoma,
  Segoe UI, Courier, Terminal, Fixedsys, plus the empty name (`crates/i7font-abi/src/map.rs`
  249-259). No FontSubstitutes are read, and no default face is used. Any other name that is a catalog
  family (Arial, Courier New, Times New Roman, ...) is realised as TrueType even with the knob off.
- **On.** Win7's mapping: (1) HKLM `FontSubstitutes` read once (32 entries in the gate hive;
  MS Shell Dlg 2 -> Tahoma, MS Shell Dlg -> Microsoft Sans Serif, Helv -> MS Sans Serif,
  Helvetica -> Arial ...); (2) a name that is, or substitutes to, a Win7 bitmap `.fon` family
  (System, Terminal, Fixedsys, Courier, MS Sans Serif, MS Serif, Small Fonts) keeps the stand-in
  (i7 stages no `.fon`); (3) otherwise the face by name, else Win7's default family by
  pitch/family/charset (`map::win7_default_family`: empty or unknown name -> Arial; fixed pitch or
  FF_MODERN -> Courier New; FF_ROMAN -> Times New Roman; SYMBOL_CHARSET -> Wingdings). [doc]
- **Consequence of (3):** MS Shell Dlg -> Microsoft Sans Serif is *not in the base font set*
  (no `micross.ttf`), so with the knob on MS Shell Dlg, and with it DEFAULT_GUI_FONT, fall back to **Arial**. [doc]
- **Not affected by the knob:** captions and menu bars (drawn by the kernel's own non-client path,
  still on the stand-in with the knob on); the `.fon` stock objects (SYSTEM_FONT, ANSI_VAR_FONT,
  ANSI_FIXED_FONT, OEM_FIXED_FONT, SYSTEM_FIXED_FONT, DEVICE_DEFAULT_FONT stay on the stand-in,
  "not TrueType, as on Win7"). [doc]
- **Knob scope:** global (kernel `AtomicBool`). A Win10-personality-only flip was rejected
  because one font object would realise differently per calling thread (bb #1139, laneb F1). [doc]

### Decision status

| item | state | source |
|---|---|---|
| D12 flip | **NOT flipped. An open operator decision.** The code part is two lines (`TRUETYPE_UI` default true; xtask `ui_column()`/`fonts_rows()` treat unset as on) plus docs | D12-TRUETYPE-UI.md "The flip"; FLIP-CHECKPOINT-M3.md; sow/FONT.md KF4, D2 ("knob unchanged") |
| prepared branches | `gdi32-owned/p9-flips` 720be4c166 (D12 + KG2), `gdi32-owned/p9-flips-micross` 324d17594c (micross staged), `p10-kg2-flips` 41084fd4bd; **stale**: on land8 head eca4f3505c, 9 / 9 / 20 merge conflicts against dwrite-owned/m3 | FLIP-CHECKPOINT-M3.md |
| earlier verdicts | "KG2 GO, D12 GO, micross GO after D12", given on eca4f3505c; must be re-run before applying | FLIP-CHECKPOINT-M3.md step 3 |
| micross.ttf staging | not staged; sow/FONT.md D2 working default: "no extra families until sized" | D12-TRUETYPE-UI.md "Open decisions"; GAPS-CLOSED.md |
| `.fon` fonts | none staged; the stand-in answers for SYSTEM_FONT etc. | D12-TRUETYPE-UI.md |
| re-baseline cost | knob-on column (font host) moves 26 gdi32 tables + `env.ui.tsv`; 448 test-data files on p9-flips | D12-TRUETYPE-UI.md; FLIP-CHECKPOINT-M3.md |

Every Industrial7 D12 number so far is judged against **Win7 SP1** (the `.win7.tsv` column). Industrial7
now also has a **Win10 personality** (w10-comctl32, user32-w10, ...). The 15/13 and 19/17 questions
come from that Win10 work (CC29-CC33, NP probes). This suite exists to supply the Win10 column.

---

## 2. The stand-in (what D12-off produces today)

Source: `kernel/kmain/src/win32k/raster.rs` 96-97 (`GLYPH_W = 8`, `GLYPH_H = 16`), the i7 recording
`userland/wintests/gdi32/data/fonts_sysfont.tsv`, and bb #1134/#1139 i7 runs.

[i7] For **every** UI face and every height (Segoe UI -12, MS Shell Dlg -11, System, Fixedsys,
the empty name), GetTextMetrics returns the same cell:

    tmHeight 16  tmAscent 12  tmDescent 4  tmInternalLeading 0  tmExternalLeading 0
    tmAveCharWidth 8  tmMaxCharWidth 8  first/default/break chars 0x20/0xff/0x7f/0x20  pitch&family 0x34
    52-letter alphabet extent 416 x 16 (8.0 px per letter)

Dialog base units (GetDialogBaseUnits) are 8x16 [i7], equal to Win10's global value 8x16 [W10, m199
`dpi.dialogbaseunits cx=8 cy=16`]. Changing the *global* value would introduce a mismatch. The difference
is in per-dialog fonts (bb #1139 note).

Effect on comctl32 (Win7 column triage, `docs/design/comctl32/win7/README.md`): **852 + 13 rows**
differ only because "the stock UI fonts are one 8x16 cell on i7 (every size, rectangle, text width,
item height, rows per page and mouse position a control derives from text)". A further 124 rows
(owner-data `LVN_GETDISPINFO` count) and 11 + 6 rows (month calendar / header clicks) "follow the font". [doc]

---

## 3. The 15/13 text heights and 19/17 row heights

### 3.1 What they are

| quantity | value | control / font | source of truth |
|---|---|---|---|
| **15** | tmHeight of **Segoe UI lfHeight -12, weight 400** (the list view's default font when no WM_SETFONT; = SPI_GETICONTITLELOGFONT) | SysListView32 (v6) default font; also NONCLIENTMETRICS caption/menu/status/message fonts | [W10] m208 CC30 `base` row `15\|12\|3\|0\|3\|6\|96`; [W10] m199 `font.message/caption/segoeui_m12 height=15 ascent=12 intlead=3 ave=6 max=31 alpha_cx=347`; [W7] fonts_sysfont.win7.tsv `h=15 a=12 d=3 il=3 ave=6 max=23` |
| **13** | tmHeight of **MS Shell Dlg lfHeight -11** (= DEFAULT_GUI_FONT's LOGFONT) | list view after WM_SETFONT(DEFAULT_GUI_FONT); every dialog using MS Shell Dlg 8 pt | [W10] m208 CC30 `guifont` row `13\|11\|2\|0\|2\|5\|96`; [W10] m199 `msshelldlg_m11 height=13 ascent=11 intlead=2 ave=5 max=23 alpha_cx=323`; [W7] DEFAULT_GUI_FONT `h=13 a=11 d=2 il=2 ave=5 max=23`, alpha 323x13 |
| **19** | report-view **row height** with the default font (Segoe UI -12) | SysListView32 LVS_REPORT, item 0 LVIR_BOUNDS 0,24,280,43; rows 24-43, 43-62, 62-81; no header: 0-19, 19-38, 38-57 | [W10] m208 CC30 (6/6 boots byte-identical, std == adm) |
| **17** | report-view **row height** with MS Shell Dlg -11 | same, guifont fixture: rows 24-41, 41-58, 58-75 | [W10] m208 CC30 |
| **24** | **header height** (every fixture with a header, both fonts) | SysHeader32 inside the list view: header rect 0,0,500,24; HDM_LAYOUT cy 24 | [W10] m208 CC30 |
| i7 today | text 16, row 20, header 20 in all fixtures | stand-in | [i7] bb #1134 CC30/CC31/CC32 i7 rows |

[W10] Other CC30 inputs: SM_CYBORDER 1, SM_CXBORDER 1, SM_CYEDGE 2, SM_CXEDGE 2, SM_CXSMICON 16,
SM_CYSMICON 16, SM_CYFIXEDFRAME 3; label rect x starts at 4 (20 with a 16x16 small image list);
LOGPIXELSY 96. WM_GETFONT with no WM_SETFONT returns a real font, GetObject 92 bytes, Segoe UI
-12, weight 400, charset 1 (DEFAULT_CHARSET).

### 3.2 Why row = text + 4 and header = 24 (Win10 code)

From the Win10 22H2 comctl32 decompile (corpus API; code facts, the values are inferred):

- `CLVView::ComputeCYItemSize` (comctl32 RVA 0x86678): `cy = max(drawstate.textCy(+0x14),
  drawstate.imageCy(+0xc)) + GetSystemMetrics(SM_CYBORDER=6)`. In views 1-2 (view id at
  `+0xa4`, `(view-1) < 2`) it is raised to `textCy + 2 * ComputeEditYPadding()`, and +SM_CYBORDER
  again when view == 2 and `CLVThemesManager::HasCustomTheme`.
- `CLVInPlaceEditingManager::ComputeEditYPadding` (RVA 0x1a6a8): creates a temporary edit control
  (`_CreateEditWnd` with the list's draw state) and returns the **top of its EM_GETRECT (0xB2)
  formatting rect**. So the padding comes from the edit control's margins *in that font*, not from a
  constant.
- So 19 = 15 + 2*2 and 17 = 13 + 2*2 **[inf]**: EditYPadding = 2 at both fonts, and text cy =
  tmHeight. CC30 does not record EditYPadding or the draw-state text cy directly. lanea's i7
  crate encodes the same formula (`crates/w10-comctl32-lv/src/lv/layout.rs` 139-153,
  edit_y_padding = 2 from `lv_host.rs` 734). With the right GDI metrics it already gives 19/17. [doc,
  bb #16657]
- `Header_OnLayout` (RVA 0x4b2a8): `cy = textCy(+0x4c) + 2 * GetLegacyScaledEdgeHeight()`, raised
  to the theme min height (`+0x160`) when a theme is open (`+0x130`). `Header_NewFont` sets
  `+0x4c = GetTextExtentPointW("...").cy`. `_Header_InitTheme` sets `+0x160 = max(
  GetThemePartSize(theme "Header", part 1 = HP_HEADERITEM, state 0, TS_TRUE).cy,
  GetThemePosition(part 1, prop 0xd4b).y)`.
- So **the 24 is a theme size, not a font size** [inf, strongly code-grounded]: 15 + 4 = 19 and
  13 + 4 = 17 are both below 24, so the themed minimum wins for both fonts. On i7 the header host
  reports `Theme::default()` (active false, min_height 0) and uxtheme-w10 is themeless
  (OpenThemeData NULL), so i7's header = 16 + 4 = 20. D12 alone would give i7 **19 / 17 headers,
  not 24** [inf]. The header needs a recorded theme value (bb #16659 ranked item 4).

### 3.3 Which gaps D12 closes for the list view (bb #1134 ranking)

1. D12 flip: text metrics -> 15 / 13, rows -> 19 / 17 [inf from W7 knob-on data and the formula].
2. micross.ttf: MS Shell Dlg -11 -> 13 instead of Arial's 14 (see section 4).
3. comctl32 default font (no WM_SETFONT -> create the icon-title font): **done** on
   `lanea/lv-default-font` 328b8700d4. CC31 shows exactly the 4 font rows changed, now equal to Win10.
4. Header 24: needs HP_HEADERITEM part size / theme data, independent of D12.
5. LVS_NOCOLUMNHEADER: `lanea/lv-nohdr-header` 1c41e07fc5 (CC32/CC33: nohdr layout now equal).

Residual after 3+5 (bb #1134, CC33): 44 differing row pairs against Win10, all D12-downstream (text
metrics, row heights 20 vs 19/17) or header 20 vs 24. NOT RUN anywhere: D12 knob on with the
Win10 personality, admin identity on i7, any DPI other than 96.

---

## 4. micross.ttf and the MS Shell Dlg -11 "1 px off" issue

| column | MS Shell Dlg -11 (DEFAULT_GUI_FONT) realised as | tmHeight / ascent / descent / intlead | ave / max | alphabet extent | source |
|---|---|---|---|---|---|
| Win7 SP1 | Microsoft Sans Serif (FontSubstitutes) | 13 / 11 / 2 / 2 | 5 / 23 | 323 x 13 | [W7] fonts_sysfont.win7.tsv GFNT.SYSFONT.003 |
| Win10 22H2 | not recorded: m199's `face=MS_Shell_Dlg` is the requested name (the API behind it is not checked); m208 is the LOGFONT. GetTextFace / OTM names are still to be recorded | 13 / 11 / 2 / 2 | 5 / 23 | 323 x 13 | [W10] m199 `msshelldlg_m11`; m208 `guifont` |
| i7 D12 off | stand-in | 16 / 12 / 4 / 0 | 8 / 8 | 416 x 16 | [i7] fonts_sysfont.tsv |
| i7 D12 on, no micross | **Arial -11** (default-family fallback) | **14** / 11 / 3 / 3 | 5 / 29 | 322 x 14 | [i7] fonts_sysfont.ui.tsv (marked `gap:K-FONT ... tmHeight got=0xe want=0xd`) |
| i7 D12 on + micross (p9-flips-micross) | Microsoft Sans Serif | expected = Win7 [doc: "would make MS Shell Dlg and DEFAULT_GUI_FONT Win7's (h 13 instead of Arial's 14)"] | | | D12-TRUETYPE-UI.md; not re-measured on a current head |

So "1 px off even with D12 on" means: **tmHeight 14 (Arial) vs 13 (Microsoft Sans Serif)**,
ascent equal (11), descent and internal leading +1. The average width (5) and the alphabet (322 vs
323) are already almost equal. Consequences [inf]: list view rows 18 instead of 17; every dialog
that uses MS Shell Dlg (DS_SETFONT 8 pt) gets dialog base unit y = 14 instead of 13, which scales
every DLU height by 14/13. Also, Win10's MS Shell Dlg tmMaxCharWidth is 23, while Arial -11 gives 29.

Data Industrial7 already holds for micross: `docs/design/dwrite/win7-fonts.manifest.tsv` row
`micross.ttf 652664 32a9b170...abe3d8 Microsoft Sans Serif/Regular 400 Version 5.02` (the Win7 RTM
WIM). The same sha256 is in `docs/design/gdi32/fontlink-fonts.sha256` (role: "MS Shell Dlg's
FontSubstitutes target; MS UI Gothic, Gulim, MingLiU chains"). Win7 FONTLINK measurement: the
linked em equals the base em for Microsoft Sans Serif at lfHeight -30..-6 and 6..30, except the
11 -> 12 and 13 -> 15 steps (FONTLINK.md 1). Staging it also adds a family to the DirectWrite
system collection (11 -> 12 families, CHROME-M3.md G5).

Open question for this suite: Win10's micross.ttf version and hash, and whether its metrics at
-8..-16 equal Win7's v5.02 (they do at -11 for tm and alphabet, per the table above).

---

## 5. Win10 vs Win7 font files: a trap for "equal to Win7"

- [W10] Segoe UI -12 tmMaxCharWidth = **31**. [W7] and [i7 knob on] = **23**. i7 stages the
  **Win7 RTM** `segoeui.ttf` (v5.01, sha256 38be0d2b...6a8c1). bb #1139 F1: "only max differs (23 vs
  31: the staged file is Win7's segoeui.ttf)". Height, ascent, intlead, ave and the 347 alphabet are equal.
- [W10] MS Shell Dlg 2 -11 (-> Tahoma): height 13, ave 5, max 27, alphabet 309.
- [W10] lfHeight 0 with an empty face: Arial, height 18, ascent 15, intlead 2, ave 7, max 43, alphabet 472.
  (Win7 rule: empty -> Arial [doc].)
- [W10] Screen DC default font (System): height 16, ave 7. [W7] SYSTEM_FONT h 16 a 13 d 3 il 3 ave 7
  max 14 (bitmap vgasys.fon), alphabet 429x16.
- FONT-ENUM.md 5: i7 base faces are RTM files while the Win7 VM has SP1's. Arial, Wingdings and Symbol
  head boxes differ by 1 px at em 32; Courier New entry 26+10 vs 27+9; Segoe UI/Tahoma charset bits differ.

So a D12 "PASS against Win10" needs **per-file provenance**: every recorded Win10 metric row must
carry the font file version and sha256 it came from. Max-width, ABC and kerning rows will differ
wherever the Win10 file differs from the staged Win7 file. That is a file difference, not a mapper
bug, and the triage must be able to tell the two apart.

---

## 6. What Industrial7 already has (data inventory)

| asset | path | content |
|---|---|---|
| base font set (staged on i7) | `images/components/fonts-base.i7` | 29 Win7 RTM faces (segoeui/b/i/z, tahoma/bd, arial x4, times x4, cour x4, verdana x4, consola x4, lucon, symbol, wingding) + Inter Regular/SemiBold. **No micross, no .fon** |
| Win7 font manifest | `docs/design/dwrite/win7-fonts.manifest.tsv` | 436 files of the Win7 RTM WIM: bytes, sha256, sweep flag, family/subfamily, weight, version (incl. all `.fon`: vgasys, sserife, coure, ...) |
| link fonts | `docs/design/gdi32/fontlink-fonts.sha256`, `FONTLINK.md` | sha256 + size + SystemLink role for msgothic, mingliu, simsun, gulim, meiryo, msyh, msjh, malgun, mangal, seguisym, **micross** |
| font catalog/manifest format | `i7font_abi::manifest`, `C:\Windows\System32\i7fonts.base` (written by `host/i7image/src/fontbase.rs`) | the catalog's face list |
| Win7 recordings (gdi32) | `userland/wintests/gdi32/data/*.win7.tsv`: `fonts_sysfont`, `fonts_map`, `fonts_map_px`, `fonts_map_tf`, `fonts_metrics`, `fonts_metrics_buf`, `fonts_metrics_dim`, `fonts_metrics_px`, `fonts_widths`, `text_extent`, `fonts_vdmx_walk`, `fonts_width`, ... | Win7 SP1 6.1.7601 column; the `.ui.tsv` files are the i7 knob-on (font host) column; the plain `.tsv` are the i7 default column |
| Win10 recordings (bb depot) | m199 NP metrics (220 rows), m200 icon, m201 msgbox, m202 wndsize, m188 CC29 (LV autosize), m208 CC30 (LV geometry, 72 rows), m210/m212/m216 (later tasks) | 6 boots each (std x3, adm x3), byte-identical; 96 DPI only |
| row/record format precedent | `fonts_sysfont.win7.tsv` | `CASE/sub <TAB> call <TAB> fields <TAB> "face"`, e.g. `GFNT.SYSFONT.002/lfCaptionFont GetTextMetricsW 1 h=15 a=12 d=3 il=3 el=0 ave=6 max=23 w=400 oh=0 asp=96,96 ch=0x..,0x..,0x..,0x.. it=0 ul=0 so=0 pf=0x00000027 cs=0 "Segoe UI"` |

Not yet held anywhere (as far as this read found): any **Win10** row at a DPI other than 96;
Win10 font file versions/hashes; Win10 FontSubstitutes / SystemLink registry contents; Win10
per-character widths / ABC / extents for the UI faces at heights other than the default; Win10
theme part sizes (HP_HEADERITEM etc.); Win10 EditYPadding per font; Win10 MapDialogRect for
DS_SETFONT dialogs.

---

## 7. Open questions -> the measurement that settles each

Q1. **Turn D12 on?** It is settled if Win10's GetTextMetrics / extents / widths for the UI faces equal
what i7 produces with the knob on (Win7 RTM files) closely enough, and the residue can be attributed
to file versions. Measure on Win10: full TEXTMETRICW, OUTLINETEXTMETRIC key fields,
GetTextExtentPoint32 of fixed strings, GetCharWidth32 / GetCharABCWidths 0x20-0x7E (+ a few
non-ASCII), GetTextFace, GetObject LOGFONT, GdiRealizationInfo flags, for each UI face x heights x
weights x DPIs, plus the font file version + sha256 that served it.

Q2. **15/13 text heights**: confirmed at 96 DPI [W10]. Still needed: the same at 120/144/192 DPI and
for the DPI-awareness contexts (per-monitor v2 vs unaware), since comctl32 v6 scales with
DPISCALEINFO.

Q3. **19/17 row heights**: confirmed at 96 DPI [W10]. Still needed: the inputs, recorded next to the
outputs, so the formula is checked rather than assumed: EditYPadding (EM_GETRECT top of an edit
control in that font), draw-state text cy (expected tmHeight), row height in LVS_REPORT / LVS_LIST
/ LVS_SMALLICON / LVS_ICON, with/without small image list, LVS_EX_GRIDLINES (+1?), for each UI font
and DPI.

Q4. **Header 24**: GetThemePartSize("Header", HP_HEADERITEM, 0, TS_TRUE), GetThemePosition(1, 0,
0xd4b), other header parts read by `_Header_InitTheme` (parts 4, 5, 7), the HDM_LAYOUT cy for each
font and DPI, and the classic (theme off / high contrast) header height.

Q5. **micross for MS Shell Dlg -11**: Win10 FontSubstitutes values; GetTextFace / GetObject /
OUTLINETEXTMETRIC otmpFamilyName / otmpFullName for MS Shell Dlg at -8..-16 (proves the realised
face is Microsoft Sans Serif); TEXTMETRIC + widths of MS Shell Dlg, Microsoft Sans Serif and Arial
at the same heights side by side (shows how far the Arial fallback is from the real face at each
size, not only at -11); micross.ttf version and sha256 on Win10.

Q6. **CreateFontIndirect face substitution** (beyond MS Shell Dlg): for each requested name (the
UI families, FontSubstitutes keys, unknown names, empty name) x charset x pitch/family, the realised
face (GetTextFace), the GetObject LOGFONT, and TEXTMETRIC. Settles `win7_default_family` against
Win10 and the `.fon` rule (System, Terminal, Fixedsys, Courier, MS Sans Serif, MS Serif, Small Fonts
-> bitmap on Win10? tmPitchAndFamily TMPF_TRUETYPE bit, GdiRealizationInfo flags).

Q7. **Stock objects** (DEFAULT_GUI_FONT, SYSTEM_FONT, ANSI_VAR_FONT, ANSI_FIXED_FONT, OEM_FIXED_FONT,
SYSTEM_FIXED_FONT, DEVICE_DEFAULT_FONT): GetObject + TEXTMETRIC + alphabet extent on Win10 (the
GFNT.SYSFONT.003 shape) at each DPI. Sets the expected values for the stand-in/`.fon` question,
since D12 keeps them on the stand-in.

Q8. **Every comctl32 control, not just the list view**: per control, the text-derived geometry
with the control's default font and with DEFAULT_GUI_FONT: tree view item height (TVM_GETITEMHEIGHT),
tab item size (TCM_GETITEMRECT), toolbar button size (TB_GETBUTTONSIZE), status bar height, header
height, combo box item/selection heights (CB_GETITEMHEIGHT -1/0), list box item height
(LB_GETITEMHEIGHT), edit EM_GETRECT, button ideal size (BCM_GETIDEALSIZE), tooltip size. Record the
font's TEXTMETRIC next to each so a later i7 diff separates "font input" from "formula".

Q9. **Dialog sizing**: GetDialogBaseUnits (global, 8x16 [W10]) vs MapDialogRect({0,0,4,8}) on a
real DS_SETFONT / DS_SHELLFONT dialog in MS Shell Dlg 8 and Segoe UI 9. Expected 7x15 for Segoe UI
if the 52-letter rule holds [inf, bb #1139]; MS Shell Dlg 8: alphabet 323/52 = 6.2 -> 6, cy 13 [inf].
Also gpsi's message-font char cell (GetCharDimensions equivalent; i7 sets Segoe UI 7x15 with the knob
on [doc]).

Q10. **Rendering, as a cross-check only**: small greyscale glyph bitmaps (GetGlyphOutline GGO_GRAY8 /
GGO_BITMAP) and ExtTextOut into a 1-bpp / 32-bpp DIB for a short string in each UI face at 96 DPI,
hashed. Settles whether "metrics equal" also means "same pixels" (hinting / hdmx / VDMX). ClearType
vs grayscale differences (H-W7-SMOOTH) are out of D12's scope but cheap to record under
NONANTIALIASED / ANTIALIASED / CLEARTYPE quality.

---

## 8. DATA REQUIREMENTS (question -> measurement -> format)

Axes used below (the minimum matrix, extendable):

- **FACES:** Segoe UI, MS Shell Dlg, MS Shell Dlg 2, Tahoma, Microsoft Sans Serif, MS Sans Serif,
  Arial, Courier, Courier New, System, Fixedsys, Terminal, "" (empty), plus one unknown name.
- **HEIGHTS (lfHeight):** -8, -9, -10, -11, -12, -13, -14, -15, -16, -20, -24, 0, and the cell
  forms +13, +15, +16. At 96 DPI, -11 = MS Shell Dlg 8 pt and -12 = Segoe UI 9 pt.
- **WEIGHTS:** 400, 700. **ITALIC:** 0 (1 for Segoe UI / Tahoma / Arial only).
- **DPI:** 96 is mandatory, then 120, 144, 192 (system DPI set before boot). Also record the process
  awareness (unaware / system / per-monitor v2), because the NP probe exe is subsystem 5.2 and
  that changes system metrics [doc bb #1139 F3].
- **IDENTITY:** std user; admin once per boot as a cross-check (CC30: std == adm).

Every file starts with a header block: Windows build (`RtlGetVersion` + UBR), gdi32/gdi32full/
win32kfull/comctl32/uxtheme file versions, LOGPIXELSX/Y, awareness context, font-smoothing SPI
values, and the probe exe's sha256. Rows are one per line, tab-separated, `KEY<TAB>field=value ...`,
fixed field order, integers in decimal (flags and chars in 0x%08x), sorted by key. Same key = same
measurement across Windows, Wine and i7, so `diff` works.

| # | question | measurement | record (format) |
|---|---|---|---|
| R1 | Q1 Q5 provenance | for every file in `C:\Windows\Fonts` that serves a FACE: file name, bytes, sha256, name ID 1/2/4/5 (family, subfamily, full name, version), OS/2 usWeightClass, head.unitsPerEm, presence of hdmx / VDMX / kern / EBDT; the HKLM `Fonts` value that names it | `fontfiles.tsv`: `file<TAB>bytes<TAB>sha256<TAB>family/sub<TAB>weight<TAB>version<TAB>upem<TAB>tables` (same columns as win7-fonts.manifest.tsv plus upem/tables). Files themselves never committed |
| R2 | Q5 Q6 | registry: `FontSubstitutes` (all values), `FontLink\SystemLink` (all values, REG_MULTI_SZ joined by `\|`), `GRE_Initialize` / `FontLinkDefaultChar` / `FontLinkControl` if present | `registry_fonts.tsv`: `key<TAB>value_name<TAB>type<TAB>data`, sorted |
| R3 | Q1 Q2 Q5 | for FACES x HEIGHTS x WEIGHTS x DPI: CreateFontIndirectW -> GetObjectW LOGFONTW (all fields), GetTextFaceW, GetTextMetricsW (all 20 fields), GdiRealizationInfo / GetFontRealizationInfo flags, GetOutlineTextMetricsW key fields (otmEMSquare, otmAscent, otmDescent, otmLineGap, otmsCapEmHeight, otmsXHeight, otmrcFontBox, otmpFamilyName, otmpFaceName, otmpFullName) | `textmetrics.tsv`: key `TM/<dpi>/<face>/<h>/<w>/<it>`; `lf=... face="..." tm h= a= d= il= el= ave= max= w= oh= asp= ch=0x..,0x..,0x..,0x.. it= ul= so= pf= cs= ri=0x.. otm=...` (the fonts_sysfont.win7.tsv field spelling, so Win7/Win10/i7 rows line up) |
| R4 | Q1 | same matrix: GetCharWidth32W and GetCharABCWidthsW for 0x20-0x7E and a fixed list of 32 non-ASCII chars (Latin-1, U+2026, U+20AC, U+2122, CJK one char -> linking); GetCharWidthI / GetCharABCWidthsI for glyph indices of those chars | `widths.tsv`: key `CW/<dpi>/<face>/<h>/<w>`; `w=<95 comma-separated ints>`, `abc=a:b:c,...`; plus `sha256=` of the packed int32 array for a quick equality check. Big raw arrays compressed if large |
| R5 | Q1 Q9 | GetTextExtentPoint32W / GetTextExtentExPointW (nFit + partial extents at max 100 px) for a fixed string set: the 52-letter alphabet, "...", "Hello world", "Name", "Size", "Modified", digits "0123456789", "W"x10, "i"x10, a 60-char sentence, a Tahoma/Segoe DLU string, "&File", a CJK string. Also SetTextCharacterExtra 1 and SetTextJustification (Tahoma -13 "Hello world" 99/71 Win7 [doc]) | `extents.tsv`: key `EXT/<dpi>/<face>/<h>/<w>/<string-id>`; `cx= cy= fit= dx=<csv>`; string table in `strings.tsv` (`id<TAB>utf-16 hex<TAB>printable`) |
| R6 | Q7 | stock objects GetStockObject(0..17 fonts) + the NONCLIENTMETRICS / SPI_GETICONTITLELOGFONT LOGFONTs, each realised: R3 fields + alphabet extent + truetype flag | `stockfonts.tsv` using the GFNT.SYSFONT.001-004 row shapes verbatim (`<case>/<object><TAB>GetObjectW<TAB>...`) |
| R7 | Q3 | list view (v6, manifest-activated): for the default font, DEFAULT_GUI_FONT, Segoe UI -12, MS Shell Dlg -11, Tahoma -11, Arial -11/-12, Microsoft Sans Serif -11, each DPI: TEXTMETRIC of the WM_GETFONT font; EditYPadding (EM_GETRECT of an Edit created with that font: rect.top/left, and EM_GETMARGINS); LVM_GETITEMRECT LVIR_BOUNDS/LABEL/ICON rows 0-2 in LVS_REPORT, LVS_LIST, LVS_SMALLICON, LVS_ICON; with/without small image list; LVS_EX_GRIDLINES / FULLROWSELECT / CHECKBOXES; LVM_GETITEMSPACING; LVM_APPROXIMATEVIEWRECT 3 items | `lv_geometry.tsv`, CC30's field order extended: `LV/<dpi>/<font>/<view>/<ex>/<fixture><TAB>font=... tm=h\|a\|d\|el\|il\|ave\|dpi pad=t,l hdr=... rows=r0;r1;r2 label=...` |
| R8 | Q4 | uxtheme on the list view's header: OpenThemeData(hdr,"Header"); GetThemePartSize(HP 1..7, state 0, TS_MIN/TS_TRUE/TS_DRAW) cx/cy; GetThemePosition(1,0,0xd4b); GetThemeMargins / GetThemeInt for HP_HEADERITEM where defined; HDM_LAYOUT cy for each font/DPI; same with SetWindowTheme(hdr,L"",L"") (classic) | `theme_header.tsv`: `TH/<dpi>/<part>/<state>/<ts><TAB>hr=0x.. cx= cy=`; `HL/<dpi>/<font>/<themed\|classic><TAB>cy= textcy=` |
| R9 | Q8 | per comctl32 control (tree, tab, toolbar, status, header, combo, combo ex, list box, edit, button, static, tooltip, up-down, trackbar, date picker, month calendar): the font-derived sizes listed in Q8, default font and DEFAULT_GUI_FONT, each DPI, plus TEXTMETRIC of the font used | `cc_geometry.tsv`: `CC/<dpi>/<class>/<font>/<metric><TAB>value(s)` |
| R10 | Q9 | dialogs: GetDialogBaseUnits; DS_SETFONT dialog in MS Shell Dlg 8, MS Shell Dlg 2 8, Segoe UI 9, Tahoma 8: WM_GETFONT -> R3 fields, alphabet extent, MapDialogRect({0,0,4,8}) and ({0,0,100,100}); DS_SHELLFONT variant; MessageBox short text size (cross-check with m201 123x130) | `dialogs.tsv`: `DLG/<dpi>/<font>/<style><TAB>bu=cx,cy map48=... map100=... tm=... alpha=cx,cy` |
| R11 | Q10 | glyph images: GetGlyphOutlineW GGO_GRAY8_BITMAP and GGO_BITMAP + GLYPHMETRICS for 'A','g','W','i','0','.' in each UI face at -11/-12/-16, 96 DPI; ExtTextOutW of "Hello world" into a 32-bpp DIB with NONANTIALIASED / ANTIALIASED / CLEARTYPE quality | `glyphs.tsv`: `GL/<dpi>/<face>/<h>/<char>/<fmt><TAB>gm=bx,by,ox,oy,incx,incy size= sha256=`; raw buffers in `glyphs/` as gzip'd PGM (small, deduped by hash); `text_px.tsv` with DIB sha256 + ink bbox + ink pixel count |
| R12 | all | i7 / Wine comparability: the same exes run unchanged under Wine (plumbing only) and i7; a `NOTE` line wherever Windows behaviour depends on the build or the file | per-run `results/<area>.txt` (harness RUN/OK/FAIL/NOTE) + the TSVs above |

Settling rules (to be applied when the data exists):

- **D12 on** is supported if, for Segoe UI / Tahoma / Arial / Courier New at the D12 heights, R3 tm
  height/ascent/descent/intlead/ave and R4/R5 widths and extents equal i7 knob-on, *except* rows
  explained by an R1 file difference (e.g. Segoe UI max 31 vs 23).
- **micross needed** if R3/R7 show MS Shell Dlg (and DEFAULT_GUI_FONT) realised as Microsoft Sans
  Serif with tmHeight != Arial's at the same lfHeight at any of the HEIGHTS (known: -11 -> 13 vs 14),
  and R7/R10 show the 1 px propagating into row heights or dialog base units.
- **15/13, 19/17, 24** are settled per DPI by R3 + R7 + R8: text cy, padding and theme min height
  recorded next to every rectangle they produce.

---

## 9. Facts vs inferences summary

Facts (measured): Win10 96 DPI Segoe UI -12 = 15/12/3/il 3/ave 6/max 31, alphabet 347; MS Shell Dlg
-11 = 13/11/2/il 2/ave 5/max 23, alphabet 323; list view rows 19 (default font) / 17 (MS Shell Dlg
-11); header 24 in both; i7 stand-in 16/12/4/il 0/ave 8/max 8, row 20, header 20; i7 knob-on DEFAULT_GUI_FONT = Arial -11
h 14, alphabet 322; Win7 DEFAULT_GUI_FONT = Microsoft Sans Serif h 13, alphabet 323.

Code facts (Win10 decomp): row = max(text, image) + SM_CYBORDER, raised to text + 2*EditYPadding
in views 1-2, where EditYPadding = EM_GETRECT.top of a temporary edit; header = text cy ("..."
extent) + 2*edge, raised to the theme's HP_HEADERITEM size.

Inferences: EditYPadding = 2 for both fonts at 96 DPI; the header's 24 = HP_HEADERITEM part size
(so D12 alone gives i7 a 19/17 header, not 24); micross staging fixes MS Shell Dlg -11 to 13 on
i7; rows/DLUs scale with the 1 px error at other heights and DPIs. None of this is measured above
96 DPI.
