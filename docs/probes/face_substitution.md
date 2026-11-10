# Probe `face_substitution`: what the font mapper chooses for a requested LOGFONT

Source: `probes/face_substitution/face_substitution.c`. Conformance tests: `tests/face_substitution/face_substitution.c`.
Format: REC v1 (docs/PROBE_FORMAT.md). Probe version `v=2`. v1 keyed the request fields as `h w wt it cs pf op cp q esc ori`;
v2 keys them `req_*` so they never clash with the result columns of the same name (`cs` = tmCharSet, `wt` = tmWeight, `pf`,
`it`), names the `enum_style` LOGFONT key fields `ent_h ent_w`, prefixes the stock object's own LOGFONT `obj_*` (v1 had two
`lf_face` columns in one `S` line), and adds the `rz_*` columns.

    face_substitution.exe [--out F | --com1] [--tag T] [--full | --quick] [--no-filehash]

`--full` (default) is what the VM runs: `tools/vm_run.sh face_substitution --args --full`. `--quick` is a plumbing check.
`--no-filehash` skips hashing `%WINDIR%\Fonts` (file matching by hash then fails; `rz_file` still works); debugging only.

## Conditions

- Screen DC (`GetDC(NULL)`) or one memory DC (`CreateCompatibleDC`, 1bpp); key `dc=screen|mem`. MM_TEXT, identity transform,
  so logical units = device pixels (`:px`). `gm` = graphics mode set for that row (1 = GM_COMPATIBLE, 2 = GM_ADVANCED).
- `req_h` is lfHeight exactly as passed: negative = character (em) height, positive = cell height, 0 = default.
  Result `h` = tmHeight (cell = ascent + descent, includes internal leading); `il`/`el` = internal/external leading.
- Process DPI: the exe has no manifest (DPI-unaware), so a run at another boot DPI is a separate recording (`ENV dpi.*`).
- System locale matters for localized family names and for the `A` API; the VM is en-US / ACP 1252 (`ENV locale.*`).
  `A` rows are only made for ASCII face names.
- Read-only on the system: registry reads with KEY_READ, file reads, GDI objects created and deleted. No windows, dialogs,
  SystemParametersInfo or font installation. All loops are bounded; no call waits on anything.

## Tables

| table | key | what |
|---|---|---|
| `map` | `src api face req_h req_w req_wt req_it req_cs req_pf req_op req_cp req_q req_esc req_ori dc gm` | one request -> the measure block (below). `src` = sub-matrix: `base` (every face list entry, W/A/ExW/CreateFontW), `edge` (32-WCHAR unterminated name), `subst` (every FontSubstitutes value name, DEFAULT and its `,cs`), `family` (every enumerated family), `height` (15 faces x heights x gm, mem DC), `charset`, `pitchfamily` (DEFAULT/FIXED/VARIABLE/MONO x FF_*), `outprec` (0..10), `clipprec`, `quality` (0..6), `orient` (esc/ori x gm), `weight`, `width`, `signature` (empty and absent face x charset x pitch/family) |
| `answer` | `face req_h req_wt gm dc` | MS Shell Dlg, MS Shell Dlg 2, MS Sans Serif, Microsoft Sans Serif, Tahoma, Arial, Segoe UI, Courier, Courier New at -8..-13, weight 400 and 700, both graphics modes: face, family, hash-matched file, `rz_file`, type, TEXTMETRIC, 52-letter extent, width hash. `answer_bmp`: 1bpp render of "Shell Dlg 0123" (96x18) for the first four faces at weight 400. `N answer micross.ttf ...` = presence in the Fonts listing |
| `font` | `id` | one row per distinct realised sfnt font: hash-matched file, GetFontData(0) size + SHA-256, 'ttcf' size + SHA-256, tables, upem, glyph count, head revision, name-table strings |
| `fontfile` | `dir file` | `%WINDIR%\Fonts` (`dir=windir`) and the per-user font dir (`dir=user`): size, SHA-256, extension |
| `reg` | `k n i` | registry dumps (FontSubstitutes 64/32-bit views, Fonts, FontLink\SystemLink, FontMapper, FontMapperFamilyFallback, GRE_Initialize, FontDPI, FontAssoc, LanguagePack, Windows\*font*, HKCU Desktop font/DPI values ...); `i` = index inside a REG_MULTI_SZ; `type=missing` = key absent |
| `enum_fam` / `enum_count` | `q_cs face cs type` | EnumFontFamiliesExW per requested charset (22 charsets) |
| `enum_style` | `q_face face full style cs type ent_h ent_w` | EnumFontFamiliesExW(face, DEFAULT_CHARSET) for every family (all styles/charsets/raster sizes) |
| `enum_old` | `api q_cs face cs type h wt it` | EnumFontsW / EnumFontFamiliesW (NULL face) and EnumFontsW on each stock face |
| `stock` | `obj dc` | the 7 stock fonts and a new DC's initial font: `obj_*` LOGFONT, the measure block, `is_system_font` |
| `fallback` / `fallback_bmp` | `face req_h q text` | GetGlyphIndicesW with and without GGI_MARK_NONEXISTING_GLYPHS for 20 scripts (CJK, kana, hangul, emoji, Arabic, Thai, ...), extent, realised font; 1bpp TextOut render at -12 (does linking/fallback draw it?) |
| `proto` | `case` | buffer protocol / bad parameters (GetObject sizes, GetTextFace counts, GetFontData offsets, CreateFontIndirectExW axes, field truncation) |
| `fontres` | `file` | GetFontResourceInfoW(file) description and first LOGFONT |

### The measure block (`map`, `stock`)

`ok`: 1 = created and selected; 0 = CreateFont* returned NULL; -1 = SelectObject failed. On 0/-1 every other column is `!`
(nothing is ever measured on the DC's previous font). Then GetTextFace (`got`, `got_n` = return value: W counts the NUL, A
does not), GetTextFaceAliasW, TEXTMETRIC (`h a d il el ave max wt ovh dax day first last dflt brk it ul so pf cs`; `pf` =
tmPitchAndFamily incl. TMPF_*), GetObject read-back (`lf_eq` = all fields and the face equal to the request), `type`
(`tt`/`raster`/`vector`, `+dev`), `font` id + hash-matched `file`, OTM family/face/style/full name and em, GetFontUnicodeRanges
(glyphs, ranges, hash), GetTextCharsetInfo + fsCsb, hash of GetCharWidth32(0x20..0x7E), extents of "The quick brown fox"
and the 52 letters, and:

- `rz`: struct size GetFontRealizationInfo accepted (0x32c on Win10: NtGdiGetRealizationInfo @0xa6090 takes only 0x10 and
  0x32c; 0x18 on Wine). `rz_flags` = DWORD at +4; `rz_nfiles` (+0x10), `rz_face` (+0x14, index in a TTC), `rz_sim` (+0x16:
  bit0 emboldened, bit1 obliqued) per RFONTOBJ::GetRealizationInfo @0xaa044.
- `rz_file`: lowercase basename of the path GetFontFileInfo(instance, file 0) returns; `rz_fsize:B` its size (+8). This names
  the `.fon` strike file of raster fonts and the `.ttc` of TTC members, which the hash match (`file`) cannot.

## Sizes and run time

Wine (216 families) `--full`: 14.3k lines, 4.5 MB, 3.7 s. Win10 is expected in the same range (about 5-8 MB raw, roughly 1 MB
gzipped; `rec.py split --gzip-over` stores it as `.rec.gz`). Per-section timings: `ENV fs.ms.*`.
