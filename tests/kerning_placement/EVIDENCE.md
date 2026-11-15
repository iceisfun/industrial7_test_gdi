# kerning_placement: evidence

Oracle: Windows 10 22H2 build 19045.2965 guest, stock fonts at 24 px (lfHeight -24), memory DC compatible with the screen.
Runs: two guest runs of the final binary (byte-identical logs), plus two earlier-binary runs whose NOTE values agree. "obs" = observed on the
oracle, asserted because stable across runs; the docs disagree where marked.

| Expectation (test) | Source | Stability |
|---|---|---|
| Arial and Times New Roman realised fonts have a `kern` table (GetFontData); else SKIP | obs | all runs |
| GetKerningPairsA/W with nPairs=0, NULL buffer return counts > 0 (kerning_count_and_pairs) | docs nf-wingdi-getkerningpairsa | all runs |
| A count <= W count (docs say the same count: they do NOT, see surprises) | obs: Arial A=120 W=909; TNR A=116 W=867 | all runs |
| Every ANSI pair that is present in the W list has the same iKernAmount (mismatches = 0) | obs | all runs |
| Pair ('A','V') present in A and W forms, iKernAmount < 0 and equal in both (Arial -2, TNR -3 at 24 px; values only NOTEd) | docs + obs | all runs |
| nPairs=1 with a buffer smaller than the count returns 1 and the first W pair (truncation, no failure) | obs, docs imply failure | all runs |
| No kern table (Courier New, Terminal, System): counts 0 for A and W and GetLastError left unchanged | obs | all runs |
| GetKerningPairsW/A(NULL dc) returns 0 | obs (last error value only NOTEd) | all runs |
| The undecorated gdi32 export GetKerningPairs returns the same count and the same pair list as GetKerningPairsA, and 0 for a NULL DC (kerning_generic_export) | gdi32 export table: GetKerningPairs and GetKerningPairsA name the same entry point; obs (Arial 120 = 120) | one guest run; same code as the A form |
| GetTextExtentPoint32W("AV") equals the sum of GetCharWidth32W widths (extents carry no pair kerning; kerning_not_in_extent) | obs | all runs |
| GetCharacterPlacementA/W return value LOWORD = width, HIWORD = height; sum(lpDx) = width; width = GetTextExtentPoint32 + sum of adjacent kern amounts (from GetKerningPairsW), with and without GCP_USEKERNING; unkerned faces (Courier New) equal the extent (gcpa_plain_extent, gcpw_kerning) | docs nf-wingdi-getcharacterplacementa for layout of the result; kerning applied unconditionally: obs | all runs |
| GCP_MAXEXTENT with a wider limit: natural width, nMaxFit = n; GCP_JUSTIFY\|GCP_MAXEXTENT: width and sum(lpDx) equal nMaxExtent exactly (limits natural+60, natural, natural+1); a narrower limit gives 0 < nMaxFit < n and prefix width <= limit (gcpa_justify_maxextent) | docs + obs | all runs |
| A vs W on the same ASCII text, flags 0 and GCP_GLYPHSHAPE: identical return value, nGlyphs, lpDx, lpGlyphs (gcp_a_vs_w_glyphshape) | obs | all runs |
| GetCharacterPlacementA(NULL dc) returns 0 (last error 87 NOTEd) | obs | all runs |
| SetTextCharacterExtra returns the previous value, GetTextCharacterExtra reads it, GetTextExtentPoint32W of n chars grows by n*extra (positive and negative); GetTextExtentExPointW total and last dx include the extra; GetCharWidth32W ignores it; TextOut does not reset it; per-DC; NULL DC returns 0x80000000 (char_extra) | docs nf-wingdi-settextcharacterextra; ExPoint/CharWidth/TextOut: obs | all runs |
| SetTextJustification(extra, breaks): extent of a string with breaks grows by extra, persists over repeated extent queries, a string without breaks is unchanged, extra smaller than the break count still adds exactly extra, (0,0) clears, combines with char extra; NULL DC returns FALSE (text_justification) | docs nf-wingdi-settextjustification; rest obs | all runs |
| SetTextJustification setting is NOT cleared by TextOutW (asserted with a 300x60 bitmap selected; the default 1x1 bitmap case is NOTEd and agrees) | obs, contradicts docs | all runs |
| Default layout of memory and screen DCs is 0; SetLayout returns the previous layout; GetLayout returns the new value incl. LAYOUT_BITMAPORIENTATIONPRESERVED (0x9); layout is per DC; RTL does not change extents; GetLayout/SetLayout(NULL) return GDI_ERROR (layout) | docs nf-wingdi-setlayout, getlayout; rest obs | all runs |
| NOTE only: DC created compatible with an RTL DC inherits layout 1; enhanced metafile DC GetLayout/SetLayout return 0 with no error | obs | all runs |
| OBSERVE-ONLY (test `layout_width_observe` has no assertions, only NOTE lines): SetLayoutWidth(hdc, a, b) behaves like SetLayout(hdc, b) with an extra first parameter: (NULL,..) = GDI_ERROR; (mem,-1,0) ret 0; (mem,-1,RTL) ret 0, layout becomes 1; (mem,100,0) ret 1 (previous layout) | obs; decomp of the export forwards to the same syscall as SetLayout | all runs |

## Oracle surprises
- GetKerningPairsA count is far smaller than GetKerningPairsW (120 vs 909 for Arial). Some ANSI pairs, e.g. (T,;), (V,;), (G,,), (G,.),
  are returned by the A form but are absent from the W list.
- GetCharacterPlacement always applies pair kerning (width is extent + kern sum), also without GCP_USEKERNING.
- SetTextJustification is not cleared by a text-out call.
- GetKerningPairs with a too-small buffer truncates and returns the number copied.
- GetLastError is left untouched for a face without kerning.
- GetCharacterPlacement with JUSTIFY|MAXEXTENT stretches the string to exactly nMaxExtent, including a limit smaller than 1 unit above natural.

## Skips
All tests skip with a reason only when the face is not realised, has no `kern` table or GetFontLanguageInfo reports GCP_REORDER/GLYPHSHAPE/LIGATE/DIACRITIC/KASHIDA/ERROR; none skipped on the oracle.

## Ideas not implemented
- Kerning counts for other point sizes and mapping modes (MM_ANISOTROPIC scaling of iKernAmount).
- Bidi/Arabic GCP_REORDER, GCP_CLASSIN, GCP_LIGATE with a language pack.
- Full ANSI-pair reconciliation (why the A list holds pairs the W list lacks) with other ANSI code pages (locale change).
- ExtTextOut/DrawText consumption of SetTextJustification and RTL layout rendering.
- SetLayoutWidth with a non-trivial first parameter on a bitmap DC and its effect on mirrored output.
