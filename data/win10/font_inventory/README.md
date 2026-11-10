# data/win10/font_inventory

Font inventory recorder (`--full`): registry font tables, %WINDIR%\Fonts directory listing, every font file parsed (name, head, hhea, OS/2, post, maxp, cmap, gasp, kern, VDMX, hdmx, LTSH, EBLC), full table directory.
File: `dpi96.rec.gz` (49788 lines: R=49047 H=543; 4.0 MB raw, 0.4 MB gz). Probe time 86 s. Boots: quick 1, full 2.
Determinism: IDENTICAL (diff 0). 339 files, 360823736 bytes in %WINDIR%\Fonts.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).
