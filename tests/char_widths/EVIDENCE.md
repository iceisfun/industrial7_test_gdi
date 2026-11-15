# Evidence: char_widths

Area: parameter validation and invariants of the width and extent calls: `GetCharWidth{,32}{A,W}`, `GetCharWidthI`,
`GetCharABCWidths{W,Float,I}`, `GetKerningPairsW`, `GetTextExtentPoint32W`, `GetTextExtentExPointW`, `GetTabbedTextExtentW`,
`DrawTextW` with `DT_CALCRECT`, `GetTextMetricsW` identities and `GetObjectW` on fonts. Source: `tests/char_widths/char_widths.c`.
Per-face, per-height width values of Windows 10 are not asserted; they are in `data/win10/char_widths/dpi96.rec.gz` and the golden test.

Oracle: Windows 10 22H2 (19045.2965, en-US, ACP 1252, 96 DPI, screen DC, one elevated session), `tools/vm_test.sh char_widths`.
Runs: two boots of the earlier code (`PASS tests=12 failed=0 skipped=0 checks=2183/2183`, logs identical, 106 NOTE lines of which
104 were a wrong nFit comparison, see change 1) and two boots of the final code (runs 3 and 4: `PASS tests=12 failed=0 skipped=0
checks=5605/5605`, 2 NOTE lines, logs identical).

Source codes: D = Microsoft documentation, O = observed on Win10 22H2 19045.2965 (stable on all 4 boots unless stated),
X = decompilation read through the reference corpus (gdi32full, win32kfull, user32 19041.2965; function and address named),
R = recording `data/win10/char_widths/dpi96.rec.gz`.
D urls: `https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-<lowercase api name>`; DrawText is in `winuser`.

Faces used for the sweeps: Arial, Tahoma, Segoe UI, Courier New, Times New Roman, MS Shell Dlg, MS Shell Dlg 2, Microsoft Sans Serif,
System, Fixedsys, Terminal, Courier, MS Sans Serif, at heights -8,-11,-12,-13,-16,-24,13,16 and weights 400/700. A face whose
font cannot be selected is skipped by the helper with a NOTE; no such NOTE occurred. The sweeps do not require the exact face
(fallback faces satisfy the invariants), except where the test says it asserts the TrueType face itself.

## Asserted expectations

| Test | Expectation | Source | Stability |
|---|---|---|---|
| bad_params | 19 bad-argument cases run in a child process (a crash or hang kills only the child, the parent restarts after the failing case). Cases 0-14, 16, 17: return FALSE (or 0 for GetKerningPairsW) and last error `ERROR_INVALID_PARAMETER` for: NULL buffer, `iFirst > iLast`, `iLast > 0xFFFF` (W), NULL DC with `iLast < 0x80`, `GetCharWidthI` with NULL widths or NULL indices and a large `giFirst`, `GetKerningPairsW(dc,0,buf)`, `GetTextExtentPoint32W(c = -1)` | X gdi32full!bGetCharWidthW 0x52508, bGetCharWidthA 0x52174, bGetCharABCWidthsW 0x22070, GetCharWidthI 0x503f0, GetKerningPairsW 0x51140, GetTextExtentPointWInternal 0xe8dc. D only says "fails" (getcharwidth32w etc.) | 4/4 (the child exits 0, no case died) |
| bad_params | case 15 `GetCharWidthI(NULL,0,0,gi,w)` (cgi = 0) returns TRUE; case 18 `GetTextExtentPoint32W(NULL,"abc",0)` returns TRUE with size {0,0} (both before the DC is looked at) | X GetCharWidthI 0x503f0, GetTextExtentPointWInternal 0xe8dc; O | 4/4 |
| cw32_valid_single | `GetCharWidth32W(dc,'A','A',w)` succeeds (one-element range); `GetCharWidthI(dc,0,0,...)` succeeds; `GetTextExtentPoint32W(c=0)` returns TRUE with {0,0} | D getcharwidth32w; X as above | 4/4 |
| kerning_pairs_protocol | `GetKerningPairsW(dc,0,NULL)` returns the pair count of Arial -12 (909, reported as a NOTE, not asserted: font data); with a buffer of exactly that size the return is exactly that count; with `nPairs=1` the return is 1 and the pair equals the first of the full list | D getkerningpairsw (NULL buffer: "the total number of kerning pairs"; success: "the number of kerning pairs returned") for the size query and the full-size call. The `nPairs=1` result is O only and contradicts the documentation, which says the function "returns an error" when the font has more pairs than `nPairs`; Windows 10 returns the first pair instead, stable 4/4 (see change 2) | 4/4 |
| drawtext_null_string | `DrawTextW(dc, NULL, -1, rc, DT_CALCRECT\|DT_SINGLELINE)` returns 0 | X user32!DrawTextExWorker 0x1e810 | 4/4 |
| tm_height_is_ascent_plus_descent | `tmHeight == tmAscent + tmDescent` for 13 faces x 8 heights x 2 weights; and `tmOverhang == 0` for TrueType faces | D textmetricw; X win32kfull!vIFIMetricsToTextMetricWStrict (tmDescent = tmHeight - tmAscent; overhang 0 without the bitmap/stroke flag) | 4/4 |
| abc_sum_equals_width_truetype | for Arial, Tahoma, Segoe UI, Courier New, Times New Roman (asserted to be the TrueType face itself: `need_tt`), 6 heights, 2 weights: for U+0020..U+007E `abcA + abcB + abcC == GetCharWidth32W - tmOverhang` | D getcharabcwidthsw (A+B+C is the advance) + X win32kfull!GreGetCharABCWidthsW / GreGetCharWidthW (both round fxD; GetCharWidth32 adds the overhang). Skips if no TrueType face is realised | 4/4 |
| width_a_w_parity_ascii | `GetCharWidth32A` equals `GetCharWidth32W` for 0x20..0x7E on all 13 faces at -12 | X bGetCharWidthA (ASCII from the same CFONT cache) | 4/4 |
| width_a_w_parity_ascii | `GetCharWidthW` succeeds and equals `GetCharWidth32W` on ASCII for all 13 faces at -12 | O (a NOTE in the earlier code, silent on 2 boots; asserted in the final code, 2 boots) | 4/4 |
| extent_cy_is_tmheight | `GetTextExtentPoint32W` returns TRUE and `cy == tmHeight` for 5 strings x 13 faces x 8 heights, hard-checked for ASCII strings, and for non-ASCII strings only when the face is TrueType and every character has a glyph (`GetGlyphIndicesW` with `GGI_MARK_NONEXISTING_GLYPHS`) | X GetTextExtentPointWInternal 0xe8dc (cy = cached tmHeight), RFONTOBJ::bTextExtent (cy = tmHeight). The gate keeps font linking out | 4/4 |
| extent_cy_is_tmheight | same gate: the extent `cx` equals the sum of the `GetCharWidth32W` advances of the characters; `GetTextExtentExPointW` returns TRUE and the same size | O (NOTE-only in the earlier code and silent on 2 boots; asserted in the final code, 2 boots). Not documented, and gettextextentexpointw remarks that rounding can make the extents differ from the per-character `GetCharWidth` widths; in MM_TEXT on these faces at 96 DPI they never did. | 4/4 |
| extent_cy_is_tmheight | `GetTextExtentExPointW(..., nMaxExtent = 100, &nFit, dx)` under the same gate: `0 <= nFit <= n`; the first `nFit` characters measure `<= 100`; if `nFit < n` the first `nFit + 1` measure `> 100`; `dx[nFit-1]` equals the extent of the first `nFit` characters; `dx[]` is non-decreasing over the fitting entries | D gettextextentexpointw (lpnFit: the maximum number of characters that fit nMaxExtent; lpnDx: partial extents, valid for the first lpnFit characters only, "any values in the rest of the array should be ignored"), O for the exact dx values (see change 1) | 4/4 |
| drawtext_calcrect_single_line_height | `DT_CALCRECT\|DT_SINGLELINE` on 3 strings (`"The quick brown fox"`, `"&File"`, `"Name\tSize"`): returns `tmHeight` and sets `bottom = top + tmHeight`; with `DT_EXTERNALLEADING` returns `tmHeight + tmExternalLeading` | X user32!DrawTextExWorker 0x1e810 and DT_InitDrawTextInfo 0x1ed80 | 4/4 |
| drawtext_calcrect_single_line_height | the width of the calculated rect for the plain string equals `GetTextExtentPoint32W` | O (NOTE-only before, silent on 2 boots; asserted in the final code, 2 boots) | 4/4 |
| width_i_matches_w_observed | Arial -12 (asserted to be TrueType Arial): `GetCharWidthI` over the `GetGlyphIndicesW` indices of U+0020..U+007E equals `GetCharWidth32W`; `GetCharABCWidthsI` equals `GetCharABCWidthsW` byte for byte (95 entries) | D getcharwidthi ("glyph indices"), O (NOTE-only before, silent on 2 boots; asserted now, 2 boots) | 4/4 |
| width_i_matches_w_observed | `GetTabbedTextExtentW(dc, "Hello world", 11, 0, NULL)` equals `GetTextExtentPoint32W` for a tab-free string (low word cx, high word cy) | D gettabbedtextextenta (no tabs: plain extent), O (NOTE-only before, asserted now) | 4/4 |
| font_getobject_roundtrip | `GetObjectW(hfont, 0, NULL)` is `sizeof(LOGFONTW)`; `GetObjectW(hfont, size, &lf)` returns the size and gives back every LOGFONT field unchanged (height, width, escapement, orientation, weight, italic, underline, strikeout, charset, out/clip precision, quality, pitch/family) and the requested face name `MS Shell Dlg`, not a substitute | X win32kfull!GreExtGetObjectW (docs/research/02 section 1) | 4/4 |
| font_getobject_roundtrip | `SelectObject` returns a non-NULL old object, `GetCurrentObject(OBJ_FONT)` returns the font, reselecting the old object returns the font | D selectobject, getcurrentobject | 4/4 |

## NOTE-only (not asserted)

| Test | Output | Why only a NOTE |
|---|---|---|
| kerning_pairs_protocol | `Arial -12 GetKerningPairsW count = 909` | font-version data, not behaviour |
| raster_font_abc_observed | `SYSTEM_FONT pf=0x21 tt=0: GetCharABCWidthsW=1 err=0, GetCharABCWidthsFloatW=1`: ABC widths of a raster font succeed on Win10 | getcharabcwidthsw says the function "succeeds only with TrueType fonts" and points to `GetCharWidth` for other fonts; the observed `GetCharABCWidthsW` success is stable 4/4 but contradicts that. (getcharabcwidthsfloatw, by contrast, documents that the Float call "retrieves widths for any font", so its success agrees with the docs.) Kept as an observation so an implementation can choose either reading; it is the only NOTE that is a behavioural fact |
| extent_cy_is_tmheight | non-gated non-ASCII strings (linked-font or missing-glyph cases) would report `cy != tmHeight` / `cx != sum` NOTEs; none occurred on the 4 boots | font linking depends on the installed font set |

## Skips

None occurred. The tests that need a realised face skip with "no face could be selected" or "font setup failed" (and a NOTE naming
the face and what it was realised as) when the guest lacks the faces; `abc_sum_equals_width_truetype` skips with "no TrueType face realised".

## Changes made to the test (all in `tests/char_widths/char_widths.c`)

1. `extent_cy_is_tmheight`: the `GetTextExtentExPointW` partial-extent comparison was wrong, not Windows. It counted `dx[f2] <= 100`
   up to `n`, but Windows 10 fills `dx[]` only for the characters that fit (`nFit` entries; the caller's zero-initialised values stay
   after that, and gettextextentexpointw says the rest of the array is to be ignored), so it reported `nFit 27, partial extents say 43` on every non-fitting case (104 of the 106 NOTEs, identical on both
   boots). Replaced by assertions against the documented meaning of nFit, measured with prefix extents (`<= 100` for nFit characters,
   `> 100` for nFit+1), plus `dx[nFit-1]` equal to the prefix extent. All pass on 2 further boots.
2. `kerning_pairs_protocol`: `g <= n` (always true for a correct implementation and for a buggy one that returns fewer pairs) is now
   `g == n`, and `GetKerningPairsW(nPairs=1)` returning other than 1 is now a failure instead of a NOTE. That second check
   asserts observed Windows 10 behaviour against the documented error return (stable on all 4 boots, see the table).
3. Promoted from NOTE to assertion, because none of them fired in 2 boots of the earlier code and all pass on 2 boots of the final
   code: `GetCharWidthW == GetCharWidth32W`; extent == sum of advances and ExPoint size == extent (gated as the `cy` check);
   `GetCharWidthI`/`GetCharABCWidthsI` == the W calls for Arial -12; tabbed extent == plain extent; DT_CALCRECT width == extent.
4. `tm_height_is_ascent_plus_descent`, `width_a_w_parity_ascii`, `extent_cy_is_tmheight`, `drawtext_calcrect_single_line_height`:
   the loops silently skipped unrealised faces, so a guest with none of them would pass with zero checks. Each now counts the faces
   it exercised and skips with a reason when it is zero. The header comment was updated accordingly.
   No assertion that had a source was weakened.

## Ideas not implemented

- Asserting the sum-of-advances and ExPoint relations for non-gated strings (font linking, missing glyphs) once a stable rule is
  known; and with kerning enabled by `GetFontLanguageInfo` (GCP_USEKERNING faces) and with `SetTextCharacterExtra` /
  `SetTextJustification` (`text_output_extents` covers part of it).
- `GetCharWidthFloatW`, `GetCharWidthInfo`, `GetCharABCWidthsI` with a NULL index list and `GetCharWidthI` with `GGI_MARK` ranges.
- ABC widths of bitmap fonts versus `GetCharWidth32` for all raster faces; behaviour for `iLast > 0xFF` with `GetCharWidth32A` in a DBCS charset.
- Non-MM_TEXT mapping modes and non-96 DPI (the recorder is DPI 96 only).
