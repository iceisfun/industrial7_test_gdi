# glyph_bitmaps: evidence

Area: `GetGlyphOutlineA/W` (metrics and bitmap formats), `GetTextMetricsA/W`, `GetCharABCWidthsW`, `GetCharWidth32W`,
`GetTextExtentPoint32A/W`, `GetGlyphIndicesA/W`, `GetFontData`, `GetTextFaceW`, `GetOutlineTextMetricsW`, `GetObjectW`,
`TextOutW`, `ExtTextOutW`, `DrawTextW` (measurement), plus pixel-level invariants of rendered text on a 32 bpp DIB section.
File: `glyph.c`, built to `build/glyph_bitmaps.exe`. This area asserts invariants only; the recorded glyph data is in `data/win10/glyph_bitmaps/`.

Oracle: Windows 10 Home 22H2, 10.0.19045.2965, en-US (ACP 1252), 1024x768 32 bpp, QEMU guest, elevated Administrator, 96 DPI.
Setup: memory DC (`fdc_open`) with one face realised at one height, identity `MAT2`; the face is checked with `GetTextFaceW` and the test
skips with "face X not installed" when another face was realised. Faces: Arial, Tahoma, Segoe UI, Courier New (TrueType) plus MS Shell Dlg,
System, Courier, MS Sans Serif in the TEXTMETRIC test. Heights -8, -11, -12, -13, -16, -20, -32.

Sources: **D** = documentation, **O** = observed on Win10 22H2 19045.2965 (asserted only because stable), **C** = reading of the
decompiled code of `gdi32full.dll` / `win32kfull.sys` 19041.2965 in the reference corpus, **N** = NOTE only.

Stability: two boots of the final build, each: 35 tests, 4364 checks, 0 failed, 0 skipped, 5 NOTE; the `glyph_bitmaps.log` of both boots
is identical line for line.

## Expectations

| # | Test | Expectation | Source | Stable |
|---|------|-------------|--------|--------|
| 1 | glyph_ggo_null_metrics_ptr_fails | `GetGlyphOutlineW` with `lpgm == NULL` returns `GDI_ERROR` for GGO_METRICS, GGO_BITMAP, GGO_GRAY2/4/8_BITMAP (the loop covers formats 0, 1, 4, 5, 6) | D (lpgm is a required out parameter; GDI_ERROR on failure); C (`GetGlyphOutlineInternalW` returns -1 before the syscall) | yes |
| 2 | glyph_ggo_null_matrix_fails | `lpmat2 == NULL` returns `GDI_ERROR` and the GLYPHMETRICS buffer is left untouched (0x5A pattern) | D (lpmat2 required); the untouched buffer is C and O | yes |
| 3 | glyph_ggo_null_hdc_invalid_handle | `GetGlyphOutlineW(NULL, ...)` returns `GDI_ERROR` with `ERROR_INVALID_HANDLE` for GGO_METRICS and GGO_GRAY8_BITMAP | C (`GreGetGlyphOutlineInternal`); O (the error code is undocumented) | yes |
| 4 | glyph_ggo_buffer_protocol | for 4 faces x 7 heights x chars 'A', 'g', U+00E9: GGO_BITMAP size query == `rows * ((bbX + 31) / 32 * 4)`; GRAY2/4/8 size query == `rows * ((bbX + 3) & ~3)`; the fill call returns that size; query and fill report identical GLYPHMETRICS; 16 guard bytes after the buffer stay untouched; gray values <= 4 / 16 / 64; the bitmap is not all zero; `gmCellIncX/Y` equal the GGO_METRICS values for every format | D (GetGlyphOutline: DWORD-aligned rows, 2/4/8 bpp gray levels 0..4/16/64, size query with cjBuffer 0) + O | yes |
| 5 | glyph_ggo_space_has_no_bitmap | for ' ' in Arial -12 GGO_GRAY8_BITMAP the size query returns 0 and `gmCellIncX > 0` | D (blank glyphs have no bitmap); O for the return 0 | yes |
| 6 | glyph_ggo_glyph_index_matches_char | for 4 faces at -13 and chars 'A' 'g' 'W' '0' '.': `GGO_GLYPH_INDEX` with the index from `GetGlyphIndicesW` returns the same size, GLYPHMETRICS and bitmap bytes as the character call, for GGO_METRICS and the four bitmap formats | D (GGO_GLYPH_INDEX: "indices instead of characters"); C | yes |
| 7 | glyph_ggo_unhinted_ignored_for_bitmaps | adding `GGO_UNHINTED` to GGO_METRICS or a bitmap format changes neither the return value, the GLYPHMETRICS nor the bitmap bytes (4 faces, -11 and -16, 'A' 'g' '0') | D (GGO_UNHINTED applies to outline formats); the equality is C (flag only reaches the outline query path) and O | yes |
| 8 | glyph_ggo_a_w_parity | `GetGlyphOutlineA` and `GetGlyphOutlineW` agree on return value, GLYPHMETRICS and bitmap bytes for ASCII 0x21..0x7e, Tahoma -11, GGO_GRAY8_BITMAP | D (A takes the char in the ANSI code page); O | yes |
| 9 | glyph_ggo_metrics_advance_equals_charwidth | `GGO_METRICS gmCellIncX == GetCharWidth32W` for chars 0x20..0x7e, 4 faces x 7 heights at NONANTIALIASED_QUALITY | C (both from the rounded advance); O | yes |
| 10 | glyph_tm_height_is_ascent_plus_descent | `tmHeight == tmAscent + tmDescent`, `0 <= tmInternalLeading < tmHeight`, `tmAveCharWidth > 0`, `tmMaxCharWidth >= tmAveCharWidth` for 8 faces (incl. System, Courier, MS Sans Serif) x 7 heights | D (TEXTMETRIC field definitions); O for the leading and width bounds | yes |
| 11 | glyph_tm_truetype_em_height | for a TrueType face at a negative `lfHeight`: `tmHeight - tmInternalLeading == -lfHeight` and `TMPF_TRUETYPE` set; 4 faces x 7 heights (not MS Shell Dlg: that alias gives -lfHeight - 1 at -12, see font_metrics row 3) | D (CreateFont: negative height = character height, i.e. height without internal leading); O | yes |
| 12 | glyph_tm_null_buffer_and_null_dc | `GetTextMetricsW(dc, NULL)` returns 0 and `GetTextMetricsW(NULL, &tm)` returns 0. The NULL buffer call is skipped when the harness reports a non-native run | C (`GetTextMetricsW` leaves the result 0); O | yes |
| 13 | glyph_tm_a_w_parity | A and W numeric fields agree for 4 faces at -12 | C; O | yes |
| 14 | glyph_abc_bad_params | `GetCharABCWidthsW` with last < first or a NULL buffer fails with `ERROR_INVALID_PARAMETER` | D (returns zero on failure); the error code is C and O | yes |
| 15 | glyph_abc_sum_is_width_minus_overhang | `A + B + C == GetCharWidth32W - tmOverhang` for 0x20..0x7e, 4 faces x 7 heights x regular/bold, NONANTIALIASED | D (ABC meaning); the overhang term is C and O | yes |
| 16 | glyph_extent_zero_and_negative_count | `GetTextExtentPoint32W(dc, s, 0, &sz)` returns TRUE with `{0,0}` (the SIZE is overwritten from 77,77); a count of -1 fails with `ERROR_INVALID_PARAMETER` | C (`GetTextExtentPointWInternal`); O (not documented) | yes |
| 17 | glyph_extent_is_sum_of_widths | for 6 strings x 4 faces x 7 heights (NONANTIALIASED): `cy == tmHeight` and `cx == sum of GetCharWidth32W` (no kerning, `MM_TEXT`, no character extra) | D (extent of the string); the sum equality and the cy rule are C and O. Holds on these faces at integer pixel sizes; it is NOT claimed for kerned or hinted-fractional cases | yes |
| 18 | glyph_extent_a_w_parity | A and W extents of "Cancel Apply OK" agree (Segoe UI -12) | O | yes |
| 19 | glyph_indices_count_and_missing_mark | `GetGlyphIndicesW(.., 4 chars, gi, GGI_MARK_NONEXISTING_GLYPHS)` returns 4; U+FFFF maps to 0xFFFF; 'A' maps to a non-zero, non-0xFFFF index and 'B' to a different one; without the flag U+FFFF is not 0xFFFF (the default glyph) and the indices of 'A' and 'z' are unchanged | D (GGI_MARK_NONEXISTING_GLYPHS: "0xFFFF for characters without a glyph"; returns the number of indices) | yes |
| 20 | glyph_indices_null_dc | `GetGlyphIndicesW(NULL, ...)` returns `GDI_ERROR` | D (GDI_ERROR on failure); C | yes |
| 21 | glyph_indices_a_w_parity | A and W give the same 95 indices for ASCII 0x20..0x7e (Arial -12) | O | yes |
| 22 | glyph_fontdata_null_dc_invalid_handle | `GetFontData(NULL, 0, 0, NULL, 0)` returns `GDI_ERROR` with `ERROR_INVALID_HANDLE` | D (GDI_ERROR on failure); the code is C and O | yes |
| 23 | glyph_fontdata_buffer_protocol | Arial: `GetFontData(dc, 0, 0, NULL, 0)` returns a size > 12; `cjBuffer 0xFFFFFFFF` with NULL buffer returns `GDI_ERROR`; a NULL buffer with cjBuffer 100 still returns the full size; the `head` table size is 54, its `magicNumber` is 0x5F0F3CF5, `dwOffset 12` returns the same 4 bytes; the whole file starts with the sfnt version 0x00010000 | D (GetFontData: NULL buffer returns the size; table 0 = whole file); the NULL-with-count 100 and 0xFFFFFFFF behaviour is C and O; the table layout is the OpenType specification | yes |
| 24 | glyph_fontdata_missing_table | a nonexistent table tag `zqxj` returns `GDI_ERROR` | D | yes |
| 25 | glyph_font_getobject_roundtrip | a LOGFONT with face "MS Shell Dlg 2", bold italic underline, CLEARTYPE_QUALITY is returned by `GetObjectW` unchanged (all fields before the name and the name itself); `GetObjectW(f, 0, NULL)` is `sizeof(LOGFONTW)`; `SelectObject` returns the previous font and `GetCurrentObject(OBJ_FONT)` returns the selected one | D (GetObject, SelectObject); C | yes |
| 26 | glyph_textface_buffer_protocol | Tahoma: `GetTextFaceW(dc, 0, NULL)` returns 7; the fill returns a count (printed) and the text is "Tahoma" | D (needed length incl. NUL); O | yes |
| 27 | same | N: the fill call returns 7 (the length including the NUL) | O (the documentation says the number of characters copied) | yes |
| 28 | glyph_textface_buffer_with_bad_count | buffer with count 0 or -1 fails with `ERROR_INVALID_PARAMETER` and writes nothing | C; O | yes |
| 29 | glyph_otm_size_query | Segoe UI: `GetOutlineTextMetricsW(dc, 0, NULL)` returns >= `sizeof(OUTLINETEXTMETRICW)`; a NULL buffer with a nonzero count returns the same size; the fill succeeds, `otmSize` equals the size, `otmTextMetrics` height and ascent equal `GetTextMetricsW`, `otmEMSquare > 0` | D (nf-wingdi-getoutlinetextmetricsw); C; O | yes |
| 30 | glyph_render_textout_equals_exttextout | for 4 faces x NONANTIALIASED / ANTIALIASED / CLEARTYPE quality: `TextOutW`, `ExtTextOutW` with no options and `ExtTextOutW(ETO_GLYPH_INDEX)` with the indices of the same string produce identical 32 bpp pixels; the output has ink | D (TextOut is ExtTextOut without options); O (pixel identity of the three paths) | yes |
| 31 | glyph_render_clipped_stays_inside | `ETO_CLIPPED` with a 30x10 rectangle (ANTIALIASED -16, "WWWWWWWWWW"): no non-white pixel outside the rectangle, at least one inside | D (ETO_CLIPPED) | yes |
| 32 | glyph_render_opaque_fills_rect | `ETO_OPAQUE` with an empty string fills the rectangle with the background colour (the pixel inside is blue) and leaves a pixel outside the rectangle white | D (ETO_OPAQUE) | yes |
| 33 | glyph_render_nonaa_is_bilevel | NONANTIALIASED_QUALITY, black on white, -13: every pixel is pure black or pure white (4 faces) | D (NONANTIALIASED_QUALITY: "never antialiased"); O | yes |
| 34 | glyph_render_aa_is_grey | ANTIALIASED_QUALITY, black on white, -16: no pixel with R != G or G != B (no colour fringes) for 4 faces | D (ANTIALIASED_QUALITY is greyscale, not ClearType); O | yes |
| 35 | same | N: number of intermediate grey pixels at -16 for "Name OK Cancel Size": Arial 0, Tahoma 0, Segoe UI 436, Courier New 0. Whether ANTIALIASED_QUALITY actually antialiases depends on the font (embedded bitmaps / `gasp` table) and the font smoothing setting, so the number is not asserted | O | yes |
| 36 | glyph_drawtext_height_and_singleline | `DrawTextW(DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX)` on "Cancel" returns `tmHeight`, and the rectangle equals `GetTextExtentPoint32W` cx by `tmHeight`; "&Cancel" without DT_NOPREFIX measures the same width as "Cancel" | D (DT_CALCRECT returns the height; DT_NOPREFIX; the prefix is not drawn, documented in DrawText); O | yes |
| 37 | glyph_synth_bold_italic_flags | bold + italic + underline + strike-out requested at -12: `tmItalic`, `tmUnderlined`, `tmStruckOut` are nonzero and `tmWeight >= FW_SEMIBOLD` (4 faces), whether the face is real or synthesised | D (TEXTMETRIC); O | yes |

## Known limits

- Row 8 uses ASCII only, so the ANSI code page does not matter. Rows 4..9 assume the four TrueType faces of a stock Windows 10 install and skip with the face name otherwise.
- Rows 30..34 depend on the guest's font smoothing setting only through the grey count in row 35, which is not asserted. Row 30 asserts identity between three drawing paths, not a recorded pixel pattern; pixel-exact recorded glyphs are in `data/win10/glyph_bitmaps/`.
- All runs are at 96 DPI.
- Row 17 is verified for the faces and sizes listed; the NOTE-only extent differences (kerning, ClearType) are covered by text_output_extents and kerning_placement.

## Corrections

None needed: no assertion failed on the guest and none was changed or weakened. The source comment in `glyph_tm_null_buffer_and_null_dc`
was reworded to name no particular implementation; the executed code is unchanged.
