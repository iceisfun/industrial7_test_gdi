# face_substitution: evidence

Area: `CreateFontW`, `CreateFontIndirectA/W/ExW`, `GetObjectA/W` on fonts, `SelectObject`/`GetCurrentObject`, `GetTextFaceA/W`, `GetFontData`,
`GetFontUnicodeRanges`, TEXTMETRIC / extent / width relations, `EnumFontFamiliesExW`, `GetGlyphIndicesW`, `GetFontRealizationInfo`, `GetFontFileInfo`.
The tests assert protocol invariants that hold for any installed font set; the font-specific substitution answers are recorded in
`data/win10/face_substitution/dpi96.rec.gz` (cited as R rows). Built into `build/face_substitution.exe`; run with `tools/vm_test.sh face_substitution`.

Oracle: Windows 10 22H2, 10.0.19045.2965, en-US (ACP 1252), 96 DPI, standard font set, Administrator session. Two fresh boots; the logs agree line for
line (14 tests, 5048 checks, 0 failed, 0 skipped, 22 NOTE lines).

Source key: **doc** = Microsoft Learn; **decomp** = Win10 22H2 (19041.2965) decompilation of the function named in the test header; **obs** = observed on the
oracle, stable in both boots; **R** = row of `data/win10/face_substitution/dpi96.rec.gz` (tables `proto`, `answer`, `map`, `fontfile`, `reg`).

Docs: `CreateFontW`, `CreateFontIndirectW`, `CreateFontIndirectExW`, `GetObject`, `GetTextFace`, `GetFontData`, `GetFontUnicodeRanges`, `GLYPHSET`,
`EnumFontFamiliesEx`, `GetGlyphIndices`, `SelectObject`, `GetCurrentObject`, `GetTextExtentPoint32`, `GetCharABCWidths`
(all `learn.microsoft.com/windows/win32/api/wingdi/`).

## Expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| createfont_null_logfont_returns_null | `CreateFontIndirectW(NULL)` and `CreateFontIndirectA(NULL)` return NULL | decomp (early return); R `proto case=CreateFontIndirectW.null` / `.A.null` = 0 | 2 boots |
| getobjectw_returns_logfont_unchanged | For 17 face names (including empty, unknown, `@`-prefixed, a name filling the 32-character field), 8 charsets (including 3 and 254) and 7 heights, with out-of-range precision/quality/pitch values: `GetObjectW` returns 92 and the LOGFONT is byte-identical to the one passed in (no charset, quality or name normalisation is visible), `GetObjectType` is `OBJ_FONT` | decomp (stored LOGFONT copy) + obs | 2 boots, 952 combinations |
| getobject_font_size_protocol | `GetObjectW(font, 0 or 1000, NULL)` = 92; `GetObjectW(font, 10, buf)` = 10, copies exactly the first 10 bytes and touches no more; `GetObjectA(font, 0, NULL)` = 60; `GetObjectA(font, 60 or 92, buf)` = 60 with the converted LOGFONTA (face "Arial"); `GetObjectA(font, 10, buf)` = 10 | doc (return value is the byte count) + decomp; R `proto case=GetObjectW.font.c0` = 92, `GetObjectA.font.c0` = 60, `.c1`/`.c4`/`.c28` echo the count | 2 boots |
| createfontw_builds_logfont | `CreateFontW` stores every argument into its LOGFONT field: DWORD arguments are truncated to the BYTE fields (italic 0x1ff -> 0xff, underline 0x102 -> 2, strikeout 0x103 -> 3, charset 0x1cc -> 0xcc, out precision 0x107 -> 7, clip 0x1f2 -> 0xf2, quality 0x105 -> 5, pitch/family 0x122 -> 0x22); a NULL face gives an empty name; a 40 character face keeps the first 31 characters (the 32nd read-back character is NOTE only: 0x58 'X' in both boots) | decomp + obs (the truncation is a C-level consequence of the BYTE fields; the docs do not state it) | 2 boots |
| createfontindirectexw_design_axes_limit | `dvNumAxes` of 17 or 0x80000000 gives NULL; 0 gives a font whose LOGFONT reads back unchanged | decomp + R `proto case=CreateFontIndirectExW.axes16` = 1, `.axes17` = 0, `.axes100` = 0, `.axes0` = 1 (and `.nostamp` = 1) | 2 boots |
| createfontindirecta_matches_w | For 6 names, `CreateFontIndirectA` stores the same LOGFONT as the W path: the W read-back equals the A input up to the face name, the name equals the ACP conversion, `GetObjectA` round-trips | doc + decomp (A converts and calls the same path) | 2 boots |
| selectobject_font_roundtrip | `SelectObject` returns the previously selected font; selecting the same font again returns it; `GetCurrentObject(OBJ_FONT)` tracks the selection and the restore | doc (`SelectObject` return value) + decomp | 2 boots |
| gettextface_buffer_protocol | For 17 requested names: `GetTextFaceW(dc, 0, NULL)` returns the length including the NUL (>= 2); a large buffer returns that count and the name; `c = 3` returns 3 and copies 2 characters plus a NUL, `c = 1` returns 1 with an empty string; `c = 0` or negative with a buffer returns 0 with error 87 (also for the A entry point); for ASCII names `GetTextFaceA` returns the length without the NUL (n-1) and equals the ACP conversion, with a NULL buffer it returns n | doc (`GetTextFace`: count of characters including the NUL for W) + decomp + obs (truncation count, error 87) | 2 boots |
| getfontdata_buffer_protocol | For every requested face that is backed by an sfnt font (11 of 17 here, see NOTE lines): a NULL buffer with a nonzero size is a size query; the size equals the full-font read; a 12 byte read equals the first 12 bytes; `cb = 0xffffffff` and an offset+size overflow return `GDI_ERROR` (buffers large enough that a copying implementation fails the check instead of corrupting memory). Faces with no sfnt data (raster faces) return `GDI_ERROR` and are NOTE only. At least one face must give sfnt data | doc (`GetFontData`: `GDI_ERROR` on failure) + decomp (user-mode argument checks) + obs; R `answer` rows for Arial and Segoe UI name `arial.ttf` / `segoeui.ttf`; `fontfile` `arial.ttf` is 1036584 bytes | 2 boots |
| getfontunicoderanges_protocol | For Arial: `GetFontUnicodeRanges(dc, NULL)` returns a size; the filled `GLYPHSET` has `cbThis` equal to it, the same return value, and `offsetof(ranges) + cRanges * 4 <= size`. `sum(cGlyphs) != cGlyphsSupported` would be NOTE only (it did not occur) | doc (`GLYPHSET`) + decomp | 2 boots |
| textmetric_extent_width_invariants | For 17 faces x 11 heights (negative, positive, zero): `tmHeight == tmAscent + tmDescent`; the A and W metrics agree on 12 fields; the extent height equals `tmHeight`; when every advance (`GetCharWidthFloatW`) is integral and `tmOverhang == 0`, the extent width equals the sum of `GetCharWidth32W` (otherwise a difference is NOTE only); TrueType faces have `tmOverhang == 0` and `A + B + C == GetCharWidth32` for 'A'..'z' | doc (`GetTextMetrics`, `GetCharABCWidths`) + decomp (see test header) + obs; R `map` rows give height 13 for `-11`/`-12` Courier (raster) and 14 for an unknown face mapped to Arial | 2 boots |
| enumfontfamiliesex_returns_last_callback_value | A callback returning 7 for every font makes `EnumFontFamiliesExW` return 7; a callback returning 0 stops after the first call and the function returns 0; `NULL` LOGFONT enumerates the same number of fonts as a default-charset, empty-name LOGFONT. An unknown face is NOTE only: returns 1 with no callback | doc (`EnumFontFamiliesEx`: the last callback value, 0 stops) + obs | 2 boots |
| glyph_indices_observations | `GetGlyphIndicesW` for Tahoma returns 3 (not `GDI_ERROR`) with and without `GGI_MARK_NONEXISTING_GLYPHS`; the indexes are NOTE only (0x0024 for 'A', 0xffff / 0x0000 for the two missing characters with / without the flag) | doc (`GGI_MARK_NONEXISTING_GLYPHS` gives 0xffff) + obs; the return value is the only assertion | 2 boots |
| font_file_info_protocol | `GetFontRealizationInfo` (undocumented export, resolved by name) with struct size 0x10 succeeds for Arial; the instance id leads `GetFontFileInfo` to a path (non-empty), a size > 12 and a needed-size value; a buffer of 16 bytes fails with `ERROR_INSUFFICIENT_BUFFER` and reports a larger need; an out-of-range file index fails with 87. NOTE: sizes 0x32c and 0x18 also succeed; Arial is backed by `arial.ttf` (1036584 bytes, need 78) | decomp (header of the test) + obs; R `answer face=Arial;...` realised file `arial.ttf`, size 1036584, `rz` 0x32c; `fontfile` `arial.ttf` 1036584 | 2 boots |

## Oracle surprises

- `GetObjectW` shows no normalisation of the LOGFONT (charset, quality, precision, pitch/family are returned exactly as passed, even out-of-range
  values), although the realised font differs: R `map src=base;face=NoSuchFace` gets "Arial" and R `map face=Courier` gets the raster "Courier"
  at heights 11 and 12 (cell height 13 for both).
- The font realisation struct (`GetFontRealizationInfo`) accepts sizes 0x10, 0x18 and 0x32c on the oracle; the decompilation of the kernel entry accepts
  only 0x10 and 0x32c, so the 0x18 case is NOTE only.
- `GetFontData` succeeds for `@SimSun` (index 14), with `tmPitchAndFamily` 0x7 (TrueType, vector and pitch bits set, family `FF_DONTCARE`, where the
  other sfnt faces report a family, e.g. 0x27); this is NOTE only and no check depends on it.

## Skips

None in either boot.

## Not covered

- The substitution table itself (which installed face the mapper picks for a request). It is recorded in `data/win10/face_substitution` (R `map`, `answer`,
  `fallback`, `reg` FontSubstitutes: MS Shell Dlg -> Microsoft Sans Serif, MS Shell Dlg 2 -> Tahoma) and checked by the golden tests, not here.
- Private fonts, font linking and charset-dependent fallback; `GetFontData` table-by-tag reads (see `font_inventory`).
