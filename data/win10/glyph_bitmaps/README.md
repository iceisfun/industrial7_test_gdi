# data/win10/glyph_bitmaps

Glyph bitmap recorder (`--full`): GetGlyphOutline bitmaps/metrics, GGO_NATIVE outlines, TrueType table summaries (head, hhea, OS/2, gasp, hdmx, VDMX), text rendering canvases (ClearType/grey/mono).
File: `dpi96.rec.gz` (41644 lines: R=33702 H=1092 B=6624, 1.2 MB compressed). Probe time 150 s. Boots: quick 1, full 2.
Determinism: IDENTICAL (diff 0, bitmaps included).
Known limits: DPI 96, font smoothing ClearType RGB as in the guest (ENV spi.*).

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).
