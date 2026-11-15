# data/win10/drawtext_layout

`DrawTextW` / `DrawTextExW` layout results: the returned height and the rectangle for DT_CALCRECT and for the drawing calls, the string after
DT_MODIFYSTRING with the three ellipsis modes, DRAWTEXTPARAMS margins / tab length / uiLengthDrawn. 5 faces (Segoe UI, Tahoma, Arial, Times New Roman,
Courier New) at lfHeight -12 and -16, 9 strings (`word`, `fox`, `amp` with prefixes, `tabs`, `path`, `lines` with CRLF and LF, `indent` with leading and
trailing spaces, `longword`, `empty`), rectangle widths 50 and 160. No pixel is read; the target is a 32bpp memory DIB DC.
File: `dpi96.rec` (18903 lines, R=18730). Probe run time 46 s. Boots: 2, identical.
Source: `probes/drawtext_layout/drawtext_layout.c` (flag mnemonics are defined there and in the file header).

All rectangles start at (10,20) with height 60 (a changed left/top shows an alignment effect). Tables:
- `font` face;req_h: realised face and TEXTMETRIC.
- `dtc` face;req_h;s;fl;w: DT_CALCRECT with 50 flag sets (default, DT_SINGLELINE, WORDBREAK, NOPREFIX/HIDEPREFIX/PREFIXONLY, EXPANDTABS with DT_TABSTOP 2/4/8,
  the three ellipsis flags without MODIFYSTRING, NOCLIP, CENTER/RIGHT/VCENTER/BOTTOM with and without SINGLELINE, EXTERNALLEADING, INTERNAL, EDITCONTROL,
  RTLREADING, NOFULLWIDTHCHARBREAK). Values: return, l, t, r, b.
- `dtm` face;req_h;s;fl;calc;w: ellipsis flags + DT_MODIFYSTRING, each with (calc=1) and without (calc=0) DT_CALCRECT. Values: return, l, t, r, b, the
  length in WCHARs of the buffer after the call and its text (`=` = unchanged).
- `dtn` face;req_h;s;fl;w: DrawTextW without DT_CALCRECT (alignment flags, NOCLIP, tabs, EXTERNALLEADING, INTERNAL, PREFIXONLY): same value columns as `dtm`.
- `dtx` face;req_h;s;fl;w: DrawTextExW; the last column is DRAWTEXTPARAMS.uiLengthDrawn (pre-set to 0xFFFF; `!` when no params were passed).
  Cases: margins 3/5, iTabLength 0/2/6, DT_TABSTOP in the flags without params, rectangle height 20 (`*_h20`).
Return value = DrawText's height result. Known limits: DPI 96, ANSI-range text, no bidi/complex strings, default quality only.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252, OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2`, 4 GiB. No comctl32 v6 manifest for the base exe (comctl32 5.82.10586). Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance log `data/win10/_runs/dpi96.char_placement+drawtext_layout+exttextout_extents.log`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (diffs=0 for all three families; the first recording is not stored).
