# Probe `glyph_bitmaps`: rasterisation fingerprints

Source: `probes/glyph_bitmaps/` (main.c, ggo.c, text.c, fonttab.c). Conformance tests: `tests/glyph_bitmaps/glyph.c`.
Format: REC v1 (docs/PROBE_FORMAT.md). Probe version `v=1`.

    glyph_bitmaps.exe [--out F | --com1] [--tag T] [--full | --quick] [--only spi,font,gidx,fonttab,ggo,text,methods,styles]

`--full` is the default and is what the VM runs. `--quick` covers Segoe UI, Tahoma, Arial and MS Shell Dlg at -11/-12/-16, the
core characters only, and skips the unhinted pass. It is meant for plumbing checks.

## What it answers

It lets Industrial7 compare its rasteriser with Win10 glyph by glyph and string by string, for the faces D12 covers.
It covers GLYPHMETRICS, hinted and unhinted glyph images, the gasp, EBLC and hdmx tables that explain small-size
behaviour, text rendered at each lfQuality, and real versus synthesized bold and italic.

## Conditions

All measurements use `CreateCompatibleDC(NULL)` (MM_TEXT, identity transform), so px == logical units. Fonts are created with
CreateFontIndirectW, DEFAULT_CHARSET and default precisions. `req_h` is lfHeight exactly as passed; a negative value is the em
(character) height in px. Pixel-height results do not depend on system DPI (docs/VM_RUN.md 5.1), so one 96 DPI run is the
reference. ENV records DPI, smoothing SPIs, and font file hashes.

Faces: Segoe UI, Tahoma, Microsoft Sans Serif, Arial, Courier New (outline faces); MS Shell Dlg and MS Shell Dlg 2 (aliases:
core characters plus set hashes only); MS Sans Serif, Courier, System (raster: one row per height showing what GGO does).
Heights (GGO): -8..-16, -18, -20, -24, -32. Character set: 0x20..0x7E plus A0 A9 B0 B5 BF C5 C9 D6 D7 D8 DF E6 E9 F1 FC
2026 20AC 2122 (113 chars). Core set (raw bytes recorded): `. 0 A C H K N O S W a c e g i l m n z`, the letters of
Name/OK/Cancel/Size/Hg plus A W i 0 and the period.

## Tables

| table | key | content |
|---|---|---|
| spi | name | SystemParametersInfo FONTSMOOTHING / TYPE / CONTRAST / ORIENTATION / CLEARTYPE (ret, value) |
| font | face req_h | realised face (GetTextFaceW), TEXTMETRICW, GetFontData(0) size, otmEMSquare |
| gidx | face flags | GetGlyphIndicesW over the char set plus U+0001 U+4E00 U+E000 U+FFFF; flags 0 and GGI_MARK_NONEXISTING_GLYPHS (1) |
| gm | face req_h u c | GGO_METRICS return value, glyph index, GLYPHMETRICS (bbx bby ox oy incx incy), then for GGO_BITMAP / GRAY2 / GRAY4 / GRAY8 the return size plus a hash of the repacked bytes; `gm_same` and `gi_same` bit masks |
| gmf | face req_h u c fmt | GLYPHMETRICS of a bitmap format, written only when it differs from GGO_METRICS |
| gmu | face req_h u c | the same with GGO_UNHINTED (bm and g8); `gm_same_hinted` = 1 if unhinted metrics and bm/g8 bytes equal the hinted ones |
| ggo_all (H) | face req_h set fmt hint | one SHA-256 over (u, ret, 6 metrics, len, bytes) for each char of the set in order. `set=core` lines exist for every face, so the alias faces compare with their targets |
| ggo_bm / ggo_g2 / ggo_g4 / ggo_g8 (B) | face req_h u c | raw core glyphs, bbx x bby. bm is 1bpp. g2 is 4bpp with levels 0..4. g4 and g8 are 8bpp with levels 0..16 and 0..64. Values are raw GGO levels, not 0..255 |
| ggou_bm / ggou_g8 (B) | same | unhinted core glyphs at -11, -12, -16 and -24 |
| ffile | face wt it | whole font: length, sha, sfnt version, table directory tags |
| ftab | face wt it tag | every requested or present table: length + sha (cmap hmtx hhea OS/2 head maxp EBLC EBDT EBSC gasp hdmx LTSH VDMX cvt fpgm prep kern name post glyf loca GDEF GPOS GSUB DSIG) |
| fhead fhhea fos2 fmaxp | face wt it | parsed header fields (font units) |
| gasp | face wt it i | rangeMaxPPEM, behavior (1 gridfit, 2 dogray, 4 symmetric gridfit, 8 symmetric smoothing) |
| eblc | face wt it i | embedded bitmap strike: ppemX ppemY glyph range, bitDepth, flags, subtables, hori ascender/descender/widthMax |
| hdmx | face wt it | version, record count and size, plus a `ppem:maxWidth` list |
| vdmxr / vdmx | face wt it ratio [ppem] | VDMX ratio groups, and yMax/yMin per ppem up to 48 |
| tx | face req_h q col dib s | TextOutW into a d32 or d8 canvas, black-on-white (bow) and white-on-black (wob), for 8 strings at 5 heights and 4 qualities. Records canvas size, extent, ink box, ink sum, ink px, colour px and canvas sha |
| txprof | face req_h q s | row and column ink profiles over the ink box (short strings, bow, d32) |
| txb (B) | = tx key | ink-box crop of the short strings (bow, d32). 8bpp grey, or 24bpp R,G,B when ClearType colour is present |
| txm | face req_h q s m | textout, eto_0, eto_opaque, eto_clipped, eto_opaque_clipped, eto_glyph_index, eto_dx, drawtext, drawtext_prefix and d1_textout (1bpp). Records the return value, ink stats, and whether the canvas is identical to textout |
| txm_d1 (B) | = txm key | 1bpp render of the short strings, as an 8bpp crop |
| sty, styb (B) | face req_h st q | styles reg, b (700), sb (600), l (300), i, bi, u, s and bu. Records TEXTMETRIC, OUTLINETEXTMETRIC emphasis fields, full/style name, font file length and head checkSumAdjustment (real vs synthesized), extent of "Cancel", and "Hg" ink plus its crop |

Canvas details for tx, txm and sty: the canvas is a top-down DIB section, `w = extent.cx + |req_h| + 12` and
`h = tmHeight + 4`, with the text origin at (4,2) and TA_TOP|TA_LEFT. d8 uses a 256-entry grey palette. Grey for 32bpp is
`(R*77 + G*150 + B*29) >> 8`. Ink is `255 - grey` for bow and `grey` for wob. The ink box is l t r b with r and b exclusive.
The sha covers R,G,B per pixel for d32 and indices or bits for d8/d1, rows without padding.

All hashes are the first 64 bits of SHA-256 in lowercase hex.

## Size and runtime

The full run writes about 36k lines and about 6 MB (about 1 MB gzipped) under Wine. The Win10 size will be similar.
Rough table sizes: gm 7.9k rows, gmu 7.3k, tx 6.4k, txm 2.2k, about 5k core B lines, 1k txb B lines, 1.5k ggo_all H lines.
It runs in under 3 s under Wine. Commit the Win10 recording gzipped (`rec.py split --gzip-over`).

## Notes for readers

- `gm_same`: bit 1 = bm, 2 = g2, 4 = g4, 8 = g8. A set bit means that format's GLYPHMETRICS equal GGO_METRICS.
- `gi_same`: bit 16 = metrics plus the bitmap bits above. A set bit means the GGO_GLYPH_INDEX call with GetGlyphIndicesW's index
  returned identical metrics and bytes.
- Raster faces: the GGO row for 'A' records what Windows returns for a .fon face. GetFontData returns `!` for them.
- Wine output is plumbing only (docs/research/04). Never copy it into data/.
