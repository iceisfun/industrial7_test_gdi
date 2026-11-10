# Probe `char_widths`: widths, ABC widths, kerning, extents, DrawText rects

Source: `probes/char_widths/char_widths.c`. Conformance tests: `tests/char_widths/char_widths.c`.
Format: REC v1 (docs/PROBE_FORMAT.md). Probe version `v=2`; v1 used the key name `h` where v2 uses `req_h`.

    char_widths.exe [--out F | --com1] [--tag T] [--full | --quick] [--heights px|pt|all]

`--full` is the default and is what the VM runs. `--quick` runs 5 faces at -11/-12/-16 + 8/9 pt, weight 400 only, as a
plumbing check. `--heights pt` keeps only the 8..12 pt heights of this process's LOGPIXELSY (DPI sweeps; pixel heights do not
depend on DPI, docs/VM_RUN.md 5.1); `--heights px` keeps only the pixel and cell heights.

## Conditions

- Screen DC (`GetDC(NULL)`), MM_TEXT, identity transform, DEFAULT_CHARSET, DEFAULT_QUALITY unless the table key says
  otherwise. `:px` = device pixels; `fx` uses `:lu` (logical units of that row's mapping mode or world transform).
- Font configuration key `face;req_h;wt;it`: requested face name, lfHeight exactly as passed (negative = character/em height,
  positive = cell height), lfWeight, lfItalic. `font.h` is the RESULT tmHeight (cell height).
- Faces (`--full`): core = Segoe UI, MS Shell Dlg, MS Shell Dlg 2, Tahoma, Arial, Courier New, Microsoft Sans Serif;
  others = MS Sans Serif, Courier, System, Fixedsys, Terminal, Times New Roman, Verdana, Consolas, and the absent name
  `NoSuchFace I7` (records what the mapper falls back to in `font.got`).
- Heights: -8..-16, -18, -20, -24, -32; 8/9/10/11/12 pt at LOGPIXELSY (`ptmap`); cell heights 13/15/16/19 for core faces only.
  Weights 400 and 700; italic (weight 400) for Segoe UI, Tahoma, Arial.
- Core grid (exdx, abcf, fx, DrawText, tabs): core faces at -11/-12/-13/-16 (abcf: -11/-12/-16; fx: -11/-12/-16/-24),
  always plus the 8 pt and 9 pt heights of the boot DPI, so a `--heights pt` run at 144 DPI still records them.
- A font that cannot be created or selected gets a `font` row of `!` and an `N` note, and nothing else, so no row ever
  holds the previous font's numbers. The probe only reads: no SystemParametersInfo changes, no windows, no dialogs,
  DT_CALCRECT only (nothing is drawn). DC state changed by `fx` is enclosed in SaveDC/RestoreDC.

## Tables

| table | key | content |
|---|---|---|
| chars | blk | the 16 code points of each of the 21 blocks: U+0020..007F, U+00A0..00FF, 32 Latin Extended-A, 32 Greek, U+0410..044F, misc (U+2013 2014 2018 2019 201A 201C 201D 201E 2020 2022 2026 2030 20AC 2122 FFFD E000) |
| str | s | the 24 test strings (UTF-16 length, text) |
| ptmap | pt | pt -> lfHeight = -MulDiv(pt, LOGPIXELSY, 72), dpi |
| font | face;req_h;wt;it | GetTextFaceW + TEXTMETRICW |
| cw32 | ...;blk | GetCharWidth32W, 16 per row |
| abc | ...;blk | GetCharABCWidthsW A B C (only blocks where the call succeeds) |
| abcf | ...;blk | GetCharABCWidthsFloatW (core grid) |
| gi | face;req_h;wt;it;blk | GetGlyphIndicesW(GGI_MARK_NONEXISTING_GLYPHS), once per face/wt/it at its first height |
| cons | face;req_h;wt;it | counts: GetCharWidthW vs 32W, 32A vs 32W on U+0020..007F, GetCharWidthI vs 32W, A+B+C vs width-overhang, ABC I vs W, ABCFloat sum vs width, missing glyphs, ASCII width sum and extent, GetCharWidthFloatW rounded vs width-overhang (`cwf_ne`) |
| kern | face;req_h;wt;it | GetKerningPairsW count + first 16 pairs (kern amount px) |
| ext | ...;s | GetTextExtentPoint32W cx cy; GetTextExtentExPointW(max 100) nFit, size, last partial extent; GetTextExtentExPointI cx nFit; sum of GetCharWidth32W; `idx_ne` = partial extents that differ between ExPointI and ExPointW |
| exdx | ...;s;i | GetTextExtentExPointW partial extents from index i, 16 per row (core grid) |
| tab | ...;s;tabs | GetTabbedTextExtentW (default, {40,100}, {32}) |
| fx | ...;s;fx | SetTextCharacterExtra 1/2/-1, SetTextJustification(10, breaks) with and without extra, MM_TWIPS (same lfHeight / scaled), GM_ADVANCED transforms (identity, x2, x1.5, x-only 2, rot 30, rot 90), lfEscapement 900 in GM_ADVANCED and GM_COMPATIBLE |
| qual | face;req_h;q | lfQuality effect: ASCII width sum, pangram extent, tmHeight, tmAveCharWidth |
| dt | ...;s;fl;w | DrawTextW(DT_CALCRECT \| fl) on (0,0,w,1000): return value + rect; 11 texts x 17 flag sets x 2 widths |

H lines per configuration: `h_cw32 h_cw h_cwf h_cwi h_abc h_abci h_abcf h_kern h_exdx h_exdxi` (SHA-256/16 of the raw
arrays in block order; a failed block hashes as 0xFF bytes), and `h_qual_cw32` per qual row.

## Size and runtime

Under Wine (plumbing only, not Windows numbers): `--full` = 74k lines, 8.1 MB raw, 0.77 MB gzip -9, 2.3 s;
`--quick` = 9.9k lines, 1.0 MB; `--full --heights pt` = 37.7k lines, 4.0 MB. `dt` is about a third of the rows.
x86 and x64 builds produce identical R/H lines under Wine. Wine's GetCharWidthFloatW returns 1/16 of the pixel width, so
`cwf_ne` = 336 on every Wine row; on Windows research 02 6.5 predicts 0 for TrueType (inference until the VM run).

## Suggested VM plan

`char_widths --full` at 96 DPI (base, plus v6pm2 as a check), then `--full --heights pt` with the sys / pm2 variants at
120/144/192. No Win10 recording exists yet.
