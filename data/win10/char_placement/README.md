# data/win10/char_placement

`GetCharacterPlacementW` / `GetCharacterPlacementA` with every GCP_ flag combination that matters for Latin text, 6 faces (Segoe UI, Tahoma, Arial,
Times New Roman, Courier New, System) at lfHeight -12 and -16, 7 strings (`word` Hello, `avat` AVAT, `kern2` "To AV Ty", `spaces` "a  b   c",
`digits`, `accent` U+00C0 U+00E9 U+00F1 U+00FC U+00DF, `sent` "Hello world"). Numbers only.
File: `dpi96.rec` (5776 lines, R=5604). Probe run time 34 s. Boots: 2, identical.
Source: `probes/char_placement/char_placement.c`; schema in the `S` lines and the file header.

Tables (key columns first; values per `S`):
- `font` face;req_h: realised face (GetTextFaceA) and TEXTMETRICA h/a/d/il/el.
- `nat` api;face;req_h;s: the flags-0 return value (raw DWORD, cy<<16|cx) and GetTextExtentPoint32W of the same string.
- `gcp` api;face;req_h;s;combo: one call per flag set (31 combos: flags 0, USEKERNING, MAXEXTENT at 5 limits, JUSTIFY with/without MAXEXTENT,
  GLYPHSHAPE, REORDER, LIGATE, DIACRITIC, NUMERICSLATIN/LOCAL, SYMSWAPOFF, CLASSIN with 4 input class bytes, and mixes). The limit is relative to the
  flags-0 width of that string (`maxext_nat7` = width + 7; the absolute value is column `maxext`; `flags0_ext_half` passes a limit without the flag).
  Columns: flags, maxext, raw return, cx, cy, `nglyphs` / `nmaxfit` (GCP_RESULTS fields after the call), then the first n elements of lpDx, lpCaretPos,
  lpGlyphs (hex glyph indices), lpOutString (hex code units), lpOrder, lpClass (comma lists).
- `gcpm` api=W;face;req_h;s;mode (strings `word`, `avat` only): which output pointers are supplied (`results_null` = lpResults NULL, only lpDx,
  only lpCaretPos, only lpGlyphs with and without GLYPHSHAPE, only lpOutString, only lpClass, only lpOrder with GCP_REORDER).

Conventions: every output array is pre-filled with a sentinel before the call, so an element the call did not write is visible: 30583 (lpDx,
lpCaretPos, lpOrder), `7777` hex (lpGlyphs, W lpOutString), `0077` (A lpOutString), 119 (lpClass, when not CLASSIN). `nGlyphs` is set to the character
count and `nMaxFit` to 0x5555 before the call. The A form converts the string with the ANSI code page (1252); lpGlyphs is a WORD array for both forms.
Differences between the A and W rows (336 of 2604 pairs) are confined to the A-form out-string columns (the sentinel width), not to widths, dx or glyphs.
Known limits: DPI 96 only; Latin text only (no RTL, Arabic or complex-script strings, so REORDER/LIGATE/DIACRITIC/NUMERICS rows document that the
flags change nothing for Latin); no kerning-sensitive claims beyond the data (use `tests/kerning_placement` for asserted behaviour).

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252, OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2`, 4 GiB. No comctl32 v6 manifest for the base exe (comctl32 5.82.10586). Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance log `data/win10/_runs/dpi96.char_placement+drawtext_layout+exttextout_extents.log`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (diffs=0 for all three families; the first recording is not stored).
