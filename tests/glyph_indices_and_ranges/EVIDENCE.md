# Evidence: glyph_indices_and_ranges

Oracle: Windows 10 22H2, build 19045.2965, en-US, ANSI code page 1252, one elevated session, screen DC.
Fonts: Arial and Segoe UI (TrueType, identity checked three ways in `gir_open`: `GetTextFaceW`, the family name in
the `OUTLINETEXTMETRICW`, and the nameID 1 record of the `name` table read with `GetFontData`), Symbol, Terminal.
"Boots" below counts separate guest boots of the final test code plus earlier exploratory boots that showed the same
value (exploratory boots are marked `x`). Doc links are `https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-<name>`.
"Decomp" means the Windows 10 22H2 binaries in the shared reference corpus (gdi32full, win32kfull), read, not run.

Source classes: DOC = documentation, OBS = observed on the oracle, DEC = read in the decompilation and confirmed by OBS.

## Expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| ggi_pair_marked | `GetGlyphIndicesW(dc,{U+0041,U+0378},2,out,GGI_MARK_NONEXISTING_GLYPHS)` returns 2 (not GDI_ERROR), in both orders | DOC getglyphindicesw | all boots |
| ggi_pair_marked | absent slot is 0xFFFF; present slot is not 0xFFFF and equals the single-character lookup and the glyph index the `cmap` table (read with `GetFontData`) assigns | DOC + the font file (cmap format 4/12) | all boots |
| ggi_pair_marked | the absent char (first candidate missing from the cmap: U+0378 for both faces) also looks up as 0xFFFF alone, which double-checks the cmap parse | OBS | all boots |
| ggi_pair_marked | flag 0: return count is 2, present slot unchanged; the value for the absent entry is only noted (observed 0x0000 for Arial and Segoe UI) | DOC (count) + OBS (NOTE only) | all boots |
| ggi_ansi_equals_wide | `GetGlyphIndicesA` of bytes 0x20..0xFF equals `GetGlyphIndicesW` of the same bytes converted with the code page of the font's charset, with and without the flag (224 entries) | OBS | all boots |
| ggi_ansi_equals_wide | the conversion code page is the font charset's, not the process ACP: Arial with GREEK_CHARSET converts with 1253 (byte 0xE1 gives the glyph that GetGlyphIndicesW returns for U+03B1, asserted equal, and not the U+00E1 glyph; all return values are checked to be 1 and the output arrays are initialised); ANSI Arial with 1252 (0x80 gives the Euro glyph) | OBS (the task assumed the ACP) | x2, all boots |
| ggi_count_and_arg_semantics | W: cc=0, cc=-1, NULL output, NULL string, NULL DC all return GDI_ERROR, output untouched, last error stays 0; A: cc=1 with NULL output is GDI_ERROR, but A with cc=0 returns 0 (not GDI_ERROR) | OBS, no doc | x2 + all boots |
| ggi_count_and_arg_semantics | an undefined flag bit (2) is not rejected (NOTE only); a 3000-character run returns 3000 and each slot equals the single lookup | OBS | all boots |
| ggi_surrogate_units_are_separate_chars | two surrogate code units are two WORDs: return 2, both 0xFFFF with the flag (neither is in the cmap) | DOC (each WORD is a character) + cmap | all boots |
| font_unicode_ranges_vs_cmap | `GetFontUnicodeRanges(NULL)` returns the size; the filled `GLYPHSET` has cbThis == that size == 16 + 4*cRanges, cGlyphsSupported == sum of range lengths, ranges ascending | DOC getfontunicoderanges + OBS | all boots |
| font_unicode_ranges_vs_cmap | for Arial (161 ranges, 3361 glyphs) and Segoe UI (160 ranges, 3952) the ranges cover exactly the BMP characters of the cmap (no character missing either way) | OBS, no doc; the ranges are never used to pick the absent char | x2 + all boots |
| text_face_counts | `GetTextFaceW/A(dc,0,NULL)` returns 6 (name plus NUL) | DOC gettextfacew | all boots |
| text_face_counts | W with a short buffer: truncated, always NUL-terminated, returns the count copied including the NUL (min(n,6)) | OBS (docs say "characters copied") | x + all boots |
| text_face_counts | A with a buffer: same copy, but the return value excludes the NUL (min(n,6)-1): differs from W and from the A size query | OBS | x + all boots |
| text_face_counts | `GetTextFaceW(dc,-1,buf)` returns 0 with ERROR_INVALID_PARAMETER; NULL DC returns 0; `(dc,32,NULL)` returns 6 | OBS | x + all boots |
| font_language_info | Arial and Segoe UI: exactly FLI_GLYPHS(0x40000)\|GCP_USEKERNING(8); GCP_USEKERNING is set exactly when `GetKerningPairsW` finds pairs (909 and 8292) | DOC getfontlanguageinfo (flags) + OBS | x + all boots |
| font_language_info | Symbol (asserted only when Symbol is realised, otherwise SKIP): FLI_GLYPHS only; Terminal (bitmap): 0; NULL DC: GCP_ERROR | DOC (GCP_ERROR) + OBS | x + all boots |
| char_width_info | `GetCharWidthInfo` (gdi32, undocumented, forwards to NtGdiGetCharWidthInfo) fills 3 LONGs {lMaxNegA, lMaxNegC, lMinWidthD} (12 bytes, per the kernel stub) | DEC NtGdiGetCharWidthInfo / GreGetCharWidthInfo | all boots |
| char_width_info | at -48 and -96 px for Arial, Segoe UI, Times New Roman: lMaxNegA, lMaxNegC are within 1 of the minimum abcA, abcC and lMinWidthD within 1 of the minimum nonzero advance over all glyphs of the face (`GetCharABCWidthsI`, glyph count from `maxp`); signs <=0,<=0,>0; the -96 values are twice the -48 values within 2 | OBS (cross-check against `GetCharABCWidthsI`) | x + all boots; Arial lMaxNegA matches exactly, others differ by 0 or 1 through rounding |
| char_width_info | NULL output and NULL DC return FALSE; Terminal returns TRUE with 0,0,0 | OBS | x + all boots |
| rasterizer_caps | with a 4-byte aligned buffer and cb=6 (gdi32 and gdi32full): TRUE, nSize 6, wFlags has TT_ENABLED and TT_AVAILABLE, nLanguageID == `GetSystemDefaultLangID()` | DOC getrasterizercaps + DEC NtGdiGetRasterizerCaps | run3, run4 (the first two boots of the final code); run2 on a misaligned buffer showed the alignment dependence |
| rasterizer_caps | cb 1..5 copy exactly that prefix and succeed, cb>6 copies 6 bytes, cb=0 and NULL return FALSE, a 2-byte aligned buffer returns FALSE | DEC (min(cb,6), probe with alignment 4) + OBS | run3, run4 |
| text_charset_and_info | Arial with ANSI and DEFAULT charset (ACP 1252): `GetTextCharset` == `GetTextCharsetInfo` == tmCharSet == 0; FONTSIGNATURE has Latin-1 bit 0 and not the symbol bit; both requests give the same signature; `GetTextCharsetInfo(dc,NULL,0)` works | DOC gettextcharset, gettextcharsetinfo + OBS (csb 400001ff for Arial) | all boots |
| text_charset_and_info | Arial with GREEK_CHARSET: charset 161 for both calls, the signature stays the face's own | OBS | all boots |
| text_charset_and_info | Symbol with SYMBOL_CHARSET: charset 2, fsCsb[0]=0x80000000, everything else 0; Arial with SYMBOL_CHARSET is replaced by a different (symbol) face (Wingdings) | DOC (bit 31 = symbol) + OBS | all boots |
| text_charset_and_info | Terminal with OEM_CHARSET: charset 255, all-zero signature | OBS | x + all boots |
| text_charset_and_info | NULL DC: both return DEFAULT_CHARSET with ERROR_INVALID_HANDLE | OBS | x + all boots |
| gdi_code_page | `GdiGetCodePage` (gdi32) and `GetCodePage` (gdi32full) return the same value, equal to the `TranslateCharsetInfo` ACP of `GetTextCharset(dc)`, for ANSI, DEFAULT, Greek, East Europe, Russian, Turkish, Hebrew, Arabic, Baltic and Symbol (1252, 1253, 1250, 1251, 1254, 1255, 1256, 1257, 42) | DEC gdi32full!GetCodePage (returns the charset's code page for the selected font) + OBS | x + all boots |
| gdi_code_page | invalid handle: returns the system ACP and sets ERROR_INVALID_PARAMETER; Terminal OEM: 0 | DEC (guintAcp, GdiSetLastError(0x57)) + OBS | x + all boots |
| translate_charset_info | the 17 table entries (charset, ACP, fsCsb[0]) below map in all three directions (SRCCHARSET value, SRCCODEPAGE value, SRCFONTSIG pointer to a one-bit signature), fsCsb[1] and fsUsb are 0; ANSI->1252, SHIFTJIS->932, 1252->ANSI, 932->SHIFTJIS; SRCLOCALE 0x409 -> ANSI/1252 and 0x411 -> SHIFTJIS/932 | DOC translatecharsetinfo (ANSI, SHIFTJIS) + OBS (all 17) | x + all boots |
| translate_charset_info_failures | DEFAULT_CHARSET, OEM_CHARSET, 0x7F, 0x55, 3, 0x180 as charset and 437, 0, 12345, 1, 65000 as code page return FALSE without writing the output | DOC (zero on failure) + OBS | x + all boots |
| translate_charset_info_failures | SRCFONTSIG needs a pointer to two DWORDs with the second 0 and the first exactly one table bit: {3,0}, {1,1}, {0,0} and a real face's multi-bit signature all return FALSE | DEC (lpSrc[1]==0, exact match) + OBS | all boots |
| translate_charset_info_failures | flags 0 and 0x10: FALSE with ERROR_INVALID_PARAMETER; NULL output FALSE; locale 0xFFFF FALSE | DEC + OBS | x + all boots |

TranslateCharsetInfo table (charset, ACP, fsCsb[0]): (0,1252,0x1) (2,42,0x80000000) (128,932,0x20000) (129,949,0x80000)
(130,1361,0x200000) (134,936,0x40000) (136,950,0x100000) (161,1253,0x8) (162,1254,0x10) (163,1258,0x100) (177,1255,0x20)
(178,1256,0x40) (186,1257,0x80) (204,1251,0x4) (222,874,0x10000) (238,1250,0x2) (254,65001,0x4000000).

## Where Windows 10 differed from the first assumption

1. `GetRasterizerCaps` needs a 4-byte aligned buffer. With a plain stack `RASTERIZER_STATUS` (2-byte alignment) the call
   returned FALSE on some boots and TRUE on others, depending on where the compiler placed the variable (the first
   exploration reported FALSE for every size). Fixed in the test by using an aligned buffer; with it the documented result
   (TRUE, nSize 6, TT_ENABLED|TT_AVAILABLE, nLanguageID 0x409) holds, and a misaligned pointer is asserted FALSE.
2. `TranslateCharsetInfo(TCI_SRCFONTSIG)` does not take a `FONTSIGNATURE*` and does not pick "the" charset of a multi-bit
   signature: it reads a pointer to two DWORDs, requires the second to be zero and the first to equal exactly one table
   entry's bit. A real face's signature never matches (FALSE).
3. `GetGlyphIndicesA` converts with the code page of the DC font's charset, not with `CP_ACP`.
4. `GetTextFaceA` with a buffer returns the length without the NUL while `GetTextFaceW` counts the NUL.
5. `GetGlyphIndicesA` with cc=0 returns 0 while `GetGlyphIndicesW` returns GDI_ERROR.
6. `GetFontUnicodeRanges` matches the cmap exactly for the stock faces (not an over-approximation there).

## Skips

* ggi_pair_marked, ggi_ansi_equals_wide, ggi_count_and_arg_semantics, ggi_surrogate_units_are_separate_chars, text_face_counts,
  font_language_info: SKIP when Arial (or, for the pair test, both Arial and Segoe UI) is not realised as that TrueType face with a
  matching `name` table family ("`<face>` realised as `<other>`").
* ggi_ansi_equals_wide: SKIP unless the ANSI code page is 1252.
* gdi_code_page, text_charset_and_info, translate_charset_info, translate_charset_info_failures: SKIP unless the build is 19041..19045 and the
  ANSI code page is 1252 (the table and the font-signature bits are SKU/locale dependent).
* char_width_info: SKIP when none of Arial, Segoe UI, Times New Roman can be verified.
* Terminal- and Symbol-dependent parts are skipped or noted when the face is not realised.
* None of these skipped in any recorded run.

## Run record

Final code, two separate boots (`tools/vm_test.sh glyph_indices_and_ranges`): run3 and run4 both PASS, tests=13 failed=0
skipped=0, checks=877/877, 22 NOTE lines, and the two test logs are byte-identical (`diff` empty). Earlier boots of earlier
revisions (exploration, plus one with a wrong buffer check in `rasterizer_caps`) agree with the final code on every other test.

## Not implemented here (special setup or destructive ideas)

* Other ANSI code pages and locales (932, 936, 949, 950 system locales): `GetGlyphIndicesA` with DBCS lead bytes and the A/W
  count on a DBCS code page; `TranslateCharsetInfo` on a non-1252 SKU.
* Fonts with a symbol (3,0) cmap or format 12-only cmaps: `GetGlyphIndices` flag semantics for the 0xF0xx symbol range.
* Added fonts (AddFontResourceEx private fonts) with a sparse cmap and a known default glyph: the unflagged value for an absent
  character (observed 0 for Arial and Segoe UI) against a font whose glyph 0 differs or whose cmap maps a "missing" glyph.
* A font without TrueType support (system with gcTrueTypeFonts == 0) for `GetRasterizerCaps` wFlags; a system whose default
  language differs (nLanguageID).
* Corrupt `cmap`/`maxp` tables for `GetGlyphIndices` and `GetCharWidthInfo`.
* `GetCharWidthInfo` under non-MM_TEXT mapping modes and with a world transform (the kernel code scales by a float).
* Killing a process between `GetFontUnicodeRanges` size query and fill.
