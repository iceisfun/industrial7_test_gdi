# font_metrics: evidence

Area: `CreateFontIndirectW`, `SelectObject` (fonts), `GetCurrentObject`, `GetObjectW/A` on fonts and stock fonts,
`GetTextFaceW/A`, `GetTextMetricsW/A`, `GetOutlineTextMetricsW`, `GetCharABCWidthsW`, `GetCharWidth32W`,
`GetTextExtentPoint32W`, `GetTextExtentExPointW`, `GetFontLanguageInfo`, `GetCharacterPlacementW`.
File: `font_metrics.c`, built to `build/font_metrics.exe`. The recorded numbers for this family live in
`data/win10/font_metrics/` and are not repeated here; this area asserts only invariants.

Oracle: Windows 10 Home 22H2, 10.0.19045.2965, en-US (ACP 1252), 1024x768 32 bpp, QEMU guest, elevated Administrator, 96 DPI.
Setup: screen DC (or a compatible memory DC where stated), `MM_TEXT`, no world transform. Faces: Arial, Tahoma, Segoe UI,
Courier New, Times New Roman, MS Shell Dlg, MS Shell Dlg 2, Microsoft Sans Serif, System, Terminal, Courier, MS Sans Serif and the
empty face name, at heights -6 .. -40 and 0, 8 .. 40. The raster-font cases rely on the faces of a stock Windows 10 install.

Sources: **D** = documentation, **O** = observed on Win10 22H2 19045.2965 (asserted only because it was stable), **C** = reading of the
decompiled code of `gdi32full.dll` / `win32kfull.sys` 19041.2965 in the reference corpus (the `[Vn]` tags in the source comments),
**N** = NOTE only (printed, never asserted).

Stability: two boots of the final build, each: 17 tests, 3641 checks, 0 failed, 0 skipped, 28 NOTE; the `font_metrics.log` of both
boots is identical line for line.

## Expectations

| # | Test | Expectation | Source | Stable |
|---|------|-------------|--------|--------|
| 1 | createfont_null_fails | `CreateFontIndirectW(NULL)` returns NULL | C [V1] (no documented contract); O | yes |
| 2 | tm_height_is_ascent_plus_descent | `tmHeight == tmAscent + tmDescent` for every face x height x weight (13 faces, 14 heights, 400/700, including the empty face name and heights 0 and positive) | D (TEXTMETRIC: "tmHeight: the height (ascent + descent)") | yes |
| 3 | same | N: for a TrueType face at negative lfHeight, `tmHeight - tmInternalLeading != -lfHeight` is printed (observed only for MS Shell Dlg at -12, where it is 11). This is NOT asserted: it is the mapper's choice of a face-specific size | O | yes (same 2 lines each boot) |
| 4 | tm_truetype_overhang_and_aspect | for Arial, Tahoma, Courier New, Times New Roman, regular and italic, at -12: `tmOverhang == 0` and `tmDigitizedAspectX == GetDeviceCaps(LOGPIXELSY)` | D for the field meaning (overhang is for raster fonts); the aspect equality is C [V5] and O | yes |
| 5 | tm_underline_strikeout_from_logfont | `tmUnderlined` / `tmStruckOut` equal the requested `lfUnderline` / `lfStrikeOut` for all 13 faces, all four combinations (this includes raster faces) | D (TEXTMETRIC: nonzero if the font is underlined / struck out), O for the raster faces | yes |
| 6 | tm_a_w_parity | `GetTextMetricsA` and `GetTextMetricsW` agree in all numeric fields (height, ascent, descent, both leadings, average and maximum width, weight, overhang, both aspects, italic, underlined, struck out, pitch-and-family, charset) for 13 faces x 5 heights; the char-typed first/last/default/break fields are not compared (their width differs by design) | C [V6]; O | yes |
| 7 | getobject_logfont_roundtrip | `GetObjectW(font, 0, NULL)` returns `sizeof(LOGFONTW)`; the filled LOGFONTW equals the one passed to `CreateFontIndirectW`, byte for byte (no substitution, no realised height) for 13 faces x 14 heights with synthetic width/weight/italic/charset/quality values | D ("lpvObject NULL: returns the buffer size needed"); the byte-for-byte equality is C [V3] and O | yes |
| 8 | same | `GetObjectW(font, 10, buf)` returns 10, copies exactly the first 10 bytes and leaves the rest of the buffer untouched | D ("copies up to cbBuffer bytes"); untouched tail is O | yes |
| 9 | getobject_logfonta_observed | N: `GetObjectA` of a Tahoma -11 bold italic font into a LOGFONTA returns 60 with the same face and height | O | yes |
| 10 | getobject_dc_handle_fails | `GetObjectW` on a memory-DC handle returns 0 with last error `ERROR_INVALID_HANDLE` | C [V2]; O (undocumented error code) | yes |
| 11 | selectobject_returns_previous_font | on a fresh memory DC `GetCurrentObject(OBJ_FONT)` is non-NULL; `SelectObject` returns the previously selected font (including the same font twice and the original font back), `GetCurrentObject` returns the last selected font | D (SelectObject: "return value is the previously selected object"; GetCurrentObject) | yes |
| 12 | same | N: whether a fresh memory DC's font is the stock `SYSTEM_FONT` is only printed (it was, nothing was printed) | O | yes |
| 13 | textface_buffer_protocol | `GetTextFaceW(dc, 0, NULL)` returns the length including the NUL and equals the fill call's return, which equals `lstrlenW + 1`; a count of 3 returns 3, copies two characters and a NUL; a count of 1 returns 1 and writes only a NUL; count 0 or -1 with a buffer fails with `ERROR_INVALID_PARAMETER` | D ("c = size of the buffer in chars; if lpFaceName NULL returns the needed length"); the count 0 / -1 failure, the NUL forcing and the return of 3 and 1 are C [V4] and O | yes |
| 14 | same | N: `GetTextFaceA` vs the ANSI conversion of the W name is printed only on a mismatch (none was printed) | O | yes |
| 15 | otm_buffer_protocol | for Arial, Tahoma, Segoe UI, Courier New, Times New Roman at 7 heights: `GetOutlineTextMetricsW(dc, 0, NULL)` returns a size >= `sizeof(OUTLINETEXTMETRICW)`; a non-NULL buffer with count 0 returns the same size; count 100 returns 100, leaves byte 100 untouched and fills `otmTextMetrics.tmHeight`; `size - 1` returns 0; the full call returns `otmSize` and a value <= the size query; `otmTextMetrics` equals `GetTextMetricsW` in height, ascent, descent, both leadings, average and maximum width, weight, pitch-and-family and charset; the four name offsets lie inside `[sizeof struct, size)` | D (nf-wingdi-getoutlinetextmetricsw: size query with NULL; failure when the buffer is too small); count 0 with a non-NULL buffer, the partial fill of 100 and the 0xe9 boundary are C [V7] and O (also scanned over every count in the outline_and_abc area) | yes |
| 16 | same | N: differences between the size query and the full call, and superscript vs subscript size, are printed only when they occur (none occurred) | O | yes |
| 17 | otm_bad_dc | `GetOutlineTextMetricsW(NULL, 0, NULL)` returns 0 with `ERROR_INVALID_HANDLE` | C [V7]; O | yes |
| 18 | otm_raster_font_observed | N: System, Terminal, MS Sans Serif, Courier, Fixedsys: pitch-and-family 0x21 / 0x30 and an OTM size (System 264, Terminal 272, MS Sans Serif 292, Courier 268, Fixedsys 272). Raster faces return an OUTLINETEXTMETRIC although the documentation describes the function for TrueType fonts; not asserted here (asserted in outline_and_abc) | O | yes |
| 19 | gettextmetrics_bad_dc | `GetTextMetricsW(NULL, &tm)` returns FALSE | D (returns zero on failure); C | yes |
| 20 | abc_sum_equals_width_truetype | for 5 TrueType faces x 8 heights x 2 weights: for every char 0x20..0x7e `A + B + C == GetCharWidth32W - tmOverhang`; `GetTextExtentPoint32W(...).cy == tmHeight` for the 52-letter string | D for the ABC meaning (A + B + C = advance); the overhang term and the cy rule are C [V8] and O | yes |
| 21 | same | N: extent cx vs the sum of per-character `GetCharWidth32W` and `GetTextExtentExPointW` fit/last-dx vs extent are printed only on mismatch (none occurred in the final runs) | O | yes |
| 22 | stock_fonts_getobject_and_tm | all 7 stock fonts exist; `GetObjectW(.., 0, NULL)` and the fill return `sizeof(LOGFONTW)`; `SelectObject` of each succeeds; `tmHeight == tmAscent + tmDescent` | D (GetStockObject, GetObject); C [V2] | yes |
| 23 | same | N: per stock font LOGFONT height, face, GetTextFace result and TEXTMETRIC (e.g. OEM_FIXED Terminal 12 / 12/10/2, ANSI_FIXED Courier 13/11/2, ANSI_VAR MS Sans Serif 13/11/2 il 2, SYSTEM and DEVICE_DEFAULT System 16/13/3, SYSTEM_FIXED Fixedsys 15/12/3, DEFAULT_GUI MS Shell Dlg lfHeight -11 tm 13/11/2) | O (see data/win10/font_metrics) | yes |
| 24 | language_info_and_placement_observed | `GetFontLanguageInfo(dc) != GCP_ERROR` for every face | D (returns GCP_ERROR on failure) | yes |
| 25 | same | N: `GetCharacterPlacementW` with `dwFlags 0` and a GCP_RESULTS whose only member set is `lStructSize` returned 0 for every face on this guest (not the extent). The note text compares it against the extent; the correct reading is "the call returns 0 when no output array is supplied", an undocumented result that is NOT asserted. Behaviour with output arrays is asserted in kerning_placement. `GetFontLanguageInfo` values 0x40008 (Arial, Tahoma, Segoe UI, Times New Roman, MS Shell Dlg 2, empty face), 0x40000 (Courier New, MS Shell Dlg, Microsoft Sans Serif), 0 (raster faces) are printed in the note | O | yes |

## Known limits

- Tests that use the non-raster faces assume they are installed (a stock Windows 10 Home); they check the realised kind per case (`TMPF_TRUETYPE`) and do not assert a case whose face is not TrueType (rows 4 and 15 print a NOTE; row 20 skips the case silently).
- All runs are at 96 DPI, en-US. Rows 2 and 20 are expected to hold at any DPI (not run at another DPI); the raster-font notes (rows 18 and 23) are DPI and locale dependent and are not asserted.
- Rows 7 and 8 depend on the LOGFONT being stored as passed. Row 22 checks only the `GetObjectW` sizes of the 7 stock fonts, not their contents.

## Corrections

None needed: no assertion in this area failed on the guest, and none was changed or weakened for this document.
