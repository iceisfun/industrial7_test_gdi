# font_inventory: evidence

Area: font-data and font-object protocol: `CreateFontIndirect*` NULL, `GetObjectA/W` sizes, `GetTextFaceA/W`, cell invariant of `GetTextMetrics`,
`GetOutlineTextMetricsW` size protocol, `GetFontData` (whole font, offsets, table tags, argument checks), `GetFontUnicodeRanges`, plus observation-only checks of
width relations and the font registry keys. The installed-font facts (files, names, tables) are recorded in `data/win10/font_inventory/dpi96.rec.gz`
(cited as R rows). Built into `build/font_inventory.exe`; run with `tools/vm_test.sh font_inventory`.

Oracle: Windows 10 22H2, 10.0.19045.2965, en-US (ACP 1252), 96 DPI, 339 font files in the fonts directory, Administrator session. Two fresh boots; the logs
agree line for line (9 tests, 1958 checks, 0 failed, 0 skipped, 5 NOTE lines).

Source key: **doc** = Microsoft Learn; **decomp** = Win10 22H2 (19041.2965) decompilation, tags [D1]..[D8] in the test header; **obs** = observed on the
oracle, stable in both boots; **R** = row of `data/win10/font_inventory/dpi96.rec.gz`; **fmt** = the OpenType file format specification.

Docs: `GetObject`, `GetTextFace`, `GetTextMetrics`, `GetOutlineTextMetrics`, `GetFontData`, `GetFontUnicodeRanges`, `GLYPHSET` (`learn.microsoft.com/windows/win32/api/wingdi/`);
OpenType specification for `head`, the sfnt version and the table directory.

## Expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| fi_createfont_null | `CreateFontIndirectW(NULL)` and `CreateFontIndirectA(NULL)` return NULL | decomp [D4] | 2 boots |
| fi_getobject_font_sizes | `GetObjectW(font, 0, NULL)` = 92, `GetObjectA(font, 0, NULL)` = 60; a full W read returns 92 with every field as created (height, width, weight, italic, underline, charset, precisions, quality, pitch/family, face "Tahoma"); `GetObjectW(font, 10, buf)` = 10 with the first 10 bytes; `GetObjectA` with 60 or 92 returns 60 with the converted LOGFONTA; with `sizeof(ENUMLOGFONTEXA)` (188) returns 188 with the face name | doc + decomp [D1][D2] | 2 boots |
| fi_textface_protocol | On the first installed TrueType UI face (Arial here): NULL buffer returns length+1 (W and A); a large buffer returns length+1 for W and the length for A, with the name (A equals the ACP conversion); `c = 3` returns 3 (W) / 2 (A) with a terminator; `c = 0` or -1 with a buffer returns 0 with error 87 (W and A) | doc + decomp [D3]; obs for the A count (the docs do not give the A return value) | 2 boots |
| fi_textmetrics_cell_invariant | For 17 face names x 14 heights x 4 style combinations (normal/bold/italic/both), including an empty and an unknown name: `tmHeight == tmAscent + tmDescent`. `GetTextMetricsA` versus W differences would be NOTE only (none arose) | decomp [D5]; obs for the full cross product | 2 boots, 952 fonts |
| fi_outline_tm_protocol | On a TrueType face: the size query (`cjCopy` 0 with NULL, and 0 with a buffer) returns the size (>= `sizeof(OUTLINETEXTMETRICW)`); `cjCopy` 0xe8 returns 0xe8; `size-1` returns 0; a full copy is nonzero; `GetOutlineTextMetricsW(NULL, 0, NULL)` returns 0 with `ERROR_INVALID_HANDLE`. Differences between the full-copy return and the size query, `otmSize`, `otmTextMetrics` versus `GetTextMetricsW`, and `otmEMSquare` versus `head.unitsPerEm` are NOTE only (none arose) | doc (`GetOutlineTextMetrics`: size query with NULL) + decomp [D6] (the partial-copy rule is not in the docs) | 2 boots |
| fi_fontdata_protocol | Whole-font read (table 0): size >= 12 equals the NULL-buffer size and a NULL buffer with a nonzero size; the returned bytes start with an sfnt version 0x00010000, `OTTO` or `true`; a read at offset 4 for 8 bytes equals bytes 4..11; `head` (tag 0x64616568) has size 54 and magic 0x5F0F3CF5; `cb = 0xffffffff` and offset 0xfffffff0 + 0x20 return `GDI_ERROR` (buffers larger than the font). Table directory size mismatches and the size of an absent table are NOTE only | doc (`GetFontData`) + fmt + decomp [D7] | 2 boots; R `dir file=arial.ttf` 1036584 bytes, `face`/`head` rows for the same file |
| fi_unicode_ranges_protocol | `GetFontUnicodeRanges(dc, NULL)` returns a size >= 16 with `(size-16) % 4 == 0`; the filled `GLYPHSET` returns the same size, has `cbThis` equal to it, `cRanges == (size-16)/4` and `flAccel` 0 or 1. Unsorted ranges and a `cGlyphs` sum different from `cGlyphsSupported` are NOTE only (neither arose) | doc (`GLYPHSET`) + decomp [D8] | 2 boots |
| fi_width_relations_observed | NOTE only, no assertion: over 28 TrueType font/height pairs (7 faces x 4 heights): glyphs with `A+B+C != GetCharWidth32` (0), cases with extent width != sum of widths or height != `tmHeight` (0) | obs | 2 boots (same counts); the assertion version of these relations lives in `dpi_scaling` and `face_substitution` |
| fi_font_registry_present | NOTE only: the value counts of the font keys: `Fonts` 154, `FontSubstitutes` 26, `FontLink\SystemLink` 67, `GRE_Initialize` 2 (key absence would be reported, not failed) | obs; R `regkey` rows for the same keys | 2 boots |

## Oracle surprises

- None that required correcting an assertion; the NOTE-only items above were chosen as such because the decompilation does not fix them, and none
  deviated in either boot.
- `GetTextFaceA` returns the character count without the NUL while `GetTextFaceW` returns it with the NUL (both asserted, from the decompilation and the oracle).

## Skips

None in either boot. The TrueType-dependent tests would skip with "no TrueType UI face installed" if none of Arial, Tahoma, Segoe UI, Courier New,
Times New Roman, Microsoft Sans Serif were a TrueType face; Arial was selected here.

## Not covered

- The inventory itself (file list, name/head/OS/2/cmap tables, sizes, strikes): recorded only, in `data/win10/font_inventory/dpi96.rec.gz`.
- `AddFontResource*`, `AddFontMemResourceEx` (see `font_resources`), `EnumFontFamilies*` ordering (see `font_enum_order`).
