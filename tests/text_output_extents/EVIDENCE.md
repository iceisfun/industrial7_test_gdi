# text_output_extents: evidence

Oracle: Windows 10 22H2 build 19045.2965 guest, 1024x768 std VGA display, 96 DPI, test executable without a manifest
(DPI unaware). Font under test: Arial 18 px (`lfHeight=-18`), `DEFAULT_QUALITY` for pure metric tests,
`NONANTIALIASED_QUALITY` for pixel tests (documented: glyphs are never antialiased, so a pixel is exactly the
background or the text colour). Rendering is to a 32bpp (and one 1bpp) DIB section selected into a memory DC; no
window is used. Files: `extents.c`, `state_map.c`, `draw.c`, `dpi.c`, `toe_common.h`.

Source codes: **DOC** = Microsoft documentation page named in the row; **OBS** = observed on Win10 22H2 19045.2965
(asserted only because the value was the same in every run listed); **DECOMP** = read in the 19041.2965 binaries
(gdi32full / win32kfull) through the reference corpus.

Runs: R1 = first guest boot, R2..R4 = later separate boots (R1 and R2 ran earlier test revisions, see "Oracle
surprises"). "stable" = identical PASS/NOTE content in the last two runs of the final revision (R3, R4: 25 tests, 2783 checks, 0 failed, 0 skipped, 27 NOTE lines; the two logs are byte identical).

## Assertions

| # | Test | Expectation | Source | Stability |
|---|------|-------------|--------|-----------|
| 1 | extent_variants_agree | GetTextExtentPointA/W, GetTextExtentPoint32A/W and GetTextExtentPointI (glyph indices from GetGlyphIndicesW) give the same cx and cy for 8 ASCII strings; cy equals tmHeight | DOC nf-wingdi-gettextextentpointa, -gettextextentpointi | stable |
| 2 | extent_variants_agree | cx equals the sum of GetCharWidth32W advances and the sum of GetCharWidthI advances (no kerning applied) | DOC (extent = string width, no kerning), OBS | stable |
| 3 | extent_empty_and_concat | count 0 gives TRUE and size 0x0 (A and I); extent("AVAW") = extent("AV") + extent("AW") | DOC, OBS | stable |
| 4 | extent_ex_fit_and_prefix_sums | GetTextExtentExPoint A/W/I: nFit is the largest k whose partial sum is <= nMaxExtent (limits 1, 10, 50, 100, cx-1, cx, cx+1, 100000 and every partial sum itself); alpDx[0..nFit) is the prefix sum of the advances and monotonic; the last element equals cx when everything fits; lpSize equals GetTextExtentPoint32; NULL lpnFit and/or NULL alpDx still return the size and the other outputs | DOC nf-wingdi-gettextextentexpointa; "only the first nFit elements of alpDx are defined" is OBS (elements beyond nFit are left untouched) and is not asserted | stable |
| 5 | extent_ex_zero_and_negative_limit | nMaxExtent 0: call succeeds and nFit is 0 | DOC (no character fits), OBS | stable |
| 6 | gdi_get_char_dimensions | GdiGetCharDimensions(hdc, &tm, &h): for a variable-pitch font returns `(extent("a..zA..Z")/26 + 1)/2`, for a fixed-pitch font tmAveCharWidth; `h` and `tm` equal tmHeight and GetTextMetricsW; NULL tm / NULL h accepted; NULL DC returns 0. Faces Arial, Times New Roman, Courier New, Tahoma at -12, -18, -24 | DECOMP gdi32full!GdiGetCharDimensions @0xe180; formula confirmed OBS (Arial -18: 10, Times -18: 10, Courier New -18: 11, Tahoma -18: 10) | stable |
| 7 | text_state_defaults_and_roundtrip | fresh DC: text colour black, bk colour white, bk mode OPAQUE, align 0 (TA_LEFT\|TA_TOP\|TA_NOUPDATECP), map mode MM_TEXT. Set* returns the previous value, Get* the new one (colour, bk mode, align incl. TA_UPDATECP/TA_CENTER/TA_BASELINE); DCs are independent; SaveDC/RestoreDC restore the attributes | DOC nf-wingdi-settextcolor, -setbkcolor, -setbkmode, -settextalign, -gettextalign | stable |
| 8 | text_state_invalid | NULL DC: Get/SetTextColor and Get/SetBkColor return CLR_INVALID; Get/SetBkMode return 0; Get/SetTextAlign return GDI_ERROR; Get/SetMapMode return 0; SetMapMode with an invalid mode (0, 9) returns 0 and keeps the mode | DOC (return values of each function) | stable |
| 9 | map_mode_set_get_and_fixed_ratios | SetMapMode returns the previous mode over all 8 modes; GetMapMode reads it. In MM_LOMETRIC, HIMETRIC, LOENGLISH, HIENGLISH, TWIPS: window extent = physical size (HORZSIZE x VERTSIZE mm converted to the unit), viewport extent = HORZRES x -VERTRES, origin maps to 0,0, 100 units map to 100*HORZRES/windowExtent (y negative) within 1.5 px; SetWindowExtEx/SetViewportExtEx are ignored. MM_TEXT: extents 1x1, origins 0,0 | DOC nf-wingdi-setmapmode, nf-wingdi-getdevicecaps (HORZSIZE...); the mapping onto HORZSIZE/HORZRES is OBS (see surprises) | stable |
| 10 | map_anisotropic_transform | SetWindowExtEx/SetViewportExtEx/SetWindowOrgEx/SetViewportOrgEx return the previous values through the out parameter (extent default 1x1, origin 0,0) and Get* read them; LPtoDP/DPtoLP follow `D = (L - wOrg) * vExt / wExt + vOrg` exactly for exactly divisible points, and round-trip within 1 unit over a grid | DOC nf-wingdi-lptodp, -setwindowextex, -setviewportextex, -setwindoworgex, -setviewportorgex; formula in DOC "Window-to-Viewport Conversion" | stable |
| 11 | map_offset_and_scale | OffsetViewportOrgEx/OffsetWindowOrgEx add the offsets and return the previous origin; ScaleWindowExtEx/ScaleViewportExtEx set `new = old * num / denom` per axis and return the previous extent; the transform follows; origins are independent of the mapping mode (the window origin set under MM_ANISOTROPIC still applies after SetMapMode(MM_TEXT)) | DOC nf-wingdi-offsetviewportorgex, -offsetwindoworgex, -scalewindowextex, -scaleviewportextex; independence from the mode OBS | stable |
| 12 | map_isotropic_keeps_ratio | MM_ISOTROPIC: after window 100x100 / viewport 200x400 one logical unit has the same device size on both axes (observed viewport becomes 200x200) | DOC nf-wingdi-setmapmode (MM_ISOTROPIC); the adjusted values are only noted | stable |
| 13 | map_loenglish_font_height | In MM_LOENGLISH a font with lfHeight=-25 reports tmHeight and extents in logical units equal to those of the same font created at the corresponding device height in MM_TEXT, divided by the pixels-per-unit ratio (within 1.5 / 2 units); ascent+descent=height and extent cy=tmHeight; lfHeight=+25 gives tmHeight 25 (cell height in logical units); LPtoDP of the height gives the device height | DOC nf-wingdi-createfontindirectw (lfHeight sign), nf-wingdi-setmapmode; numbers OBS | stable |
| 14 | textouta_ink_within_extent | TextOutA of 5 strings at 20,20 (TRANSPARENT, black on white): every pixel is background or text colour; ink rows lie inside the extent rows; ink left edge = x + abcA of the first glyph and ink right edge = x + cx - abcC of the last glyph (within 1 px); ink width is not smaller than cx - A - C - 3; TextOutW and ExtTextOutA(options 0) are pixel identical to TextOutA; empty string draws nothing | DOC nf-wingdi-textouta, nf-wingdi-getcharabcwidthsw (ABC definitions), nf-wingdi-createfonta (NONANTIALIASED_QUALITY) | stable |
| 15 | textouta_text_colour | ink is exactly the selected text colour | DOC nf-wingdi-settextcolor | stable |
| 16 | textouta_bkmode | OPAQUE: every pixel of the extent box (x..x+cx, y..y+cy) is the bk colour or the text colour, bk + ink pixels = box area, the bottom corners are bk, nothing outside the box changes. TRANSPARENT: the bk colour never appears, the row below the baseline stays untouched; glyph shapes are identical in both modes | DOC nf-wingdi-setbkmode, nf-wingdi-textouta | stable |
| 17 | settextalign_horizontal_vertical_shift | Pixel-identical to TA_LEFT\|TA_TOP at (60,30): TA_RIGHT at x+cx, TA_CENTER at x+cx/2 (even cx), TA_BOTTOM at y+cy, TA_BASELINE at y+tmAscent, TA_RIGHT\|TA_BOTTOM; for odd cx TA_CENTER starts the string at x - ceil(cx/2) (asserted exactly: the string is pixel-identical to TA_LEFT at x - (cx/2 + 1); the x - cx/2 result is reported by NOTE only) | DOC nf-wingdi-settextalign; odd-cx rounding OBS | stable |
| 18 | settextalign_updatecp | With TA_UPDATECP the x,y arguments are ignored, the current position is the reference and advances by the string extent (TA_LEFT); two successive calls continue; without TA_UPDATECP the current position is untouched; TA_RIGHT\|TA_UPDATECP draws the string ending at the current position; ExtTextOutA with lpDx advances the position by the sum of lpDx | DOC nf-wingdi-settextalign (TA_UPDATECP), nf-wingdi-exttextouta; lpDx advance OBS | stable |
| 19 | exttextouta_opaque_and_clipped | ETO_OPAQUE fills lprc with the bk colour even when the mode is TRANSPARENT, text drawn on top is identical to TextOut, nothing outside lprc changes; lpString NULL with cb 0 only fills; ETO_CLIPPED: nothing outside lprc changes and the inside equals the unclipped drawing; lprc without ETO_CLIPPED does not clip; CLIPPED\|OPAQUE combination | DOC nf-wingdi-exttextouta | stable |
| 20 | exttextouta_dx_overrides_advances | lpDx of 30 places glyph k at x + sum of previous lpDx (ink right edge moves by 3*30), 5 overlaps (3*5); lpDx equal to the natural advances is identical to NULL | DOC nf-wingdi-exttextouta (lpDx) | stable |
| 21 | exttextoutw_glyph_index | ETO_GLYPH_INDEX with indices from GetGlyphIndicesW is pixel identical to drawing the string | DOC nf-wingdi-exttextoutw | stable |
| 22 | polytextout_two_strings | PolyTextOutW with 2 entries returns TRUE and equals two TextOutW calls pixel for pixel; the second string's ink is at its own x,y; PolyTextOutA equals W; per-entry ETO_OPAQUE fills the entry rcl and ETO_CLIPPED clips to it; the DC alignment (TA_RIGHT) applies; cStrings = 1 draws only the first | DOC nf-wingdi-polytextoutw, -polytextouta | stable |
| 23 | textouta_1bpp_dib | On a 1bpp DIB (index 0 white, 1 black): black text sets bits and its bounding box lies in the extent box; white text on the white entry changes no bit | DOC nf-wingdi-textouta, nf-wingdi-createdibsection | stable |
| 24 | drawtext_extents_and_alignment | DrawTextA/W DT_CALCRECT\|DT_SINGLELINE: height tmHeight, width cx; draw returns tmHeight; A and W pixel identical; DT_LEFT\|DT_TOP equals TextOutA at the rect corner; DT_RIGHT and DT_BOTTOM equal TextOut at right-cx / bottom-tmHeight; DT_CENTER\|DT_VCENTER ink is centred within a quarter of the width / half of the height | DOC nf-winuser-drawtexta; DECOMP user32!DrawTextExWorker (see char_widths area) | stable |
| 25 | drawtext_wordbreak_lines | DT_WORDBREAK in a rect narrower than "Hello World" gives 2 lines (return and bottom 2*tmHeight); the wrapped rect's width is the widest word's extent; wide rect gives 1 line of width cx; "a\nb\nc" gives 3 lines | DOC nf-winuser-drawtexta | stable |

## Observation only (NOTE, never asserted)

| Item | Observed (R2, R3, R4 identical) |
|------|----------------------------------|
| GetDCDpiScaleValue (screen DC, memory DC, DIB memory DC, NULL) | 1 in every case; NULL returning 1 matches DECOMP gdi32full!GetDCDpiScaleValue @0x10ad0 |
| GetBitmapDpiScaleValue (DIB section, compatible bitmap, NULL) | 1 |
| GetCurrentDpiInfo(primary HMONITOR, 0x60 byte buffer) | returns TRUE; dwords 100 100 100 100 0 0 325 203 (offset 8 = scale percent, as used by its callers in the corpus; the last two are the physical size in mm). Signature from DECOMP of a caller in d3d9 |
| DPI awareness / system DPI | thread awareness 0 (unaware), GetDpiForSystem 96, screen LOGPIXELSX 96 (asserted: DOC GetDpiForSystem for an unaware process) |
| SetBkMode with an undocumented value | accepted (see surprises) |
| TextOutA with an empty string | returns TRUE |
| MM_LOENGLISH font numbers | lfHeight -25: tmHeight 28 (asc 22, desc 6); +25: tmHeight 25 (asc 20, desc 5) |

## Oracle surprises (Win10 differed from the first expectation; test corrected)

1. GetTextExtentExPoint: alpDx beyond nFit is not written. Only elements `[0, nFit)` are defined.
2. Fixed mapping modes are scaled from the device's physical size (HORZSIZE/VERTSIZE and HORZRES/VERTRES), not from LOGPIXELSX/Y: on this guest (325x203 mm, 1024x768) MM_LOENGLISH is 80 px/inch horizontally and 96 vertically while LOGPIXELS is 96.
3. TA_CENTER with an odd extent starts the string at `x - ceil(cx/2)` (cx 41 and 73 both), i.e. one pixel further left than `x - cx/2`.
4. SetBkMode does not validate its argument: 0 and 3 are accepted (return value is the previous mode) although the documentation says failure returns zero. Only NULL DC returns 0 and is asserted.
5. The window origin and extent persist across SetMapMode(MM_TEXT); the origin is part of the transform in every mode.
6. (Test harness) EXPECT_EQ evaluates its argument twice; Set* calls inside it produced false failures in R1. Local macro EQV evaluates once.

## Skips

None are expected. The area skips (with the stated reason) only if Arial is not realised as a TrueType face, the DIB
section cannot be created, or an export is missing.

## Not covered

* `GetTextExtentPointA` etc. under a non-96 DPI: the runs listed above used the default 96 DPI only.
* ClearType / antialiased glyph pixels (only extents and ink bounds with NONANTIALIASED_QUALITY are checked).
* DPI awareness variants of GetDCDpiScaleValue / GetCurrentDpiInfo (need manifested builds; the existing probes record awareness-dependent metrics).
* DrawText flags other than the ones above (prefix, ellipsis, tabs, DT_NOCLIP) belong with a dedicated DrawText area.

## Destructive or special-setup ideas (not implemented)

* Run the extent and mapping-mode tests at 120/144/192 DPI with system-aware and per-monitor manifests and compare GetDCDpiScaleValue / GetCurrentDpiInfo with GetDpiForSystem.
* Change the display mode (e.g. 800x600, 1280x1024) and verify that the fixed mapping modes follow HORZRES/VERTRES and the reported physical size.
* Select a raster font (Terminal, Courier) and compare extents, GdiGetCharDimensions and TextOut ink with the TrueType results.
* TextOut onto a visible window DC and a printer-like device with different HORZSIZE to confirm the mapping-mode ratio on non-memory DCs.
* Exhaust GDI handles while a DIB-selected DC draws text to check TextOut failure returns.
