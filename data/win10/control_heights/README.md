# data/win10/control_heights

Common-control heights recorder (`--full`): buttons, edit, combo, listview, header, treeview, toolbar, statusbar, rebar, tab, tooltips, menus, message box, themes parts.
Files: `dpi96.rec` (5169 lines: R=4779), `dpi120/144/168/192.rec` (sweep, 317 s for 5 stages), `dpi96.v6.rec` (comctl32 6.16 manifest variant, 9625 lines, R=9092; boot 157 s).
These are the unaware (no dpiAware) exes: at 144+ they see the virtualised 96 DPI view (see docs/VM_RUN.md 5.1), so dpi144/168/192 differ from 96 only in a few sys rows; use a system/PM variant for real scaled controls (NOT recorded here).
Boots: quick 3 (one crash, one diagnostic), full A 1, full B 1, sweep 1, v6 1.
Determinism: IDENTICAL (dpi96 vs boot B vs the sweep's 96 stage: diff 0).
Many `!` values in `btn`, `tb`, `lv`, `tv`, `tt` etc. in the base exe: comctl32 5.82 does not implement BCM_GETIDEALSIZE/BCM_GETTEXTMARGIN/TTM_GETBUBBLESIZE (v6 messages); the v6 file has them.
Probe bug fixed (636e974): TTM_GETBUBBLESIZE crashes comctl32 5.82 with an access violation; only sent when v6 active now.

Environment (FACT, ENV rows): Windows 10 Home 22H2, 10.0.19045 UBR 2965 (buildlab 19041.1.amd64fre.vb_release.191206-1406), computer I7-WIN10,
user `Administrator`, session 1, elevated=1 (the auto-logon account is an elevated administrator, NOT a standard user), en-US (LCID 0x0409, ACP 1252,
OEMCP 437), 1024x768x32@64 Hz, one monitor, std VGA (Microsoft Basic Display Adapter), qemu/KVM `-cpu host -smp 2` (2 vCPU), 4 GiB. No comctl32 v6 manifest
for the base exe (comctl32 5.82.10586); the `v6` exe loads 6.16.10586. Produced by `tools/vm_run.sh` (docs/VM_RUN.md); provenance logs in `data/win10/_runs/`.
Determinism = a second fresh boot of the same exe at DPI 96, `python3 tools/rec.py diff A B` (the second recording is not stored).
