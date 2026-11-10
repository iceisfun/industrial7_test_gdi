# font_inventory: static inventory of the Win10 font installation

Recorder (REC v1, docs/PROBE_FORMAT.md). It does not render anything. It reads every file in `%WINDIR%\Fonts`,
parses the sfnt tables and FNT resources itself (bounded big-endian reads, OpenType spec offsets), dumps the font
registry keys, and asks GDI which file backs each UI face. Its purpose is to let the Industrial7 font catalog
(`docs/design/dwrite/win7-fonts.manifest.tsv`, `docs/design/gdi32/fontlink-fonts.sha256`) be diffed against
real Win10 (`tools/diff_font_catalog.py` writes `docs/FONT_CATALOG_DIFF.md`), and to supply the design metrics
(head/hhea/OS/2/VDMX/hdmx/gasp/EBLC) behind GetTextMetrics for the D12 decision.

    tools/vm_run.sh font_inventory --args --full        # what the VM runs (data/win10/font_inventory/dpi96.rec)
    tools/wine_probe.sh font_inventory --quick          # plumbing only; Wine's fonts are not Windows
    tools/wine_probe.sh font_inventory --dir 'Z:\some\dir'   # parser test on a host directory (adds an N line)

Options: `--full` (default: hash and parse every file, plus `tabdir` rows), `--quick` (only the key files are read;
other files get a `dir` row with size only and `!` for the hash), `--dir D` (parse D instead of `%WINDIR%\Fonts`;
plumbing tests only).

Font binaries are never written (no `X` blobs). Files are identified by their full SHA-256.

## Tables

Keys are always `file=<lowercase name>;face=<index in the collection>` (+ the extra key column named in the S line).
Units: `:B` bytes, `:fu` font design units (divide by `face.upem`), `:px` pixels (ppem values, EBLC, VDMX, hdmx).

| table | key | content |
|---|---|---|
| `dir` | file | size, full SHA-256, kind (`ttf` `ttc` `otf` `fon_ne` `fon_pe` `dir` `other`, by magic), faces in the file, VS_FIXEDFILEINFO file/product version (`!` for TrueType files, which carry no version resource) |
| `face` | file face | sfnt version, table count, name IDs 1/2/4/6, upem, glyph count, `has_flags` (28 digits, order in the N line), EBLC strikes `XxYbDepth`, gasp ranges `maxppem:behavior`, table tag list |
| `name` | + id | name IDs 0..6,16,17,21,22, en-US Windows record preferred; platform/encoding/language of the record used (ID 0 truncated to 159 chars) |
| `name_loc` | + id enc lang | every Windows-platform family/full/typographic-family name in a language other than 0x409 (the localized names GDI matches too) |
| `head` `hhea` `os2` `post` `maxp` | file face | the design metrics (all OS/2 fields incl. typo/win ascent/descent, fsSelection, panose hex, Unicode/codepage ranges, x/cap height, sub/superscript, strikeout) |
| `cmap` | file face | subtables `plat.enc.format`, the chosen one, mapped code points total / supplementary, per block (48 columns, named in the S line). H lines `cmap_cov` (coverage bitset 0..10FFFF) and `cmap_map` (cp u32 LE + gid u16 LE per mapped cp) |
| `hmtx` (H) | file face | hash of the longHorMetric array |
| `eblc` | + strike | ppemX/Y, bit depth, flags, glyph range, index subtables, hori/vert sbitLineMetrics ascender/descender/widthMax |
| `gasp` | + i | rangeMaxPPEM, behavior |
| `kern` | file face | version, subtables, format-0 pair count, formats, SHA-256/16 of the table |
| `vdmx_ratio`, `vdmx` | + r / + group ypel | VDMX ratios and every yPelHeight -> yMax/yMin record (what GDI uses for TrueType ascent/descent at a given pixel height) |
| `hdmx` | + ppem | max width, record size, SHA-256/16 of the per-glyph widths at that ppem |
| `ltsh` | file face | glyphs, how many are linear (yPel 1), max yPel, hash |
| `tabdir` | + tag | `--full` only: stored checksum, length, offset, SHA-256/16 of the table bytes |
| `fon_file`, `fon` | file / file res | `.fon` format (NE/PE), FONTRES description, and per RT_FONT resource every FNT header field (points, resolutions, ascent, leading, weight, charset, pixel width/height, avg/max width, char range), face name, copyright, resource hash, width-table hash |
| `regkey`, `reg`, `regsub` | key [name i] | value type + data of Fonts, FontSubstitutes, FontLink\SystemLink (one row per REG_MULTI_SZ element), FontLink, FontMapper, FontMapperFamilyFallback, FontDPI, GRE_Initialize, Font Drivers, FontIntensityCorrection, FontCache, FontAssoc (Associated Charset / DefaultFonts), hardware-profile Fonts, LanguagePack\SurrogateFallback, Windows NT\CurrentVersion\Windows, HKCU Fonts, HKCU WindowMetrics, HKCU Control Panel\Desktop. Binary data hex up to 256 bytes, else hash |
| `keyfile` | file | exists / size / SHA-256 / file version of the files the D12 question names (micross, tahoma(bd), segoeui*, arial*, cour*, marlett, the CJK link fonts, every common `.fon`), plus the files GRE_Initialize and SystemLink name; absent files get `exists=0` |
| `gdi_file` | face weight cs | `CreateFontIndirectA` (lfHeight -11) + screen DC: GetTextFace, tmPitchAndFamily, tmCharSet, tmWeight, `GetFontData('ttcf')` and `GetFontData(0)` sizes, SHA-256 of the font data, the `dir` files with identical bytes (so `MS Shell Dlg -> micross.ttf` is a measured fact, not a registry reading). 45 faces normal, 7 bold, 5 faces x 12 charsets |
| `summary` | ~ | totals per kind, faces, FNT resources, files hashed |

## Size and runtime

Estimate for the Win10 22H2 image (339 files, 360 MB, see docs/VM_RUN.md 5.2), *inference* from the table
layout before the first run: about 30-40k lines / 3-4 MB in `--full`, a few thousand in `--quick`. The run cost is
reading and hashing 360 MB once (seconds) plus writing the recording over COM1. The measured figures are in the
recording's `N elapsed_ms` line and in `data/win10/_runs/`.
