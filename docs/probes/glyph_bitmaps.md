# Probe `glyph_bitmaps`: rasterisation fingerprints

Source: `probes/glyph_bitmaps/` (main.c, ggo.c, text.c, fonttab.c). Conformance tests: `tests/glyph_bitmaps/glyph.c`.
Format: REC v1 (docs/PROBE_FORMAT.md). Probe version `v=2` (v1 had `gmu`/`ggou_*` and no `face` table; no v1 Win10
recording exists).

    glyph_bitmaps.exe [--out F | --com1] [--tag T] [--full | --quick] [--only spi,font,gidx,fonttab,ggo,text,methods,styles]

`--full` is the default and is what the VM runs. `--quick` covers Segoe UI, Tahoma, Arial and MS Shell Dlg at -11/-12/-16, the
core characters only. It is meant for plumbing checks.

## What it answers

It lets Industrial7 compare its rasteriser with Win10 glyph by glyph and string by string, for the faces D12 covers.
It covers GLYPHMETRICS, hinted and unhinted glyph images, the gasp, EBLC and hdmx tables that explain small-size
behaviour, text rendered at each lfQuality, and real versus synthesized bold and italic.

## Conditions

All measurements use `CreateCompatibleDC(NULL)` (MM_TEXT, identity transform), so px == logical units. Fonts are created with
CreateFontIndirectW, DEFAULT_CHARSET and default precisions. `req_h` is lfHeight exactly as passed; a negative value is the em
(character) height in px. Pixel-height results do not depend on system DPI (docs/VM_RUN.md 5.1), so one 96 DPI run is the
reference. ENV records DPI, smoothing SPIs, and font file hashes. `DEFAULT_CHARSET` makes the mapper prefer the system
locale's charset; the reference run is en-US (ENV `locale.*`), and a recording from another locale is not comparable.

**Absent faces.** The `face` table is always written first. A non-alias face counts as present only when GetTextFaceW
returns the requested name. An absent face keeps its `face`, `font` and `gidx` rows (they show the substitute) and an
`N absent face=...` note. It gets no ggo/fonttab/tx/txm/sty rows, so a substitute's glyphs are never recorded under the
requested name. The probe never aborts on a missing face. Example: Wine has no Microsoft Sans Serif and maps it to Arial.

Faces: Segoe UI, Tahoma, Microsoft Sans Serif, Arial, Courier New (outline faces); MS Shell Dlg and MS Shell Dlg 2 (aliases:
core characters plus set hashes only); MS Sans Serif, Courier, System (raster: one row per height showing what GGO does).
Heights (GGO): -8..-16, -18, -20, -24, -32. Character set: 0x20..0x7E plus A0 A9 B0 B5 BF C5 C9 D6 D7 D8 DF E6 E9 F1 FC
2026 20AC 2122 (113 chars). Core set (raw bytes recorded): `. 0 A C H K N O S W a c e g i l m n z`, the letters of
Name/OK/Cancel/Size/Hg plus A W i 0 and the period.

## Tables

| table | key | content |
|---|---|---|
| face | face | kind (outline/alias/raster, as the probe expects), got (GetTextFaceW at -12), present, tt (TMPF_TRUETYPE), fontdata_len, head checkSumAdjustment, file sha. For MS Shell Dlg / MS Shell Dlg 2, GetTextFaceW returns the alias name, so compare `file_sha` with the other rows to see which file backs the alias |
| spi | name | SystemParametersInfo FONTSMOOTHING / TYPE / CONTRAST / ORIENTATION / CLEARTYPE (ret, value) |
| font | face req_h | realised face (GetTextFaceW), TEXTMETRICW, GetFontData(0) size, otmEMSquare |
| gidx | face flags | GetGlyphIndicesW over the char set plus U+0001 U+4E00 U+E000 U+FFFF; flags 0 and GGI_MARK_NONEXISTING_GLYPHS (1) |
| gm | face req_h u c | GGO_METRICS return value, glyph index, GLYPHMETRICS (bbx bby ox oy incx incy), then for GGO_BITMAP / GRAY2 / GRAY4 / GRAY8 the return size plus a hash of the repacked bytes; `gm_same` and `gi_same` bit masks |
| gmf | face req_h u c fmt | GLYPHMETRICS of a bitmap format, written only when it differs from GGO_METRICS |
| gmo | face req_h u c | GGO_NATIVE outline: size + hash hinted (`nat_*`) and with GGO_UNHINTED (`natu_*`), `nat_same`, and `unh_same`, a mask (metrics 16, bm 1, g2 2, g4 4, g8 8) of formats whose GGO_UNHINTED result equals the hinted one |
| ggo_nat | face req_h u c hint | raw GGO_NATIVE bytes as hex (TTPOLYGONHEADER/TTPOLYCURVE, little-endian, FIXED 16.16 device px, y up, relative to the glyph origin) for the core chars at -11, -12, -16 and -24, hinted and unhinted. `!` if over 3000 bytes |
| ggo_all (H) | face req_h set fmt hint | one SHA-256 over (u, ret, 6 metrics, len, bytes) for each char of the set in order; `fmt=native` over (u, ret, bytes), `hint=hinted` and `unhinted`. `set=core` lines exist for every face, so the alias faces compare with their targets |
| ggo_bm / ggo_g2 / ggo_g4 / ggo_g8 (B) | face req_h u c | raw core glyphs, bbx x bby. bm is 1bpp. g2 is 4bpp with levels 0..4. g4 and g8 are 8bpp with levels 0..16 and 0..64. Values are raw GGO levels, not 0..255 |
| ffile | face wt it | whole font: length, sha, sfnt version, table directory tags |
| ftab | face wt it tag | every requested or present table: length + sha (cmap hmtx hhea OS/2 head maxp EBLC EBDT EBSC gasp hdmx LTSH VDMX cvt fpgm prep kern name post glyf loca GDEF GPOS GSUB DSIG) |
| fhead fhhea fos2 fmaxp | face wt it | parsed header fields (font units) |
| gasp | face wt it i | rangeMaxPPEM, behavior (1 gridfit, 2 dogray, 4 symmetric gridfit, 8 symmetric smoothing) |
| eblc | face wt it i | embedded bitmap strike: ppemX ppemY glyph range, bitDepth, flags, subtables, hori ascender/descender/widthMax |
| hdmx | face wt it | version, record count and size, plus a `ppem:maxWidth` list |
| vdmxr / vdmx | face wt it ratio [ppem] | VDMX ratio groups, and yMax/yMin per ppem up to 48 |
| tx | face req_h q col dib s | TextOutW into a d32 or d8 canvas, black-on-white (bow) and white-on-black (wob), for 8 strings at 5 heights and 4 qualities. Records canvas size, extent, ink box, ink sum, ink px, colour px, canvas sha and `edge` |
| txprof | face req_h q s | row and column ink profiles over the ink box (short strings, bow, d32) |
| txb (B) | = tx key | ink-box crop of the short strings (bow, d32). 8bpp grey, or 24bpp R,G,B when ClearType colour is present |
| txm | face req_h q s m | textout, eto_0, eto_opaque, eto_clipped, eto_opaque_clipped, eto_glyph_index, eto_dx, drawtext, drawtext_prefix and d1_textout (1bpp). Records the return value, ink stats, and whether the canvas is identical to textout |
| txm_d1 (B) | = txm key | 1bpp render of the short strings, as an 8bpp crop |
| sty, styb (B) | face req_h st q | styles reg, b (700), sb (600), l (300), i, bi, u, s and bu. Records TEXTMETRIC, OUTLINETEXTMETRIC emphasis fields, full/style name, font file length and head checkSumAdjustment (real vs synthesized), extent of "Cancel", and "Hg" ink plus its crop |

Canvas details for tx, txm and sty: the canvas is a top-down DIB section, `w = extent.cx + |req_h| + 16` and
`h = tmHeight + 10`, with the text origin at (6,4) and TA_TOP|TA_LEFT. `edge` = 1 when ink touches the canvas border,
which means the render may be clipped (not expected; 0 on every row under Wine). d8 uses a 256-entry grey palette. Grey for 32bpp is
`(R*77 + G*150 + B*29) >> 8`. Ink is `255 - grey` for bow and `grey` for wob. The ink box is l t r b with r and b exclusive.
The sha covers R,G,B per pixel for d32 and indices or bits for d8/d1, rows without padding.

All hashes are the first 64 bits of SHA-256 in lowercase hex.

## Size and runtime

The full run writes about 30k lines, 5.3 MB (1.07 MB gzipped) under Wine, where Microsoft Sans Serif is absent. With it present
(Win10) expect about 34k lines and 6 MB, an estimate scaled from the Wine run. Rough table sizes under Wine: gm 6.4k rows,
gmo 5.9k, tx 5.8k, txm 1.9k, ggo_nat 0.6k, about 4k core B lines, 0.9k txb B lines, 0.9k ggo_all H lines.
It runs in under 3 s under Wine. Commit the Win10 recording gzipped (`rec.py split --gzip-over`).

## Notes for readers

- `gm_same`: bit 1 = bm, 2 = g2, 4 = g4, 8 = g8. A set bit means that format's GLYPHMETRICS equal GGO_METRICS.
- `gi_same`: bit 16 = metrics plus the bitmap bits above. A set bit means the GGO_GLYPH_INDEX call with GetGlyphIndicesW's index
  returned identical metrics and bytes.
- Raster faces: the GGO row for 'A' records what Windows returns for a .fon face. GetFontData returns `!` for them.
- GGO_UNHINTED: the 22H2 decomp (win32kfull!GreGetGlyphOutlineInternal) passes flag 0x100 only to
  PFFOBJ::QueryTrueTypeOutline (GGO_NATIVE/GGO_BEZIER), so unhinted metrics and bitmaps are expected to equal the hinted
  ones (`unh_same` 0x1f). That is why v2 stores unhinted *outlines* (gmo, ggo_nat) and not unhinted bitmaps. This is
  read from the decomp and still needs confirming on the VM.
- Wine output is plumbing only (docs/research/04). Never copy it into data/.
