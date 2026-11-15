# outline_and_abc: evidence

Area: `GetOutlineTextMetricsA/W`, `GetETM`, `GetGlyphOutlineA/W/Wow`, `GetCharABCWidthsA/W/I`, `GetCharABCWidthsFloatA/W/I`,
`GetCharWidthA/W/32A/32W/FloatA/FloatW/I`, `GetStringBitmapA/W`.
Files: `otm_etm.c`, `glyph_outline.c`, `abc_widths.c`, `string_bitmap.c`, `oa_common.[ch]` (helpers). Built to `build/outline_and_abc.exe`.

Oracle: Windows 10 Home 22H2, 10.0.19045.2965, en-US (ACP 1252), 1024x768 32 bpp, QEMU guest, elevated Administrator, 96 DPI.
Setup in every test: screen DC, `MM_TEXT`, `GM_COMPATIBLE`, identity world transform, faces Arial and Times New Roman at lfHeight -20
(raster arm: Terminal -12, MS Sans Serif / Small Fonts / Courier -13; Symbol -20). Every test skips with a concrete reason when a face
is not realised as the requested kind (TrueType or raster).

Sources: **D** = documentation, **O** = observed on Win10 22H2 19045.2965 (asserted only because stable), **C** = decompiled code in the
reference corpus (gdi32full / win32kfull 19041.2965), **N** = NOTE only (recorded, never asserted).

Stability: three boots of the final build, runs 2 and 3 are identical line for line in `outline_and_abc.log`
(18 tests, 1646 checks, 0 failed, 0 skipped, 135 NOTE). Run 1 was an earlier revision of the test (see "Corrections").
Every row below is covered by those runs unless marked otherwise.

## Expectations

| # | Test | Expectation | Source | Stable |
|---|------|-------------|--------|--------|
| 1 | otm_arial_ab, otm_times_ab | `GetOutlineTextMetricsA/W(cb=0,NULL)` returns a size >= sizeof the struct; fill with that size returns nonzero | D (nf-wingdi-getoutlinetextmetricsa) | yes |
| 2 | same | A and W `otmTextMetrics` agree in every numeric field (the char-typed first/last/default/break fields differ in width by design and are not compared); equal to `GetTextMetricsW` height/ascent/descent/avg width; `tmHeight == tmAscent + tmDescent`; TMPF_TRUETYPE set | D (struct) + O | yes |
| 3 | same | `otmEMSquare`, `otmAscent`, `otmDescent`, `otmLineGap`, `otmsCapEmHeight`, `otmsXHeight`, `otmfsSelection`, `otmfsType`, slope fields, italic angle, PANOSE, font box identical between A and W | D | yes |
| 4 | same | `otmEMSquare` equals `head.unitsPerEm` read through `GetFontData`; `otmAscent > 0`, `otmDescent < 0` | D (otmEMSquare = "em square"), O for sign | yes (2048 for both faces) |
| 5 | same | string offsets (family, face, style, full name) lie inside [sizeof struct, size), point at zero-terminated text, A text converts to the W text; face name text equals the selected face | D ("offset from the beginning of the structure") | yes |
| 6 | same | return value of the fill call is nonzero | D | yes; N: it equals the size (Arial A 287 / W 342, Times A 317 / W 402) |
| 7 | otm_size_contract | size query is repeatable; invalid DC returns 0 | D | yes |
| 8 | otm_size_contract | cbData smaller than the struct: partial fill, returns cbData, no write past cbData (W and A, cb 1, 4, 100, 231) | O (cb 1..232 all return cb, scanned once over every cb 0..size+8) | yes |
| 9 | otm_size_contract | cbData in (sizeof struct, size) returns 0; size-1 returns 0; exactly size and size+64 return size | D for "too small fails", O for the rest | yes |
| 10 | otm_size_contract | cbData=0 with a NON-NULL buffer: W returns the size, A returns 0 | O (asymmetric, undocumented) | yes |
| 11 | otm_bitmap_font | raster faces (Terminal, MS Sans Serif, Small Fonts, Courier) return an OUTLINETEXTMETRIC (size W 268..292): otmSize == size, text metrics equal GetTextMetrics, `otmAscent == tmAscent`, `otmDescent == -tmDescent`, `otmEMSquare == tmHeight - tmInternalLeading`, family and face name text == the face name, not TMPF_TRUETYPE | O (docs say the function is for TrueType/outline fonts; see Oracle surprises) | yes |
| 12 | etm_vs_otm | `GetETM` returns nonzero for TrueType, fills 52 bytes (etmSize 52), writes nothing outside them; `etmMasterUnits == otmEMSquare`; `etmKernPairs` (offset 0x30) `== GetKerningPairsA(dc,0,NULL)`; GetETM(NULL,NULL) returns 0 | C (gdi32full!GetETM stores GetKerningPairsA count at +0x30), O | yes |
| 13 | etm_vs_otm | N: GetETM on Terminal returns 0; etmCapHeight/etmXHeight (Arial 1491/512, Times 1420/512, design units); etmKernTracks 0 | O | yes |
| 14 | go_metrics_vs_width | `GGO_METRICS` 'A'..'Z': `gmCellIncX == GetCharWidth32W` advance, `gmCellIncY == 0`, black box > 0 and `<= tmHeight`, `0 < gmptGlyphOrigin.y <= tmAscent`; A and W entry points identical | D (GLYPHMETRICS), O | yes |
| 15 | go_native_stream | `GGO_NATIVE` size query == fill return; GLYPHMETRICS equal between query and fill; stream is a chain of TTPOLYGONHEADER (dwType == TT_POLYGON_TYPE), `cb` values sum to the returned size, TTPOLYCURVE records tile each header exactly; 'A' and 'O' have 2 contours; unhinted variant parses; `GGO_BEZIER` has only line and cubic records; blank glyph (space) returns 0 | D (nf-wingdi-getglyphoutlinea, TTPOLYGONHEADER) | yes |
| 16 | go_native_stream | N: cbData = size-1 returns GDI_ERROR; contour/line/curve counts per glyph | O | yes |
| 17 | go_bitmap_formats | `GGO_BITMAP` size == rows * ((bbX+31)/32*4); `GGO_GRAY2/4/8_BITMAP` size == rows * (bbX+3 & ~3); gray values <= 4 / 16 / 64 (the observed maximum for 'A' reaches that value, asserted only as > 0 and <= limit); bitmap non-blank; no bits set past `gmBlackBoxX` in monochrome rows; query/fill metrics equal; the fill call returns the queried size; `gmCellIncX` equals the advance | D (rows DWORD aligned, 5/17/65 levels) | yes |
| 18 | go_bitmap_formats | N: `GGO_BITMAP|GGO_GRAY8_BITMAP` (=7) returns GDI_ERROR; gray2/4/8 black-box widths differ from each other by up to 2 pixels | O | yes |
| 19 | go_glyph_index_and_errors | `GGO_GLYPH_INDEX` with the `GetGlyphIndicesW` index of 'A' gives the same size and GLYPHMETRICS as the character form (NATIVE and METRICS); NULL MAT2 returns GDI_ERROR; invalid DC returns GDI_ERROR | D (MAT2 required), O | yes |
| 20 | go_glyph_index_and_errors | 2x scaling matrix: `gmCellIncX` within 1 of twice the 1x value, black box width within 2 of twice | O (27 vs 2*13, see Corrections) | yes |
| 21 | go_symbol_missing_glyph | Symbol: glyph index >= numGlyphs (`maxp`) and 0xFFFF return GDI_ERROR; last valid index succeeds | O | yes |
| 22 | go_symbol_missing_glyph | N: a missing character (U+0100) does NOT fail, it returns the default glyph's metrics (cellInc 12, box 10x14) | O | yes |
| 23 | go_bitmap_font_rejected | raster faces: `GGO_METRICS`, `GGO_BITMAP`, `GGO_NATIVE` return GDI_ERROR | D (outline retrieval is a TrueType service), O | yes |
| 24 | go_wow_and_ansi | `GetGlyphOutlineWow` has the `GetGlyphOutlineA` signature and gives identical return value, GLYPHMETRICS and native size for 'A'..'Z' step 5 | C (Wow tail-calls GetGlyphOutlineInternalA with the same 7 arguments), O | yes |
| 25 | go_wow_and_ansi | ACP 1252: `GetGlyphOutlineA/Wow(0x80)` == `GetGlyphOutlineW(U+20AC)` | D (A form converts with the DC code page), O | yes |
| 26 | abc_a_vs_w_vs_width | `GetCharABCWidthsA == W` for 'A'..'Z'; A+B+C == `GetCharWidth32W`; abcB > 0; one call over a range equals single-character calls; `tmOverhang == 0` and `GetCharWidthA/W/32A/32W` identical for TrueType | D (nf-wingdi-getcharabcwidthsa), O | yes |
| 27 | abc_a_vs_w_vs_width | ACP 1252: ABC and width of 8 high-ANSI characters (0x80, 0x85, 0x91, 0x93, 0x99, 0x9F, 0xA9, 0xE9) through the A forms equal the W forms of the mapped characters | D, O | yes |
| 28 | abc_index_variant | `GetCharABCWidthsI` with `GetGlyphIndicesW` indices equals `GetCharABCWidthsW`; `GetCharWidthI` == `GetCharWidth32W`; the `pgi == NULL` consecutive-index form equals the explicit array form | D (GetCharABCWidthsI) | yes |
| 29 | abc_float_variants | `GetCharABCWidthsFloatA`, `FloatW` and `FloatI` are bit-identical; each component is within 1 unit of the integer ABC at 100% mapping | D, tolerance of 1 unit | yes; N: the measured maximum difference is 0 for both faces at -20 |
| 30 | abc_float_variants | `GetCharWidthFloatA == FloatW`; `GetCharWidthFloat * 16 == A+B+C` exactly, and `abcfA+abcfB+abcfC == 16 * GetCharWidthFloat` | O (see Oracle surprises) | yes (also at -10, -40, -100 in a one-off scan) |
| 31 | width_proportional_vs_fixed | `GetCharWidthA == GetCharWidth32W`; 'i' < 'M' for Arial and Times New Roman (float too); 'i' == 'M' == space for Courier New and Lucida Console; `TMPF_FIXED_PITCH` (which means variable pitch when set) is set for the proportional faces and clear for the fixed ones | D (GetCharWidth, TEXTMETRIC tmPitchAndFamily), O | yes |
| 32 | abc_bitmap_font | raster faces: `GetCharABCWidthsA/W/FloatW/I` return TRUE with A = 0, B = advance, C = 0; widths A/W/Float agree; Terminal advance is constant and equals `tmAveCharWidth` | O (docs say TrueType only; see Oracle surprises) | yes |
| 33 | stringbitmap_records | `GetStringBitmapA` == `GetStringBitmapW` (return and bytes) for one character; returned size == 8 + rows * ((cx+7)/8) from the 8-byte {cx, cy} header; some bit set; header cy == tmHeight; for raster faces cx == `GetCharWidth32W` advance | C (NtGdiGetStringBitmapW: cwc must be 1, header + 1bpp rows), O | yes |
| 34 | stringbitmap_records | A with 3 chars returns 0; W with cwc == 2 returns 0; NULL DC returns 0; cjBuf = 4 returns the required size and does not write the buffer | C (A wrapper rejects count > 2, kernel requires cwc == 1), O | yes |
| 35 | stringbitmap_records | N: size queries (cjBuf 0, NULL buffer) return the required size; TrueType Arial -13 gives cx = advance + 2; space in Terminal is 8x12 (20 bytes) | O | yes |

## Skips

None triggered on the oracle. Each test skips (with the face name in the message) when its face is not realised as requested:
Arial, Times New Roman, Courier New, Lucida Console, Symbol must be TrueType; the raster tests need Terminal, MS Sans Serif, Small Fonts or
Courier realised as non-TrueType and skip as a group when none is; `GetCharABCWidthsFloatI`, `GetETM`, `GetGlyphOutlineWow`,
`GetStringBitmapA/W` skip when the export is missing; the euro/high-ANSI arms run only when the ANSI code page is 1252 (NOTE otherwise).

## Oracle surprises (Win10 differed from the first assumption; the test was corrected to match Win10)

1. `GetOutlineTextMetrics` succeeds for raster fonts (nonzero size, full OUTLINETEXTMETRIC synthesised from the text metrics). The first assumption was a 0 return.
2. `GetCharABCWidths A/W/Float/I` succeed for raster fonts (A=0, B=advance, C=0), contrary to "works only with TrueType".
3. `GetCharWidthFloat` returns the integer advance divided by 16 (Arial 'A' at -20 gives 0.8125 for a 13 pixel advance) instead of a width in logical units; `GetCharABCWidthsFloat` is in pixels (equal to the integer ABC).
4. `GetOutlineTextMetrics` with `cbData` below the structure size fills partially and returns `cbData`; between the structure size and the full size it returns 0. `cbData == 0` with a non-NULL buffer: W returns the size, A returns 0.
5. 2x MAT2: `gmCellIncX` 27 for a 1x advance of 13 (the scaled value is not twice the hinted one); assertion widened to +-1.
6. A character missing from a symbol font (U+0100 in Symbol) returns default-glyph metrics instead of GDI_ERROR; only an out-of-range glyph index fails.
7. `GGO_METRICS` returns a nonzero value that is not 0 or a size query result (Arial 'A' 1068, Times 928); NOTE only, not asserted.
8. `GetStringBitmapW` with a too-small buffer returns the required size and writes nothing.
9. `GGO_BITMAP|GGO_GRAY8_BITMAP` (= 7, not a defined format) returns GDI_ERROR.

## Corrections made after the first guest run (run 1, earlier revision)

Run 1 failed 6 of 18 tests, all of them oracle misunderstandings that were corrected to match Win10 (never by loosening to "does not crash"):
rows 10/8 (cbData behaviour), 11 (raster OTM), 20 (2x advance), 30 (float width scale), 32 (raster ABC). Runs 2 and 3 are the final build.

## Not covered

- `GetCharABCWidthsFloatI`, `GetETM`, `GetGlyphOutlineWow`, `GetStringBitmapA/W`: behavioural only for the relations in rows 12, 24, 33-35 (undocumented, signatures from decompilation). GetETM fields other than etmSize/MasterUnits/KernPairs are N.
- DBCS code pages, `GGO_*` on non-1252 ANSI input, vertical fonts (`@` faces) and rotated fonts (escapement) for the outline and ABC APIs.
- Non-identity mapping modes / world transforms for ABC and width APIs (the float result under MM_ANISOTROPIC is unobserved).
- ClearType/hinting pixel values (only ranges and sizes are asserted).
- Glyph outlines for composite glyphs, glyphs with hinting instructions disabled by a registry setting.

## Destructive or special-setup ideas (not implemented)

- Install and remove a TrueType font with unusual `head`/`hhea`/`OS/2` tables and compare `GetOutlineTextMetrics` against the table values.
- Install an OpenType-CFF font and check `GetGlyphOutline` cubic (`GGO_BEZIER`) against `GGO_NATIVE`.
- Select a font through `SetWorldTransform` rotation and 90/180 degree escapement and record `GetCharABCWidthsFloat`, `GetGlyphOutline` cellInc Y.
- High-contrast / ClearType off / standard-user sessions to see whether `GGO_GRAYn` values change.
- DBCS locale (ACP 932/936/949/950) for `GetGlyphOutlineA` lead-byte handling and `GetStringBitmapA` two-byte input.
- Exhaust glyph cache with a very large `GetGlyphOutline` size request (`cbBuffer` near the 0x2710000 limit seen in the `GetStringBitmapW` kernel path).
