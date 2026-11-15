# data/win10/outline_metrics

`GetOutlineTextMetricsW` / `GetOutlineTextMetricsA` recorder (probe `outline_metrics`, default full grid).
File: `dpi96.rec` (2290 lines: R=2116; no H lines). Probe time 12 s. Boots: 2 fresh boots, identical.
Determinism: IDENTICAL (`python3 tools/rec.py diff A B` -> `diffs=0`; the second recording is not stored).
Known limits: DPI 96 only, one screen DC (MM_TEXT, identity transform), `lfQuality` and `lfCharSet` left at their defaults,
no world transform or mapping-mode variation.

## Rows

Grid: 16 requested faces x 11 pixel heights {-8,-9,-10,-11,-12,-13,-14,-16,-20,-24,-32} (the 8/9/10/12 pt heights of
this DPI are -11/-12/-13/-16 at 96 dpi and fall into the same set, see table `ptmap`) x 4 styles (weight 400/700 x italic 0/1)
= 704 configurations. Key of every row: `face` (requested name), `req_h` (lfHeight as passed; negative = character (em) height),
`wt`, `it`. Faces: Segoe UI, MS Shell Dlg, MS Shell Dlg 2, Tahoma, Arial, Courier New, Microsoft Sans Serif, Times New Roman,
Verdana, Consolas, Calibri, Lucida Console, Segoe UI Symbol, System, Terminal, and the absent name `NoSuchFace I7`
(records the face the font mapper substitutes, column `got`).

| table | content |
|---|---|
| `ptmap` | point size -> lfHeight = -MulDiv(pt, LOGPIXELSY, 72) |
| `otm` | `got` (GetTextFaceW of the realised font), `need` (return of the size query, `cb=0`/NULL), `ret` (full call), `size` (otmSize), the embedded TEXTMETRICW (all 20 fields, prefix `tm_`), the 10 PANOSE bytes (hex), fsSelection, fsType, slope rise/run, italic angle (0.1 deg), EM square (font units), ascent/descent/line gap, cap height, x height, font box, Mac metrics, minimum PPEM, subscript/superscript size and offset, strikeout and underscore size and position |
| `otmname` | the four strings reached through `otmpFamilyName`, `otmpFaceName`, `otmpStyleName`, `otmpFullName`, the four offsets (bytes from the start of the structure) and the FONTSIGNATURE of the font (usb0..3, csb0..1) |
| `otmcall` | call contract: `need`, return for a buffer one byte too small (`ret_short`), return for a buffer of `sizeof(OUTLINETEXTMETRICW)` (`ret_hdr`), the ANSI variant (`need_a`, `ret_a`, `size_a`, the four strings as bytes) |

Lengths are device pixels unless the column says otherwise (`em:fu` font design units, `ital:0.1deg`). Every field is recorded
exactly as returned (descent, Mac descent and several offsets are negative, they are not normalised).

## Observations (FACT from this recording)

- Outlines are reported for every requested TrueType face. Of the requested names, `System` and `Terminal` also return a
  structure (their rows have `need` 264 and 272, `tm_pf` 0x21 and 0x30 without TMPF_TRUETYPE/TMPF_VECTOR, a non-zero PANOSE
  (`020b0800000000000000` and `04000509000000000000`) and zero font signature bits): the call does not fail for these raster
  faces on this system.
- The full-name string can differ from the face: the substituted name for `NoSuchFace I7` (realised as Arial) has the full name
  `Monotype:Arial Regular (Microsoft)`, while the installed Segoe UI has `Segoe UI Regular`.
- A buffer one byte smaller than `need` always returns 0 (`ret_short`, all 704 configurations). A buffer of exactly
  `sizeof(OUTLINETEXTMETRICW)` (232 bytes) succeeds and returns 232 in all 704 configurations, which is smaller than `need`: the
  header can be read without room for the strings (the buffer is not read past its size by the recorder).

## Undefined / excluded cells

- `otmFiller` is padding and is not recorded.
- A name offset that is 0 or outside the returned size is recorded as `!` (undefined); an empty string as `~`.
- Rows whose font could not be created or selected would have `got=!` and `!` everywhere; none occurred in this recording.

## Environment

Windows 10 Home 22H2, 10.0.19045 UBR 2965, user Administrator (elevated), session 1, en-US, 1024x768x32, one monitor, std VGA, qemu/KVM
guest. Full list in the `ENV` rows. Produced by `tools/vm_run.sh outline_metrics` (docs/VM_RUN.md); provenance in
`data/win10/_runs/dpi96.outline_metrics.log`.
