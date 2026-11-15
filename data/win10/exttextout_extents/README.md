# data/win10/exttextout_extents

`ExtTextOutW` extent and touched-pixel bounding boxes (NONANTIALIASED_QUALITY, black text on white in a 300x100 32bpp DIB, background colour red) for
4 faces (Segoe UI, Arial, Times New Roman, Courier New) at lfHeight -16 and -24, 4 strings (`word`, `avat`, `wgj` "Wg.j", `spaces` "a  b"), and 33 call
shapes: no/natural/+2/-1/0/alternating-0 lpDx, a rectangle without flags, ETO_CLIPPED with five rectangles, ETO_OPAQUE with five rectangles (including an
empty one, and no string), OPAQUE|CLIPPED, ETO_GLYPH_INDEX (plain, dx, clip, opaque), ETO_PDY pairs (dy 0, +1, -2, +3 on glyph 2, with dx +2, with glyph
indices, with NULL dx, with a clip). This overlaps `tests/text_output_extents` (Arial 18, equality of renderings): this recording holds the numbers.
File: `dpi96.rec` (1234 lines, R=1064). Probe run time 4-17 s. Boots: 2, identical.
Source: `probes/exttextout_extents/exttextout_extents.c`.

Tables: `font` face;req_h (realised face, TEXTMETRIC) and `eto` face;req_h;s;mode: ok (ExtTextOutW result), extent cx/cy (GetTextExtentPoint32W; for the
glyph-index modes GetTextExtentPointI), then bounding box l,t,r,b (r,b exclusive) and pixel count of the black ink, of the red background fill, a count of
any other non-white pixel (0 everywhere: nothing antialiased), and the current position after the call. Boxes with no pixel read `!` and count 0.
The text is drawn with TA_UPDATECP from the start position (20,40) set by MoveToEx, so the current position afterwards is the advance (a positive ETO_PDY
dy moves the position up: y 40 -> 36 for 4 glyphs at +1). The text-box rectangle is (20,40)-(20+extent cx, 40+tmHeight).
Known limits: DPI 96, one quality, ANSI-range text; ETO_PDY with a NULL lpDx fails (ok=0) and draws nothing.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252, OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2`, 4 GiB. No comctl32 v6 manifest for the base exe (comctl32 5.82.10586). Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance log `data/win10/_runs/dpi96.char_placement+drawtext_layout+exttextout_extents.log`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (diffs=0 for all three families; the first recording is not stored).
